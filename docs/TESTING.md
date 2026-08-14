# Testing the Vector Typography Editor

Phase 4T established the fortress; Phase 4R uses it as a corrective release
gate. A green run is evidence of usable editor workflows and repaired semantic
contracts, not just isolated helper functions. A green core suite alone does
**not** validate an interactive feature.

## Test architecture

- `vector_typography_core_tests` (`core;serialization;undo;geometry`) keeps
  deterministic model, geometry, path-layout, serializer, cache and command
  contracts.
- `vector_typography_effect_contract_tests` (`core;integration;effects`)
  validates every public `EffectRegistry` descriptor and every built-in preset.
  It now proves exact zero/disabled identity, descriptor claims, deterministic
  replay, range/mask targeting, generator metadata, magnitude and order—not
  only finite output. The explicit `contractTypes()` gate forces a contributor
  to add coverage for a newly registered effect.
- `vector_typography_oracle_tests`
  (`core;integration;oracle;serialization`) self-validates
  the semantic guards: structured geometry signatures, persistent-field copy
  equality, fingerprints, hierarchy invariants, frame round trips, identity-
  corrupt project rejection, exact/one-over resource budgets, deterministic
  legacy identity migration, and deterministic workload builders.
- `vector_typography_region_tests` (`core;region;serialization;undo;adversarial`)
  provides independent rectangle/concave/hole/cubic scanline oracles, legal
  cluster wrapping, padding/alignment/Clip behavior, cancellation
  transactionality, v8 round trips, v7 migration, duplicate identity
  rejection, and controller undo/redo plus duplicate freshening.
- `vector_typography_integration_tests`
  (`integration;undo;serialization;async;export`)
  checks real-file save/open/new/failure/migration behavior, exact undo/redo
  equivalence, deterministic async and spatial-revision races, cooperative
  work cancellation, atomic cancelled export, duplication identity, export
  text multiplicity, descriptor refusal paths, and ten seeded 200-action
  workflows.
- `vector_typography_ui_smoke_tests` (`ui;smoke`) drives a visible
  `MainWindow` through real widgets.  It is the fast first-five-minutes canary.
- `vector_typography_ui_tests` (`ui;regression;undo`) retains focused UI regression
  cases such as outside click/wheel routing, focused native text edit, trait
  mode and scale preservation.
- `vector_typography_region_ui_tests` (`ui;region;smoke`) checks real
  TypographyPanel mode, preset, padding, alignment, overflow, refresh, and
  region-edit affordances.
- `vector_typography_windows_tests` (`windows;export;clipboard`) validates the actual
  Windows Copy for Word clipboard boundary.

Shared helpers live in `tests/support`:

- `UiTestDriver` creates a `MainWindow`, clicks stable object-named controls,
  types into its native `QPlainTextEdit`, waits for publication, and captures a
  window PNG under `test-artifacts/` when requested by a scenario.
  Its synchronous `SceneEvaluator` comparison is deliberately a latest-
  publication/routing oracle, not an independent evaluator implementation;
  core fixtures and effect-contract tests provide the independent geometry
  expectations.
- `geometry_assertions` rejects NaN/Inf geometry and checks the editor proxy
  against the object frame projected through the active canvas transform.
- `semantic_geometry` quantizes coordinates at an explicit tolerance and
  records frame matrices, pivot, bounds, ordered pieces, path element types and
  coordinates, glyph/cluster identity, anchors, opacity, and generator lineage.
  Its comparator reports the first differing field; its digest is only for
  compact logs.
- `semantic_equality` enumerates every persistent `TextObject`, path, effect,
  mask, deformation, transform, layer and page field. Ordinary C++ copies
  preserve stable IDs; only explicit duplication paths are allowed to freshen
  them.
- `state_fingerprint` is a canonical persistent-only JSON representation; it
  omits dates, active-widget state and async/cache state.
- `invariant_checker` checks global ID uniqueness, hierarchical page/layer/
  object locality, numeric domains, descriptor mask/range capabilities,
  masks/deformation, selections, and full
  published-scene ownership/transform/source coherence after arbitrary
  workflows.
