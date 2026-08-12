# Remaining Test Fortress Gaps after Phase 4R

This file lists only blind spots that remain materially capable of hiding a
user-visible regression. It is not a count of missing tests. Permanent coverage
is mapped in `docs/TESTING.md`; the historical Phase 4T gaps that Phase 4R closed
are recorded below so their removal is auditable.

## Closed architectural gaps

| Former gap | Production contract now present | Deterministic evidence |
| --- | --- | --- |
| P1-03 authoritative frame freshness | `EditorController`, `SceneGeometry`, `SceneObjectGeometry`, and `ObjectFrame` carry a comparable spatial revision. Persisted page-space pointer input is converted through a synchronously derived or exact-current frame; stale transform commits are rejected. | `staleFrameCannotAuthorizeSpatialMutation` holds the pool at revision N, mutates to N+1, injects page input, proves its current-frame round trip, and rejects obsolete authorization without sleeps. |
| P1-05 aggregate work/cancellation | `ProjectSerializer` enforces byte, hierarchy, text, effect, mask, deformation, and aggregate estimated-work bounds before model construction. Shared `WorkControl` checkpoints cover shaping, effects, masks, deformation, scene evaluation, SVG, export payload, PNG, EMF, and clipboard work. | `serializedResourceBudgetsHaveExactBoundaries`, `cooperativeWorkBudgetAndCancellationAreDeterministic`, `cancelledSvgNeverCommitsPartialOutput`, and `cancellationStopsBeforeClipboardPublication`. |
| P2-03 contour-accurate mask influence | `EffectMaskDistance` uses filled-path containment plus bounded flattened-contour segment distance. Bounds are broad phase only; preview and committed evaluation travel through the same effect pipeline. | `contourMaskDistanceRejectsHolesAndConcavities` covers solid fill, a counter, concavity, real contour crossing, and bounded monotonic falloff. |
| P2-01 mixed-run cluster metadata | `TextEngine` builds one sorted/deduplicated UTF-16 cluster-boundary map from all glyph runs in a line, then assigns spans to individual glyphs. | `logicalClusterSpansUseWholeLineContext` supplies a deterministic out-of-order multi-run seam; `mixedUtf16ShapingUsesGlobalClusterSpans` exercises Latin, Cyrillic, combining marks, surrogate pairs, ligature-capable text, and bidi/fallback shaping. |
| Low-level Windows clipboard faults | `WindowsClipboardOperations` is an injectable Win32 boundary. Publication has Complete, Partial, Cancelled, and Failure outcomes, and handle ownership transfers only after successful `SetClipboardData`. | `injectedOperationsClassifyFailuresAndOwnership` forces registration/allocation/lock/transfer failures; `cancellationStopsBeforeClipboardPublication` proves cleanup and the pre-publication barrier. |

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
