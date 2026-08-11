# Testing the Vector Typography Editor

Phase 4T treats a green test run as evidence of usable editor workflows, not
just of isolated helper functions.  A green core suite alone does **not**
validate an interactive feature.

## Test architecture

- `vector_typography_core_tests` (`core`) keeps deterministic model,
  geometry, serializer, cache and command contracts.
- `vector_typography_effect_contract_tests` (`core;integration;effects`)
  validates every public `EffectRegistry` descriptor and every built-in preset.
  The explicit `contractTypes()` gate forces a contributor to add coverage for
  a newly registered effect.
- `vector_typography_integration_tests` (`integration;undo;serialization;async`)
  checks semantic save/load and undo/redo equivalence, latest async results,
  and five seeded valid-workflow runs.
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
- `state_fingerprint` is a canonical persistent-only JSON representation; it
  omits dates, active-widget state and async/cache state.
- `invariant_checker` checks ID uniqueness, selections, transform domains,
  effect IDs/ranges, current page/layer coherence, and published scene
  ownership after arbitrary workflows.

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

`seededValidWorkflows` runs seeds `1`, `42`, `1337`, `8675309`, and
`20260811`, 120 valid operations each.  A failure includes the seed, action
index and replayable action history.  Replay one row with:

```powershell
build\vector_typography_integration_tests.exe seededValidWorkflows:1337 -vs
```

## Diagnostics and manual boundary

Windows CI uploads CTest output plus `build/test-artifacts` even when tests
fail.  Smoke scenarios can save a PNG snapshot without making snapshots a
font-sensitive pass/fail oracle.  Manual testing remains responsible for
subjective UX/visual quality and actual Word/PowerPoint paste behavior; CI
does validate the EMF, SVG, PNG and Unicode clipboard formats without Office.

## Historical regression coverage

The permanent checks are intentionally mapped to the user-visible bug class:

| Regression class | Protecting tests |
| --- | --- |
| Add Text focus, first typing, pre-async placement | `addTextLatin`, `addTextCyrillic`, existing `addTextStartsFocusedAndAlignedBeforeAndAfterScenePublication` |
| Native editor outside click/wheel/session invalidation | existing `nativeEditorViewportRoutesOutsideCanvasInput`, `selectingAnotherLayerObjectEndsNativeEditorSession` |
| Pull/Smooth brush state | existing `deformationAndMaskStateDoNotOverwriteEachOther` |
| Master-strength sender lifetime | existing `valueRefreshKeepsEmittingControlsAlive` |
| Italic and exact-style trait mode | existing `traitModeIsShownAfterBoldAndItalic`; core text/cache cases |
| Small/mirrored scale and persistence | existing `scaleControlsPreserveSmallAndMirroredValues`; serializer cases |
| Effect finite output, generators, Phase 4B warps | `registeredEffectContract` data rows |
| Built-in catalog validity and styles | `builtInPresetContract` data rows, `firstFiveMinutesCanary` |
| Undo/save-load semantic identity | `semanticSaveLoadAndUndoRedoEquivalence` |
| Latest async result wins | `latestAsyncTextGenerationWins` |
| Copy for Word ownership/formats | `copyForWordPublishesPortableFormats` |
| Longer valid state changes | `seededValidWorkflows` fixed seeds |

As new historical bugs arise, extend this table and add their user-path
regression before applying the production repair.
