# Remaining Test Fortress Gaps after Phase 4T.1

This file lists only blind spots that remain materially capable of hiding a
user-visible regression. It is not a count of missing tests. The permanent
coverage map is in `docs/TESTING.md`.

## P1-03 — authoritative frame freshness during spatial input

`AsyncEvaluationGate` can now prove that an obsolete page/object result is not
published, and `frameRoundTripIsStableForStaticSnapshots` proves the static
coordinate contract. The editor still has no shared document/frame revision
that a pointer event can validate before using the currently published frame.
Therefore a transform mutation while a gesture is already reading an older
frame cannot be made deterministic without changing production architecture.

Required architecture: stamp document spatial mutations and evaluated
`ObjectFrame`s with comparable revisions, then either synchronously obtain a
current frame or reject/replay input whose revision is stale.

Future test: block evaluation after revision N, mutate transform to N+1, inject
page-space press/move/release coordinates, and prove the persisted local stroke
or transform maps back to the intended page-space pointer positions at N+1.

## P1-05 — aggregate work budgets and cooperative cancellation

Deterministic workload builders now expose 1/10/100-object matrices and exact
mask/deformation/generator counts. Tests record coarse timings without unstable
thresholds. Evaluation remains cancellable only between complete page tasks;
there is no cooperative stop token or aggregate work budget inside shaping,
mask traversal, deformation, generators, SVG, PNG or EMF rendering.

Required architecture: define shared work units/budgets, propagate a stop token
through evaluator/export loops, and distinguish cancellation from failure.

Future test: hold a worker after a known work-unit count, supersede/cancel it,
and assert a bounded number of additional units, bounded generated pieces and
no publication/output from the cancelled revision.

## Contour-accurate effect-mask influence (P2-03)

Mask contracts now prove that supported effects target only intended pieces and
unsupported generators are refused. The production influence calculation still
uses expanded contour bounds and segment AABBs as a proxy in some cases. A
stroke crossing empty space inside that proxy can affect a glyph.

Required architecture: a contour/brush-distance predicate shared by preview
and final evaluation.

Future test: use a concave/holed outline, paint entirely through empty proxy
space, and compare structured piece signatures before/after; then paint across
the real contour and require a bounded monotonic influence.

## Mixed-run text cluster metadata (P2-01)

Geometry signatures now retain cluster start and length, so loss is observable,
but there is not yet a platform-stable fixture containing multiple fallback
font runs whose expected UTF-16 cluster spans are independently known.

Required seam: deterministic test-font fallback chain (or checked-in licensed
fixtures) with explicit run/cluster expectations.

Future test: shape Latin + Cyrillic + combining marks + surrogate pairs across
at least two runs, then prove each piece's cluster span, range targeting and
save/load/export identity.

## Low-level Windows clipboard fault injection

Windows CI now fails—not skips—when complete EMF/SVG/PNG/Unicode publication
fails. Busy-clipboard, oversize fallback, repeated copy, multiplicity and the
Complete/Partial/Failure classifier are covered. Individual `GlobalAlloc`,
`GlobalLock`, `SetClipboardData`, format-registration and EMF ownership failures
cannot yet be forced independently.

Required architecture: inject a narrow Win32 clipboard-operations interface.

Future test: fail each allocation/lock/transfer once, assert handle ownership
and cleanup, independently attempt all fallbacks, and verify the precise
structured result. Actual Word/PowerPoint paste fidelity remains a manual Office
interop boundary.

## Native-platform text-tool dispatch

The offscreen Qt plugin still crashes in the pre-existing mouse-created
`QGraphicsProxyWidget` scenario, so `textToolStartsFocusedAtCurrentZoom` retains
a narrowly documented offscreen skip. Add Text focus and native-editor typing
remain covered offscreen; the exact Text-tool mouse path requires a native
Windows UI runner.

Future test: run the same case on an interactive Windows agent with no skip and
capture focus widget, proxy geometry, zoom/pan and first committed character.

## macOS clipboard/export boundary

The semantic export payload is platform-neutral, but there is no macOS vector
clipboard backend or CI job. Windows EMF contracts cannot stand in for PDF/
NSPasteboard ownership and format negotiation.

Required architecture: a macOS backend behind `VectorClipboardService` plus a
macOS runner.

Future test: publish vector PDF/SVG/PNG/Unicode formats, validate ownership and
repeat-copy behavior, and compare payload geometry to the evaluator signature.
