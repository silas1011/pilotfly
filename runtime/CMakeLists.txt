cmake_minimum_required(VERSION 3.24)
project(PilotFly LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(VJOY_SDK_DIR "" CACHE PATH "Folder of the vJoy SDK that contains inc and lib")

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW TRUE)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.5
    GIT_SHALLOW TRUE)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.7.1
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(glfw imgui Catch2)

find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
if(APPLE)
    target_compile_definitions(imgui PUBLIC GL_SILENCE_DEPRECATION)
endif()

add_library(pilotfly_core STATIC
    src/core/ControllerState.cpp
    src/core/Tx12Layout.cpp
    src/core/SafeState.cpp
    src/controller/DummySink.cpp
    src/brain/TestPatternBrain.cpp
    src/app/Config.cpp
    src/app/RunLoop.cpp)
target_include_directories(pilotfly_core PUBLIC src)
target_link_libraries(pilotfly_core PUBLIC Threads::Threads)
if(WIN32)
    target_compile_definitions(pilotfly_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(pilotfly_core PUBLIC winmm)
endif()

add_executable(pilotfly
    src/main.cpp
    src/controller/SinkFactory.cpp
    src/input/StopKeyFactory.cpp
    src/ui/Tx12View.cpp
    src/ui/MainWindow.cpp)
target_link_libraries(pilotfly PRIVATE pilotfly_core imgui)

if(WIN32)
    target_sources(pilotfly PRIVATE src/input/WinStopKey.cpp)
    if(VJOY_SDK_DIR)
        target_sources(pilotfly PRIVATE src/controller/VJoySink.cpp)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(VJOY_LIB_DIR ${VJOY_SDK_DIR}/lib/x64)
        else()
            set(VJOY_LIB_DIR ${VJOY_SDK_DIR}/lib/x86)
        endif()
        if(NOT EXISTS ${VJOY_SDK_DIR}/inc/public.h OR NOT EXISTS ${VJOY_LIB_DIR}/vJoyInterface.lib)
            message(FATAL_ERROR "VJOY_SDK_DIR must be the SDK folder of vJoy 2.2.2.0 that contains inc and lib. Got: ${VJOY_SDK_DIR}")
        endif()
        target_include_directories(pilotfly PRIVATE ${VJOY_SDK_DIR}/inc)
        target_link_libraries(pilotfly PRIVATE ${VJOY_LIB_DIR}/vJoyInterface.lib)
        target_compile_definitions(pilotfly PRIVATE PILOTFLY_HAS_VJOY)
        add_custom_command(TARGET pilotfly POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${VJOY_LIB_DIR}/vJoyInterface.dll
                $<TARGET_FILE_DIR:pilotfly>)
    else()
        message(WARNING "VJOY_SDK_DIR is not set. PilotFly is built with the dummy controller only.")
    endif()
else()
    target_sources(pilotfly PRIVATE src/input/GlfwStopKey.cpp)
endif()

enable_testing()
add_executable(pilotfly_tests
    tests/ControllerStateTests.cpp
    tests/Tx12LayoutTests.cpp
    tests/SafeStateTests.cpp
    tests/ConfigTests.cpp
    tests/TestPatternBrainTests.cpp
    tests/RunLoopTests.cpp)
target_link_libraries(pilotfly_tests PRIVATE pilotfly_core Catch2::Catch2WithMain)
add_test(NAME pilotfly_tests COMMAND pilotfly_tests)
