cmake_minimum_required(VERSION 3.21)

project(VectorTypographyEditor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

if (MSVC)
    add_compile_options(/utf-8)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Test)

set(CORE_SOURCES
    src/core/document/document.cpp
    src/core/text/font_descriptor.cpp
    src/core/text/text_engine.cpp
    src/core/geometry/vector_geometry.cpp
    src/core/effects/effect.cpp
    src/core/effects/effect_stack.cpp
    src/core/effects/wave_effect.cpp
    src/core/effects/glyph_jitter_effect.cpp
    src/core/effects/stretch_effect.cpp
    src/core/serialization/project_serializer.cpp
    src/core/presets/preset.cpp
    src/core/presets/preset_manager.cpp
    src/core/export/svg_exporter.cpp
    src/core/undo/document_commands.cpp
)

add_library(vector_typography_core STATIC ${CORE_SOURCES})
target_include_directories(vector_typography_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vector_typography_core PUBLIC Qt6::Core Qt6::Gui)

if (WIN32)
    target_compile_definitions(vector_typography_core PUBLIC VT_PLATFORM_WINDOWS=1)
endif()

set(UI_SOURCES
    src/ui/editor_controller.h
    src/ui/editor_controller.cpp
    src/ui/editor_canvas.h
    src/ui/editor_canvas.cpp
    src/ui/typography_panel.h
    src/ui/typography_panel.cpp
    src/ui/effects_panel.h
    src/ui/effects_panel.cpp
    src/ui/main_window.h
    src/ui/main_window.cpp
)

add_executable(vector_typography_editor
    src/main.cpp
    ${UI_SOURCES}
)
target_include_directories(vector_typography_editor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vector_typography_editor PRIVATE
    vector_typography_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

if (WIN32)
    set_target_properties(vector_typography_editor PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

enable_testing()

add_executable(vector_typography_tests
    tests/core_tests.cpp
    src/ui/editor_controller.h
    src/ui/editor_controller.cpp
)
target_include_directories(vector_typography_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vector_typography_tests PRIVATE
    vector_typography_core
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

add_test(NAME vector_typography_core_tests COMMAND vector_typography_tests)

install(TARGETS vector_typography_editor RUNTIME DESTINATION bin)
