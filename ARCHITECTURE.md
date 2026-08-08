# Vector Typography Editor Architecture

This repository is the foundation of a focused desktop editor whose primary object is
editable text. The current milestone targets Windows 10/11 with C++20 and Qt 6 while
keeping the document, shaping, geometry, effects, serialization, rendering, and UI
logic separated and portable.

## Component boundaries

```text
MainWindow / panels
        |
EditorController + QUndoStack
        |
Document -> TextObject -> FontDescriptor + TypographyProperties + EffectStack
        |
TextEngine -> ShapedText -> GlyphGeometryBuilder -> VectorGeometry
        |                                      |
        +--------------------------------------+
                         |
                    EffectStack
                         |
                VectorGeometry (final)
                   /                 \
          EditorCanvas             IExportBackend
                                      |
                                  SvgExporter
```

* `Document` owns document metadata and an extensible collection of text objects. The
  first vertical slice uses one primary `TextObject`, but the model is not a widget
  property bag.
* `TextEngine` uses Qt text shaping (`QTextLayout`/`QGlyphRun`) and `QRawFont` glyph
  outlines. It distinguishes a missing family, a missing style, and a present font
  that falls back for only some glyphs. Fallback remains available for preview.
* `GlyphGeometryBuilder` converts shaped glyph runs into positioned `QPainterPath`
  pieces. Geometry pieces retain a source glyph index and anchor so future effects can
  operate at glyph or path level without assuming one path per character.
* `Effect` is an extensible, serializable, cloneable interface. `EffectStack` owns an
  ordered list of effects and applies enabled effects in order. Current effects are
  Wave, Glyph Jitter, and Global Stretch.
* `EditorController` is the application-facing orchestration layer. It rebuilds the
  vector scene after model changes and pushes granular commands from
  `src/core/undo/document_commands.*`; ordinary edits do not clone or serialize the
  entire `Document` to detect a no-op.
* `EditorCanvas` only renders the current vector scene and handles viewport gestures.
  It does not own document data or effect logic.
* `IExportBackend` keeps SVG and future EMF/clipboard backends outside the geometry and
  text engine. The current `SvgExporter` writes only path geometry, never SVG `<text>`
  or raster data.
* `src/platform/windows` is reserved for Windows-native integrations. No Win32 API is
  needed by the current SVG-only slice.

## Geometry pipeline

1. Source text and typography are stored unchanged in `TextObject`.
2. `TextEngine` shapes the complete Unicode string, preserving Qt's handling of
   kerning, ligatures, combining marks, and bidirectional text. A glyph run contains
   the actual glyph IDs, positions, and physical raw font.
3. `GlyphGeometryBuilder` asks each raw font for its actual glyph outline and places it
   at the shaped glyph origin. This is vector geometry, not a bitmap snapshot.
4. `EffectStack` transforms a copy of the base geometry. Source text and shaped cache
   data remain untouched, so effects can be toggled, reordered, or removed.
5. The same final geometry is sent to the canvas and to an export backend.

The effect reference coordinate system uses the unmodified geometry's bounds:

* `referenceHeight` is the base vector bounds height, falling back to the requested
  font size when a string has no visible ink.
* horizontal progress is `(glyphAnchor.x - referenceBounds.left) /
  max(referenceBounds.width, 1)`, clamped to `[0, 1]`.
* amplitudes and positional jitter are fractions of `referenceHeight`; stretch is a
  unitless scale. Wave frequency is cycles across normalized text progress, and phase
  is expressed in cycles. This makes saved presets portable across text lengths and
  font sizes.

## Typography and font identity

`TypographyProperties::trackingEm` is additional letter spacing relative to the current
font size. It is serialized as `trackingEm` with `trackingUnit: "em"`; version 1
projects that stored absolute point spacing under `tracking` are converted at load
time. The shaper maps the relative value into Qt's percentage letter-spacing API so
the same project scales with font size rather than preserving a device-specific point
offset.

`FontDescriptor` stores the requested family, style, and weight. Its reserved
`fingerprint`, `embeddedResourceId`, and `embeddingPermission` fields are intentionally
data-only placeholders for future exact face identity, version/resource identity, and
licensing decisions. No embedding or private font loading is implemented in this
milestone.

