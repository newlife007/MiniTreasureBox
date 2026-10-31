cmake_minimum_required(VERSION 3.22)

project(MiniTreasureBox VERSION 0.1.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Concurrent Gui Multimedia Widgets)

add_library(minitreasurebox_foundation STATIC
    src/ai/handwriting_removal/removal_engine.cpp
    src/ai/handwriting_removal/removal_engine.h
    src/ai/handwriting_removal/removal_widget.cpp
    src/ai/handwriting_removal/removal_widget.h
    src/ai/ocr/ocr_engine.cpp
    src/ai/ocr/ocr_engine.h
    src/ai/ocr/ocr_widget.cpp
    src/ai/ocr/ocr_widget.h
    src/app/application.cpp
    src/app/application.h
    src/app/main_window.cpp
    src/app/main_window.h
    src/core/local_file_search.cpp
    src/core/local_file_search.h
    src/core/settings.cpp
    src/core/settings.h
    src/core/tool.h
    src/core/tool_history.cpp
    src/core/tool_history.h
    src/core/tool_registry.cpp
    src/core/tool_registry.h
    src/domain/games/game_logic.cpp
    src/domain/games/game_logic.h
    src/domain/games/game_settings.cpp
    src/domain/games/game_settings.h
    src/domain/games/game_widgets.cpp
    src/domain/games/game_widgets.h
    src/domain/games/ui/board_geometry.h
    src/domain/games/ui/game_audio_controller.cpp
    src/domain/games/ui/game_audio_controller.h
    src/domain/games/ui/game_canvas.cpp
    src/domain/games/ui/game_canvas.h
    src/domain/games/ui/game_page_shell.cpp
    src/domain/games/ui/game_page_shell.h
    src/domain/games/ui/game_visual_theme.cpp
    src/domain/games/ui/game_visual_theme.h
    src/domain/tools/builtin_tools.cpp
    src/domain/tools/builtin_tools.h
    src/domain/tools/tool_logic.cpp
    src/domain/tools/tool_logic.h
    src/domain/tools/tool_widgets.cpp
    src/domain/tools/tool_widgets.h
    src/ui/pages/home_page.cpp
    src/ui/pages/home_page.h
    src/ui/widgets/local_file_search_widget.cpp
    src/ui/widgets/local_file_search_widget.h
    src/ui/widgets/tool_card.cpp
    src/ui/widgets/tool_card.h
    src/ui/widgets/tool_grid.cpp
    src/ui/widgets/tool_grid.h
    src/ui/widgets/tool_workspace/metric_card.cpp
    src/ui/widgets/tool_workspace/metric_card.h
    src/ui/widgets/tool_workspace/responsive_pair.cpp
    src/ui/widgets/tool_workspace/responsive_pair.h
    src/ui/widgets/tool_workspace/tool_status_strip.cpp
    src/ui/widgets/tool_workspace/tool_status_strip.h
    resources/game_audio.qrc
)

