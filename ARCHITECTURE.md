# Vector Typography Editor Architecture

The application keeps editable source text, shaping, vector geometry, procedural
effects, manual deformation, rendering, and export as separate stages. Qt is used
for portable desktop/UI services; the core has no Win32 dependency.

## Component boundaries

```text
MainWindow / canvas / panels
              |
EditorController + SelectionModel + QUndoStack
              |
Document -> Page -> Layer -> TextObject -> FontDescriptor + TypographyProperties
                                                    + EffectStack + ManualDeformation
              |
immutable Page snapshot -> SceneEvaluator (QThreadPool) -> SceneGeometry
              |
TextEngine -> ShapedText -> GlyphGeometryBuilder -> VectorGeometry
                                                       |
                              EffectStack -> ManualDeformation -> final geometry
                                                       /                    \
                                              EditorCanvas              SvgExporter
```

* `Document` owns metadata and the persistent `Page -> Layer -> TextObject`
  hierarchy. Every page, layer, and object has a stable UUID-like identity;
  selection and commands refer to those IDs rather than vector positions.
  A new document contains Page 1 and Layer 1 but no text object; explicit calls
  to `primaryTextObject()` remain a compatibility adapter for older fixtures,
  not a second source of document state.
* `TextEngine` shapes the complete Unicode source with paragraph-level
  `QTextLayout`/`QGlyphRun` instances, then `GlyphGeometryBuilder` obtains each
  glyph outline through `QRawFont::pathForGlyph`. No glyph is rasterized.
* `Effect` is an ordered, cloneable, serializable interface. `EffectStack` applies
  enabled effects to a fresh geometry copy and supports whole-object or
  source-cluster ranges. Wave, Glyph Jitter, Stretch, and the deterministic
  procedural families currently provide the Phase 3 effect set.
* `ManualDeformation` is a later nondestructive geometry stage. It stores spatial
  brush samples and reevaluates them after shaping and every effect-stack change.
* `DeformationToolState` is UI interaction state, not document state. `Select` is
  an inactive canvas tool and never creates a `DeformationStroke`; brush tools are
  mapped to `BrushMode` only when a real stroke is started.
* `EditorController` owns the document, selection, undo stack, scene result,
  evaluation generation, and non-persistent preview stroke. It copies the
  current `Page` before dispatching evaluation to `QtConcurrent`; a watcher
  publishes a result only if its generation is still current. `EditorCanvas`
  only handles viewport/input, hit testing, selection/move previews, and stroke
  previews or completed strokes.
* `IExportBackend` keeps SVG and future platform exporters separate. `SvgExporter`
  writes only final path geometry.

## Phase 3 workspace model

The main window is canvas-first: a left tool palette, a central vector canvas,
and a right page/layer/inspector area. Page tabs switch the current page; the
layers panel supports active-layer selection, add/remove/rename, visibility, and
lock state. Select and Move are separate tools from deformation brushes. The
canvas supports object hit testing, additive selection, marquee selection,
selection outlines, duplicate/delete, keyboard nudge, and text-object creation.
Stable object IDs make these operations safe across asynchronous scene results.

The Text tool creates an object at the canvas point and opens a multiline
`QPlainTextEdit` overlay aligned to the object bounds. Clicking an existing
object in the Text tool reuses the same overlay, so caret movement, selection,
copy/paste, and source-range tracking remain standard text-editor behavior.
The Typography inspector remains available for precise source and font edits.

`ShortcutManager` registers commands independently of widgets, rejects duplicate
key sequences, and persists accepted bindings through `QSettings`. The
Preferences dialog covers theme, navigation, zoom direction, and the complete
registered command table. Effect instances carry stable IDs, text-range scopes,
and vector mask strokes; the Effect Mask tool paints local erase/restore strokes
which are evaluated as nondestructive geometric attenuation and are undoable.

## Geometry pipeline

1. `TextObject` retains editable source text and typography values.
2. `TextEngine` lays out explicit source paragraphs with `QTextLayout`, including
   line spacing, kerning, ligatures, combining marks, bidirectional text, and
   Qt fallback behavior. Each `QGlyphRun` is requested with string indexes so
   glyphs retain source-cluster and line metadata.