For diagnostics, `TextEngine` compares each shaped run's physical `QRawFont` with the
raw font selected for the requested query using `QRawFont` physical identity. It
records the actual fallback raw-font handles and glyph counts in `ShapedText`, and
exposes a warning without disabling Qt fallback rendering. This avoids an unstable
Windows font-name-table accessor in the shaping hot path while retaining the data
needed for a later UI-facing face-name resolver.

## Effect API and future stages

An effect has a stable type ID, enabled state, parameter serialization, cloning, a
domain/stage marker, and an `apply(VectorGeometry&, EffectContext&)` method. The stage
marker already includes layout, glyph transform, geometry, generator, mask, and
deformation domains. The stack preserves explicit user order; future domain-specific
effects can use the same data contract without adding fields to `TextObject` or
rewriting the renderer.

The geometry model deliberately supports zero, one, or many pieces per source glyph.
That allows future vector generators, masks, and brush/deformation stages without
encoding combining characters or assuming one path per original character.

## Project and preset serialization

Projects and presets are human-readable, versioned JSON. They contain plain values,
not Qt object pointers, cached painter internals, or widget state.

* A project stores document metadata, source text, font identity (family, style,
  weight, and reserved future font resource fields), typography properties, and the
  independent effect stack.
* Project format version 2 migrates the version 1 absolute tracking field when loaded;
  saving always writes the current `trackingEm` representation.
* A preset stores a stable generated UUID `id`, a Unicode display `name`, and an effect
  stack. New storage paths are `<uuid>.json`; names are never sanitized into paths.
  The manager scans and reads legacy name-based JSON files so existing ASCII presets
  remain usable.
* Applying a preset uses an undo command over the effect stack and clones every effect,
  so the document never retains a pointer to the preset definition.
* Unknown effect IDs are reported as load errors rather than silently dropped.
* Empty `resources`/future font fields are part of the shape of the model so embedded
  fonts, cached previews, masks, brush strokes, and other resources can be added in a
  later format version.

## Cache and invalidation strategy

`TextEngine` keeps a small last-result cache keyed by source text, font descriptor,
font size, and `trackingEm`. Typography changes that affect shaping invalidate that
key. The base shaped geometry is rebuilt when the key changes; effect-only changes
reuse the newly shaped base geometry and reapply the stack. The controller invalidates
the final scene on every command and emits one scene update after the command applies.
This is intentionally a CPU/vector cache rather than a premature GPU renderer.

## Undo/redo and clean state

`EditorController` uses `QUndoStack` with focused commands that store only the relevant
old/new values: text, font fields, size, relative tracking, fill, effect insertion or
removal, effect order, enabled state, effect parameter, and preset effect-stack state.
`SetTextCommand`, `SetFontSizeCommand`, `SetTrackingCommand`, and
`SetEffectParameterCommand` merge continuous updates. Effect commands own or clone
only the affected effect/stack data.

The undo stack is the source of truth for modified state. Successful save calls
`QUndoStack::setClean()`. New/open reset the stack and mark it clean. Undoing back to
the saved command index therefore clears the window modified marker and close prompt;
redoing a change makes the project dirty again. No independent modified boolean is
maintained.

## Windows CI and artifact

`.github/workflows/windows-ci.yml` runs on the GitHub-hosted `windows-latest` image.
It enables MSVC, installs Qt 6.8.3 `win64_msvc2022_64` through
`jurplel/install-qt-action@v4`, enables the x64 MSVC developer environment, configures
with the Ninja generator, builds Release, and runs CTest with failures visible. After tests pass it runs
`windeployqt` and uploads `VectorTypographyEditor-windows-x64.zip`, which contains a
runnable deployed test build rather than an installer.

## Future platform boundary

Qt's portable APIs handle fonts, files, dialogs, rendering, and SVG in this milestone.
Native clipboard and EMF export will be implemented behind `IExportBackend` and a
platform service interface under `src/platform/windows`, with a future macOS
implementation selected by CMake. No Windows path or Win32 type is allowed in core
code.
