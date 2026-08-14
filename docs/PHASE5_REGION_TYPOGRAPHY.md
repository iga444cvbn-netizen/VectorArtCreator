# Phase 5: Shape Typography / Text Regions

Phase 5 adds an explicit Region layout mode alongside Baseline and Path. A
region is a persistent layout constraint owned by one `TextObject`; it is not a
render mask and it is not applied by placing unrestricted text and clipping the
result afterward.

## Persistent model

`TextObject` stores one tagged `TypographyLayoutMode`:

- `Baseline` keeps the existing shaped placement;
- `Path` uses the Phase 4D `PathGeometry` and `PathTypographyProperties`; and
- `Region` uses `TypographyRegion` and `RegionTypographyProperties`.

`TypographyRegion` owns one closed outer `PathGeometry` and zero or more closed
hole contours. Lines and cubic segments use the same stable node/handle model as
text paths. Region, contour, and node IDs are persisted and participate in the
document identity contract. Ordinary value copies preserve them. Duplicate,
paste, and page-clone workflows call `duplicatedFresh()` and rewrite the region
reference together with every contour/node ID.

The schema is version 8. Version 7 files infer Baseline or Path from the legacy
path flag. Current loads validate references, finite coordinates, closed
topology, hole containment/non-overlap, and per-object plus aggregate contour,
node, cubic-work, and estimated-work limits before replacing the live document.

## Vector line-band layout

`RegionLayoutEngine` runs after shaping and glyph-outline construction and before
effects or deformation. For every candidate line band it:

1. flattens each cubic contour once with a bounded adaptive tolerance;
2. collects the band endpoints and every flattened contour-vertex Y event in
   that band;
3. probes each event and the midpoint of every event slab, computes half-open
   horizontal crossings for the outer contour, and subtracts every hole
   interval explicitly;
4. intersects all event/slab intervals to obtain a conservative full-band
   safe interval set. The flattened geometry is piecewise linear between
   events, so this is a bounded topology proof rather than a fixed sample
   count;
5. selects one continuous interval for the line: the widest safe interval,
   with the leftmost interval winning an equal-width tie; and
6. wraps complete shaping clusters using their UTF-16 starts/lengths and shaped
   advances.

Disconnected intervals are never joined into one logical line. This is an
intentional bounded Phase 5 policy; future linked frames or multi-region flow
may introduce a different contract.

The algorithm never splits a ligature, combining sequence, fallback cluster, or
surrogate-pair span. Logical source order is used for wrapping and effect
progress; the shaped physical piece order is used for visual placement, so RTL
runs retain their visual order without changing UTF-16 ownership. Legal Qt line
boundaries and whitespace provide break opportunities. If an unbreakable word
has no legal break before the interval ends, Phase 5 uses a deterministic
hard-break fallback between complete shaping clusters; this is an explicit
policy, not a claimed Unicode line-break opportunity. A single cluster wider
than the available interval follows `Clip`: it is emitted as one indivisible
unit and the cursor advances so it cannot be repeatedly placed at an endpoint.

Horizontal alignment supports Left, Center, Right, and Justified. Justification
adds only non-negative space to whitespace opportunities and never compresses a
line; a wrapped paragraph's final line remains ragged, while a standalone
single-line paragraph may justify its only line. Vertical alignment supports
Top, Center, and Bottom. Reflow records stable and repeated states, then picks
the deterministic candidate with the smallest origin residual, fewer lines,
and earliest origin tie-breaks; it does not accept an arbitrary fixed iteration
sample. Padding is applied to all four sides. Overflow is currently the
explicit `Clip` mode.

Successful region placement sets transient logical effect progress in source
order. Effects consume the post-region geometry, and deformation consumes the
post-effect geometry. Region progress and line placement are derived metadata;
they are never serialized. Cancellation or a bounded-work failure discards the
candidate stage and cannot poison the worker cache.

## Editor workflow

The Typography inspector exposes the mode selector, rectangle/ellipse/custom
region presets, four padding values, horizontal/vertical alignment, Clip
overflow, region editing, and hole insertion/removal. The existing curve-aware
Path Edit tool edits the selected outer or hole contour in object-local space;
the inspector cycles through hole contours and the controller accepts an edit
only when the object, contour identity, layer, and spatial revision are still
current. Region state changes are one atomic undoable command. Padding sliders
merge only within one physical gesture and object/side token.

The SceneEvaluator cache has a distinct layout stage. Its key includes the
active mode and only that mode’s owned path or region contours/holes and
settings; editing an inactive resource does not invalidate the active layout.
The evaluator branches explicitly between Baseline, Path, and Region, then
applies effects and deformation in that order. `referenceBounds` for Region is
the outer region bounds so alignment/frame behavior remains stable even when
text overflows.

## Verification

`vector_typography_region_tests` provides independent analytical oracles for
rectangle, concave, hole, and cubic/ellipse scanlines; between-event notch and
hole safety; cluster-preserving wrap; real bidi/RTL shaping with UTF-16 cluster
ownership; padding and all alignments; overlong Clip and explicit unbreakable
word fallback; cancellation transactionality; v8 round trips with malformed
atomic rejection; v7 migration; duplicate identity rejection; and controller
undo/redo plus duplicate freshening.

`vector_typography_region_ui_tests` checks real TypographyPanel controls,
signals, refresh synchronization, and mode/padding/preset affordances. UI smoke
coverage adds and edits a hole through the controller path, removes it with
undo/redo, and verifies save/open persistence. Integration/core coverage also
checks latest-generation Region publication, post-layout effect/deformation
ordering, mode-scoped cache keys, cancellation recovery, and final SVG path
export. The shared invariant checker and semantic-equality helper include
region IDs, contours, settings, and active layout mode.
