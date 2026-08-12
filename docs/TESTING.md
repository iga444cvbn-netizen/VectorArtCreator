# Testing the Vector Typography Editor

Phase 4T treats a green test run as evidence of usable editor workflows, not
just of isolated helper functions.  A green core suite alone does **not**
validate an interactive feature.

## Test architecture

- `vector_typography_core_tests` (`core`) keeps deterministic model,
  geometry, serializer, cache and command contracts.
- `vector_typography_effect_contract_tests` (`core;integration;effects`)
  validates every public `EffectRegistry` descriptor and every built-in preset.
  It now proves exact zero/disabled identity, descriptor claims, deterministic
  replay, range/mask targeting, generator metadata, magnitude and order—not
  only finite output. The explicit `contractTypes()` gate forces a contributor
  to add coverage for a newly registered effect.
- `vector_typography_oracle_tests` (`core;integration;oracle`) self-validates
  the semantic guards: structured geometry signatures, persistent-field copy
  equality, fingerprints, hierarchy invariants, frame round trips, identity-
  corrupt project rejection, and deterministic workload builders.
- `vector_typography_integration_tests` (`integration;undo;serialization;async`)
  checks real-file save/open/new/failure/migration behavior, exact undo/redo
  equivalence, deterministic async races, duplication identity, export text
  multiplicity, descriptor refusal paths, and ten seeded 200-action workflows.
- `vector_typography_ui_smoke_tests` (`ui;smoke`) drives a visible
  `MainWindow` through real widgets.  It is the fast first-five-minutes canary.
- `vector_typography_ui_tests` (`ui;regression`) retains focused UI regression
  cases such as outside click/wheel routing, focused native text edit, trait
  mode and scale preservation.
- `vector_typography_windows_tests` (`windows;export`) validates the actual
  Windows Copy for Word clipboard boundary.

Shared helpers live in `tests/support`:

- `UiTestDriver` creates a `MainWindow`, clicks stable object-named controls,
  types into its native `QPlainTextEdit`, waits for publication, and captures a
  window PNG under `test-artifacts/` when requested by a scenario.
- `geometry_assertions` rejects NaN/Inf geometry and checks the editor proxy
  against the object frame projected through the active canvas transform.
- `semantic_geometry` quantizes coordinates at an explicit tolerance and
  records frame matrices, pivot, bounds, ordered pieces, path element types and
  coordinates, glyph/cluster identity, anchors, opacity, and generator lineage.
  Its comparator reports the first differing field; its digest is only for
  compact logs.
- `semantic_equality` enumerates every persistent `TextObject`, effect, mask,
  deformation, transform, layer and page field. Ordinary C++ copies preserve
  stable IDs; only explicit duplication paths are allowed to freshen them.
- `state_fingerprint` is a canonical persistent-only JSON representation; it
  omits dates, active-widget state and async/cache state.
- `invariant_checker` checks global ID uniqueness, hierarchical page/layer/
  object locality, numeric domains, descriptor mask/range capabilities,
  masks/deformation, selections, and full
  published-scene ownership/transform/source coherence after arbitrary
  workflows.
- `AsyncEvaluationGate` occupies the global evaluation pool with a semaphore,
  creating reproducible A/B races without sleeps.
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

The UI targets intentionally use `QT_QPA_PLATFORM=offscreen` in CTest.  Use a
native Windows desktop run as a complementary manual acceptance pass for
Office interoperability and subjective visual quality.

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

## Random workflow replay

`seededValidWorkflows` runs seeds `1`, `7`, `42`, `99`, `1337`, `65537`,
`314159`, `8675309`, `12648430`, and `20260811`, 200 valid operations each.
The state-aware alphabet covers creation/editing, Cyrillic/multiline text,
font family/style and other typography, transforms, selection/clear/multiselect,
effects/parameters/range/mask/order, built-in presets, generator and deformation
state, duplication/deletion/copy/paste, pages/layers, visibility/locking,
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
| Held Style Intensity mutates while “clean” (P2-06) | `styleIntensityGestureHasImmediateDirtyTruthAndOneUndoStep` |
| Rotated marquee selects empty AABB corners (P2-07) | `rotatedMarqueeUsesInkAsNarrowPhase` |
| Equal-value objects deduplicated from plain text (P2-08) | `exportPlainTextPreservesEqualObjectMultiplicity` |
| Clipboard partial publication reported as complete (P2-09) | `publicationResultClassification`, `copyForWordPublishesPortableFormats` |
| Clipboard failure hidden behind broad skip | `busyClipboardIsAProductionFailure`, `oversizedRasterFallbackIsAProductionFailure`; complete publication must pass on Windows CI |
| Loader duplicate/missing/nonlocal identities (P1-04) | `currentSchemaLoaderRejectsIdentityCorruption`, `invariantCheckerRejectsSyntheticCorruption` |
| Object-row visibility/lock uses missing metadata | `objectRowLayerButtonsOperateOnParentLayer` |
| Capped brush resampler terminal delta points from replaced sample | `deformationResamplingIsBoundedAndDeterministic` delta-chain assertions |
| Undo/save-load semantic identity | `semanticSaveLoadAndUndoRedoEquivalence`, `realFileLifecyclePreservesComplexSemantics` |
| Latest async text but stale effects/transform/frame | `latestAsyncSemanticSnapshotWins`, `pageSwitchRejectsLatePreviousPage`, `deletedObjectCannotBeRepublished`, `controllerDestructionWithQueuedEvaluationIsSafe` |
| Longer valid state changes | `seededValidWorkflows` ten fixed seeds × 200 actions |

As new historical bugs arise, extend this table and add their user-path
regression before applying the production repair.
