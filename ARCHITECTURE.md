# Vector Typography Editor Architecture

The application keeps editable source text, shaping, vector geometry, procedural
effects, manual deformation, rendering, and export as separate stages. Qt is used
for portable desktop/UI services; the core has no Win32 dependency.

## Component boundaries

```text
MainWindow / panels
        |
EditorController + QUndoStack
        |
Document -> TextObject -> FontDescriptor + TypographyProperties + EffectStack
                                      + ManualDeformation
        |
TextEngine -> ShapedText -> GlyphGeometryBuilder -> VectorGeometry
                                                       |
                              EffectStack -> ManualDeformation -> final geometry
                                                       /                    \
                                              EditorCanvas              SvgExporter
```

* `Document` owns metadata and an extensible collection of text objects. The
  current UI edits one primary `TextObject`, not a widget-owned copy.
* `TextEngine` shapes the complete Unicode string with `QTextLayout`/
  `QGlyphRun`, then `GlyphGeometryBuilder` obtains each glyph outline through
  `QRawFont::pathForGlyph`. No glyph is rasterized.
* `Effect` is an ordered, cloneable, serializable interface. `EffectStack` applies
  enabled Wave, Glyph Jitter, and Global Stretch effects to a fresh geometry copy.
* `ManualDeformation` is a later nondestructive geometry stage. It stores spatial
  brush samples and reevaluates them after shaping and every effect-stack change.
* `EditorController` owns the document, undo stack, shaped/base geometry cache,
  final geometry, and non-persistent preview stroke. `EditorCanvas` only handles
  viewport/input and emits stroke previews or completed strokes.
* `IExportBackend` keeps SVG and future platform exporters separate. `SvgExporter`
  writes only final path geometry.

## Geometry pipeline

1. `TextObject` retains source text and typography values.
2. `TextEngine` shapes the Unicode string, including kerning, ligatures,
   combining marks, bidirectional text, and Qt fallback behavior.
3. `GlyphGeometryBuilder` creates positioned `GeometryPiece` paths from physical
   glyph outlines. A source glyph may produce zero, one, or multiple pieces.
4. `EffectStack` applies enabled procedural effects in explicit user order.
5. `ManualDeformation` evaluates persistent strokes on the post-effect geometry.
6. The resulting vector paths are drawn by the canvas or sent to SVG export.

The effect and deformation coordinate system is the document's vector coordinate
system. Effect normalization uses the unmodified reference bounds. Brush radius,
sample positions, and sample deltas are stored in that same coordinate system;
canvas input maps through the inverse viewport transform, so zoom and pan do not
change the brush's document-space size.

## Manual deformation model

`DeformationStroke` stores:

* `BrushMode`: Push, Pull, Inflate, Pinch, or Smooth;
* `BrushTarget`: Glyphs or Shape;
* radius, strength, hardness;
* a bounded sequence of `{position, delta, pressure}` samples.

The samples are persistent spatial data. They do not point at `QPainterPath`
objects, glyph array addresses, or transient contour indexes. `resampleBrushStroke`
converts irregular pointer events into deterministic, uniformly spaced samples and
caps serialized work at 4096 samples per stroke.

`DeformationEvaluator` owns the shared smooth/hard falloff and applies strokes in
document order. Glyphs mode computes a rigid displacement per geometry piece,
while Shape mode adaptively flattens each path's line/cubic contours, moves the
sampled points, and reconstructs separate closed subpaths. Multiple contours and
holes therefore remain separate vector paths; no raster or bitmap intermediate is
introduced. The current reconstruction is intentionally conservative polyline
geometry, with tolerance tied to the vector reference height.

The editor previews a current stroke by applying it to the already-built scene
without changing the document. On release, the canvas emits one completed
`DeformationStroke`; the controller pushes one undo command. Source text, font,
effect order, effect parameters, and preset application all rebuild from base
geometry, then reapply stored deformation strokes. Toggling deformation or changing
overall strength therefore remains nondestructive.

## Typography and fallback diagnostics

`TypographyProperties::trackingEm` is additional spacing in true em-relative units.
The shaper first performs normal Qt shaping, then offsets successive shaped glyph
positions by `trackingEm * fontSize` and adjusts logical width. This avoids treating
Qt's advance-relative percentage spacing as an em unit and makes tracking scale
with font size. Projects serialize `trackingEm` plus `trackingUnit: "em"`.
Version 1 absolute `tracking` values are migrated by dividing by the stored font
size.

`FontDescriptor` stores requested family, style, weight, and reserved future
fingerprint/resource/licensing fields. The requested family/style checks produce
the MissingFamily and MissingStyle states. For a present requested face, each
`QGlyphRun::rawFont()` is compared with `QRawFont::fromFont()` using physical
`QRawFont` identity. A different physical raw font marks the affected glyphs as
fallback, records the physical fallback family/style where Qt exposes them, and
produces a warning. Qt fallback remains enabled for a useful preview.

## Serialization and migration

Projects are versioned JSON. The current project format is version 3. Version 1
tracking is migrated to `trackingEm`; version 2 projects that have no `deformation`
object receive the default enabled deformation model with no strokes. The next
save writes version 3. Deformation JSON is validated for finite coordinates,
bounded sample/stroke counts, and bounded radius/strength/hardness/pressure.

Presets are version 2 JSON with a generated UUID `id`, Unicode `name`, and an
independent effect stack. New storage paths are `<uuid>.json`; human names never
become filesystem paths. Legacy ASCII name-based files are scanned for practical
backward compatibility. Applying a preset clones its effects into a focused undo
command and never changes manual deformation strokes.

## Cache and invalidation

`TextEngine` caches the last shaping result by source text, font descriptor,
font size, and `trackingEm`. The controller caches base glyph geometry by the same
shaping key, copies it for a rebuild, applies the ordered effect stack, then applies
manual deformation. A preview stroke is evaluated only on the final scene copy and
is never serialized. This avoids document-wide JSON snapshots for ordinary edits;
geometry copies remain the rendering-stage boundary rather than an undo mechanism.

## Undo/redo and clean state

`src/core/undo/document_commands.*` contains focused `QUndoCommand` types. Text,
font fields, font size, tracking, fill, effect insertion/removal/reorder/toggle,
effect parameters, preset application, deformation stroke insertion, deformation
clear, deformation enabled state, and deformation overall strength are all
represented by relevant old/new values or affected objects only. No ordinary edit
serializes the complete `Document` merely to detect a change.

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
