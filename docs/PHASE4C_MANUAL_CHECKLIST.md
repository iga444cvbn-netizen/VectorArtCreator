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
