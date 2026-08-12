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