3. `GlyphGeometryBuilder` creates positioned `GeometryPiece` paths from physical
   glyph outlines. A source glyph may produce zero, one, or multiple pieces.
4. `EffectStack` applies enabled procedural effects in explicit user order and
   filters text-range scopes by source-cluster metadata.
5. `ManualDeformation` evaluates persistent strokes on the post-effect geometry.
6. `SceneEvaluator` applies object transforms and layer visibility/lock state,
   then returns immutable scene geometry for canvas or SVG use.

`ObjectFrame` is the single object/page-space mapper.  Its legacy-compatible
matrix is `T(position) * T(pivotLocal) * R(rotation) * S(scale) *
T(-pivotLocal)`, where `pivotLocal` is the immutable base-local-bounds centre.
Scene results retain base/current local bounds, both affine matrices, an oriented
page quad, and a page AABB; the AABB is broad-phase only.  Effects and manual
deformation run in object-local coordinates before this matrix is applied.
Canvas points cross screen -> page -> object-local before persistence.  Deltas
use the inverse linear matrix (translation is removed), and brush radii are
stored as an explicitly documented local equivalent-area circle.  Consequently
their footprint is an ellipse in page space under non-uniform scale.

## Manual deformation model

`DeformationStroke` stores:

* `BrushMode`: Push, Pull, Inflate, Pinch, or Smooth;
* `BrushTarget`: Glyphs or Shape;
* radius, strength, hardness;
* a bounded sequence of object-local `{position, delta, pressure}` samples.

The samples are persistent spatial data. They do not point at `QPainterPath`
objects, glyph array addresses, or transient contour indexes. `resampleBrushStroke`
converts irregular pointer events into deterministic, uniformly spaced samples and
caps serialized work at 4096 samples per stroke.

`DeformationEvaluator` owns the shared smooth/hard falloff and applies strokes in
document order. Glyphs mode computes a rigid displacement per geometry piece,
while Shape mode adaptively flattens each path's line/cubic contours, moves the
sampled points, and reconstructs each subpath with its original QPainterPath
closure semantics. Open polylines stay open; multiple contours and holes remain
separate vector paths. No raster or bitmap intermediate is introduced. The current
reconstruction is intentionally conservative polyline geometry, with tolerance
tied to the vector reference height.

The deformation panel also offers an explicit `Select` tool. In that state the
canvas keeps normal selection-style interaction and viewport navigation but does
not show a brush cursor or create a stroke. `Smooth` is shape-only: selecting it
temporarily forces `BrushTarget::Shape` and restores the previous Glyphs/Shape
choice when another brush tool is selected.

The editor previews the same converted local stroke that it later commits,
without changing the document. On release, the canvas emits one completed
`DeformationStroke`; the controller pushes one undo command. Source text, font,
effect order, effect parameters, and preset application all rebuild from base
geometry, then reapply stored deformation strokes. Toggling deformation or changing
overall strength therefore remains nondestructive.

## Typography and fallback diagnostics

`TypographyProperties::trackingEm` is additional spacing in true em-relative units.
The shaper first performs normal Qt shaping, then offsets successive shaped glyph
positions by `trackingEm * resolvedEmSize` and adjusts logical width. The resolved
em size comes from the selected `QRawFont::pixelSize()` (or the first actual glyph
run raw font when necessary), so it is in the same logical coordinate system as
the shaped positions rather than assuming a point size is one layout unit. If a
Windows offscreen backend reports zero for that raw-font pixel size, the same
physical raw font is normalized to one pixel and its ascent is compared with
Qt's resolved `QFontMetricsF` ascent. This keeps the fallback backend-derived
and font-relative. The implementation avoids treating Qt's advance-relative
percentage spacing as an em unit and makes tracking scale with the resolved
font metrics. Projects serialize `trackingEm` plus `trackingUnit: "em"`.
Version 1 absolute `tracking` values are migrated by dividing by the stored font
size.

