# Vector Typography Editor

Vector Typography Editor is a focused C++20/Qt 6 desktop foundation for editable
typography with nondestructive procedural effects. The current slice supports one
primary text object, installed system fonts, Latin/Cyrillic shaping, vector glyph
outlines, Wave/Glyph Jitter/Global Stretch effects, JSON projects and presets, undo/
redo, and SVG path export.

## Requirements

* Windows 10 or Windows 11 for the current milestone.
* CMake 3.21 or newer.
* A C++20 compiler supported by Qt 6 (Visual Studio 2022 is the recommended Windows
  toolchain).
* Qt 6.4 or newer with the `Core`, `Gui`, `Widgets`, and `Test` components.

The project does not bundle fonts. It enumerates fonts installed in the operating
system through Qt's `QFontDatabase`. A missing project font is reported to the UI while
Qt's fallback is used only for a clearly marked preview; the project still preserves
the requested family/style/weight.

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

## Workflow

1. Enter text in the Typography panel.
2. Choose any installed family, style, weight, size, tracking, and fill.
3. Add effects to the ordered stack and edit their parameters. Effect values are
   normalized to the unmodified vector bounds where the effect is relative to text
   size.
4. Save the effect stack as a named JSON preset, then apply it to another text.
5. Save/open a `.vtproj` JSON project.
6. Use **File → Export SVG**. The SVG contains final `<path>` geometry and does not
   depend on the original font being installed.

Canvas navigation: mouse wheel zooms, middle-drag pans, and **F** fits the geometry.
Text, typography, effect edits, reordering, toggling, removal, and preset application
are undoable. Continuous text/slider edits are coalesced by Qt undo command IDs.

## Repository structure

```text
src/core/document       document and text-object ownership
src/core/text           system-font descriptors and Qt shaping
src/core/geometry       positioned vector path pieces
src/core/effects        effect interface, stack, and initial effects
src/core/serialization  versioned project JSON
src/core/presets        versioned preset JSON and storage
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

Project and preset files contain explicit format identifiers and integer versions.
Font descriptors already reserve fingerprint, embedded resource, and embedding
permission fields. Documents reserve resources/future object data for embedded fonts,
masks, cached previews, brush strokes, and multiple objects. The export interface is
ready for future EMF and clipboard backends; this milestone intentionally implements
SVG only.

See [ARCHITECTURE.md](ARCHITECTURE.md) for ownership, geometry, effect stages,
normalization, cache invalidation, and the platform boundary.

## Current limitations

This milestone intentionally does not include font embedding, EMF/Word clipboard,
brush deformation, Zalgo, masks, raster/image tools, multi-page documents, or general
vector drawing tools. The local environment used to prepare this repository did not
contain a Qt SDK or C++ build toolchain, so the code should be built and tested on a
Windows machine with the documented dependencies before the next milestone.
