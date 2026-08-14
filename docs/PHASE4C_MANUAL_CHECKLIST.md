# Phase 4C Windows acceptance checklist

Test the packaged `VectorTypographyEditor-windows-x64` artifact, outside its build
directory, before marking the release ready.

- Copy a Selection and Current Page into Word and PowerPoint; verify physical size
  and color are sensible.
- Check Latin and Cyrillic text, including counters in `O`, `B`, `Ф`, and `Я`.
- Check rotated, scaled, mirrored and multi-colour objects plus Echo/Ghost opacity.
- Resize the Office paste substantially and verify it remains vector-sharp.
- Verify a plain-text paste remains available and clipboard formats include EMF,
  SVG, and PNG where the target application exposes them.
- Repeat Copy for Word several times and verify the clipboard does not remain locked.
- Export both scopes to SVG and inspect final paths, holes, color and opacity in
  two independent SVG viewers.

## Phase 4D text-on-path acceptance

- Create a text path from the Typography inspector and verify the path overlay
  follows a rotated/scaled object frame.
- Drag anchors and cubic handles with the Path Edit tool; undo and redo each
  gesture, then verify the final path and text placement after save/load.
- Check straight and curved paths with Latin, Cyrillic, combining marks, emoji,
  and multiline text. Confirm glyph cluster/range effects still target the same
  UTF-16 source spans.
- Exercise start/baseline offsets, reverse traversal, side flip, tangent toggle,
  open-path clipping, closed-path wrapping, geometry reversal, node insertion,
  node deletion, and Escape cancellation.
- Apply an effect and manual deformation to text on a path; verify the visual
  order is path layout, effect, deformation, then one object transform.
- Change text or transform during a path gesture and confirm the stale gesture is
  rejected without changing the persisted path. Duplicate, paste, and duplicate
  a page; verify every copied path and node receives fresh identities.

## Phase 5 shape-typography acceptance

- Use a packaged Windows build outside its build directory. Add text, choose
  Region, and create Rectangle, Ellipse, and Custom regions; confirm each is
  editable vector geometry and reflows text immediately.
- Exercise concave outlines and one or more holes. Edit outer and hole anchors,
  cubic handles, node insertion/deletion, line/cubic conversion, Escape
  cancellation, undo/redo, and stale-gesture rejection.
- Check left/center/right/justified alignment, top/center/bottom vertical
  alignment, per-side padding, narrow regions, curved shoulders, and the
  documented widest-continuous-interval behavior around holes.
- Check Latin, Cyrillic, mixed bidi text, combining marks, emoji/fallback runs,
  long paragraphs, overlong tokens, effects, and manual deformation. Confirm
  effects receive post-region glyph geometry and deformation runs afterward;
  intentional post-effect geometry may leave the region.
- Test rotated, scaled, mirrored, zoomed, and panned objects. Resize the
  region, save/reopen, repeat undo/redo, duplicate/page-clone/paste, and verify
  all region/contour/node identities are fresh in copies.
- Export Selection and Current Page to SVG and inspect the final glyph paths in
  two independent SVG viewers. Copy both scopes to Word and PowerPoint, verify
  vector sharpness after resize, and repeat clipboard copy without a lock.