`FontDescriptor` stores requested family, style, weight, and reserved future
fingerprint/resource/licensing fields. The requested family/style checks produce
the MissingFamily and MissingStyle states. For a present requested face, each
`QGlyphRun::rawFont()` is compared with `QRawFont::fromFont()` using physical
`QRawFont` identity. A different physical raw font marks the affected glyphs as
fallback, records the physical raw-font handle and glyph count, and produces a
warning. The current Windows/DirectWrite-safe label is `Qt fallback font`: asking
some glyph-run raw fonts for their family name crashes in Qt 6.8, so name-table
resolution is intentionally kept out of the shaping hot path. Qt fallback remains
enabled for a useful preview.

## Serialization and migration

Projects are versioned JSON. The current project format is version 4. Version 1
tracking is migrated to `trackingEm`; versions 1-3 flat object arrays migrate to
one page and one layer while preserving object order. Version 2/3 projects that
have no `deformation` object receive the default enabled deformation model with
no strokes. The next save writes version 4 with pages, layers, stable IDs, object
transforms, and active IDs. Deformation JSON is validated for finite coordinates,
bounded sample/stroke counts, and bounded radius/strength/hardness/pressure.

Presets are version 2 JSON with a generated UUID `id`, Unicode `name`, and an
independent effect stack. New storage paths are `<uuid>.json`; human names never
become filesystem paths. Legacy ASCII name-based files are scanned for practical
backward compatibility. Applying a preset clones its effects into a focused undo
command and never changes manual deformation strokes.

## Cache and invalidation

`TextEngine` caches the last shaping result by source text, font descriptor,
font size, line spacing, and `trackingEm`. The scene evaluator receives a copied
`Page` snapshot, evaluates visible objects as bounded per-object tasks on
`QThreadPool` workers, and keeps shaping/base/effect/deformation stages in a
thread-local per-object cache. Each controller request carries a generation;
only one page evaluation is active and only the newest pending snapshot is
retained. Stale generations are discarded before publication. A preview stroke
is evaluated only on the final scene copy and is never serialized.

Independent text objects can evaluate in parallel. A single very complex text
object still applies its ordered effect and deformation pipeline mostly
sequentially; this is an intentional Phase 3 limitation. Interactive previews
reuse the object's upstream cached stages and do not claim multicore execution
inside one object's ordered deformation stack.

## Undo/redo and clean state

`src/core/undo/document_commands.*` and `scene_commands.*` contain focused
`QUndoCommand` types. Text,
font fields, font size, tracking, fill, effect insertion/removal/reorder/toggle,
effect parameters, preset application, deformation stroke insertion, deformation
clear, deformation enabled state, deformation overall strength, object moves,
object insertion/removal/duplication, page/layer operations, and selection of the
current page are all represented by relevant old/new values or affected objects
only. No ordinary edit serializes the complete `Document` merely to detect a
change.

Typing, numeric effect parameters, font size, tracking, and deformation overall
strength merge through command IDs. A completed canvas drag is one
`AddDeformationStrokeCommand`, regardless of the number of pointer events used to
construct its resampled samples.

`QUndoStack::isClean()` is the modified-state source of truth. Successful save calls
`setClean()`, and new/open reset and clean the stack. Undoing to the saved command
index clears the window marker and close prompt; redo makes it dirty again.

## Windows CI and artifact

`.github/workflows/windows-ci.yml` runs on the GitHub-hosted `windows-latest`
runner, enables x64 MSVC through `ilammy/msvc-dev-cmd@v1`, installs Qt 6.8.3
`win64_msvc2022_64` through `jurplel/install-qt-action@v4`, configures Ninja
Release, builds, and runs `ctest --output-on-failure -VV` with Qt's offscreen
platform plugin. Only after CTest succeeds does it call `windeployqt` and upload
`VectorTypographyEditor-windows-x64.zip`. The artifact is a deployed test build,
not an installer.

## Future platform boundary

Native clipboard and EMF export belong behind `IExportBackend` and a platform
service boundary under `src/platform/windows`. Font embedding, private font
loading, Zalgo/horror generators, glitch/blur effects, plugins, AI tools, and
macOS are intentionally outside this milestone.
