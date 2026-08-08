# Vector Typography Editor

Vector Typography Editor is a focused C++20/Qt 6 desktop foundation for editable
typography with nondestructive procedural effects. The current slice supports one
primary text object, installed system fonts, Latin/Cyrillic shaping, vector glyph
outlines, Wave/Glyph Jitter/Global Stretch effects, JSON projects and presets,
command-based undo/redo, and SVG path export.

## Requirements

* Windows 10 or Windows 11 for the current milestone.
* CMake 3.21 or newer.
* A C++20 compiler supported by Qt 6 (Visual Studio 2022 is the recommended Windows
  toolchain).
* Qt 6.4 or newer with the `Core`, `Gui`, `Widgets`, and `Test` components.

The project does not bundle fonts. It enumerates fonts installed in the operating
system through Qt's `QFontDatabase`. A missing family or style is reported, and the
shaper also inspects the actual `QRawFont` in each `QGlyphRun` to report glyph-level
fallback while retaining Qt's useful fallback preview.

## Build on Windows

Open a **Developer PowerShell for VS 2022** and set `CMAKE_PREFIX_PATH` to the Qt
installation prefix that contains `Qt6Config.cmake`:

```powershell
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.8.0\msvc2022_64"
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Then launch:

```powershell
.\build\Release\vector_typography_editor.exe
```

For a Ninja build, use a compiler environment with Ninja available and replace the
configure command with:

```powershell
cmake -S . -B build-ninja -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-ninja
ctest --test-dir build-ninja --output-on-failure
```

If Qt is installed in another location, use that location in `CMAKE_PREFIX_PATH` or
pass `-DQt6_DIR=<Qt-prefix>/lib/cmake/Qt6`.

## Windows GitHub Actions CI

`.github/workflows/windows-ci.yml` is the authoritative hosted validation workflow.
It runs on `windows-latest`, enables the x64 MSVC developer environment, installs
Qt 6.8.3 through `jurplel/install-qt-action@v4` (the standard desktop/base package
contains Core, Gui, Widgets, and Test), then runs:

```text
cmake configure (Visual Studio 17 2022, x64)
cmake --build ... --config Release
ctest ... -C Release --output-on-failure
```

The workflow runs for pushes to `main`, pull requests targeting `main`, and manual
dispatches. A successful run also deploys the Qt runtime with `windeployqt` and
uploads `VectorTypographyEditor-windows-x64.zip` as a workflow artifact. Download it
from the run's **Artifacts** section; it is a runnable test build, not an installer.

## Workflow

1. Enter text in the Typography panel.
2. Choose any installed family, style, weight, size, em-relative tracking, and fill.
   For example, `0.05 em` means five percent of the current font em size.
3. Add effects to the ordered stack and edit their parameters. Effect values are
   normalized to the unmodified vector bounds where the effect is relative to text
   size.
4. Save the effect stack as a named JSON preset, then apply it to another text.
5. Save/open a `.vtproj` JSON project.
6. Use **File -> Export SVG**. The SVG contains final `<path>` geometry and does not
   depend on the original font being installed.

Canvas navigation: mouse wheel zooms, middle-drag pans, and **F** fits the geometry.
Text, typography, effect edits, reordering, toggling, removal, and preset application
are undoable. The controller stores only the fields relevant to each command, and
continuous text/slider edits are coalesced by Qt undo command IDs.

Preset display names remain Unicode, including Cyrillic names such as `Бездна` and
`Паника`. New preset files use generated UUID storage IDs rather than sanitized names,
so different names cannot collide. Version 1 name-based files remain readable and are
migrated when saved.

## Repository structure

```text
src/core/document       document and text-object ownership
src/core/text           font descriptors, shaping, and fallback diagnostics
src/core/geometry       positioned vector path pieces
src/core/effects        effect interface, stack, and initial effects
src/core/serialization  versioned project JSON
src/core/presets        versioned preset JSON and UUID storage
src/core/undo           reusable granular QUndoCommand implementations
src/core/export         export interface and SVG backend
src/ui                  controller, canvas, typography/effect panels, main window
src/platform/windows    reserved boundary for future Win32 integrations
tests                   Qt Test coverage of core behavior
```

## Adding an effect

1. Derive a class from `vt::Effect` in `src/core/effects`.
2. Give it a stable `typeId`, display name, domain, clone implementation, parameter
   definitions, JSON parameter methods, and an `apply(VectorGeometry&, EffectContext&)`
   method.
3. Register the type in `createEffect` in `effect.cpp`.
4. Add its source to `CORE_SOURCES` and add serialization/determinism tests.

The UI creates parameter controls from `EffectParameter` definitions, so existing
document, renderer, and serialization classes do not need effect-specific branches.

## Versioning and future work

Project files are currently format version 2; they migrate version 1 absolute tracking
values to the em-relative `trackingEm` field. Presets are currently format version 2;
they carry a stable UUID `id` separately from the Unicode `name`. Font descriptors
reserve fingerprint, embedded resource, and embedding permission fields for future
font identity/resource work without implementing embedding now. Documents reserve
resources/future object data for embedded fonts, masks, cached previews, brush strokes,
and multiple objects. The export interface is ready for future EMF and clipboard
backends; this milestone intentionally implements SVG only.

See [ARCHITECTURE.md](ARCHITECTURE.md) for ownership, geometry, effect stages,
normalization, cache invalidation, undo/clean-state semantics, and the platform
boundary.

## Current limitations

This milestone intentionally does not include font embedding, EMF/Word clipboard,
brush deformation, Zalgo, masks, raster/image tools, multi-page documents, or general
vector drawing tools. Those remain Phase 2 work. Fonts and the Qt SDK are supplied by
the developer or by the Windows CI workflow; they are not committed to this repository.
