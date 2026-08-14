# Phase 4D: Text on Path

Phase 4D keeps text editable while introducing a persistent, object-owned path
layout stage. Source text, shaping, glyph outlines, path layout, effects,
deformation, and the object transform remain separate stages.

## Persistent model

`TextObject::path` is an optional `PathGeometry`. Each path has a stable ID and
stable node IDs. Nodes contain anchors plus optional absolute object-local cubic
handles. `PathTypographyProperties` stores the enabled flag, owned path ID,
start and baseline offsets, reverse traversal, side flip, tangent following, and
the closed-path overflow policy.

The project schema is version 7. Version 6 and older documents migrate with path
layout disabled. Current-schema load rejects missing or duplicate path/node
identities, unrelated `pathLayout.pathId` values, non-finite coordinates, and
aggregate path budgets. Save is atomic and validates the same limits.

## Geometry contract

`PathArcLengthTable` samples each line or cubic segment with bounded adaptive
subdivision. Placement queries use geometric distance, never raw cubic
parameters. Open paths clip a complete glyph when its advance would exceed the
path. Closed paths wrap by total length. Degenerate paths clear glyph pieces so
they cannot pile every glyph onto one endpoint. Layout preserves glyph cluster
and line metadata and runs before effects and manual deformation. `reverse`
changes traversal order; `flip` negates only the path normal, moving text to the
opposite side without mirroring or turning the glyph outline. Each source line
uses the same path and receives its deterministic `lineBounds.y()` normal
offset.

After successful placement, each visible path-layout piece receives transient
`GeometryPiece::effectReferenceProgress`: the normalized leading-anchor
distance returned by the same `PathArcLengthTable` traversal used for layout
(`PathPosition::distance / totalLength`). Closed paths use the table's wrapped
distance, and `reverse` builds the table from the reversed traversal. Wave and
progression-dependent procedural modes consume this scalar; ordinary text
retains its existing anchor-based fallback. `originalAnchor` remains immutable
source-layout metadata. Progress is not serialized, is unaffected by baseline
offset, flip, or later object transforms, and is published atomically with the
successful path stage and its caches.

## Editing contract

The Typography inspector exposes path creation, enablement, offsets, traversal,
side, tangent, closure, geometry reversal, and removal. The Path Edit tool draws
the object-local path through the current `ObjectFrame`; anchors and handles are
dragged in page space and converted back to object-local coordinates. Double-click
uses curve-aware nearest segment hit testing and true de Casteljau splitting,
including the closed seam. `C` converts a selected line segment to a cubic, `L`
converts a cubic segment back to a line, Delete removes a selected node when the
path remains valid, and Escape cancels the active drag. Stable path/node identity
is retained across async refreshes; edits carrying a stale spatial revision are
rejected.

Every committed node edit is an undoable command targeted by object ID. The
canvas sends the spatial revision captured at mouse press. The controller checks
the current page/layer, authoritative frame, path identity, and finite bounded
geometry before accepting the command. Transient path previews are never
serialized or used as frame authority.

## Verification checklist

- Straight and cubic arc-length placement use distance-based queries.
- Open overflow clips whole glyphs; closed overflow wraps; degenerate paths do
  not endpoint-pile glyphs.
- Reverse traversal and editable-path reversal are distinct and deterministic;
  reversing geometry twice restores the exact semantic path.
- Path effects/deformation see post-layout geometry, and transform is still
  applied exactly once at scene/export boundaries.
- Generated geometry copies preserve transient path progression, while
  interrupted path layout publishes neither partial geometry nor partial
  progression metadata; structured geometry signatures observe the metadata.
- Save/load, copy/assignment, undo/redo, duplicate, paste, and page clone retain
  or freshen path identities according to the hierarchy contract.
- Cancellation and aggregate path budgets stop work without publishing partial
  geometry.
- UI editing rejects stale spatial revisions and remains usable after async scene
  publication.
