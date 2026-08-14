# VectorArtCreator Architecture Invariants

These are normative engineering rules for the current editor. `MUST`, `MUST NOT`,
`SHOULD`, and `MAY` have their usual requirements-language meaning. A feature is
not complete until the applicable invariants are enforced in production code and
covered at the lowest useful test layer.

## 1. Authority and semantic state

- `Document` is the only authority for persisted editor state. Widgets, selection
  models, published `SceneGeometry`, caches, previews, and clipboard payloads are
  projections of that state.
- Every persisted field of `TextObject`, `Layer`, `Page`, `Effect`, and
  `Document` MUST survive value copy, assignment, save/load, undo/redo, and any
  workflow that promises semantic duplication. Adding a field requires updating
  all five contracts in the same change.
- A value copy MUST preserve semantic identity and every semantic value exactly.
  Workflows that create a new entity MUST freshen IDs explicitly at that workflow
  boundary; copy constructors MUST NOT silently invent IDs.
- Transient state MUST be named and owned as transient state. A field MUST NOT be
  serialized while tests and controller workflows simultaneously treat it as
  disposable.

## 2. Identity and hierarchy

- Page, layer, text-object, effect-instance, path, path-node, region, region
  contour, and region-node IDs MUST be nonempty and globally unique within a
  document.
- `currentPageId` MUST identify a page in the document. `activeLayerId` MUST
  identify a layer on that current page. Active and selected object IDs MUST
  identify objects on that current page. A selected effect ID MUST identify an
  effect on the active object.
- Commands MUST target stable IDs, not current indices, current selection, or
  whichever entity a lookup happens to return first.
- A missing command target MUST make the command a safe no-op or a reported
  invariant failure. It MUST NOT redirect the mutation to another entity.
- Deserialization MUST reject duplicate or ambiguous identities before replacing
  the live document. It MAY repair an absent active ID to a documented default,
  but MUST NOT repair collisions silently.
- Current-schema IDs MUST be validated exactly as persisted. Historical schemas
  that did not guarantee IDs MAY receive deterministic path-derived identities;
  repeated migration of the same bytes MUST produce the same hierarchy.
- Duplication, paste, preset application, and page cloning MUST state which
  identities are preserved and which are freshened. The rule MUST be consistent
  at every hierarchy depth.
- A text path is owned by exactly one text object. `pathLayout.pathId` MUST be
  empty when no path is present and MUST match the owned path ID otherwise.
  Path-node IDs participate in the same global identity contract.
- A text region is owned by exactly one text object. `regionLayout.regionId`
  MUST be present and match the owned region ID in Region mode. The region MUST
  have one closed, simple outer contour; holes MUST be closed, simple, wholly
  contained, non-overlapping, and non-nested. Region contour/node IDs
  participate in the same global identity contract.

## 3. Undo, dirty state, and gestures

- Every persisted user mutation MUST become undoable before or at the moment it
  becomes observable in the document.
- `isModified()` MUST become true on the first persisted mutation, including the
  first movement of a live slider or pointer gesture. Losing focus, switching
  selection, opening/newing a project, or closing the window MUST end or cancel
  an active gesture deterministically.
- A merged edit whose final semantic value equals its initial value MUST be
  obsolete and MUST restore the undo stack's clean state.
- One user gesture SHOULD produce one undo step. Preview state MAY be mutable and
  non-undoable only while it is clearly separate from the persisted document.
- Undo and redo MUST restore the complete persisted payload of the affected
  entity. They MUST preserve stable IDs unless the command is specifically an
  add/remove operation whose identity contract says otherwise.
- Undo callbacks MUST not expose a half-applied hierarchy to selection repair,
  scene rebuilding, serialization, or UI refresh.

## 4. Async evaluation and lifetime

- An evaluation task MUST capture an immutable, semantically complete value
  snapshot. It MUST NOT capture controller, widget, selection-model, or live
  document pointers.
- Published results MUST carry a monotonically increasing generation, spatial
  revision, and page ID. Only the current generation and exact current spatial
  revision for the current page may become visible.
- At most one newest pending snapshot MAY replace older pending work. Long-running
  current work MUST have a cancellation or bounded-work contract so a newer edit
  cannot wait indefinitely.
- Watcher callbacks MUST be context-bound to a live owner. Destruction MUST make
  late completion harmless.
- A published scene is a cache, never authority for accepting a new mutation.
  Spatial input MAY use a scene frame only if that frame is proven to match the
  current document revision for that object. Otherwise the gesture MUST be
  deferred/rejected or use a synchronously derived current frame.
- A preview scene MUST be marked transient and MUST NOT seed the authoritative
  frame cache. Page-space pointer input is transient; serializers MUST reject it
  unless it was normalized into the persistent object-local coordinate space.
- A preview belongs to the exact persisted revision that created it. Any semantic
  command, undo/redo, page change, New, or Open MUST invalidate deformation and
  mask previews before evaluating the next revision.
- A thread-local stage cache MUST publish a reusable value/key pair only after
  that stage completes while work remains Running. Cancelled or exhausted
  partial geometry MUST be overwritten before any later cache hit is possible.
