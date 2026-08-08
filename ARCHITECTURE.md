# Vector Typography Editor Architecture

This repository is the foundation of a focused desktop editor whose primary object is
editable text. The current milestone targets Windows 10/11 with C++20 and Qt 6, while
keeping the document, shaping, geometry, effects, serialization, rendering, and UI
logic portable to macOS.

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
  outlines. It reports a missing requested system font instead of silently hiding a
  fallback.
* `GlyphGeometryBuilder` converts shaped glyph runs into positioned `QPainterPath`
  pieces. Geometry pieces retain a source glyph index and anchor so future effects can
  operate at glyph or path level without assuming one path per character.
* `Effect` is an extensible, serializable, cloneable interface. `EffectStack` owns an
  ordered list of effects and applies enabled effects in order. Current effects are
  Wave, Glyph Jitter, and Global Stretch.
* `EditorController` is the application-facing orchestration layer. It rebuilds the
  vector scene after model changes and turns edits into snapshot commands with merge
  IDs for slider/text coalescing.
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
   the actual glyph IDs, positions, and raw font.
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

## Effect API and future stages

An effect has a stable type ID, enabled state, parameter serialization, cloning, a
domain/stage marker, and an `apply(VectorGeometry&, EffectContext&)` method. The stage
marker already includes layout, glyph transform, geometry, generator, mask, and
deformation domains. The stack preserves explicit user order; future domain-specific
effects can use the same data contract without adding fields to `TextObject` or
rewriting the renderer.

The geometry model deliberately supports zero, one, or many pieces per source glyph.
That allows future Zalgo-like vector generators, masks, and brush/deformation stages
without encoding combining characters or assuming one path per original character.

## Project and preset serialization

Projects and presets are human-readable, versioned JSON. They contain plain values,
not Qt object pointers, cached painter internals, or widget state.

* A project stores document metadata, source text, font identity (family, style,
  weight, and reserved future font resource fields), typography properties, and the
  independent effect stack.
* A preset stores only a name and an effect stack. Applying a preset clones every
  effect, so the document never retains a pointer to the preset definition.
* Version checks are centralized in the serializer. Unknown effect IDs are reported as
  load errors rather than silently dropped.
* Empty `resources`/future font fields are part of the shape of the model so embedded
  fonts, cached previews, masks, brush strokes, and other resources can be added in a
  later format version.

## Cache and invalidation strategy

`TextEngine` keeps a small last-result cache keyed by source text, font descriptor,
font size, and tracking. Typography changes that affect shaping invalidate that key.
The base shaped geometry is rebuilt when the key changes; effect-only changes reuse the
newly shaped base geometry and reapply the stack. The controller invalidates the final
scene on every model mutation and emits one scene update after a command is applied.
This is intentionally a CPU/vector cache rather than a premature GPU renderer.

## Undo/redo

The controller uses Qt's `QUndoStack` with immutable before/after document snapshots.
Snapshot commands are deep-copyable because effect stacks clone owned effects. Text
typing, parameter changes, and slider movement use merge IDs so a continuous edit does
not create hundreds of isolated undo entries. Adding/removing/reordering/toggling
effects and applying a preset are regular undoable document mutations.

## Future platform boundary

Qt's portable APIs handle fonts, files, dialogs, rendering, and SVG in this milestone.
Native clipboard and EMF export will be implemented behind `IExportBackend` and a
platform service interface under `src/platform/windows`, with a future macOS
implementation selected by CMake. No Windows path or Win32 type is allowed in core
code.
