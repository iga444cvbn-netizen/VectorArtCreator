# Phase 3 Architecture Plan

Phase 3 extends the merged Phase 2.1 foundation into a compact, canvas-first
multi-page vector typography editor. The implementation remains C++20 + Qt 6.
The existing shaping, vector-outline, effect, nondestructive deformation,
serialization, SVG, granular undo, and Windows CI boundaries remain in place.

## Design goals

- Keep editable source text, shaping, local vector geometry, effects,
  deformation, scene transforms, rendering, and export as separate stages.
- Replace the single-object editor assumption with stable pages, layers, and
  scene objects while keeping short-lived compatibility helpers for old code.
- Keep document state in the core/controller, not in widgets.
- Make selection and object movement independent of glyph geometry mutation.
- Keep heavy shaping/evaluation off the GUI thread where Qt permits it, with
  immutable snapshots and generation IDs preventing stale results from being
  published.
- Make every document mutation granular and undoable; UI preferences are not
  document undo state.

## Document model

The current `Document::objects` collection becomes a compatibility view during
migration. The normal model is:

```text
Document
  metadata, resources, currentPageId
  pages[]
    Page { id, name, size, background, layers[] }
      Layer { id, name, visible, locked, objects[] }
        TextObject { id, source, font, typography, effects, deformation,
                     transform, visible, futureData }
```

IDs are generated UUIDs and are the identity used by selection, commands,
effect scopes, cache keys, and serialization. Objects retain local geometry;
position/rotation/scale are applied by the scene evaluator. The active page and
layer are editor state, while page/layer/object content is project state.

## Project migration

The project schema becomes version 4. A version 3 `objects[]` array is loaded
into one page and one layer in original order. Text, effects, deformation,
fonts, fills, IDs, title, timestamps, metadata, and resources are preserved.
New files serialize pages/layers/objects. Loading is tolerant of missing IDs by
generating them, and the next save writes only the current schema.

## Evaluation pipeline

```text
TextObject source
  -> QTextLayout multiline shaping / glyph clusters
  -> local GlyphGeometry / VectorGeometry
  -> scoped ordered effects
  -> local nondestructive deformation
  -> object transform
  -> visible/locked page-layer scene composition
  -> canvas or path-only SVG export
```

Explicit newlines create independent `QTextLine` origins. Glyph cluster
metadata remains attached to geometry pieces so text-range effect scopes can
select clusters without assuming one Unicode code point equals one glyph.

## Selection and tools

`SelectionModel` owns selected object IDs, the active object, optional text
range selection, and marquee state. Hit testing uses final transformed visual
bounds and geometry, walking the active page from topmost visible editable
layer/object to bottom. Hidden and locked layers are excluded from normal
selection. The canvas emits intent; controller commands mutate the document.

The left palette owns persistent tool selection. Select, Move, Text, Push, Pull,
Inflate, Pinch, Smooth, Mask/Eraser placeholder, and Pan are separate tools.
Temporary Space-pan does not change the persistent tool. Move commands store
only affected object IDs and old/new transforms. Text editing uses a focused
canvas editor overlay/interaction state and routes committed text changes to a
mergeable command.

## Effects, ranges, and masks

Effects receive an explicit scope: whole object or a logical text cluster range.
The evaluator passes eligible glyph pieces to per-glyph effects without
destructively splitting objects. Existing effects remain compatible with the
whole-object scope. New procedural effects share deterministic transform,
progression, alternating, and seeded-random helpers.

Masks are scalar influence data attached to an effect instance. A mask editor
may begin as an inspector/canvas placeholder, but the data model and evaluator
boundary remain separate from manual deformation strokes. Manual deformation is
not converted into an effect or mask.

## UI workspace

MainWindow becomes a canvas-first workspace: compact menu/toolbar at the top,
icon tool palette on the left, canvas in the center, resizable context
inspector/layers panel on the right, and page tabs/status at the bottom. The
inspector uses collapsible context sections and QSettings-backed expanded
state/panel widths. The project never stores UI expansion state.

## Async evaluation and caching

The controller creates immutable `EvaluationSnapshot` values containing the
active page scene, relevant object data, effect/deformation settings, viewport
quality, and a monotonically increasing generation ID. A worker task evaluates
core-only geometry through `QThreadPool`; the GUI thread accepts a result only
if its generation is still current and the page/object cache keys match.
Shaping remains isolated if a platform font backend requires GUI-thread use.
Interactive previews may use bounded/low-quality geometry and promote to full
quality after input settles. Hidden layers and inactive pages are not evaluated
for the active canvas unless explicitly exported.

## Delivery stages

1. Add the document hierarchy, stable IDs, v3 migration, serialization, and
   model tests.
2. Add multiline shaping, object transforms, scene composition, cache keys,
   and SVG current-page export.
3. Add selection/hit testing, Move/Text tools, object commands, and text-range
   selection state.
4. Replace the prototype layout with the palette, inspector, layers/pages UI,
   compact theme, and first-launch state.
5. Add shortcuts/preferences, effect scopes, effect browser/library, sliders,
   and mask architecture.
6. Add generation-safe asynchronous evaluation, telemetry hooks, performance
   tests, documentation, manual acceptance checks, and Windows artifact CI.

Each stage must compile and keep the existing deformation/effect/SVG tests
passing. Phase 4 features such as Word/EMF, font embedding, plugins, raster
editing, AI, animation, advanced path booleans, and Zalgo remain out of scope.