- Async tests MUST compare semantic geometry/cache keys or a geometry signature,
  not a source-text field copied directly from the input snapshot.

## 5. Coordinate spaces and geometry

- Shaping, base geometry, effects, masks, and manual deformation operate in
  object-local coordinates. The object transform is applied exactly once at the
  scene/export boundary.
- Every persisted point, vector, radius, and rectangle MUST have an explicit
  coordinate-space contract. Page coordinates MUST never be stored with an
  `ObjectLocal` tag.
- Points use the full affine transform; displacement vectors exclude translation;
  radii use the documented scale policy. Non-uniform-scale behavior MUST be
  tested explicitly.
- A pivot MUST be explicit before a transform command is committed. It MUST NOT
  later change because asynchronous bounds arrived or source text was reshaped.
- Axis-aligned bounds are broad-phase accelerators only. Hit testing, marquee
  selection, mask proximity, and other semantic spatial decisions MUST perform an
  appropriate narrow-phase test against the transformed frame or actual contour.
- Geometry must remain finite and bounded after every stage. Generator and
  subdivision caps are part of the public effect/deformation contract.
- If enabled, path layout MUST run after glyph geometry and before effects. Its
  arc-length sampling MUST be bounded, distance-based, and cooperative with the
  shared work budget. Open overflow MUST clip complete glyph pieces; degenerate
  paths MUST NOT pile all glyphs onto one endpoint.
- Region layout MUST run after shaping/glyph geometry and before effects and
  deformation. It MUST use vector scanline line bands derived from the outer
  contour minus holes; unrestricted placement followed by a render clip is not
  conforming. It MUST choose one widest continuous safe interval per line,
  using the leftmost span as the deterministic equal-width tie-break, preserve
  shaping clusters/UTF-16 spans, apply padding and alignment, and terminate
  overlong clusters without endpoint pile-up. Region topology flattening and
  interval work MUST honor the shared cancellation/budget contract.

## 6. Text, fonts, and source ranges

- Source offsets and effect ranges use UTF-16 code-unit offsets consistently,
  because that is the indexing model exposed by Qt strings and glyph runs.
- A glyph cluster's span MUST be derived from the complete logical cluster map for
  the line, across all physical-font and bidi runs. A per-run terminal glyph MUST
  NOT claim the remainder of the line.
- Range operations MUST not split a surrogate pair, combining sequence, or shaped
  cluster. Tests MUST include Cyrillic, fallback-font runs, emoji/supplementary
  characters, combining marks, and mixed bidi text.
- Tracking order and direction MUST have one documented visual/logical contract
  across multiple glyph runs. Line breaks MUST reset only the state that contract
  says they reset.
- Path layout MUST consume preserved glyph advance, source-cluster, and line
  metadata without rewriting UTF-16 cluster ownership.
- Region line breaking MUST consume the same preserved glyph advance,
  source-cluster, and line metadata. It MUST NOT split ligatures, combining
  clusters, fallback clusters, bidi spans, or surrogate pairs. Region effect
  progress MUST follow logical layout order rather than raw X or contour arc
  order.
- Missing family, missing style, and per-glyph fallback are distinct states and
  MUST remain distinguishable to the user.
- Project rerendering is system-font dependent until font fingerprints or embedded
  resources are actually enforced. The UI and file format MUST not promise exact
  cross-machine typography before that exists.

## 7. Effects, generators, masks, and deformation

- `EffectDescriptor` capability flags are executable policy. UI affordances and
  controller entry points MUST reject unsupported mask/range operations.
- Stack strength zero MUST be exact identity for geometry, piece count, opacity,
  and metadata. Each effect's neutral parameter values MUST have explicit oracles,
  not only finite-output checks.
- Generator effects MUST define range and mask semantics. If unsupported, no mask
  may be painted, stored, serialized, or presented as effective.
- A mask's influence MUST be based on contour/stroke proximity (or another clearly
  documented shape metric), not merely overlap with an axis-aligned bounding box.
- Effect ordering is semantic. Save/load, copy, undo/redo, cache keys, and export
  MUST preserve it.
- Random-looking effects MUST be deterministic from persisted seeds and stable
  semantic inputs. They MUST NOT depend on hash iteration order, wall-clock time,
  thread scheduling, or process-global mutable RNG state.
- Deformation and mask data MUST have per-item and aggregate limits. Evaluation
  cost MUST be bounded as a function of strokes, samples, pieces, and contour
  points.
- One shared work-control status MUST distinguish running, cancellation, and
  budget exhaustion across expensive stages. A stage that observes interruption
  MUST stop cooperatively; its partial geometry MUST NOT become a completed scene
  or output artifact.
- The work budget boundary is inclusive and overflow-safe: the exact maximum MAY
  complete, the first unit over MUST select BudgetExceeded, and the first
  terminal state MUST win across repeated cancellation and all copied handles.

## 8. Serialization and migration

- Load is transactional: parse and validate into a temporary document, then
  replace the live document only after all hierarchy, identity, numeric, and
  workload checks succeed.