- `AsyncEvaluationGate` occupies the global evaluation pool with a semaphore,
  creating reproducible A/B races without sleeps.
- `WorkControl` counts semantic checkpoints and exposes a callback seam, so a
  test can stop shaping/effects/masks/deformation/export after a known unit
  without wall-clock assertions.
- `ProjectSerializer::resourceLimits()` and its explicit byte/resource
  validation seams allow exact-boundary and one-over fixtures without allocating
  production-sized hostile documents.
- `workload_builder` creates inspectable 1/10/100-object mask, deformation and
  generator matrices and prints coarse timings without brittle thresholds.

## Local commands

Configure with a Qt 6.8-compatible CMake toolchain, then run one layer:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=<Qt>/lib/cmake
cmake --build build --parallel
ctest --test-dir build -L smoke --output-on-failure -VV
ctest --test-dir build -L "core|integration|ui|windows" --output-on-failure -VV
```

On MSVC every repository-owned library and executable compiles with `/W4 /WX`.
Qt and other angle-bracket headers use MSVC's external-header policy at `/W0`;
do not disable warning-as-error globally or suppress a first-party diagnostic to
make CI green. Fix the warning or narrow a suppression to a documented external
boundary.

The UI targets intentionally use `QT_QPA_PLATFORM=offscreen` in CTest. Use a
native Windows desktop run as a complementary manual acceptance pass for
Office interoperability and subjective visual quality.

The authoritative Phase 4R gate is the `Windows Qt CI` pull-request workflow:
configure with MSVC/Qt 6.8.3, build Release, run every
`core|integration|ui|windows` CTest label, then package and upload
`VectorTypographyEditor-windows-x64`. A package is never produced after a build
or test failure. The final Phase 4R evidence must come from one run whose head
SHA exactly matches the draft PR head; all nine CTest executables must pass and
the complete compiler log must contain no first-party warning.

## Regression and feature policy

When a human finds a bug, first add a reproducer through the user path where
practical, prove it fails on the buggy revision, then fix it and retain that
test permanently.  Do not substitute a helper-only test for a button/focus/
coordinate routing failure.

Every new effect needs: registry contract, finite/extreme behavior,
serialization, undo/controller coverage where editable, and a UI application
path if it is exposed in the panel.  Every persistent property needs model and
save/load coverage, plus undo if editable.  Every spatial tool must cover a
non-identity zoom or transform.

Text-on-path additions additionally require distance-based straight/cubic math,
open/closed/degenerate overflow cases, cancellation and aggregate path limits,
post-layout effect/deformation ordering, current-schema identity validation,
duplicate/paste freshening, stale spatial-revision rejection, and a real-widget
inspector/canvas handoff test.

Region-typography additions additionally require independent scanline oracles
for rectangles, concavities, cubic contours, and holes; widest-continuous-
interval selection with deterministic ties; shaping-cluster/UTF-16-safe
wrapping; overlong Clip termination; horizontal/vertical alignment and
padding; post-layout effect ordering; v8 identity/migration/resource
validation; cancellation without partial geometry; controller undo/redo and
duplicate freshening; and a real-widget inspector refresh/signal test.

## Random workflow replay

`seededValidWorkflows` runs seeds `1`, `7`, `42`, `99`, `1337`, `65537`,
`314159`, `8675309`, `12648430`, and `20260811`, 200 valid operations each.
The state-aware alphabet covers creation/editing, Cyrillic/multiline text,
font family/style and other typography, transforms, selection/clear/multiselect,
effects/parameters/range/mask/order, built-in presets, generator and deformation
state, path creation/editing/reversal/closure/offsets and duplicate path
identities, duplication/deletion/copy/paste, pages/layers, visibility/locking,
SVG eligibility/output, undo/redo and periodic semantic round trips. A failure
prints the seed, action, page/layer/object IDs, selection, effect/tool and replay history.
Replay one seed (and optionally change its scale) with:

```powershell
$env:VT_WORKFLOW_SEED=1337
$env:VT_WORKFLOW_STEPS=200
build\vector_typography_integration_tests.exe seededValidWorkflows -vs
```

## Diagnostics and manual boundary

Windows CI uploads CTest output plus `build/test-artifacts` even when tests
fail. Smoke scenarios can save a PNG snapshot without making snapshots a
font-sensitive pass/fail oracle. The Windows clipboard test requires complete
EMF, SVG, PNG and Unicode publication on the headless Windows runner; an EMF
creation or transfer failure is a test failure, not a skip. Manual testing
remains responsible for subjective UX/visual quality and actual Word/
PowerPoint paste behavior.

## Historical regression coverage

The permanent checks are intentionally mapped to the user-visible bug class:

| Historical/forensic bug | Exact permanent test(s) |
| --- | --- |
| Add Text focus, first typing, pre-async placement | `addTextLatin`, `addTextCyrillic`, existing `addTextStartsFocusedAndAlignedBeforeAndAfterScenePublication` |
| Native editor outside click/wheel/session invalidation | existing `nativeEditorViewportRoutesOutsideCanvasInput`, `selectingAnotherLayerObjectEndsNativeEditorSession` |
| Canvas editor stale overwrite after inspector edit (P1-02) | `inspectorEditEndsCanvasSessionWithoutStaleOverwrite` |
| Hidden native editor receiving synthetic keys | user-authentic `UiTestDriver::typeText` / `pressKey`; smoke workflows use actual focus |
| Pull/Smooth brush state | existing `deformationAndMaskStateDoNotOverwriteEachOther` |
| Master-strength sender lifetime | existing `valueRefreshKeepsEmittingControlsAlive` |
| Italic and exact-style trait mode | existing `traitModeIsShownAfterBoldAndItalic`; core text/cache cases |
| Small/mirrored scale and persistence | existing `scaleControlsPreserveSmallAndMirroredValues`; serializer cases |
| Rotated live transform and stable pivot | `sceneSignatureIncludesFrameAndPivot`, `frameRoundTripIsStableForStaticSnapshots`, existing rotated transform cases |
| Move command captured IDs | `seededValidWorkflows`, existing captured-ID command cases |
| Tracking decorations and font cache/style authority | existing core shaping/cache cases; `semanticCopyContractCoversPersistentInventory` inventories all descriptor fields |
| Effect finite-but-wrong/no-op behavior | `registeredEffectContract`, `waveAndStretchHaveDirectionalMagnitude` |
| Effect ordering silently commutative | `effectOrderIsSemanticallyNonCommutative` |
| Effect range or mask ignored / unsupported mask offered by UI (P2-02) | `maskAndTextRangeRespectDescriptorClaims`, registry-driven `unsupportedEffectMaskIsRefusedWithoutMutation`, registry-driven real-widget `unsupportedEffectDisablesMaskUiAcrossRefreshes` |
| Noise nondeterminism/seed ignored | `deterministicSeedsAndGeneratorMetadata` |
| Generator count/opacity/source lineage loss | `deterministicSeedsAndGeneratorMetadata` |
| Master Strength zero/copy omission (P1-01) | `registeredEffectContract` (all effects), `semanticCopyContractCoversPersistentInventory`, `semanticFingerprintExcludesOnlyDeclaredTransientState` |
| Built-in catalog validity and styles | `builtInPresetContract` data rows, `firstFiveMinutesCanary` |
| Page duplicate preserving effect IDs (P2-04) | `duplicatePageFreshensEntireIdentityHierarchy` including undo/redo |
| Object duplicate/paste/preset clone identity collisions | `duplicatePasteAndPresetFreshenEffectIdentities` |
| Effect reorder/delete/undo loses order or identity | `effectReorderDeleteUndoRestoresSemanticOrder` |
| Net-zero merged edit remains dirty (P2-05) | `mergeableCommandReturningToStartRestoresClean` rows for text, font size, tracking, line spacing, effect parameter/master, deformation strength and move |
| Held Style Intensity mutates while “clean” (P2-06) | `styleIntensityGestureHasImmediateDirtyTruthAndOneUndoStep`, including multi-value gesture merge, separate gestures, selection handoff, save/clean boundary, and fingerprint undo/redo |
| Rotated marquee selects empty AABB corners (P2-07) | `rotatedMarqueeUsesInkAsNarrowPhase` |
| Equal-value objects deduplicated from plain text (P2-08) | `exportPlainTextPreservesEqualObjectMultiplicity` |
| Path layout spacing, clipping, degeneracy, and cluster preservation | `pathArcLengthMatchesIndependentDenseOracle`, `pathSubdivisionAndDegenerateGeometryStayBounded`, `pathLayoutClipsOpenOverflowWithoutEndpointPileup`, `pathLayoutPreservesClustersThroughEffects` |
| Path identity, malformed persistence, stale gestures, and object-targeted undo | `pathSerializationRejectsCorruptionAndBudgets`, `pathControllerDuplicateAndStaleGestureKeepIdentitySafe`, `pathUndoTargetsExplicitObjectAndRestoresFingerprint` |
| Real-widget path creation, tool dispatch, anchor gesture, offsets, and disable/undo | `pathTypographyControlsAndAnchorGesture` |
| Region scanline layout, shaping-safe wrap, topology, alignment, Clip, persistence, and controller semantics | `vector_typography_region_tests` |
| Region inspector controls and refresh synchronization | `vector_typography_region_ui_tests` |
| Stale scene frame authorizes page-space mutation (P1-03) | `staleFrameCannotAuthorizeSpatialMutation` holds revision N, maps mask/deformation through N+1, and proves stale-transform fingerprint/undo neutrality; `transientPreviewNeverBecomesDocumentOrFrameAuthority` proves preview geometry differs without becoming document/frame/cache authority |
| Aggregate hostile workload / noncancellable work (P1-05) | `serializedResourceBudgetsHaveExactBoundaries` including saturating overflow/composite limits, `workControlHasExactSharedTerminalBoundaries`, `evaluationCancellationDoesNotPoisonWorkerCaches`, `cooperativeWorkBudgetAndCancellationAreDeterministic`, `cancelledSvgNeverCommitsPartialOutput`, Windows `cancellationStopsBeforeClipboardPublication` |
| Per-run UTF-16 cluster span consumes the rest of a line (P2-01) | deterministic `logicalClusterSpansUseWholeLineContext`; real `mixedUtf16ShapingUsesGlobalClusterSpans`; downstream `mixedUtf16ClustersSurviveEffectsPersistenceAndExport` |
| Mask AABB leaks into counters and concavities (P2-03) | `contourMaskDistanceRejectsHolesAndConcavities`, `contourMaskDistanceHandlesAdversarialGeometryAndCancellation` |
| Clipboard partial publication reported as complete (P2-09) | `publicationResultClassification`, exhaustive `injectedOperationsClassifyFailuresAndOwnership`, `cancellationStopsBeforeClipboardPublication`, `copyForWordPublishesPortableFormats` |
| Clipboard failure hidden behind broad skip | `busyClipboardIsAProductionFailure`, `oversizedRasterFallbackIsAProductionFailure`; complete publication must pass on Windows CI |
| Load/save duplicate, missing, or nonlocal identities (P1-04) | `currentSchemaLoaderRejectsIdentityCorruption` covers same/cross-page collisions and transactional save; `historicalIdentityMigrationIsDeterministic`, `legacyV1V2V3MigrationSurvivesSaveReloadAndUndoRedo`, `malformedOrOversizedClipboardPasteIsTransactional`, `invariantCheckerRejectsSyntheticCorruption` |
| Object-row visibility/lock uses missing metadata | `objectRowLayerButtonsOperateOnParentLayer` |
| Capped brush resampler terminal delta points from replaced sample | `deformationResamplingIsBoundedAndDeterministic` delta-chain assertions |
| Undo/save-load semantic identity | `semanticSaveLoadAndUndoRedoEquivalence`, `realFileLifecyclePreservesComplexSemantics` |
| Latest async text but stale effects/transform/frame | `latestAsyncSemanticSnapshotWins`, `mixedRapidMutationsPublishOnlyFinalSemanticScene`, `pageSwitchRejectsLatePreviousPage`, `deletedObjectCannotBeRepublished`, `controllerDestructionWithQueuedEvaluationIsSafe` |
| Longer valid state changes | `seededValidWorkflows` ten fixed seeds × 200 actions |

As new historical bugs arise, extend this table and add their user-path
regression before applying the production repair.
