cmake_minimum_required(VERSION 3.20)
project(MultiPadTester LANGUAGES CXX RC)

# Match vcpkg x64-windows-static (/MT) so CRT and dependency .lib agree.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(imgui CONFIG REQUIRED)
find_package(WIL CONFIG REQUIRED)
find_package(gameinput CONFIG REQUIRED)

file(GLOB_RECURSE SOURCE_FILES src/*.cpp)
file(GLOB_RECURSE HEADER_FILES src/*.h)

add_executable(MultiPadTester WIN32
    ${SOURCE_FILES}
    ${HEADER_FILES}
    res/app.rc
)

source_group("Source Files" FILES ${SOURCE_FILES})
source_group("Header Files" FILES ${HEADER_FILES})

target_include_directories(MultiPadTester PRIVATE src res)

target_link_libraries(MultiPadTester PRIVATE
    imgui::imgui
    WIL::WIL
    Microsoft::GameInput
    d3d11.lib
    d3dcompiler.lib
    dxgi.lib
    xinput.lib
    hid.lib
    dinput8.lib
    dxguid.lib
    setupapi.lib
    runtimeobject.lib
)

target_compile_definitions(MultiPadTester PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

