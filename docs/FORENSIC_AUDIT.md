# VectorArtCreator Forensic Engineering Audit

## Phase 4R closure report (2026-08-12)

The detailed audit below is an immutable historical analysis of commit
`8fb4536737c3df4e30c7dacaa830d440984549e4`; its present-tense defect statements
describe that audited revision, not the repository after Phase 4R. Phase 4R was
rebased on merged `main` at `670557ab1c6130149529a831d5daf9a05b1e9328`,
re-audited every P1/P2 entry, and used the merged Phase 4T fortress as the
regression baseline.

Seven findings had already been repaired by work merged after the audit. Their
production paths were left alone and their permanent regressions were verified.
The remaining seven findings were reproduced from code or deterministic seams
and repaired in `codex/phase-4r-forensic-correctness` (draft PR #11).

| ID | Phase 4R status | Production closure | Permanent evidence |
| --- | --- | --- | --- |
| P1-01 | CLOSED before 4R; re-verified | Value copies preserve `effectStackStrength`. | `semanticCopyContractCoversPersistentInventory`, `semanticFingerprintExcludesOnlyDeclaredTransientState`, `registeredEffectContract` |
| P1-02 | CLOSED before 4R; re-verified | Canvas and inspector use one live text-edit owner/handoff. | `inspectorEditEndsCanvasSessionWithoutStaleOverwrite` |
| P1-03 | CLOSED in 4R | A monotonically comparable spatial revision stamps snapshots, scenes, and frames. Persisted page input is normalized only through an authoritative current frame; stale transform commits are rejected; transient previews are invalidated at every semantic revision boundary. | `staleFrameCannotAuthorizeSpatialMutation`, `transientPreviewNeverBecomesDocumentOrFrameAuthority` |
| P1-04 | CLOSED in 4R | Current schemas reject missing/colliding global identities and wrong-hierarchy active IDs transactionally; v1-v3 identities migrate deterministically through current-schema save/reload and later undo/redo. | `currentSchemaLoaderRejectsIdentityCorruption`, `historicalIdentityMigrationIsDeterministic`, `legacyV1V2V3MigrationSurvivesSaveReloadAndUndoRedo`, `serializedResourceBudgetsHaveExactBoundaries` |
| P1-05 | CLOSED in 4R | Pre-construction aggregate resource limits, saturating composite estimates, and shared cooperative semantic work controls bound evaluation and export. Exact terminal boundaries are first-state-wins; cancelled partial stages cannot poison caches, publish, or commit output. | `serializedResourceBudgetsHaveExactBoundaries`, `workControlHasExactSharedTerminalBoundaries`, `evaluationCancellationDoesNotPoisonWorkerCaches`, `cooperativeWorkBudgetAndCancellationAreDeterministic`, `cancelledSvgNeverCommitsPartialOutput`, `cancellationStopsBeforeClipboardPublication` |
| P2-01 | CLOSED in 4R | Cluster spans come from the complete line's sorted unique UTF-16 boundaries across all glyph runs and retain their range semantics through geometry, persistence, and export. | `logicalClusterSpansUseWholeLineContext`, `mixedUtf16ShapingUsesGlobalClusterSpans`, `mixedUtf16ClustersSurviveEffectsPersistenceAndExport` |
| P2-02 | CLOSED before 4R; re-verified | Registry capabilities gate mask UI and controller mutations. | `maskAndTextRangeRespectDescriptorClaims`, `unsupportedEffectMaskIsRefusedWithoutMutation`, `unsupportedEffectDisablesMaskUiAcrossRefreshes` |
| P2-03 | CLOSED in 4R | Filled-path containment and bounded contour-segment distance replace AABB/proxy authorization; invalid inputs and cancellation yield no partial influence. | `contourMaskDistanceRejectsHolesAndConcavities`, `contourMaskDistanceHandlesAdversarialGeometryAndCancellation` |
| P2-04 | CLOSED before 4R; re-verified | Page duplication freshens the complete page/layer/object/effect identity hierarchy. | `duplicatePageFreshensEntireIdentityHierarchy` |
| P2-05 | CLOSED before 4R; re-verified | Net-zero merged commands become obsolete and restore the authoritative undo clean index. | `mergeableCommandReturningToStartRestoresClean` |
| P2-06 | CLOSED in 4R | Style Intensity pushes a mergeable undo command on first movement; tokens isolate physical gestures, object switches, and save/clean boundaries; no persisted slider mutation occurs outside `QUndoStack`. | `styleIntensityGestureHasImmediateDirtyTruthAndOneUndoStep` |
| P2-07 | CLOSED before 4R; re-verified | Marquee bounds are broad phase followed by transformed ink narrow phase. | `rotatedMarqueeUsesInkAsNarrowPhase` |
| P2-08 | CLOSED before 4R; re-verified | Ordered export records preserve equal-text object multiplicity. | `exportPlainTextPreservesEqualObjectMultiplicity` |
| P2-09 | CLOSED in 4R | Windows publication reports Complete, Partial, Cancelled, or Failure; the injected Win32 matrix proves registration/allocation/lock/publication, retry/empty/close, cleanup, and ownership transfer for EMF/SVG/PNG/Unicode. | `publicationResultClassification`, `injectedOperationsClassifyFailuresAndOwnership`, `cancellationStopsBeforeClipboardPublication`, `copyForWordPublishesPortableFormats` |

Unresolved forensic P1/P2 findings: **none**. Remaining platform/manual and
telemetry limits are listed explicitly in `docs/TEST_FORTRESS_GAPS.md`; they do
not weaken the repaired contracts. The release gate is a same-head-SHA Windows
GitHub Actions configure/build/labelled-CTest/package run. Draft PR #11 records
the exact final run and artifact, which are necessarily generated after this
versioned report is committed. The gate also compiles all first-party targets
with MSVC `/W4 /WX` while treating dependency headers as external.

---

## Historical audit snapshot

- Audit date: 2026-08-11
- Audited branch: `codex/phase-4t-test-fortress`
- Audited commit: `8fb4536737c3df4e30c7dacaa830d440984549e4`
- Scope: the complete tracked repository at that commit, including history, build
  configuration, production code, tests, workflow documentation, serialization,
  export, and Windows clipboard integration.
- Change policy: documentation only. No production implementation or test was
  modified.

Confidence vocabulary:

- **CONFIRMED**: the incorrect contract follows directly from the implementation;
  no environment-dependent premise is needed.
- **HIGH CONFIDENCE**: the execution path is present and the failure follows when
  a realistic timing/font/workload precondition occurs, but that precondition was
  not forced in a local executable during this audit.
- **SUSPICION**: evidence warrants a targeted test, not a defect claim.

Priority vocabulary:

- **P0**: immediate release stop; unavoidable catastrophic loss or compromise.
- **P1**: next-feature blocker; silent wrong output/state, plausible data loss, or
  an unbounded application stall.
- **P2**: material but localized correctness, workflow, or invariant failure.
- **P3**: minor hardening/UX/test debt with a contained consequence.
- **Observation**: limitation, tradeoff, or architectural fact rather than a bug.

## 1. Executive summary

No P0 was found. The current head does, however, contain five P1 defects and nine
P2 defects. The most consequential is a missing field in `TextObject`'s hand-written
copy constructor and assignment operator: `effectStackStrength` is serialized and
used by evaluation, but every page/object value snapshot resets it to `1.0`. This
breaks the feature in normal async rendering and export, and also loses it through
duplication and several undo payloads.

The other release-significant themes are:

1. two live text editors can overwrite each other with a stale whole-buffer value;
2. brush, mask, and pivot commands sometimes treat an asynchronous scene frame as
   current model authority;
3. the loader accepts duplicate hierarchy identities and ambiguous active-ID
   locality even though commands depend on unique stable IDs; and
4. persisted mask/deformation data has no aggregate work budget, while export runs
   evaluation synchronously and background evaluation cannot cancel current work.

The repository is not structurally unsound. Its page/layer/object model, stable-ID
command direction, object-local geometry pipeline, atomic save, value-snapshot
worker boundary, generation-gated publication, deterministic effect seeds, and
path-only SVG boundary are good foundations. The right response is a focused
correctness pass, not a rewrite.

### Finding index

| ID | Priority | Confidence | Short name |
|---|---:|---|---|
| P1-01 | P1 | CONFIRMED | `TextObject` copies omit stack strength |
| P1-02 | P1 | CONFIRMED | Canvas and inspector text buffers overwrite each other |
| P1-03 | P1 | HIGH CONFIDENCE | Stale/missing scene frames authorize spatial mutations |
| P1-04 | P1 | CONFIRMED | Project load accepts identity collisions and wrong-page active IDs |
| P1-05 | P1 | HIGH CONFIDENCE | Persisted workloads are aggregate-unbounded and noncancellable |
| P2-01 | P2 | HIGH CONFIDENCE | Cluster spans are computed per glyph run instead of per line |
| P2-02 | P2 | CONFIRMED | Mask UI ignores effect capabilities; generator masks are no-ops |
| P2-03 | P2 | HIGH CONFIDENCE | Mask “contour” proximity is an AABB intersection shortcut |
| P2-04 | P2 | CONFIRMED | Page duplication preserves effect instance IDs |
| P2-05 | P2 | CONFIRMED | Net-zero merged edits remain dirty |
| P2-06 | P2 | HIGH CONFIDENCE | Live style-intensity mutation can remain “clean” |
| P2-07 | P2 | CONFIRMED | Rotated marquee selection uses only page AABBs |
| P2-08 | P2 | CONFIRMED | Clipboard plain text deduplicates equal objects |
| P2-09 | P2 | CONFIRMED | Partial clipboard publication is reported as full success |

### Test evidence and limitation

The local machine has no discoverable CMake, C++ compiler, Qt toolchain, Ninja,
qmake, or existing build tree, so a fresh local configure/build/CTest run was not
possible without installing a substantial toolchain. No dependency installation
was authorized or performed. The repository's exact audited SHA does have a
successful Windows GitHub Actions run: [Windows Qt CI run 31507686307](https://github.com/iga444cvbn-netizen/VectorArtCreator/actions/runs/31507686307),
including configure, build, full labelled CTest execution, and packaging. That is
useful same-SHA evidence, but it is not represented here as a fresh forensic run.

No temporary diagnostic file was created. The only audit changes are this file and
`docs/ARCHITECTURE_INVARIANTS.md`.

### History signal

- `bc847ef` introduced `effectStackStrength`; the pre-existing copy special
  members were not extended. `git log -S effectStackStrength` isolates that
  provenance.
- `9830fd3` established the object-local frame conversions and the latest
  sequential-in-worker evaluation design. Its missing-frame fallback stores the
  unconverted page positions.
- `5f10f2d` established the Test Fortress. It added useful invariants and seeded
  replay, but its operation alphabet and geometry oracles are narrower than its
  documentation implies.
- `8fb4536` deliberately freshened effect IDs for object duplication, paste, and
  preset application, but page duplication remains outside that repair.

## 2. Current architecture map

### Static map

| Layer | Primary files | Current responsibility |
|---|---|---|
| Document model | `src/core/document/document.*` | `Document -> Page -> Layer -> TextObject`, stable IDs, copyable semantic payloads |
| Text/font | `src/core/text/*` | `FontDescriptor`, Qt shaping, glyph-run metadata, vector glyph construction |
| Effects | `src/core/effects/*` | registry/descriptors, ordered nondestructive stack, ranges, masks, deterministic transforms/generators |
| Deformation | `src/core/deformation/*` | persisted object-local brush samples, contour sampling, glyph/shape deformation |
| Frames/scene | `src/core/scene/*` | local/page matrices, staged caches, page evaluation, immutable published geometry |
| Persistence | `src/core/serialization/*` | JSON schema v6, v1-v5 migrations, atomic `QSaveFile` save |
| Undo | `src/core/undo/*` | stable-ID document/scene commands and mergeable continuous edits |
| Export | `src/core/export/*` | selection/page payload construction and path-only SVG output |
| Platform | `src/platform/*` | portable facade plus Windows EMF/SVG/PNG/text clipboard transaction |
| Controller | `src/ui/editor_controller.*` | workflow orchestration, selection synchronization, undo stack, async generation gate |
| Widgets | `src/ui/*panel*`, `main_window.*`, `editor_canvas.*` | canvas interaction, native text editing, inspectors, layer/page controls |
| Tests | `tests/*` | core, effect contracts, integration/replay, smoke, UI regression, Windows clipboard |

### Normal edit/evaluate path

`MainWindow`/panel/canvas signal -> `EditorController` validation -> `QUndoStack`
command -> `Document` mutation -> `onCommandChanged()` -> value-copy current page
-> one `QtConcurrent` page task -> sequential object shaping/base/effect/deformation
caches -> object transform -> generation check -> publish `SceneGeometry` -> canvas.

The worker boundary is page-wide and safe by value. Object evaluation inside that
worker is intentionally ordered and sequential (`SceneEvaluator::evaluate`,
`src/core/scene/scene_evaluator.cpp:214-246`) to avoid nested global-thread-pool
starvation. Exactly one newest pending page snapshot replaces older pending work
(`EditorController::rebuildScene`, `src/ui/editor_controller.cpp:1891-1932`).

### Save/open path

Save serializes the live document to schema v6 and commits via `QSaveFile`.
Open parses into a temporary `Document`, performs structural migration and limited
active-ID repair, moves the result into the controller, clears undo, repairs
selection, and schedules a new scene. Numeric workload and global identity
validation are incomplete; see P1-04 and P1-05.

### Export/clipboard path

Export does not trust the published async scene. It value-copies the current page,
evaluates it synchronously, builds a scoped payload, then writes path-only SVG or
publishes Windows clipboard formats. This boundary is conceptually correct, but
P1-01 corrupts the copy and large evaluation can block the UI. Windows clipboard
uses EMF as the required format with SVG, PNG, and Unicode text as fallbacks.

### Identity/undo path

Commands generally store object/layer/page IDs and payload values. Global document
lookups return the first matching ID. That is safe only while uniqueness is a hard
invariant. The test invariant checker asserts global uniqueness, but production
deserialization does not enforce it. Copy semantics are therefore as critical as
ID semantics: command payloads, page snapshots, and cloned hierarchy values all
pass through the same hand-written `TextObject` copy members.

## 3. Confirmed bugs

There are no confirmed P0 defects. The following P1/P2 findings are directly
entailed by the current implementation.

### P1-01 — `TextObject` value copies omit `effectStackStrength`

- **Classification:** P1, CONFIRMED.
- **Exact files/classes/functions:** `TextObject::TextObject(const TextObject&)`
  and `TextObject::operator=` in `src/core/document/document.cpp:74-103`; field
  declaration in `src/core/document/document.h:46-48`; page snapshot callers
  `EditorController::buildExportPayload` at `src/ui/editor_controller.cpp:1862-1878`
  and `EditorController::rebuildScene` at `1891-1932`; secondary copy in
  `SceneEvaluator::evaluate` at `src/core/scene/scene_evaluator.cpp:221-243`.
- **Execution path:** a user changes Style Intensity -> the live object stores the
  value -> controller copies its page for async evaluation/export -> `Layer`/`Page`
  deep copy invokes `TextObject` copy -> the omitted field uses its member default
  `1.0` -> effect evaluation and cache key see `1.0`. Duplication, paste-to-command,
  delete/undo, page/layer add/remove, and move-to-layer payloads also copy the
  object through `src/core/undo/scene_commands.h:23,40,73,128,146,224,240`.
- **Preconditions:** any object with stack strength other than `1.0`; no timing or
  platform condition is required.
- **Why it fails:** serialization correctly reads/writes the field
  (`project_serializer.cpp:23,56-57`) and evaluation correctly consumes it
  (`scene_evaluator.cpp:88-92,149-153`), but both copy special members omit it.
- **User/system consequence:** Style Intensity is ineffective in normal rendered
  scenes and export; the UI/model can say `1.5` while output uses `1.0`. Several
  duplicate/undo/paste workflows silently reset the persisted value.
- **Minimal reproduction:** create text, apply a visible effect, set Style
  Intensity to `0` or `1.5`, wait for scene publication, then export or duplicate.
  Compare the live object's value with the snapshot/duplicate and geometry. A
  direct `TextObject copy(source)` check reproduces the reset without UI.
- **Existing-test status:** `firstFiveMinutesCanary` sets `1.5` but checks only
  visible finite geometry/invariants. `effectStackStrengthZeroIsIdentity` applies
  an effect stack directly. The serializer round trip does not set/assert this
  field. No copy-contract test exists.
- **Smallest safe fix:** add `effectStackStrength(other.effectStackStrength)` to
  the copy constructor and assign it in `operator=`. Do not freshen IDs in the
  special members.
- **Required regression test:** direct copy and assignment equality at nondefault
  strength; serializer round trip; async scene geometry signature at strengths 0,
  1, and 1.5; export signature; and preservation through object/page duplicate,
  paste, move/delete undo, and redo.

### P1-02 — native canvas text and inspector text can overwrite each other

- **Classification:** P1, CONFIRMED.
- **Exact files/classes/functions:** `EditorCanvas::beginTextEditing` and its
  whole-buffer `textChanged` connection in `src/ui/editor_canvas.cpp:172-260`;
  `EditorCanvas::setScene` at `56-80`; `EditorCanvas::eventFilter` at `820-857`;
  `MainWindow` editor/inspector connections at `src/ui/main_window.cpp:292-313`
  and `339-360`; `MainWindow::refreshUi` at `538-569`;
  `TypographyPanel::refresh` at `src/ui/typography_panel.cpp:212-280`.
- **Execution path:** double-click text -> canvas creates a `QPlainTextEdit` and
  seeds it once -> click the inspector text editor (outside the canvas viewport)
  -> inspector edits update `Document` and refresh both scene and inspector -> the
  still-live canvas editor is neither closed nor synchronized -> return to it and
  type -> it emits its stale entire buffer -> `EditorController::setText` replaces
  the newer inspector value.
- **Preconditions:** native text session remains open while the same object's text
  or font is changed through the typography inspector. Clicking the sidebar does
  not hit the viewport-only outside-editor filter and focus loss is not handled.
- **Why it fails:** there are two mutable buffers for one semantic property. UI
  refresh updates `TypographyPanel`, while `EditorCanvas::setScene` updates only
  geometry/session validity and never the editor text/font.
- **User/system consequence:** silent loss of the user's newer text; font, size,
  and editor display can also remain stale until the native session is restarted.
- **Minimal reproduction:** start canvas editing `abc`; click inspector and change
  it to `XYZ`; click the still-visible native editor and append `d`. The model
  becomes `abcd`, losing `XYZ`.
- **Existing-test status:** UI tests cover add-text focus, editor alignment, and
  ending a session when another object is selected. They never hand one object's
  text between the two real editors.
- **Smallest safe fix:** pick one live owner. The least risky patch is to commit
  and end the native session when an inspector field for that object takes focus.
  If simultaneous editing is desired, add guarded bidirectional synchronization
  with cursor/selection preservation.
- **Required regression test:** real-widget test that edits the same object in
  canvas -> inspector -> canvas, asserting final text, font, cursor-safe sync,
  one coherent undo history, and no stale overwrite.

### P1-04 — deserialization accepts duplicate identities and wrong-page active IDs

- **Classification:** P1, CONFIRMED.
- **Exact files/classes/functions:** `ProjectSerializer::fromJson` in
  `src/core/serialization/project_serializer.cpp:271-403`; hierarchy parsers at
  `125-146` and `184-205`; first-match lookup functions
  `Document::pageById/layerById/objectById` in
  `src/core/document/document.cpp:296-399`; test-only checks in
  `tests/support/invariant_checker.cpp:16-61`.
- **Execution path:** open a syntactically valid project containing two equal
  page/layer/object IDs -> parser constructs both and only checks whether active
  IDs exist somewhere -> controller/commands later call global first-match lookup
  -> UI may display an object from the current page while an ID-targeted command
  mutates the earlier colliding object. An `activeLayerId` that exists only on a
  different page also passes the global check at serializer lines 390-391.
- **Preconditions:** a corrupted, manually edited, older-tool, or shared project
  contains a duplicate/nonlocal ID. The file need not violate JSON or schema
  version checks.
- **Why it fails:** uniqueness and hierarchy locality are treated as test
  assertions, not loader validation. Global lookups necessarily resolve a
  collision to the first instance.
- **User/system consequence:** selection, editing, undo, effects, or save can
  silently target the wrong entity and preserve the ambiguous state on resave.
- **Minimal reproduction:** clone a serialized object JSON entry without changing
  its `id`, place the clone on a later page/layer, make that page current, open the
  file, select the visible clone, and change text. Observe the first global match
  returned by `objectById`. Repeat with duplicate page/layer/effect IDs and with an
  active layer from another page.
- **Existing-test status:** generated documents pass `checkInvariants`, but no
  malformed fixture attempts to load any collision. Serializer tests cover types,
  versions, migration, and round trips only.
- **Smallest safe fix:** before `*document = std::move(result)`, validate nonempty
  global uniqueness for every page/layer/object/effect ID and validate active-ID
  locality. Reject with an exact JSON hierarchy path; do not silently rename
  current-schema identities.
- **Required regression test:** one fixture each for duplicate page, layer,
  object, and effect IDs, plus wrong-page active layer/object. Assert transactional
  failure, precise error text, and unchanged destination document.

### P2-02 — mask affordances ignore capabilities and generator masks do nothing

- **Classification:** P2, CONFIRMED.
- **Exact files/classes/functions:** `EffectDescriptor::supportsMask` in
  `src/core/effects/effect_registry.h:11-23`; unsupported descriptors in
  `effect_registry.cpp:36-45,93-103`; mask enablement in
  `src/ui/main_window.cpp:400-410,538-548`; controller acceptance in
  `src/ui/editor_controller.cpp:1340-1410`; generator fast path in
  `src/core/effects/effect_stack.cpp:254-264`; `TrailEffect::apply` in
  `src/core/effects/trail_effect.cpp:12`.
- **Execution path:** select any effect -> `MainWindow` enables mask painting merely
  because the effect ID is nonempty -> controller stores the stroke -> for Echo,
  Ghost, or Afterimage, `EffectStack::apply` detects `generatesGeometry()` and
  calls the effect before all generic scope/mask logic -> `TrailEffect` reads scope
  but never mask strokes.
- **Preconditions:** paint a mask on a registry entry whose descriptor says masks
  are unsupported, especially a generator.
- **Why it fails:** descriptor metadata is not enforced by either UI or controller,
  and generator evaluation deliberately bypasses mask processing.
- **User/system consequence:** the UI offers an editable, undoable, serialized
  mask that has no visual effect, misleading users and bloating projects.
- **Minimal reproduction:** add Echo, select it, choose Effect Mask, paint over
  part of the text, then undo/redo and save/reopen. The stored stroke changes but
  generated copies remain unchanged.
- **Existing-test status:** mask tests use mask-capable effects. Registry validation
  checks factory/type/domain, not capability enforcement. No real UI generator-mask
  test exists.
- **Smallest safe fix:** gate both canvas enablement and controller entry points on
  the selected effect's descriptor. Reject unsupported persisted edits with a clear
  status. Implement generator masks only as a separate, explicitly designed change.
- **Required regression test:** select each unsupported descriptor through real
  widgets; mask tool must disable/refuse the stroke and serialization must remain
  unchanged. Add a controller-level rejection test so programmatic callers cannot
  bypass the UI.

### P2-04 — page duplication reuses every effect instance ID

- **Classification:** P2, CONFIRMED.
- **Exact files/classes/functions:** `assignFreshEffectInstanceIds` in
  `src/ui/editor_controller.cpp:39-47`; `duplicateSelectedObjects` at `752-776`;
  `duplicateCurrentPage` at `894-923`; global effect-ID invariant in
  `tests/support/invariant_checker.cpp:45-50`.
- **Execution path:** duplicate a page containing effects -> `Page` deep copy
  clones effect stacks and preserves their instance IDs -> page/layer/object IDs
  are freshened -> effect IDs are not. The same helper is correctly called for
  object duplication/paste/preset workflows, but not page duplication.
- **Preconditions:** current page contains at least one effect.
- **Why it fails:** the page clone's ID-remapping loop stops at object identity.
- **User/system consequence:** the document immediately violates its declared
  global identity invariant. Current effect lookup is mostly object-scoped, so the
  immediate visual result can look correct, but selection references, future
  cross-object effect links, diagnostics, and invariant-based workflows are
  ambiguous.
- **Minimal reproduction:** add Wave, call Duplicate Page, enumerate effect IDs
  across both pages, or run `checkInvariants`.
- **Existing-test status:** the seeded action alphabet duplicates objects but has
  no page operations. No duplicate-page invariant test exists.
- **Smallest safe fix:** call `assignFreshEffectInstanceIds(&object->effects)` for
  every nonnull cloned object inside `duplicateCurrentPage`.
- **Required regression test:** page with multiple layers/objects/effects ->
  duplicate -> assert globally unique page/layer/object/effect IDs, semantic effect
  equality apart from IDs, and identity stability through undo/redo.

### P2-05 — merged edits that return to the saved value remain dirty

- **Classification:** P2, CONFIRMED.
- **Exact files/classes/functions:** merge implementations in
  `src/core/undo/document_commands.cpp:97-105,272-279,309-316,346-353,514-565,837-846`
  and `src/core/undo/scene_commands.cpp:208-215`; `EditorController::isModified` in
  `src/ui/editor_controller.cpp:176-179`.
- **Execution path:** save/set the undo clean index -> type or drag a value away
  from its initial value -> return it to the initial value before the merge series
  ends -> `mergeWith` updates only the command's final value -> the command remains
  at an index after the clean index even though its net semantic effect is zero.
- **Preconditions:** a mergeable edit (`SetText`, font size, tracking, line spacing,
  effect parameter/master strength, deformation strength, or object move) returns
  exactly to its starting value.
- **Why it fails:** no merge implementation calls `setObsolete(true)` when final
  equals initial, and dirty state is defined solely as `!QUndoStack::isClean()`.
- **User/system consequence:** unnecessary save/close prompts and a misleading undo
  entry for a document identical to the saved state.
- **Minimal reproduction:** save; type `x` in a text buffer and delete it within
  the same merge chain, or drag tracking away and back; assert semantic fingerprint
  equals the saved fingerprint while `isModified()` remains true.
- **Existing-test status:** undo tests check forward/reverse state, not clean index,
  merged net-zero behavior, stack count, or obsolescence.
- **Smallest safe fix:** after each compatible merge, compare the command's final
  semantic payload with its original payload and mark the command obsolete when
  equal. Centralize equality helpers for floating values and composite scopes.
- **Required regression test:** for every mergeable command family, set the stack
  clean, change/revert, then assert semantic equality, `isClean()`, stack index/count,
  and that a subsequent independent edit still undoes correctly.

### P2-07 — marquee selection treats rotated AABBs as object shape

- **Classification:** P2, CONFIRMED.
- **Exact files/classes/functions:** marquee emission in
  `src/ui/editor_canvas.cpp:615-678`; `EditorController::selectObjectsInRect` in
  `src/ui/editor_controller.cpp:633-645`; `SceneObjectGeometry::visualBounds` set
  from `ObjectFrame::pageAabb` in `src/core/scene/scene_evaluator.cpp:174-179`.
- **Execution path:** draw a selection rectangle -> controller iterates scene
  objects -> selects whenever the rectangle intersects `visualBounds` -> for a
  rotated object, that field is the axis-aligned enclosure rather than its
  transformed frame/paths.
- **Preconditions:** a rotated or skewed/scaled text object's page AABB contains
  empty corner area; marquee intersects only that area.
- **Why it fails:** broad-phase bounds are used as the final semantic predicate.
- **User/system consequence:** apparently empty marquee gestures select objects the
  rectangle never touched, especially around 30-60 degree rotations.
- **Minimal reproduction:** rotate a narrow word 45 degrees and drag a tiny marquee
  through an empty corner of its AABB.
- **Existing-test status:** frame tests validate AABB containment. There is no
  transformed marquee narrow-phase test.
- **Smallest safe fix:** retain AABB rejection as broad phase, then intersect the
  marquee with the transformed local selection quad or union of visible paths,
  according to the documented selection policy.
- **Required regression test:** rotated and mirrored objects with inside-shape,
  empty-AABB-corner, containment, additive, hidden, and locked cases.

### P2-08 — export clipboard text removes duplicate object text

- **Classification:** P2, CONFIRMED.
- **Exact files/classes/functions:** `ExportPayloadBuilder::build` in
  `src/core/export/export_payload_builder.cpp:8-72`, specifically
  `sourceTexts.contains` at lines 46-47; Unicode clipboard publication in
  `src/platform/windows/windows_vector_clipboard_service.cpp:236-247`.
- **Execution path:** export/copy two visible objects with identical source text ->
  each contributes geometry records -> plaintext collection checks value equality
  and adds the string only once -> `CF_UNICODETEXT` contains one line.
- **Preconditions:** two or more included objects have the same nonempty
  `sourceText`.
- **Why it fails:** deduplication confuses equal values with equal object identity.
- **User/system consequence:** plain-text paste loses multiplicity and no longer
  corresponds to the included artwork/object order.
- **Minimal reproduction:** create two `Hello` objects and Copy Current Page; inspect
  payload/plain clipboard text. It is `Hello`, not `Hello\nHello`.
- **Existing-test status:** the payload test uses distinct `Привет` and `World`
  strings. No equality/multiplicity case exists.
- **Smallest safe fix:** append source text once for every included object in scene
  order; deduplicate geometry pieces by object ID only if ever required, never the
  text value.
- **Required regression test:** equal text, empty text, mixed visibility, selection
  order, and current-page order, asserting geometry records and plain-text lines
  agree on object multiplicity.

### P2-09 — partial Windows clipboard publication is reported as full success

- **Classification:** P2, CONFIRMED.
- **Exact files/classes/functions:**
  `WindowsVectorClipboardService::copyForOffice` in
  `src/platform/windows/windows_vector_clipboard_service.cpp:197-255` and
  `MainWindow::copyForWord` in `src/ui/main_window.cpp:729-738`.
- **Execution path:** EMF publication succeeds -> one of SVG, PNG, or Unicode text
  fails -> service writes a warning into the `error` out-parameter but returns
  `true` -> controller propagates true -> `MainWindow` ignores nonempty `error` and
  announces complete success for Word/PowerPoint/other apps.
- **Preconditions:** a fallback allocation/registration/`SetClipboardData` call
  fails after EMF succeeds.
- **Why it fails:** a boolean plus an overloaded error string cannot express
  complete success versus vector-only success, and the caller assumes true means
  no warning.
- **User/system consequence:** users receive a false success message; downstream
  applications that prefer the failed fallback can paste nothing or degraded data
  without explanation.
- **Minimal reproduction:** inject a failure into `setBytes` or Unicode allocation
  after successful EMF transfer and call the MainWindow action.
- **Existing-test status:** the Windows test checks that all formats exist on a
  successful run, but any service failure is converted to `QSKIP` at
  `tests/platform/windows/clipboard_tests.cpp:35-38`. It does not test partial
  publication or UI messaging.
- **Smallest safe fix:** return a structured result (`Complete`, `VectorOnly`,
  `Failure`) with warning text, or at minimum surface a nonempty warning even when
  the boolean is true.
- **Required regression test:** injectable clipboard backend tests for complete,
  fallback-partial, EMF failure, busy clipboard, and ownership transfer; UI must
  show a warning for partial success and success only for complete publication.

## 4. High-confidence defects

These paths depend on a race, font run layout, or adversarial/large input that was
not executable locally during this audit. The implementation and consequence are
nevertheless sufficiently direct to treat them as defects rather than suspicions.

### P1-03 — stale or missing scene frames authorize persistent spatial mutations

- **Classification:** P1, HIGH CONFIDENCE.
- **Exact files/classes/functions:** `EditorCanvas::handleCanvasMousePress` in
  `src/ui/editor_canvas.cpp:579-597`; `EditorCanvas::currentStroke` at `917-937`;
  default coordinate tag in `src/core/deformation/manual_deformation.h:28-47`;
  `EditorController::setObjectTransform` in
  `src/ui/editor_controller.cpp:817-834`; `addEffectMaskStroke` at `1340-1384`;
  `setEffectMaskPreview` at `1387-1410`; async queue/publication at `1891-1976`;
  scene frame contract in `src/core/scene/scene_geometry.h:15-30`.
- **Execution path:** a document mutation schedules async evaluation but the old
  scene remains published -> a brush/mask/transform gesture starts before the new
  generation arrives -> coordinate conversion uses the old `SceneObjectGeometry`
  frame. If the scene has no object yet, deformation falls back to the active ID
  and stores unconverted page positions while `DeformationStroke::coordinateSpace`
  remains `ObjectLocal`. Transform pivot capture similarly succeeds only when a
  scene object happens to be present.
- **Preconditions:** create/select/transform/reshape an object and begin spatial
  input before matching scene publication, or keep a long evaluation in flight.
  The app's deliberate async design makes this a normal timing window.
- **Why it fails:** `SceneGeometry` carries page/object IDs and copied transform,
  but no public generation/model-revision proof for input consumers. Callers treat
  presence as freshness. The missing-scene deformation branch changes values but
  not their coordinate-space tag.
- **User/system consequence:** brush/deformation/mask strokes persist in the wrong
  local location/radius and later drift with transforms; an implicitly derived
  transform pivot can change after text/layout bounds arrive. These are saved and
  undoable wrong edits, not merely stale display.
- **Minimal reproduction:** (a) create text and immediately switch to a deformation
  brush before first publication; paint away from page origin; inspect stored
  samples. (b) transform/resize text, immediately paint an effect mask, then wait;
  compare conversion through old versus current frame. (c) set rotation with
  `hasPivot=false` before first scene, then change source text and observe pivot-
  dependent placement.
- **Existing-test status:** `addTextStartsFocusedAndAlignedBeforeAndAfterScenePublication`
  tests editor overlay placement only. Object-frame tests exercise static point/
  radius transforms. No test performs spatial mutation in a stale-generation
  window, and async tests compare copied `sourceText`, not frame/geometry revision.
- **Smallest safe fix:** introduce an object semantic/frame revision into snapshots
  and published scene objects. Before committing spatial input, require a matching
  revision; otherwise derive an authoritative current frame synchronously or defer
  the gesture. Never persist page values as object-local. Compute an explicit pivot
  synchronously when the first transform is committed.
- **Required regression test:** deterministic blocked-worker tests for create-
  before-publish, transform-then-mask, reshape-then-deform, page switch, and first-
  transform pivot. Assert persisted local samples map back to the pointer's page
  positions under the current frame and remain stable after publication/save/load.

### P1-05 — persisted geometry workloads lack aggregate budgets and cancellation

- **Classification:** P1, HIGH CONFIDENCE.
- **Exact files/classes/functions:** unbounded hierarchy arrays in
  `src/core/serialization/project_serializer.cpp:125-146,184-205,326-366` and
  whole-file `readAll` at `434-443`; unbounded mask points/strokes in
  `src/core/effects/effect.cpp:48-63,114-125` and
  `src/core/effects/effect_stack.cpp:24-69`; deformation limits in
  `src/core/deformation/manual_deformation.cpp:19,268-361`; nested evaluation in
  `src/core/deformation/deformation_evaluator.cpp:127-177,201-234`; sync export at
  `src/ui/editor_controller.cpp:1862-1878`; noncancellable async worker at
  `1891-1954`.
- **Execution path:** load or paste a large valid JSON payload -> each deformation
  may contain 4,096 strokes of 4,096 samples (about 16.7 million samples per
  object), while mask strokes/points and hierarchy counts have no caps -> shape
  deformation loops stroke x sample x sampled contour point (up to 8,192 per
  contour), and mask evaluation loops piece x stroke x segment -> background work
  cannot be interrupted; export repeats evaluation synchronously on the GUI thread.
- **Preconditions:** a shared/crafted project or clipboard object, or a sufficiently
  large organically edited document. Values may stay within all current per-item
  checks.
- **Why it fails:** limits are local maxima rather than one document-wide resource
  budget, and neither evaluator accepts a cancellation/deadline token. The
  “cancellable only at task boundary” comment means the currently running task is
  not cancellable.
- **User/system consequence:** unbounded memory allocation/CPU, a worker that
  starves all newer scene generations, and a frozen export UI. For shared project
  files this is an application-level denial of service.
- **Minimal reproduction:** generate one schema-v6 object with 4,096 valid shape
  strokes each containing 4,096 samples, or millions of mask points; load it and
  trigger scene evaluation/export. A smaller calibrated fixture can demonstrate
  superlinear latency without exhausting the test host.
- **Existing-test status:** tests cover one-over per-stroke sample validation and a
  4,096 generated-piece cap, but not total samples, mask points, hierarchy size,
  input bytes, deadlines, cancellation, memory, or export latency. No benchmark
  budget exists.
- **Smallest safe fix:** define schema budgets for input bytes, pages, layers,
  objects, effects, total mask strokes/points, and total deformation strokes/
  samples; validate while parsing before allocation. Add a cooperative cancellation
  token and periodic work-budget checks to mask/deformation/effect evaluation.
  Keep export synchronous only after a measured small-work guarantee, otherwise
  expose progress/cancel or evaluate off-thread.
- **Required regression test:** exact-boundary and one-over fixtures for every
  budget; aggregate-over-limit with individually valid children; cancellation when
  a newer generation arrives; bounded completion/memory benchmark; and a UI test
  proving export remains responsive or cancellable.

### P2-01 — glyph cluster lengths are inferred within each physical-font run

- **Classification:** P2, HIGH CONFIDENCE.
- **Exact files/classes/functions:** `TextEngine::shape`, especially run iteration
  and cluster construction in `src/core/text/text_engine.cpp:180-267`; propagated
  metadata in `GlyphGeometryBuilder::build` at `324-336`; range selection in
  `src/core/effects/effect_stack.cpp:269-278`.
- **Execution path:** Qt shapes one logical line into multiple `QGlyphRun`s due to
  fallback font or bidi segmentation -> for each run, code derives a glyph's
  cluster length from the next `stringIndexes` entry in that run -> the last glyph
  of every run receives `lineText.size() - localClusterStart` -> that span can cross
  all later runs. Descending/nonadjacent bidi indexes also do not provide a valid
  next logical boundary.
- **Preconditions:** a line with multiple physical-font or bidi runs, such as Latin
  plus an emoji/fallback glyph, mixed Cyrillic coverage, or mixed LTR/RTL text;
  apply an effect to a source range near a run boundary.
- **Why it fails:** run-local glyph order is not the line's global sorted logical
  cluster-boundary map.
- **User/system consequence:** a range-scoped effect can include or exclude glyph
  geometry outside the selected text, particularly around fallback glyphs and
  mixed-direction runs.
- **Minimal reproduction:** shape a font-backed Latin string containing a character
  forced into fallback (or a mixed-bidi string), dump `(clusterStart,
  clusterLength, physical font)` for every glyph, and compare spans with the sorted
  unique UTF-16 cluster starts for the complete line. Apply Wave to a one-cluster
  range at the boundary and inspect which pieces move.
- **Existing-test status:** fallback tests assert only warning/count; multiline
  tests assert positive cluster metadata; UTF-16 range rebase tests do not inspect
  mixed-run geometry ownership.
- **Smallest safe fix:** collect all valid logical string indexes across every run
  for a line, sort/deduplicate them, add the line end, and derive each cluster span
  from that global boundary map while retaining visual glyph order separately.
- **Required regression test:** deterministic installed/test-font cases for mixed
  Latin/Cyrillic fallback, emoji/surrogate pairs, combining marks, and LTR/RTL.
  Assert nonoverlapping logical spans and exact range-effect piece ownership.

### P2-03 — mask proximity uses expanded axis-aligned glyph bounds

- **Classification:** P2, HIGH CONFIDENCE.
- **Exact files/classes/functions:** `maskInfluence` in
  `src/core/effects/effect_stack.cpp:13-69`, especially lines 38-53; its use in
  `applyMaskedEffect` at `100-118`.
- **Execution path:** for every piece and mask stroke, code first measures the
  anchor-to-polyline distance, then forces distance to zero whenever a stroke point
  or segment rectangle intersects the glyph path's radius-expanded bounding box.
  It never tests distance to the actual `QPainterPath` contour.
- **Preconditions:** the brush crosses empty space inside a glyph/path AABB (a
  counter, concavity, or corner) without coming within the brush radius of the
  actual contour.
- **Why it fails:** the implementation comment promises “actual contour
  proximity”, but an AABB rectangle-overlap heuristic is the final narrow phase.
- **User/system consequence:** masks affect whole glyph pieces the brush did not
  touch; holes and concave letters produce visibly coarse leakage.
- **Minimal reproduction:** use an outlined/concave glyph such as `O` or `C`, place
  a small-radius stroke in empty interior/corner space, and compare
  `maskInfluence` with actual minimum contour distance.
- **Existing-test status:** `maskUsesPieceGeometryWhenAnchorIsOutsideBrush` protects
  against anchor-only behavior but accepts a bounding-region case. There is no
  counter/concavity false-positive oracle.
- **Smallest safe fix:** keep the bounds test only as broad-phase rejection, then
  compute stroke-to-flattened-contour distance (with a bounded sampler/tolerance)
  or use a documented filled-area mask raster in object-local coordinates.
- **Required regression test:** stroke near a contour, inside a counter, in an
  empty AABB corner, across a concavity, and under nonuniform scale; assert
  monotonic falloff and no false full influence.

### P2-06 — style-intensity drag mutates persisted state while dirty remains false

- **Classification:** P2, HIGH CONFIDENCE.
- **Exact files/classes/functions:** `EditorController::isModified` at
  `src/ui/editor_controller.cpp:176-179`; `setEffectStackStrength`,
  `beginEffectStackStrengthGesture`, and `endEffectStackStrengthGesture` at
  `392-428`; slider gesture wiring in `src/ui/main_window.cpp:382-390` and
  `src/ui/slider_spin_box.cpp:28-42`.
- **Execution path:** slider press starts a gesture -> each value change writes
  directly into the live `TextObject` and calls `onCommandChanged` -> no undo
  command exists yet, so `QUndoStack::isClean()` remains true -> only mouse release
  rewinds to the start and pushes one command.
- **Preconditions:** inspect dirty/close/open/new state while the mouse gesture is
  active, or interrupt it before the release signal by focus/window/document/
  selection change. Spin-box edits take the ordinary command path; the defect is
  the slider gesture path.
- **Why it fails:** persisted preview and committed document state are the same
  storage, while modified state is delegated exclusively to the undo stack.
- **User/system consequence:** a changed project can display no dirty marker and
  can take a no-prompt destructive workflow if release never commits; gesture
  fields can also outlive the object/document they began on.
- **Minimal reproduction:** mark the stack clean, press and move the style slider
  without releasing, assert the object changed and `isModified()==false`, then
  invoke a focus/document transition that loses the release.
- **Existing-test status:** smoke uses the spin box (`UiTestDriver::setStyleIntensity`),
  not slider press/move/release. No test observes modified state during a gesture
  or interrupts one.
- **Smallest safe fix:** keep live preview outside the document, or push a mergeable
  command on first movement so dirty becomes true immediately and merge subsequent
  samples. Add explicit finish/cancel on focus loss, selection change, open/new,
  and controller destruction.
- **Required regression test:** real slider press/move/release gives one undo step
  and immediate dirty state; move-back-to-start restores clean; focus loss,
  selection change, New/Open, and window close deterministically commit or cancel
  with no lost change.

## 5. Test Fortress blind spots

The test layout is sensible: six CTest executables separate core, effect contract,
integration, smoke, UI regression, and Windows clipboard concerns. Stable widget
names, fixed workflow seeds, semantic JSON fingerprints, diagnostic screenshots,
and an explicit invariant checker are all valuable. The same-SHA CI success proves
these tests build and pass together on the intended Windows/Qt environment.

The main problem is oracle strength, not raw test count.

### Effect contracts

`registeredEffectContract` (`tests/integration/effect_contract_tests.cpp:85-111`)
sets each parameter to minimum and then maximum, applies only the final/max state,
and asserts finite geometry plus a 4,096-piece ceiling. The stack-strength-zero
case also asserts only finiteness. A no-op, wrong direction, wrong magnitude,
ignored mask, ignored scope, wrong ordering, or non-identity zero-strength effect
can pass. Built-in preset contracts likewise require only registered effects and
finite output. Missing gates:

- neutral geometry equality and metadata equality;
- descriptor claims (`supportsMask`, text range, topology/piece preservation);
- per-effect semantic/visual signatures at representative parameters;
- order noncommutativity where expected;
- deterministic equality across repeated/process-order evaluation; and
- generator source/scope/mask/opacity ownership.

### Async tests

`latestAsyncTextGenerationWins` waits for a `SceneObjectGeometry::sourceText` value
that is copied into the result before shaping (`scene_evaluator.cpp:106-114`).
`UiTestDriver::waitForSceneGeneration` uses the same copied field
(`tests/support/ui_test_driver.cpp:158-171`). These tests can pass while geometry,
frame, effects, strength, or cache content came from a semantically incomplete
snapshot. Missing gates include geometry signature, frame transform/revision,
page switch, rapid object switch, hidden/deleted target, controller destruction,
pending replacement, long-task cancellation, and font-cache invalidation races.

### Seeded workflows

The advertised 5 x 120 replay is deterministic and useful, but its alphabet at
`tests/integration/workflow_integration_tests.cpp:68-80` contains only create,
text, italic, fixed move, add Wave/Bend, duplicate object, undo, and redo. It never
deletes, cuts/pastes, reorders, locks/hides, changes pages/layers, duplicates a page,
changes font family/style/size/tracking, transforms, paints a mask/deformation,
loads a real file, exports, or interrupts async work. Serialization uses in-memory
`toJson/fromJson`, not file-open controller lifecycle.

### Invariant checker and fingerprint

`checkInvariants` (`tests/support/invariant_checker.cpp:11-84`) is helpful but:

- validates active layer/object/selection existence globally rather than ownership
  by the current page/layer;
- accepts a scene object if the same ID exists anywhere in the document;
- does not compare scene source/effects/deformation/strength/transform/frame with
  the current document revision;
- does not validate `effectStackStrength`, typography finiteness, mask/deformation
  budgets, or descriptor capabilities; and
- is not invoked by production load.

`semanticFingerprint` intentionally removes `activeObjectId`
(`tests/support/state_fingerprint.cpp:11-17`) even though the serializer writes it.
The architecture must decide whether that field is semantic or transient.

### UI and platform paths

- Smoke applies presets through the controller and Style Intensity through the
  spin box, not the actual gallery/slider gesture path. Its final oracle is merely
  visible finite geometry and the incomplete invariant checker.
- UI tests cover either canvas editing or inspector editing, never ownership
  handoff for the same object (P1-02).
- `UiTestDriver::pressKey` targets the allocated native editor whenever it exists,
  even if hidden (`tests/support/ui_test_driver.cpp:117-120`), which can route a
  later scenario differently from a user.
- Several font tests `QSKIP` when a suitable installed font is unavailable. This
  is reasonable for platform discovery but cannot be the only correctness oracle
  for mixed-run cluster ownership.
- The Windows clipboard test converts every `copyForOffice` failure into a
  “headless runner” skip (`tests/platform/windows/clipboard_tests.cpp:35-38`),
  masking unrelated production regressions. It checks format presence/basic
  headers, not EMF physical size, complete Office paste, partial publication, or
  ownership/failure paths.

`docs/TESTING.md` should therefore describe the suite as a broad regression
foundation, not conclusive evidence of usable workflows until the semantic oracles
above are added.

## 6. Async/lifetime audit

### Sound properties

- `EditorController::rebuildScene` copies the page and the worker lambda captures
  only that value. No controller, document, widget, or selection pointer crosses
  the worker boundary.
- `QFutureWatcher` is parented to the controller and its completion connection has
  the controller as context. Late delivery after owner destruction is therefore
  suppressed by Qt object lifetime rules.
- `m_evaluationGeneration` is monotonic; stale finished results are rejected both
  in the callback and `publishSceneResult`.
- While a worker is active, only one newest pending snapshot is retained. This
  prevents unbounded keystroke tasks from queueing.
- Thread-local text/stage caches are invalidated by a global font epoch and cleared
  once their object-ID map exceeds 64 entries (`scene_evaluator.cpp:35-38,116-125`).

### Risks and gaps

- P1-01 makes the supposedly immutable snapshot semantically incomplete.
- P1-03 lets input consumers use an old scene without generation proof. Latest-
  result publication is safe; latest-frame command creation is not.
- P1-05 means current work is not cooperatively cancellable. A newest pending page
  can wait behind a pathological old page indefinitely.
- Page evaluation makes another `TextObject` copy per visible object in its `Task`
  vector (`scene_evaluator.cpp:221-243`), compounding both P1-01 and copy cost.
- Sequential object evaluation is an intentional starvation-avoidance tradeoff,
  not a defect. It should be documented as such and benchmarked before attempting
  per-object parallelism.
- There is no explicit test for controller destruction with active/pending work,
  page switch during work, or watcher completion ordering. Static ownership looks
  safe, but lifecycle tests should lock in that safety.

Conclusion: preserve the current value-snapshot/generation architecture. Repair
snapshot completeness, frame revisioning, cancellation/budgets, and tests; do not
replace it with shared live model access.

## 7. Undo/redo and identity audit

The command architecture mostly targets stable IDs and separates semantic payload
from current selection. `DocumentCommand::targetObject` also avoids redirecting a
missing ID to the active object; the existing `missingUndoTargetDoesNotRedirectToAnotherObject`
test is an important guard.

Material failures are P1-01 (copied payload loss), P1-04 (IDs are not guaranteed
unique on load), P2-04 (page clone duplicates effect IDs), P2-05 (net-zero dirty),
and P2-06 (persisted gesture mutation before command creation).

Additional findings:

- **P3, CONFIRMED — active object persistence has contradictory contracts.** The
  serializer writes/reads `activeObjectId` (`project_serializer.cpp:243,345`), but
  the semantic fingerprint removes it and `EditorController::openProject` calls
  `SelectionModel::clear` before resynchronizing (`editor_controller.cpp:1809-1816`).
  The selection-changed connection can clear the just-loaded document field. Pick
  one policy: make selection transient and stop serializing it, or restore it
  deterministically and include it in semantic tests.
- **Observation — selection restoration on hierarchy undo is limited.** Add/remove
  commands restore document payloads, then controller synchronization chooses a
  valid current selection; it does not promise the exact former multi-selection
  or selected effect. That is acceptable only if editor selection is formally
  transient.
- **Observation — command merge IDs are coarse but scoped.** Text and numeric
  commands check object/effect/parameter identity before merging. Their core
  target discipline is sound; the defect is failure to obsolete net-zero results.
- **Required identity gate:** run the strengthened invariant checker after every
  duplicate, paste, page/layer move, delete, undo, and redo, and reject invalid
  serialized input in production before commands ever see it.

## 8. Geometry/coordinate-space audit

`ObjectFrame` is a strong abstraction. It separates local reference/visual bounds,
local-to-page and inverse matrices, point/vector conversion, a stable local pivot,
and area-equivalent radius conversion. Scene evaluation applies effects and manual
deformation locally, constructs the frame, then transforms geometry once. Static
frame tests cover translation, rotation, mirroring, and radius behavior.

The dominant problem is authority over freshness (P1-03), followed by using AABBs
as final spatial predicates (P2-07 and P2-03).

Other observations:

- **P3, CONFIRMED — resampler terminal delta is wrong at the hard cap.** In
  `resampleBrushStroke`, when the sample vector is already at `boundedMaximum`,
  lines `manual_deformation.cpp:189-191` replace the last position with the final
  input point but set delta to `final - oldLast`. The documented/normal contract is
  delta from the preceding emitted sample. It should use
  `final - samples[samples.size()-2].position`. The branch is rare because spacing
  is adjusted to the cap, but it is deterministic once reached.
- **Observation — nonuniform scale makes a local circular brush appear elliptical
  in page space.** The area-equivalent radius policy is explicit and coherent; do
  not change it accidentally. If artists expect screen-circular brushes, that is a
  new tool contract requiring anisotropic local influence, not a bug fix.
- **Observation — click hit testing is path-fill oriented.** Counters and whitespace
  can be harder to select than frame-based tools. Decide whether selection means
  visible ink or object frame before changing it.
- Transform pivot capture must be synchronous and explicit on the first transform;
  it cannot wait for asynchronous glyph bounds (P1-03).

## 9. Text/font/shaping audit

The text subsystem correctly uses `QTextLayout`/`QGlyphRun`, requests string
indexes explicitly, stores UTF-16 cluster offsets, records per-run physical font
fallback, uses raw-font outlines for path export, and distinguishes missing family,
missing style, and glyph fallback warnings. Cyrillic is exercised in core and
smoke tests.

P2-01 is the primary correctness defect. Additional items:

- **SUSPICION — tracking order across mixed runs/bidi.** `lineOrdinal` increments
  in the order Qt returns runs/glyphs, while the tracking sign is chosen per run
  (`text_engine.cpp:219-258`). That ordering is not proven to be stable visual order
  for multiple fallback/bidi runs. A mixed LTR/RTL/fallback test should compare
  expected inter-cluster advances before changing code.
- **Observation — multiline is explicit hard-line layout, not paragraph wrapping.**
  Each newline-separated segment gets one extremely wide `QTextLine`
  (`text_engine.cpp:180-195,279-296`). This matches the present feature set, but
  future paragraph/vertical text cannot be layered on without a new layout model.
- **Observation — exact project rerender is system-font dependent.** `fingerprint`,
  `embeddedResourceId`, and `embeddingPermission` are serialized but not enforced.
  Exported paths are portable; reopening the project on another machine is not
  guaranteed to reproduce outlines or fallback runs.
- **Observation — named style load resolves traits from the current system.**
  `FontDescriptor::fromJson` overwrites serialized weight/italic when `styleName`
  is present (`font_descriptor.cpp:42-59`). This is coherent with “named style is
  authoritative” but makes absent/substituted style behavior machine-dependent.
- **P3/quality — underline and strikeout geometry is heuristic.** Decorations are
  constructed from line bounds/reference height rather than the resolved font's
  underline/strike metrics (`text_engine.cpp:376-390`). It is stable but may look
  wrong for unusual scripts/faces.

Before adding variable fonts, vertical text, or font embedding, first make cluster
ownership and font reproducibility explicit invariants.

## 10. Effects/deformation/mask audit

The effect stack has useful properties: ordered evaluation, stable per-effect
instance IDs, cache keys based on serialized stack plus object strength,
deterministic seeded jitter/procedural behavior, an explicit generator piece cap,
and local-before-transform execution. Deformation distinguishes legacy ambiguous
page-space strokes from current object-local strokes rather than silently applying
known-ambiguous data.

Material defects are P1-01, P1-03, P1-05, P2-02, and P2-03.

Additional observations:

- Masked effects clone the selected geometry twice (`before` and `after`) before
  blending every piece (`effect_stack.cpp:100-118`). This is a reasonable simple
  implementation for small text but becomes a performance multiplier for large
  geometry/masks.
- Generator effects bypass the generic range/mask pipeline. `TrailEffect` manually
  honors source range and generation depth but not masks. Capability flags must be
  enforced as policy so this exception is not hidden.
- `blendPath` falls back to all-before/all-after at influence 0.5 when topology
  changes. Descriptors claim whether topology is preserved, but production does
  not enforce that claim. Add contract tests before introducing topology-changing
  maskable effects.
- Effect stack strength zero has a dedicated core test, but the Test Fortress
  contract checks only finiteness. Exact identity must include paths, anchors,
  piece count/order, opacity, generator metadata, and bounds.
- Legacy `LegacyPageAmbiguous` deformation is deliberately skipped. This is safer
  than applying it in the wrong space, but migration should surface a per-object
  warning so visual loss is not silent.

## 11. Serialization audit

Strengths:

- schema/version marker and bounded supported-version range;
- transactional parse into a temporary `Document` before assignment;
- deterministic v1-v3 flat-object migration and later field migrations;
- preservation containers for future document/object data;
- atomic save through `QSaveFile`; and
- explicit errors for wrong root types, malformed hierarchy element types, and
  invalid deformation samples.

Release-significant gaps are P1-04 and P1-05. Further issues:

- **P3, HIGH CONFIDENCE — numeric validation is inconsistent.** Typography
  `fontSize` and `trackingEm` are accepted as arbitrary finite/possibly extreme
  JSON doubles (`font_descriptor.cpp:87-106`); page sizes and several transform
  values likewise rely on later normalization or geometry finiteness. Very large
  finite values can reach Qt shaping/path code. Validate all persisted numbers at
  the schema boundary using the same production ranges as controller setters.
- Effect mask points use `toDouble` and clamping for brush settings but do not
  reject nonfinite coordinates or count limits (`effect.cpp:48-63`).
- Clipboard object paste invokes `textObjectFromJson` on unbounded MIME bytes and
  silently skips invalid entries (`editor_controller.cpp:1608-1645`). Apply the
  same input-size, aggregate-budget, and precise diagnostic policy as project load.
- `activeObjectId` is both serialized and treated as transient (Section 7).
- Current round-trip tests serialize the live object directly; they do not pass
  through hierarchy value copy, so they cannot catch P1-01. Copy and serialization
  contracts need separate tests.
- Empty text objects are intentionally persistent/selectable editor entities
  (`scene_evaluator.cpp:164-173`) and tests codify that. Do not “clean them up” as
  malformed data.

## 12. UI workflow audit

### Text creation and editing

Add Text and the Text tool create the semantic object before async geometry is
available, then attach a native editor using a fallback frame. Recent regression
tests make focus/alignment substantially safer. Selection/page/lock invalidation
also ends incompatible sessions synchronously. P1-02 remains because the same
field can be edited concurrently by the native editor and typography inspector.

The native editor intercepts its own Ctrl+Z while global editor actions are
disabled. That is reasonable for typing feel, but model updates still create
mergeable global text commands. Tests should define the expected relationship
between the widget-local undo history, model command merging, Escape, and a later
global Undo.

### Effects and continuous controls

Effect rows and parameter widgets retain control identity across refresh, which
avoids dangling signal/widget behavior. Capability-driven enablement is incomplete
(P2-02). Style-intensity gesture batching mutates the model outside the undo stack
(P2-06), while other spin/slider controls mostly use mergeable commands.

### Selection, layers, and pages

Page tabs, selection synchronization, hidden/locked filtering, and stable-ID
object moves are coherent in normal generated documents. P2-07 affects rotated
marquee selection and P2-04 affects page duplication identity.

- **P3, CONFIRMED — layer buttons misinterpret object rows.** Object tree rows set
  only `UserRole+1` (object ID) at `src/ui/layers_panel.cpp:167-175`, but the shared
  Visible/Lock buttons read row-local `UserRole+2/+3` at lines 82-91 and emit
  `setActiveLayerVisible/Locked` through `main_window.cpp:484-487`. On an object row
  both absent values read false: Visible always requests true and Lock always
  requests true. Once the layer is locked, the same object-row button cannot
  unlock it. Read the parent layer state or disable layer buttons on object rows;
  add a real-widget row-kind test.
- Rename correctly refuses object rows because they have no layer ID. Move-to-layer
  context actions correctly inspect source/destination layer visibility/lock.

### File/output workflow

New/Open/Save/Export actions are centralized and close prompting uses undo clean
state. That makes P2-05 and P2-06 user-visible. Export eligibility is intentionally
a cheap visible-object precheck; the action can still reject empty/unrenderable
geometry with a specific message. Copy-for-Word messaging mishandles partial
success (P2-09).

## 13. Performance audit

There are no benchmarks or explicit latency/memory budgets. Correctness caps exist
for generator pieces, deformation samples per stroke, contour subdivision depth,
raster dimensions, and thread-local cache size, but they do not compose into a
document work budget (P1-05).

Hot-path observations:

- Every scene rebuild deep-copies a page, then `SceneEvaluator::evaluate` copies
  each visible `TextObject` again into a task vector. Complex effects/deformation
  make those payload copies nontrivial.
- Object evaluation is sequential in one worker. This avoids nested-pool deadlock
  and is likely correct for small typography pages; measure before parallelizing.
- Stage caches prevent reshaping and reapplying unchanged effects/deformation.
  They are keyed by object ID plus semantic hashes and bounded by clearing above 64
  entries. This is a sound simple policy.
- Masked effects copy whole scoped geometry twice and compute mask influence per
  piece/stroke/segment. Shape deformation repeatedly samples paths and nests
  stroke/sample/contour-point loops.
- Synchronous export repeats full page evaluation on the UI thread. It guarantees
  a current snapshot but becomes a freeze point under large workloads.
- Windows clipboard acquisition retries eight times with `Sleep(12)`
  (`windows_vector_clipboard_service.cpp:34-41`), allowing roughly 96 ms of direct
  UI-thread blocking before failure, in addition to EMF/PNG/SVG rendering.
- SVG path string generation is linear in `QPainterPath` elements and writes
  atomically; numeric precision is fixed at four decimals. Add a visual tolerance
  test before changing precision for size/performance reasons.

Recommended measurements: cold/warm shaping, 1/10/100 objects, mask segment x glyph
matrix, shape-deformation work matrix, rapid-edit latest-scene latency, export
latency, peak snapshot memory, cancellation latency, and clipboard render time.

## 14. Cross-platform/export readiness

The core is mostly portable Qt code and the Win32/GDI+ boundary is cleanly isolated
under `src/platform/windows`. Windows headers do not leak into document, shaping,
geometry, UI, or generic export. Path-only SVG avoids runtime font dependency in
the artifact and `QSaveFile` gives portable atomic output.

Current readiness is nevertheless Windows-first by design:

- README names Windows 10/11 as the current milestone and CI runs only
  `windows-latest`. There is no Linux/macOS build or smoke signal.
- Copy for Word is explicitly unavailable outside Windows through the portable
  facade; the UI disables it. This is an honest product boundary, not a bug.
- Project rerendering depends on installed font/fallback behavior, so cross-machine
  editable-file fidelity is weaker than exported-path fidelity.
- EMF uses a documented 96 logical DPI and 0.01 mm frame units; PNG uses 192 DPI
  with dimension/pixel caps. Automated tests do not verify physical size in Word/
  PowerPoint or inspect actual pasted appearance. Keep the manual acceptance list
  until an Office automation harness exists.
- `VectorExportPayload` carries `pageBackground`, but SVG and Windows renderers do
  not paint it. Current docs do not promise a page-background rectangle. Decide
  whether Current Page means transparent artwork on a page-sized canvas or an
  opaque page before downstream formats proliferate.
- P1-01 makes exported stack intensity wrong despite export's otherwise correct
  current-snapshot design. P2-08/P2-09 weaken clipboard semantic fallbacks.

Near-term export readiness means fixing those correctness defects, adding golden
SVG structure/geometry signatures and physical-size clipboard tests, then adding
at least compile/core-test CI on one non-Windows platform. It does not require
porting the Office clipboard feature immediately.

## 15. Technical debt worth fixing

1. **Make semantic copying mechanically complete.** Prefer defaulted special
   members where all members are copyable, or centralize a field-list equality
   contract and test it. Hand-maintained field copies are already proven fragile.
2. **Create one production `DocumentValidator`.** Share hierarchy/identity/locality,
   numeric, capability, and workload rules with tests; run it transactionally on
   load/paste and optionally as a debug post-command assertion.
3. **Version authoritative frames.** Add an object semantic/frame revision and a
   small synchronous frame derivation service used by both scene evaluation and
   input command creation.
4. **Separate preview from persisted mutation.** A generic gesture transaction
   should own begin/update/commit/cancel and dirty/undo behavior for style,
   transform, and future continuous controls.
5. **Coordinate text-edit ownership.** Put canvas/inspector session handoff in one
   component instead of relying on incidental focus and refresh behavior.
6. **Introduce parse/evaluation budgets and cancellation.** One context object can
   track input bytes, aggregate counts, deadline/cancel state, and a precise path
   for errors.
7. **Strengthen semantic oracles.** Geometry signatures, exact neutral identities,
   capability matrices, transformed spatial tests, and malformed fixtures provide
   more value than simply adding more finite-output assertions.
8. **Return structured platform results.** Replace boolean-plus-error overloads
   with success/warning/failure results before adding another export backend.
9. **Split `EditorController` only along proven boundaries.** At roughly two
   thousand lines it owns file, selection, undo, effects, deformation, clipboard,
   and async evaluation. Extract validator/export/session/gesture services while
   fixing their defects, not as a standalone rewrite.
10. **Add performance telemetry/benchmarks.** Work caps cannot be chosen responsibly
    without representative measurements.

## 16. Technical debt NOT worth fixing yet

- Do not replace the value-snapshot/generation async architecture; its ownership
  model is sound.
- Do not reintroduce nested per-object thread-pool parallelism before benchmarks
  show sequential page evaluation is the bottleneck and a nonstarving executor
  design exists.
- Do not replace Qt shaping with a custom HarfBuzz/DirectWrite stack merely to fix
  P2-01; build the correct global cluster map around Qt's runs first.
- Do not implement font embedding/licensing, variable-font axes, vertical text,
  paragraph wrapping, or collaborative references until copy/identity/range
  invariants are hard gates.
- Do not port the Office clipboard backend to macOS/Linux during this correctness
  pass. Portable SVG already supplies a clean cross-platform output boundary.
- Do not change intentional empty-object persistence; it supports create-first,
  type-later editing and is covered by tests.
- Do not introduce a full raster mask engine solely for P2-03. A bounded flattened-
  contour narrow phase is a smaller first correction.
- Do not redesign the full widget hierarchy or visual style. The critical UI debt
  is edit ownership, capability enablement, and gesture state.
- Do not make scene caches authoritative model state. Fix revision checks instead.

## 17. Things architecturally sound

- Document hierarchy uses ownership-safe `unique_ptr` composition and explicit
  stable IDs.
- Undo commands generally target IDs and avoid redirecting missing targets.
- Save is atomic and load is already temporary-before-commit.
- Object-local shaping/effect/deformation followed by one object transform is the
  correct geometry order.
- `ObjectFrame` explicitly separates points, vectors, radii, pivots, inverse
  transforms, and broad-phase AABBs.
- Async workers capture values, results are generation-gated, watcher callbacks are
  owner-bound, and pending work is coalesced to the newest snapshot.
- Stage cache keys include text/font/typography, ordered effect JSON and stack
  strength, deformation JSON, transform-dependent frame construction, and a font
  epoch.
- Effect ordering and deterministic seeded behavior are serialized.
- Generator piece count, contour recursion, raster output, and thread-local cache
  size already have useful local caps.
- SVG is path-only, scoped export is explicit, selection export is rebased to a
  local origin, and page export uses the page rectangle.
- The Windows platform boundary has careful handle ownership transfer and keeps
  Win32 types out of core/UI contracts.
- Tests are split by layer, have stable UI hooks, fixed replay seeds, diagnostics,
  and a same-SHA green Windows CI baseline. They need stronger oracles, not removal.

## 18. Recommended order of next work

1. **Freeze the audit SHA and add failing semantic-copy tests, then fix P1-01.**
   This restores trust in every snapshot, export, command payload, and duplicate.
2. **Add production document validation and reject P1-04/P1-05 inputs.** Start with
   identity/locality and conservative aggregate limits; this prevents ambiguous or
   pathological state from entering all later workflows.
3. **Add frame revisions/current-frame derivation and fix P1-03.** Spatial tests
   should use a deliberately blocked worker so races are deterministic.
4. **Unify text-edit ownership (P1-02).** Do this before adding any new typography
   inspector or canvas editing capability.
5. **Repair undo/gesture truth (P2-05/P2-06).** Dirty state and close safety must be
   reliable before broader UI work.
6. **Correct text cluster ownership (P2-01)** with mixed fallback/bidi UTF-16 tests.
7. **Enforce effect capabilities and contour mask semantics (P2-02/P2-03).** Add
   descriptor-driven contract tests at the same time.
8. **Close small identity/spatial/output defects (P2-04/P2-07/P2-08/P2-09).** They
   are mostly isolated once shared validators/results exist.
9. **Strengthen the Test Fortress and add performance/cancellation budgets.** Make
   tests prove geometry, locality, lifecycle, and complete/partial platform states.
10. **Only then resume feature work**, using
    `docs/ARCHITECTURE_INVARIANTS.md` as the change gate.

## 19. Prioritized backlog XS/S/M/L/XL

Sizing is relative engineering effort including focused tests: XS is a contained
patch, S is a small subsystem change, M spans several paths, L is a substantial
cross-cutting change, and XL needs staged design/performance work.

| Order | Item | Priority | Size | Acceptance boundary |
|---:|---|---:|:---:|---|
| 1 | P1-01 complete `TextObject` copy/assignment plus field-contract tests | P1 | S | nondefault strength survives every copy/snapshot/output workflow |
| 2 | P1-04 production hierarchy identity/locality validator | P1 | M | duplicate/nonlocal IDs rejected transactionally with precise errors |
| 3 | P1-05 conservative input/aggregate budgets | P1 | L | byte/count/work one-over fixtures reject before heavy allocation |
| 4 | P1-03 scene/object revision and authoritative current-frame input | P1 | L | blocked-worker spatial gestures store correct object-local values |
| 5 | P1-02 single live text-edit owner/handoff | P1 | M | canvas-inspector-canvas flow cannot lose or stale-overwrite text |
| 6 | P2-05 obsolete net-zero merged commands | P2 | M | clean index restored for every mergeable command family |
| 7 | P2-06 generic gesture commit/cancel and immediate dirty state | P2 | M | interruption cannot lose a persisted slider change |
| 8 | P2-01 global line cluster-boundary map | P2 | M | fallback/emoji/bidi ranges own exact glyph pieces |
| 9 | P2-02 descriptor-enforced mask capability | P2 | S | unsupported effects cannot paint/store masks via UI or controller |
| 10 | P2-03 bounded contour-distance mask narrow phase | P2 | M | counters/concavities have no AABB leakage |
| 11 | P2-04 fresh effect IDs during page clone | P2 | XS | global invariant holds through duplicate/undo/redo |
| 12 | P2-07 marquee narrow phase | P2 | S | empty rotated-AABB corners do not select |
| 13 | P2-08 preserve plaintext object multiplicity | P2 | XS | equal objects produce equal repeated text lines |
| 14 | P2-09 structured clipboard result and UI warning | P2 | M | complete, vector-only, and failure states are distinguishable |
| 15 | Cooperative evaluator cancellation/deadline | P1 support | XL | newest generation/export cancel has measured bounded latency |
| 16 | Strengthen async geometry/frame/lifecycle tests | Test debt | M | tests prove semantic latest-result and destruction safety |
| 17 | Expand seeded operation alphabet and invariant locality | Test debt | L | pages/layers/delete/paste/masks/deform/export participate in replay |
| 18 | Exact effect/preset semantic contract matrix | Test debt | L | neutral/capability/order/determinism oracles for every descriptor |
| 19 | Persisted numeric finite/range validation | P3 | M | all schema numbers use documented production ranges |
| 20 | Decide/remove/restore serialized active selection contract | P3 | S | save/open/fingerprint agree on one deterministic policy |
| 21 | Fix object-row layer buttons | P3 | XS | row-kind-aware visible/lock behavior has UI test |
| 22 | Fix capped resampler terminal delta | P3 | XS | final delta is from preceding emitted sample |
| 23 | Performance benchmark/telemetry suite | Debt | M | representative cold/warm/mask/deform/export budgets are recorded |
| 24 | Mixed-run tracking/bidi diagnostic oracle | Suspicion | M | visual/logical tracking contract is proven before any code change |
| 25 | Non-Windows compile/core CI | Readiness | M | portable core/SVG stays buildable outside Windows |
| 26 | Decide transparent versus painted page background | Observation | XS | export contract/docs/tests agree |
| 27 | Exact font resource/fingerprint enforcement | Future | XL | defer until current invariants are green |

## 20. Do before next feature

1. Fix and regress P1-01 across copy, async scene, export, duplicate, paste, and
   undo payloads.
2. Make one production validator reject duplicate/nonlocal identities before load
   or paste commits state.
3. Add conservative file/hierarchy/mask/deformation aggregate budgets and one-over
   tests; begin evaluator cancellation work.
4. Require a current frame revision for every persisted spatial gesture and make
   first-transform pivots synchronous.
5. Establish one live text-edit owner so canvas and inspector cannot overwrite each
   other.
6. Make continuous gestures dirty immediately and make net-zero merged edits clean.
7. Build cluster spans from the full line across fallback/bidi runs, with UTF-16
   range-effect regression tests.
8. Enforce effect descriptor capabilities and replace AABB-only mask proximity.
9. Close page-duplicate effect IDs and rotated marquee false positives.
10. Preserve clipboard text multiplicity, surface partial publication, and upgrade
    the Test Fortress from finite/existence checks to semantic geometry/lifecycle
    oracles.

After these ten gates, the existing architecture is a credible base for the next
feature. Before them, new fields, spatial tools, text-range features, or effect
types are likely to compound already-demonstrated contract gaps.