target_include_directories(minitreasurebox_foundation PUBLIC src)
target_compile_definitions(minitreasurebox_foundation PRIVATE
    MINITREASUREBOX_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(minitreasurebox_foundation PUBLIC
    Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Multimedia Qt6::Widgets)

add_executable(MiniTreasureBox src/main.cpp)
target_link_libraries(MiniTreasureBox PRIVATE minitreasurebox_foundation)
set_target_properties(MiniTreasureBox PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "${PROJECT_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.newlife007.minitreasurebox"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)
if(APPLE)
    set_property(TARGET MiniTreasureBox PROPERTY
        INSTALL_RPATH "@executable_path/../Frameworks"
    )
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)

    add_executable(test_tool_registry tests/unit/test_tool_registry.cpp)
    target_link_libraries(test_tool_registry PRIVATE minitreasurebox_foundation Qt6::Test)
    add_test(NAME tool_registry COMMAND test_tool_registry)

    add_executable(test_settings_history tests/unit/test_settings_history.cpp)
    target_link_libraries(test_settings_history PRIVATE minitreasurebox_foundation Qt6::Test)
    add_test(NAME settings_history COMMAND test_settings_history)

    add_executable(test_tool_logic tests/unit/test_tool_logic.cpp)
    target_link_libraries(test_tool_logic PRIVATE minitreasurebox_foundation Qt6::Test)
    add_test(NAME tool_logic COMMAND test_tool_logic)

    add_executable(test_shell_interactions tests/unit/test_shell_interactions.cpp)
    target_link_libraries(test_shell_interactions PRIVATE minitreasurebox_foundation Qt6::Test)
    add_test(NAME shell_interactions COMMAND test_shell_interactions)

    add_executable(test_tool_cards tests/unit/test_tool_cards.cpp)
    target_link_libraries(test_tool_cards PRIVATE minitreasurebox_foundation Qt6::Test)
    add_test(NAME tool_cards COMMAND test_tool_cards)

    add_executable(test_game_logic tests/unit/test_game_logic.cpp)
    target_link_libraries(test_game_logic PRIVATE minitreasurebox_foundation Qt6::Test)
    add_test(NAME game_logic COMMAND test_game_logic)

    add_executable(test_game_widgets tests/unit/test_game_widgets.cpp)
    target_link_libraries(test_game_widgets PRIVATE minitreasurebox_foundation Qt6::Test)
    add_test(NAME game_widgets COMMAND test_game_widgets)

    add_executable(fake_paddleocr_worker tests/fixtures/fake_paddleocr_worker.cpp)
    if(MSVC)
        set_property(TARGET fake_paddleocr_worker PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()

    add_executable(ocr_runtime_smoke
        tests/fixtures/ocr_runtime_smoke.cpp
        tests/fixtures/ocr_runtime_smoke.qrc)
    target_link_libraries(ocr_runtime_smoke PRIVATE minitreasurebox_foundation)

    add_executable(test_ocr_engine tests/unit/test_ocr_engine.cpp)
    target_link_libraries(test_ocr_engine PRIVATE minitreasurebox_foundation Qt6::Test)
    add_dependencies(test_ocr_engine fake_paddleocr_worker)
    add_test(NAME ocr_engine COMMAND test_ocr_engine)

    add_executable(test_ocr_widget tests/unit/test_ocr_widget.cpp)
    target_link_libraries(test_ocr_widget PRIVATE minitreasurebox_foundation Qt6::Test)
    add_dependencies(test_ocr_widget fake_paddleocr_worker)
    add_test(NAME ocr_widget COMMAND test_ocr_widget)

    add_executable(test_removal_engine tests/unit/test_removal_engine.cpp)
    target_link_libraries(test_removal_engine PRIVATE minitreasurebox_foundation Qt6::Test)
    add_test(NAME removal_engine COMMAND test_removal_engine)

    add_executable(test_removal_widget tests/unit/test_removal_widget.cpp)
    target_link_libraries(test_removal_widget PRIVATE minitreasurebox_foundation Qt6::Test)
    add_test(NAME removal_widget COMMAND test_removal_widget)

    add_executable(test_local_file_search tests/unit/test_local_file_search.cpp)
    target_link_libraries(test_local_file_search PRIVATE minitreasurebox_foundation Qt6::Test)
    add_test(NAME local_file_search COMMAND test_local_file_search)

    set_tests_properties(
        tool_logic shell_interactions tool_cards game_widgets
        ocr_engine ocr_widget removal_engine removal_widget local_file_search
        PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()

include(GNUInstallDirs)

set(MINITREASUREBOX_OCR_RUNTIME_ROOT "" CACHE PATH
    "Path to a staged, relocatable PaddleOCR worker runtime")
option(MINITREASUREBOX_REQUIRE_BUNDLED_OCR
    "Fail configuration unless a complete bundled PaddleOCR runtime is staged" OFF)

set(_mtb_ocr_runtime_ready FALSE)
if(MINITREASUREBOX_OCR_RUNTIME_ROOT)
    get_filename_component(_mtb_ocr_root
        "${MINITREASUREBOX_OCR_RUNTIME_ROOT}" ABSOLUTE)
    if(WIN32)
        set(_mtb_ocr_worker "${_mtb_ocr_root}/bin/paddleocr-worker.exe")
    else()
        set(_mtb_ocr_worker "${_mtb_ocr_root}/bin/paddleocr-worker")
    endif()
    if(NOT EXISTS "${_mtb_ocr_worker}")
        message(FATAL_ERROR
            "The staged OCR runtime has no PaddleOCR worker: ${_mtb_ocr_root}")
    endif()
    if(NOT IS_DIRECTORY "${_mtb_ocr_root}/bin/_internal")
        message(FATAL_ERROR
            "The staged PaddleOCR worker has no bundled Python dependencies")
    endif()
    foreach(_model PP-OCRv5_mobile_det_onnx PP-OCRv5_mobile_rec_onnx)
        foreach(_model_file inference.onnx inference.yml)
            if(NOT EXISTS "${_mtb_ocr_root}/models/${_model}/${_model_file}")
                message(FATAL_ERROR
                    "The staged PaddleOCR runtime is missing ${_model}/${_model_file}")
            endif()
        endforeach()
    endforeach()
    if(NOT EXISTS "${_mtb_ocr_root}/licenses/THIRD_PARTY_MANIFEST.md")
        message(FATAL_ERROR
            "The staged PaddleOCR runtime is missing third-party license notices")
    endif()
    set(_mtb_ocr_runtime_ready TRUE)
endif()

if(MINITREASUREBOX_REQUIRE_BUNDLED_OCR AND NOT _mtb_ocr_runtime_ready)
    message(FATAL_ERROR
        "MINITREASUREBOX_REQUIRE_BUNDLED_OCR requires MINITREASUREBOX_OCR_RUNTIME_ROOT")
endif()

option(MINITREASUREBOX_STAGE_BUNDLED_OCR_FOR_BUILD
    "Copy the configured PaddleOCR runtime beside build-tree executables" ON)
if(_mtb_ocr_runtime_ready AND MINITREASUREBOX_STAGE_BUNDLED_OCR_FOR_BUILD)
    string(SHA256 _mtb_ocr_root_hash "${_mtb_ocr_root}")
    string(SUBSTRING "${_mtb_ocr_root_hash}" 0 12 _mtb_ocr_root_hash_short)
    set(_mtb_build_ocr_root "${CMAKE_BINARY_DIR}/ocr-runtime")
    set(_mtb_build_ocr_stamp
        "${_mtb_build_ocr_root}/.staged-${_mtb_ocr_root_hash_short}")
    set(_mtb_ocr_stage_inputs
        "${_mtb_ocr_worker}"
        "${_mtb_ocr_root}/models/PP-OCRv5_mobile_det_onnx/inference.onnx"
        "${_mtb_ocr_root}/models/PP-OCRv5_mobile_det_onnx/inference.yml"
        "${_mtb_ocr_root}/models/PP-OCRv5_mobile_rec_onnx/inference.onnx"
        "${_mtb_ocr_root}/models/PP-OCRv5_mobile_rec_onnx/inference.yml"
        "${_mtb_ocr_root}/licenses/THIRD_PARTY_MANIFEST.md"
    )
    add_custom_command(
        OUTPUT "${_mtb_build_ocr_stamp}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${_mtb_build_ocr_root}"
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
                "${_mtb_ocr_root}" "${_mtb_build_ocr_root}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_mtb_build_ocr_stamp}"
        DEPENDS ${_mtb_ocr_stage_inputs}
        COMMENT "Staging bundled PaddleOCR runtime for build-tree execution"
        VERBATIM
    )
    add_custom_target(stage_minitreasurebox_ocr_runtime ALL
        DEPENDS "${_mtb_build_ocr_stamp}")
    add_dependencies(MiniTreasureBox stage_minitreasurebox_ocr_runtime)
endif()

if(APPLE)
    install(TARGETS MiniTreasureBox BUNDLE DESTINATION .)
elseif(WIN32)
    install(TARGETS MiniTreasureBox RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
else()
    install(TARGETS MiniTreasureBox RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

if(_mtb_ocr_runtime_ready AND WIN32)
    install(DIRECTORY "${_mtb_ocr_root}/"
        DESTINATION "ocr-runtime"
        USE_SOURCE_PERMISSIONS)
elseif(_mtb_ocr_runtime_ready AND APPLE)
    install(DIRECTORY "${_mtb_ocr_root}/"
        DESTINATION "MiniTreasureBox.app/Contents/Resources/ocr-runtime"
        USE_SOURCE_PERMISSIONS)
endif()

if(WIN32 OR APPLE)
    qt_generate_deploy_app_script(
        TARGET MiniTreasureBox
        OUTPUT_SCRIPT deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT "${deploy_script}")

    set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
    set(CPACK_PACKAGE_VENDOR "MiniTreasureBox")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "MiniTreasureBox desktop utilities")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
    set(MINITREASUREBOX_PACKAGE_ARCH_SUFFIX "" CACHE STRING
        "Optional architecture suffix for native package file names")
    set(_mtb_package_arch_suffix "")
    if(MINITREASUREBOX_PACKAGE_ARCH_SUFFIX)
        set(_mtb_package_arch_suffix "-${MINITREASUREBOX_PACKAGE_ARCH_SUFFIX}")
    endif()

    if(WIN32)
        set(CPACK_GENERATOR "NSIS")
        set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-windows-x64-unsigned-preview")
        set(CPACK_PACKAGE_INSTALL_DIRECTORY "${PROJECT_NAME} ${PROJECT_VERSION}")
        set(CPACK_PACKAGE_EXECUTABLES "MiniTreasureBox" "MiniTreasureBox")
        set(CPACK_NSIS_DISPLAY_NAME "${PROJECT_NAME} ${PROJECT_VERSION}")
        set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    else()
        set(CPACK_GENERATOR "DragNDrop")
        set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${PROJECT_VERSION}-macos${_mtb_package_arch_suffix}-unsigned-preview")
        set(CPACK_DMG_VOLUME_NAME "${PROJECT_NAME} ${PROJECT_VERSION}")
    endif()

    include(CPack)
endif()
