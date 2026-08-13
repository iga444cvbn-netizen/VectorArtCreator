# Remaining Test Fortress Gaps after Phase 4R

This file lists only blind spots that remain materially capable of hiding a
user-visible regression. It is not a count of missing tests. Permanent coverage
is mapped in `docs/TESTING.md`; the historical Phase 4T gaps that Phase 4R closed
are recorded below so their removal is auditable.

## Closed architectural gaps

| Former gap | Production contract now present | Deterministic evidence |
| --- | --- | --- |
| P1-03 authoritative frame freshness | `EditorController`, `SceneGeometry`, `SceneObjectGeometry`, and `ObjectFrame` carry a comparable spatial revision. Persisted page-space pointer input is converted through a synchronously derived or exact-current frame; stale transform commits are rejected; semantic revisions clear all previews. | `staleFrameCannotAuthorizeSpatialMutation` proves current-frame mask/deformation round trips and stale-transform fingerprint/undo neutrality. `transientPreviewNeverBecomesDocumentOrFrameAuthority` proves a visibly different preview is neither persisted nor reused as authority. |
| P1-05 aggregate work/cancellation | `ProjectSerializer` enforces byte, hierarchy, text, effect, mask, deformation, and saturating composite-work bounds before model construction. Shared `WorkControl` checkpoints cover shaping, effects, masks, deformation, scene evaluation, SVG, export payload, PNG, EMF, and clipboard work; cache keys publish only after complete stages. | `serializedResourceBudgetsHaveExactBoundaries`, `workControlHasExactSharedTerminalBoundaries`, `evaluationCancellationDoesNotPoisonWorkerCaches`, `cooperativeWorkBudgetAndCancellationAreDeterministic`, `cancelledSvgNeverCommitsPartialOutput`, and `cancellationStopsBeforeClipboardPublication`. |
| P2-03 contour-accurate mask influence | `EffectMaskDistance` uses filled-path containment plus bounded flattened-contour segment distance. Bounds are broad phase only; invalid/cancelled work returns no influence; preview and committed evaluation travel through the same effect pipeline. | `contourMaskDistanceRejectsHolesAndConcavities` plus `contourMaskDistanceHandlesAdversarialGeometryAndCancellation` cover crossings, holes, fill rules, open/closed/multiple contours, exact radius, degenerate points, curves, invalid values, and cancellation. |
| P2-01 mixed-run cluster metadata | `TextEngine` builds one sorted/deduplicated UTF-16 cluster-boundary map from all glyph runs in a line, then assigns spans to individual glyphs. | `logicalClusterSpansUseWholeLineContext` covers repeated/invalid/final/empty boundaries and empty lines; `mixedUtf16ShapingUsesGlobalClusterSpans` exercises real shaping; `mixedUtf16ClustersSurviveEffectsPersistenceAndExport` carries range semantics through geometry, save/load, and export. |
| Low-level Windows clipboard faults | `WindowsClipboardOperations` is an injectable Win32 boundary. Publication has Complete, Partial, Cancelled, and Failure outcomes, and handle ownership transfers only after successful `SetClipboardData`. | `injectedOperationsClassifyFailuresAndOwnership` exhausts EMF/SVG/PNG/Unicode registration/allocation/lock/publication plus open retry, empty, close, transfer, free-once, and no-double-free paths; `cancellationStopsBeforeClipboardPublication` proves cleanup and the pre-publication barrier. |

## Native-platform text-tool dispatch

The offscreen Qt plugin still crashes in the pre-existing mouse-created
`QGraphicsProxyWidget` scenario, so `textToolStartsFocusedAtCurrentZoom` retains
a narrowly documented offscreen skip. Add Text focus and native-editor typing
remain covered offscreen; the exact Text-tool mouse path requires a native
Windows UI runner.

Future test: run the same case on an interactive Windows agent with no skip and
capture focus widget, proxy geometry, zoom/pan, and the first committed character.

## Actual Office interoperability

Windows CI proves EMF/SVG/PNG/Unicode publication, structured fallback results,
failure classification, and ownership cleanup. It does not launch Microsoft
Word or PowerPoint, inspect their negotiated paste format, or compare the visual
result produced by those applications.

Future test: run an Office-enabled interactive Windows job, paste a fixed payload
into Word and PowerPoint, and compare the imported bounds, text fallback, and
vector/raster choice with the publication result.

## Performance telemetry

Phase 4R provides deterministic safety bounds and cooperative cancellation; it
does not establish representative latency or memory targets for ordinary files.
Coarse workload timings intentionally remain diagnostic rather than pass/fail
oracles, because shared GitHub runners are not a stable benchmark environment.

Future work: publish cold/warm shaping, mask, deformation, scene, SVG, PNG, and
EMF telemetry from controlled hardware and set product budgets only after enough
samples exist.

## macOS clipboard/export boundary

The semantic export payload is platform-neutral, but there is no macOS vector
clipboard backend or CI job. Windows EMF contracts cannot stand in for PDF/
NSPasteboard ownership and format negotiation.

Required architecture: a macOS backend behind `VectorClipboardService` plus a
macOS runner.

Future test: publish vector PDF/SVG/PNG/Unicode formats, validate ownership and
repeat-copy behavior, and compare payload geometry to the evaluator signature.

## Phase 4D path-typography coverage

Path typography is now covered as a first-class layout stage rather than as a
rendering-only exception. The permanent core/UI evidence includes:

- independent dense arc-distance and cubic-split spacing checks;
- closed-seam wrapping, reverse direction, side flip, affine mirror/rotation,
  and final export geometry checks;
- bounded degenerate-path behavior with no endpoint pile-up;
- source cluster metadata preservation through path layout and effects, plus a
  full path -> masked effect -> geometry warp -> generator -> deformation
  pipeline check;
- duplicate path/node identity rejection, aggregate path budgets, and atomic
  save failure preservation;
- explicit object-ID path undo targeting, stale spatial-revision rejection,
  duplicate/paste identity freshening, and seeded path actions;
- offscreen UI coverage for default path creation, path-tool dispatch, anchor
  dragging, offsets, disable/undo, and control refresh behavior.

The native-platform limitation remains narrower than the pre-existing text
editor gap: the offscreen suite verifies the real controller/canvas path seam,
but does not claim pixel-perfect overlay review under a native Windows display.
Manual acceptance continues to cover rotated, mirrored, zoomed, and panned
path editing plus Office paste appearance.
