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

set(ONNXRUNTIME_VERSION 1.24.4)
set(DIRECTML_VERSION 1.15.4)
if(WIN32)
    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8 OR CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
        message(FATAL_ERROR "PilotFly needs a 64 bit x64 build on Windows. Use the Visual Studio toolchain with architecture amd64.")
    endif()
    FetchContent_Declare(onnxruntime_prebuilt
        URL https://www.nuget.org/api/v2/package/Microsoft.ML.OnnxRuntime.DirectML/${ONNXRUNTIME_VERSION}
        URL_HASH SHA256=57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681
        DOWNLOAD_NAME onnxruntime-directml-${ONNXRUNTIME_VERSION}.zip
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    FetchContent_Declare(directml_prebuilt
        URL https://www.nuget.org/api/v2/package/Microsoft.AI.DirectML/${DIRECTML_VERSION}
        URL_HASH SHA256=4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9
        DOWNLOAD_NAME directml-${DIRECTML_VERSION}.zip
        DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    FetchContent_MakeAvailable(onnxruntime_prebuilt directml_prebuilt)
    set(ONNXRUNTIME_INCLUDE_DIR ${onnxruntime_prebuilt_SOURCE_DIR}/build/native/include)
    set(ONNXRUNTIME_BIN_DIR ${onnxruntime_prebuilt_SOURCE_DIR}/runtimes/win-x64/native)
    set(DIRECTML_INCLUDE_DIR ${directml_prebuilt_SOURCE_DIR}/include)
    set(DIRECTML_BIN_DIR ${directml_prebuilt_SOURCE_DIR}/bin/x64-win)
    foreach(REQUIRED_FILE
            ${ONNXRUNTIME_INCLUDE_DIR}/onnxruntime_cxx_api.h
            ${ONNXRUNTIME_INCLUDE_DIR}/dml_provider_factory.h
            ${ONNXRUNTIME_BIN_DIR}/onnxruntime.lib
            ${ONNXRUNTIME_BIN_DIR}/onnxruntime.dll
            ${ONNXRUNTIME_BIN_DIR}/onnxruntime_providers_shared.dll
            ${DIRECTML_INCLUDE_DIR}/DirectML.h
            ${DIRECTML_BIN_DIR}/DirectML.dll)
        if(NOT EXISTS ${REQUIRED_FILE})
            message(FATAL_ERROR "The downloaded ONNX Runtime package is missing ${REQUIRED_FILE}")
        endif()
    endforeach()
    set(ONNXRUNTIME_RUNTIME_FILES
        ${ONNXRUNTIME_BIN_DIR}/onnxruntime.dll
        ${ONNXRUNTIME_BIN_DIR}/onnxruntime_providers_shared.dll
        ${DIRECTML_BIN_DIR}/DirectML.dll)
    add_library(onnxruntime SHARED IMPORTED GLOBAL)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION ${ONNXRUNTIME_BIN_DIR}/onnxruntime.dll
        IMPORTED_IMPLIB ${ONNXRUNTIME_BIN_DIR}/onnxruntime.lib)
    target_include_directories(onnxruntime INTERFACE ${ONNXRUNTIME_INCLUDE_DIR} ${DIRECTML_INCLUDE_DIR})
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
        FetchContent_Declare(onnxruntime_prebuilt
            URL https://github.com/microsoft/onnxruntime/releases/download/v${ONNXRUNTIME_VERSION}/onnxruntime-osx-arm64-${ONNXRUNTIME_VERSION}.tgz
            URL_HASH SHA256=93787795f47e1eee369182e43ed51b9e5da0878ab0346aecf4258979b8bba989
            DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    else()
        FetchContent_Declare(onnxruntime_prebuilt
            URL https://github.com/microsoft/onnxruntime/releases/download/v1.23.0/onnxruntime-osx-universal2-1.23.0.tgz
            URL_HASH SHA256=5e4365fb4a05aef353f6232b9a1848f37e608c421c9227e9224572205c0cfc08
            DOWNLOAD_EXTRACT_TIMESTAMP FALSE)
    endif()
    FetchContent_MakeAvailable(onnxruntime_prebuilt)
    set(ONNXRUNTIME_INCLUDE_DIR ${onnxruntime_prebuilt_SOURCE_DIR}/include)
    set(ONNXRUNTIME_LIBRARY ${onnxruntime_prebuilt_SOURCE_DIR}/lib/libonnxruntime.dylib)
    if(NOT EXISTS ${ONNXRUNTIME_INCLUDE_DIR}/onnxruntime_cxx_api.h OR NOT EXISTS ${ONNXRUNTIME_LIBRARY})
        message(FATAL_ERROR "The downloaded ONNX Runtime package is missing its include or lib folder")
    endif()
    set(ONNXRUNTIME_RUNTIME_FILES "")
    add_library(onnxruntime SHARED IMPORTED GLOBAL)
    set_target_properties(onnxruntime PROPERTIES IMPORTED_LOCATION ${ONNXRUNTIME_LIBRARY})
    target_include_directories(onnxruntime INTERFACE ${ONNXRUNTIME_INCLUDE_DIR})
else()
    message(FATAL_ERROR "PilotFly downloads ONNX Runtime only for Windows x64 and macOS.")
endif()

function(pilotfly_copy_runtime_files TARGET_NAME)
    foreach(RUNTIME_FILE ${ONNXRUNTIME_RUNTIME_FILES})
        add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                ${RUNTIME_FILE}
                $<TARGET_FILE_DIR:${TARGET_NAME}>)
    endforeach()
endfunction()

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
    src/capture/DummyFrameSource.cpp
    src/brain/TestPatternBrain.cpp
    src/app/Config.cpp
    src/app/RunLoop.cpp)
target_include_directories(pilotfly_core PUBLIC src)
target_link_libraries(pilotfly_core PUBLIC Threads::Threads)
if(WIN32)
    target_compile_definitions(pilotfly_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(pilotfly_core PUBLIC winmm)
endif()

add_library(pilotfly_brain STATIC
    src/brain/OnnxBrain.cpp)
target_link_libraries(pilotfly_brain PUBLIC pilotfly_core onnxruntime)

add_executable(pilotfly
    src/main.cpp
    src/controller/SinkFactory.cpp
    src/capture/FrameSourceFactory.cpp
    src/input/StopKeyFactory.cpp
    src/ui/Tx12View.cpp
    src/ui/MainWindow.cpp)
target_link_libraries(pilotfly PRIVATE pilotfly_brain imgui)
pilotfly_copy_runtime_files(pilotfly)

if(WIN32)
    target_sources(pilotfly PRIVATE src/input/WinStopKey.cpp src/capture/GdiFrameSource.cpp)
    target_link_libraries(pilotfly PRIVATE user32 gdi32)
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
    tests/RunLoopTests.cpp
    tests/DummyFrameSourceTests.cpp
    tests/OnnxBrainTests.cpp)
target_link_libraries(pilotfly_tests PRIVATE pilotfly_brain Catch2::Catch2WithMain)
target_compile_definitions(pilotfly_tests PRIVATE PILOTFLY_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
pilotfly_copy_runtime_files(pilotfly_tests)
add_test(NAME pilotfly_tests COMMAND pilotfly_tests)