- All persisted numbers MUST be finite and within documented production bounds.
  All arrays and input byte sizes MUST have per-container and aggregate limits.
- Bounds MUST cover combined processing cost, not only independent child counts.
  Project and private-clipboard bytes and aggregate hierarchy/text/effect/mask/
  deformation/path/region products MUST be rejected before constructing the live
  model. Region contour/node/topology work MUST have explicit per-object and
  aggregate limits.
  Composite estimates MUST use saturating arithmetic; no count product may wrap
  into an admissible value.
- Migrations MUST be deterministic, idempotent at the current schema, and covered
  by fixtures for every supported source version.
- Unknown forward-compatible data MUST be retained only in the designated future
  data containers. Known malformed data MUST fail with a precise path-oriented
  error; it MUST NOT be coerced into an ambiguous state.
- Save MUST remain atomic (`QSaveFile` or equivalent). A failed save MUST leave the
  previous file intact and the undo clean index unchanged.
- The serialization field list, copy field list, cache-key field list, semantic
  fingerprint, and invariant checker MUST evolve together.

## 9. UI editing sessions

- At most one component owns a live edit buffer for a semantic field. If canvas
  and inspector editors are both visible, they MUST synchronize bidirectionally
  with reentrancy guards and cursor preservation, or focus transfer MUST commit
  and close the previous owner first.
- A hidden or unfocused native editor MUST NOT receive editor shortcuts or emit a
  stale whole-buffer replacement.
- Selection, page, visibility, lock, document, and tool changes MUST end an
  incompatible native session synchronously, before an async scene refresh.
- UI enablement MUST derive from the same capabilities enforced in the controller;
  disabling only the widget is insufficient.
- Path-node gestures MUST carry the captured spatial revision and stable object/
  path identity; stale or locked gestures MUST be rejected without mutation.
- Region contour gestures MUST carry the captured spatial revision and stable
  object/region-contour identity; stale, open, self-intersecting, or locked
  edits MUST be rejected without mutation. Region settings and padding gestures
  MUST remain undoable and synchronize with the inspector without signal loops.
- Success messages MUST describe what actually succeeded. Partial platform output
  failures MUST be surfaced as warnings.

## 10. Export and platform boundaries

- Export evaluates a current, semantically complete document snapshot. It MUST NOT
  depend on whichever async scene happens to be published.
- Export scope, object order, text multiplicity, transform, opacity, effect order,
  and geometry must be preserved. Equal source strings from distinct objects are
  still distinct records.
- SVG remains path-only unless the format contract changes explicitly. No hidden
  dependency on installed fonts may remain in the exported artifact.
- Clipboard publication MUST distinguish complete success, partial/fallback
  success, cancellation, and complete failure. Platform resources transfer
  ownership only after the platform API confirms success; untransferred handles
  remain the producer's cleanup responsibility.
- Registration, allocation, lock, and publication failures for every fallback
  format MUST leave each handle in exactly one state: transferred once or freed
  once. Clipboard open retry/close and `EmptyClipboard` failure follow the same
  single-owner rule.
- SVG/file export MUST retain its previous atomic output when interrupted.
  Clipboard rendering MAY be cancelled only before its short ownership-transfer
  transaction begins; no cancelled render may be reported as publication success.
- Platform-specific tests MUST skip only for a proven environmental precondition.
  An arbitrary production failure MUST fail the test.

## 11. Required test gates

For every new persisted field or public workflow, add the applicable gates:

1. direct copy-constructor and copy-assignment equality;
2. current-version round trip and relevant migration fixtures;
3. undo/redo plus clean-index behavior, including a net-zero edit;
4. async snapshot and stale-generation race coverage;
5. geometry signature or semantic visual oracle;
6. transformed/non-identity coordinate test;
7. malformed-input boundary and one-over-limit rejection;
8. real-widget handoff test when multiple controls edit the same state;
9. export/clipboard contract test when output is affected; and
10. invariant check after duplicate, paste, delete, page/layer move, undo, and redo.
11. deterministic semantic-work cancellation when the workflow can be expensive;
12. path geometry and layout: arc-length math, overflow/degenerate behavior,
    path-schema migration, identity freshening, undo/redo, stale gestures, and
    post-layout effect/deformation ordering;
13. region geometry and layout: outer/hole topology, cubic scanlines,
    concavity, widest-continuous-interval selection and deterministic ties,
    event/slab full-band safety, shaping-safe wrapping, real bidi/RTL cluster
    ownership, explicit unbreakable-word fallback, alignment, padding, Clip
    termination, cancellation/cache recovery, v8 migration, identity
    freshening, controller undo/redo, post-layout effect ordering, export, and
    real-widget inspector synchronization.

All first-party MSVC targets MUST build warning-clean under `/W4 /WX`. External
dependency headers MAY be demoted through the compiler's external-header policy;
repository-owned warnings MUST NOT be globally suppressed.

The invariant checker itself is production-adjacent specification code. It MUST
verify locality as well as global existence, include every persisted semantic
field needed for freshness, and never be the only place an invariant is enforced.
