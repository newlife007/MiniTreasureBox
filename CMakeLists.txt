cmake_minimum_required(VERSION 3.22)

project(MiniTreasureBox VERSION 0.1.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Concurrent Gui Widgets)

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
)

target_include_directories(minitreasurebox_foundation PUBLIC src)
target_compile_definitions(minitreasurebox_foundation PRIVATE
    MINITREASUREBOX_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(minitreasurebox_foundation PUBLIC Qt6::Core Qt6::Concurrent Qt6::Gui Qt6::Widgets)

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
    find_package(Python3 REQUIRED COMPONENTS Interpreter)

    add_test(NAME ocr_runtime_fetch
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/unit/test_fetch_ocr_runtime.py"
                "${CMAKE_SOURCE_DIR}/scripts/fetch_ocr_runtime.py")
    add_test(NAME ocr_runtime_staging
        COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_SOURCE_DIR}/tests/unit/test_stage_ocr_runtime.py"
                "${CMAKE_SOURCE_DIR}/scripts/stage_ocr_runtime.py")

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

    add_executable(fake_tesseract tests/fixtures/fake_tesseract.cpp)
    target_link_libraries(fake_tesseract PRIVATE Qt6::Core)

    add_executable(ocr_runtime_smoke tests/fixtures/ocr_runtime_smoke.cpp)
    target_link_libraries(ocr_runtime_smoke PRIVATE minitreasurebox_foundation)

    add_executable(test_ocr_engine tests/unit/test_ocr_engine.cpp)
    target_link_libraries(test_ocr_engine PRIVATE minitreasurebox_foundation Qt6::Test)
    add_dependencies(test_ocr_engine fake_tesseract)
    add_test(NAME ocr_engine COMMAND test_ocr_engine)

    add_executable(test_ocr_widget tests/unit/test_ocr_widget.cpp)
    target_link_libraries(test_ocr_widget PRIVATE minitreasurebox_foundation Qt6::Test)
    add_dependencies(test_ocr_widget fake_tesseract)
    add_test(NAME ocr_widget COMMAND test_ocr_widget)

    add_executable(test_removal_engine tests/unit/test_removal_engine.cpp)
    target_link_libraries(test_removal_engine PRIVATE minitreasurebox_foundation Qt6::Test)
    add_test(NAME removal_engine COMMAND test_removal_engine)

    add_executable(test_removal_widget tests/unit/test_removal_widget.cpp)
    target_link_libraries(test_removal_widget PRIVATE minitreasurebox_foundation Qt6::Test)
    add_test(NAME removal_widget COMMAND test_removal_widget)

    set_tests_properties(tool_logic shell_interactions tool_cards game_widgets ocr_engine ocr_widget removal_engine removal_widget PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )

    add_executable(test_local_file_search tests/unit/test_local_file_search.cpp)
    target_link_libraries(test_local_file_search PRIVATE minitreasurebox_foundation Qt6::Test)
    add_test(NAME local_file_search COMMAND test_local_file_search)

    set_tests_properties(tool_logic shell_interactions tool_cards local_file_search PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    )
endif()

include(GNUInstallDirs)

set(MINITREASUREBOX_OCR_RUNTIME_ROOT "" CACHE PATH
    "Path to a staged, relocatable Tesseract runtime")
option(MINITREASUREBOX_REQUIRE_BUNDLED_OCR
    "Fail configuration unless a complete bundled OCR runtime is staged" OFF)

set(_mtb_ocr_runtime_ready FALSE)
if(MINITREASUREBOX_OCR_RUNTIME_ROOT)
    get_filename_component(_mtb_ocr_root
        "${MINITREASUREBOX_OCR_RUNTIME_ROOT}" ABSOLUTE)
    if(WIN32)
        set(_mtb_ocr_executable_candidates
            "${_mtb_ocr_root}/Library/bin/tesseract.exe"
            "${_mtb_ocr_root}/bin/tesseract.exe")
        file(GLOB _mtb_ocr_runtime_libraries
            "${_mtb_ocr_root}/Library/bin/*.dll"
            "${_mtb_ocr_root}/bin/*.dll")
    elseif(APPLE)
        set(_mtb_ocr_executable_candidates "${_mtb_ocr_root}/bin/tesseract")
        file(GLOB _mtb_ocr_runtime_libraries "${_mtb_ocr_root}/lib/*.dylib")
    endif()

    set(_mtb_ocr_executable "")
    foreach(_candidate IN LISTS _mtb_ocr_executable_candidates)
        if(EXISTS "${_candidate}")
            set(_mtb_ocr_executable "${_candidate}")
            break()
        endif()
    endforeach()
    if((WIN32 OR APPLE) AND NOT _mtb_ocr_executable)
        message(FATAL_ERROR
            "The staged OCR runtime has no Tesseract executable: ${_mtb_ocr_root}")
    endif()
    if((WIN32 OR APPLE) AND NOT _mtb_ocr_runtime_libraries)
        message(FATAL_ERROR
            "The staged OCR runtime has no transitive runtime libraries: ${_mtb_ocr_root}")
    endif()
    foreach(_language eng chi_sim)
        if(NOT EXISTS "${_mtb_ocr_root}/share/tessdata/${_language}.traineddata")
            message(FATAL_ERROR
                "The staged OCR runtime is missing ${_language}.traineddata")
        endif()
    endforeach()
    if(NOT EXISTS "${_mtb_ocr_root}/licenses/THIRD_PARTY_MANIFEST.md")
        message(FATAL_ERROR
            "The staged OCR runtime is missing third-party license notices")
    endif()
    set(_mtb_ocr_runtime_ready TRUE)
endif()

if(MINITREASUREBOX_REQUIRE_BUNDLED_OCR AND NOT _mtb_ocr_runtime_ready)
    message(FATAL_ERROR
        "MINITREASUREBOX_REQUIRE_BUNDLED_OCR requires MINITREASUREBOX_OCR_RUNTIME_ROOT")
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
    install(FILES
        "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        "${CMAKE_SOURCE_DIR}/licenses/Apache-2.0.txt"
        DESTINATION "licenses")
elseif(_mtb_ocr_runtime_ready AND APPLE)
    install(DIRECTORY "${_mtb_ocr_root}/"
        DESTINATION "MiniTreasureBox.app/Contents/Resources/ocr-runtime"
        USE_SOURCE_PERMISSIONS)
    install(FILES
        "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
        "${CMAKE_SOURCE_DIR}/licenses/Apache-2.0.txt"
        DESTINATION "MiniTreasureBox.app/Contents/Resources/licenses")
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
