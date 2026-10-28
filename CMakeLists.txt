cmake_minimum_required(VERSION 3.22)

project(MiniTreasureBox VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

add_library(minitreasurebox_foundation STATIC
    src/app/application.cpp
    src/app/application.h
    src/app/main_window.cpp
    src/app/main_window.h
    src/core/settings.cpp
    src/core/settings.h
    src/core/tool.h
    src/core/tool_history.cpp
    src/core/tool_history.h
    src/core/tool_registry.cpp
    src/core/tool_registry.h
    src/domain/tools/builtin_tools.cpp
    src/domain/tools/builtin_tools.h
    src/domain/tools/tool_logic.cpp
    src/domain/tools/tool_logic.h
    src/domain/tools/tool_widgets.cpp
    src/domain/tools/tool_widgets.h
    src/ui/pages/home_page.cpp
    src/ui/pages/home_page.h
)

target_include_directories(minitreasurebox_foundation PUBLIC src)
target_link_libraries(minitreasurebox_foundation PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)

add_executable(MiniTreasureBox src/main.cpp)
target_link_libraries(MiniTreasureBox PRIVATE minitreasurebox_foundation)

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
endif()
