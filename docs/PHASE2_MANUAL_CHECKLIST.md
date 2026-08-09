# Phase 2 manual checklist

Run this checklist with the deployed Windows ZIP from a clean Windows 10/11 x64
machine. The machine does not need a Qt SDK.

- [ ] Launch `vector_typography_editor.exe` from the unpacked artifact.
- [ ] Enter Latin and Cyrillic text and confirm both render as vector outlines.
- [ ] Select a font family/style and confirm missing family/style diagnostics are
      visible in the status bar when an invalid value is loaded.
- [ ] Choose `Select` in the Manual deformation panel. Confirm left-click/drag
      does not create a stroke, the brush circle is hidden, and the cursor is
      normal. Confirm middle-drag, Space-drag, and wheel zoom still work.
- [ ] Use `Push`, `Pull`, `Inflate`, `Pinch`, and `Smooth` in `Shape` mode. Confirm
      the canvas shows a circular cursor and the result remains vector geometry.
- [ ] Select `Glyphs`, then choose `Smooth`. Confirm the target becomes disabled
      and `Shape`; switch to another brush and confirm `Glyphs` is restored.
- [ ] Repeat a short stroke in `Glyphs` mode and confirm each affected glyph moves
      rigidly rather than being rasterized.
- [ ] Zoom and pan before brushing. Confirm the radius stays in document units.
- [ ] Hold Space or use middle-drag while moving the pointer. Confirm panning does
      not create a deformation stroke. Press Escape during a drag and confirm it
      is cancelled.
- [ ] Complete one drag, undo it once, and redo it once. Confirm exactly one
      deformation undo step is used.
- [ ] Toggle **Apply stored strokes**, adjust Overall strength, and use Clear. Confirm
      each is undoable and does not destroy the stored stroke data.
- [ ] Save, edit, undo back to the saved state, and confirm the `*` window marker
      disappears. Redo and confirm it returns.
- [ ] Save and reopen a project containing effects and deformation strokes.
- [ ] Save presets named `Бездна`, `Паника`, `Шёпот`, and
      `Искажение реальности`; confirm they list, load, and delete independently.
- [ ] Export SVG and verify it contains paths only, with no SVG text/image or base64
      raster data.
