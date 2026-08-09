# Vector Typography Editor

Vector Typography Editor is a C++20/Qt 6 desktop editor for editable text,
vector glyph outlines, nondestructive procedural effects, and persistent vector
deformation strokes. The current application supports one primary text object,
Latin/Cyrillic shaping, Wave/Glyph Jitter/Global Stretch, Select/Push/Pull/
Inflate/Pinch/Smooth deformation, JSON projects and presets, command-based undo/redo,
and SVG path export.

## Requirements

* Windows 10 or Windows 11 for the current milestone.
* CMake 3.21 or newer.
* A C++20 compiler supported by Qt 6 (Visual Studio 2022 is recommended).
* Qt 6.4 or newer with Core, Gui, Widgets, and Test.

The project does not bundle fonts. It enumerates fonts installed in the operating
system through Qt's `QFontDatabase`. A missing family or style is reported, and
the shaper inspects the physical `QRawFont` in each `QGlyphRun` to report
glyph-level fallback while retaining Qt's useful fallback preview.

## Build on Windows

Open a Developer PowerShell for Visual Studio and set `CMAKE_PREFIX_PATH` to the
Qt installation prefix containing `Qt6Config.cmake`:

```powershell
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.0\msvc2022_64"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
.\build\Release\vector_typography_editor.exe
```

## Windows GitHub Actions CI

`.github/workflows/windows-ci.yml` is the authoritative hosted validation
workflow. It runs on `windows-latest`, enables the x64 MSVC developer
environment, installs Qt 6.8.3 (`win64_msvc2022_64`) with
`jurplel/install-qt-action@v4`, configures a Ninja Release build, compiles the
editor and tests, and runs CTest with the Qt offscreen platform plugin.

It runs for pushes to `main`, pull requests targeting `main`, and manual
dispatches. After tests pass, `windeployqt` creates and uploads
`VectorTypographyEditor-windows-x64.zip`. Download it from the workflow run's
**Artifacts** section; it is a runnable deployed test build, not an installer.

## Workflow

1. Edit source text and typography in the Typography panel. Tracking is stored as
   `trackingEm`; `0.05 em` means an additional five percent of the current font
   em size between shaped glyphs. The UI labels this unit explicitly.
2. Add effects to the ordered stack and edit their parameters. Effects are
   nondestructive and are reapplied after source text, font, or typography edits.
3. Use the Manual deformation panel to choose `Select`, Push, Pull, Inflate,
   Pinch, or Smooth. `Select` is an inactive canvas mode: left-click/drag does
   not create a deformation stroke, the brush cursor is hidden, and middle-drag,
   Space-drag, and wheel zoom remain available. For brush tools, choose Glyphs or
   Shape and set radius, strength, and hardness. Smooth is Shape-only and restores
   the previous target when you switch back to another brush. The radius is in
   document coordinates, so zoom does not change the affected size. Escape cancels
   an active stroke.
4. Toggle stored deformation or adjust Overall strength. Clear removes all
   stored strokes as one undoable operation.
5. Save the effect stack as a named JSON preset, then apply it to another text.
   Preset names remain Unicode, including `Бездна`, `Паника`, `Шёпот`, and
   `Искажение реальности`.
6. Save/open a `.vtproj` JSON project.
7. Use **File -> Export SVG**. The SVG contains final `<path>` geometry and does
   not depend on the original font being installed.

Tracking is stored as `trackingEm`. The shaper converts it using the resolved
`QRawFont::pixelSize()` em metric in the same logical coordinates as the shaped
glyph positions; the project does not assume that a point size is one layout unit.
Shape deformation preserves open versus closed QPainterPath subpaths during
sampling and reconstruction.

All ordinary document edits use focused `QUndoCommand` objects. Text and slider
updates merge where appropriate, a completed deformation drag is one command,
and the undo stack's clean index is the source of truth for the window modified
marker and close confirmation.

## Repository structure

```text
src/core/document       document and text-object ownership
src/core/text           font descriptors, shaping, and fallback diagnostics
src/core/geometry       positioned vector path pieces
src/core/effects        effect interface, stack, and procedural effects
src/core/deformation    spatial strokes, sampling, falloff, reconstruction
src/core/serialization  versioned project JSON
src/core/presets        versioned preset JSON and UUID storage
src/core/undo           reusable granular QUndoCommand implementations
src/core/export         export interface and SVG backend
src/ui                  controller, canvas, typography/effect/deformation panels
src/platform/windows    reserved boundary for future Win32 integrations
tests                   Qt Test coverage of core and controller behavior
```

## Serialization and compatibility

Project files are currently format version 3. Version 1 absolute tracking values
are migrated to `trackingEm`; version 2 projects without deformation data load
with an empty deformation stack. A saved project always writes the current
schema. Each deformation stroke stores its mode, target, document-space samples,
radius, strength, hardness, and pressure. Strokes are spatial data, not pointers
to transient painter paths or glyph indexes.

Presets are format version 2. Each preset has a generated UUID `id` and a Unicode
display `name`; new files use `<uuid>.json` and never use a sanitized human name
as the path. Legacy name-based JSON files remain readable where practical.

`FontDescriptor` reserves fingerprint, embedded-resource, and embedding-permission
fields for future exact-face and licensing work. Actual embedding and private
font loading are intentionally not implemented.

## Current limitations

This milestone intentionally does not include Zalgo, horror generators, glitch or
blur systems, Word clipboard/EMF export, font embedding or licensing parsing,
macOS support, plugins, AI tools, masks, or multi-page documents. Those are
future work and are not part of the current deformation foundation.

See [ARCHITECTURE.md](ARCHITECTURE.md) for ownership, geometry stages, stroke
evaluation, fallback diagnostics, cache invalidation, undo/clean-state semantics,
and the platform boundary.

The deployed-build smoke test is documented in
[docs/PHASE2_MANUAL_CHECKLIST.md](docs/PHASE2_MANUAL_CHECKLIST.md).
