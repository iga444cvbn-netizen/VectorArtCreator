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

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Test Concurrent)

set(CORE_SOURCES
    src/core/document/document.cpp
    src/core/scene/scene_geometry.cpp
    src/core/scene/object_frame.cpp
    src/core/scene/scene_evaluator.cpp
    src/core/deformation/manual_deformation.cpp
    src/core/deformation/contour_sampler.cpp
    src/core/deformation/deformation_evaluator.cpp
    src/core/text/font_descriptor.cpp
    src/core/text/text_engine.cpp
    src/core/geometry/vector_geometry.cpp
    src/core/effects/effect.cpp
    src/core/effects/effect_registry.cpp
    src/core/effects/text_range_rebaser.cpp
    src/core/effects/effect_stack.cpp
    src/core/effects/wave_effect.cpp
    src/core/effects/glyph_jitter_effect.cpp
    src/core/effects/stretch_effect.cpp
    src/core/effects/procedural_effect.cpp
    src/core/effects/geometry_warp_effect.cpp
    src/core/effects/trail_effect.cpp
    src/core/serialization/project_serializer.cpp
    src/core/presets/preset.cpp
    src/core/presets/preset_catalog.cpp
    src/core/presets/preset_resources.cpp
    src/core/presets/preset_manager.cpp
    resources/resources.qrc
    src/core/export/svg_exporter.cpp
    src/core/export/export_payload_builder.cpp
    src/platform/vector_clipboard_service.cpp
    src/core/undo/document_commands.cpp
    src/core/undo/scene_commands.cpp
)

add_library(vector_typography_core STATIC ${CORE_SOURCES})
target_include_directories(vector_typography_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vector_typography_core PUBLIC Qt6::Core Qt6::Gui Qt6::Concurrent)

if (WIN32)
    target_compile_definitions(vector_typography_core PUBLIC VT_PLATFORM_WINDOWS=1)
    target_sources(vector_typography_core PRIVATE
        src/platform/windows/windows_vector_clipboard_service.cpp
    )
    target_link_libraries(vector_typography_core PRIVATE user32 gdi32 gdiplus)
endif()

set(UI_SOURCES
    src/ui/editor_controller.h
    src/ui/editor_controller.cpp
    src/ui/selection_model.h
    src/ui/selection_model.cpp
    src/ui/deformation_tool_state.h
    src/ui/slider_spin_box.h
    src/ui/slider_spin_box.cpp
    src/ui/collapsible_section.h
    src/ui/collapsible_section.cpp
    src/ui/transform_panel.h
    src/ui/transform_panel.cpp
    src/ui/editor_canvas.h
    src/ui/editor_canvas.cpp
    src/ui/tool_palette.h
    src/ui/tool_palette.cpp
    src/ui/layers_panel.h
    src/ui/layers_panel.cpp
    src/ui/shortcut_manager.h
    src/ui/shortcut_manager.cpp
    src/ui/preferences_dialog.h
    src/ui/preferences_dialog.cpp
    src/ui/typography_panel.h
    src/ui/typography_panel.cpp
    src/ui/effects_panel.h
    src/ui/effects_panel.cpp
    src/ui/style_gallery.h
    src/ui/style_gallery.cpp
    src/ui/deformation_panel.h
    src/ui/deformation_panel.cpp
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
    Qt6::Concurrent
)

if (WIN32)
    set_target_properties(vector_typography_editor PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

enable_testing()

set(TEST_SUPPORT_SOURCES
    tests/support/geometry_assertions.cpp
    tests/support/invariant_checker.cpp
    tests/support/state_fingerprint.cpp
    tests/support/test_fonts.cpp
)

add_library(vector_typography_test_support STATIC ${TEST_SUPPORT_SOURCES})
target_include_directories(vector_typography_test_support PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(vector_typography_test_support PUBLIC
    vector_typography_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)

set(CONTROLLER_TEST_SOURCES
    src/ui/editor_controller.h
    src/ui/editor_controller.cpp
    src/ui/selection_model.h
    src/ui/selection_model.cpp
    src/ui/shortcut_manager.h
    src/ui/shortcut_manager.cpp
)

# This is a hand-written test driver, not a uic-generated ui_*.h header.
set_source_files_properties(tests/support/ui_test_driver.cpp tests/support/ui_test_driver.h
    PROPERTIES SKIP_AUTOUIC ON)

add_executable(vector_typography_tests
    tests/core_tests.cpp
    ${CONTROLLER_TEST_SOURCES}
)
target_include_directories(vector_typography_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vector_typography_tests PRIVATE
    vector_typography_core
    Qt6::Core
    Qt6::Gui
    Qt6::Test
    Qt6::Concurrent
)

add_test(NAME vector_typography_core_tests COMMAND vector_typography_tests)
set_tests_properties(vector_typography_core_tests PROPERTIES LABELS "core")

add_executable(vector_typography_effect_contract_tests
    tests/integration/effect_contract_tests.cpp
)
target_include_directories(vector_typography_effect_contract_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vector_typography_effect_contract_tests PRIVATE vector_typography_test_support Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME vector_typography_effect_contract_tests COMMAND vector_typography_effect_contract_tests)
set_tests_properties(vector_typography_effect_contract_tests PROPERTIES LABELS "core;integration;effects")

add_executable(vector_typography_integration_tests
    tests/integration/workflow_integration_tests.cpp
    ${CONTROLLER_TEST_SOURCES}
)
target_include_directories(vector_typography_integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vector_typography_integration_tests PRIVATE vector_typography_test_support Qt6::Core Qt6::Gui Qt6::Test Qt6::Concurrent)
add_test(NAME vector_typography_integration_tests COMMAND vector_typography_integration_tests)
set_tests_properties(vector_typography_integration_tests PROPERTIES LABELS "integration;undo;serialization;async")

add_executable(vector_typography_ui_smoke_tests
    tests/ui/smoke_workflow_tests.cpp
    tests/support/ui_test_driver.cpp
    ${UI_SOURCES}
)
target_include_directories(vector_typography_ui_smoke_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vector_typography_ui_smoke_tests PRIVATE vector_typography_test_support vector_typography_core Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Qt6::Concurrent)
add_test(NAME vector_typography_ui_smoke_tests COMMAND vector_typography_ui_smoke_tests)
set_tests_properties(vector_typography_ui_smoke_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "ui;smoke")

add_executable(vector_typography_ui_tests
    tests/ui_regression_tests.cpp
    ${UI_SOURCES}
)
target_include_directories(vector_typography_ui_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vector_typography_ui_tests PRIVATE
    vector_typography_core
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
)
add_test(NAME vector_typography_ui_tests COMMAND vector_typography_ui_tests)
set_tests_properties(vector_typography_ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "ui;regression")

add_executable(vector_typography_windows_tests tests/platform/windows/clipboard_tests.cpp)
target_include_directories(vector_typography_windows_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(vector_typography_windows_tests PRIVATE vector_typography_core Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
add_test(NAME vector_typography_windows_tests COMMAND vector_typography_windows_tests)
set_tests_properties(vector_typography_windows_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" LABELS "windows;export")

install(TARGETS vector_typography_editor RUNTIME DESTINATION bin)
