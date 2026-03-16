cmake_minimum_required(VERSION 3.20)
project(MultiPadTester LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(imgui CONFIG REQUIRED)
find_package(WIL CONFIG REQUIRED)

option(USE_GAMEINPUT "Build with GameInput backend (requires Microsoft.GameInput NuGet)" OFF)
set(GAMEINPUT_ROOT "" CACHE PATH "Path to GameInput SDK (e.g. NuGet packages Microsoft.GameInput.*/build/native)")

file(GLOB_RECURSE SOURCE_FILES src/*.cpp)
file(GLOB_RECURSE HEADER_FILES src/*.h)
if(NOT USE_GAMEINPUT OR NOT GAMEINPUT_ROOT)
	list(REMOVE_ITEM SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/gameinput_backend.cpp")
endif()

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

if(USE_GAMEINPUT AND GAMEINPUT_ROOT)
	target_include_directories(MultiPadTester PRIVATE ${GAMEINPUT_ROOT}/include)
	# Microsoft.GameInput NuGet places arch-specific libs under lib/x64 or lib/x86
	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(GAMEINPUT_LIB_DIR "${GAMEINPUT_ROOT}/lib/x64")
	else()
		set(GAMEINPUT_LIB_DIR "${GAMEINPUT_ROOT}/lib/x86")
	endif()
	target_link_directories(MultiPadTester PRIVATE ${GAMEINPUT_LIB_DIR})
	target_link_libraries(MultiPadTester PRIVATE GameInput.lib)
	target_compile_definitions(MultiPadTester PRIVATE USE_GAMEINPUT)
	# Delay-load GameInput.dll so the app starts even when the redist is not installed
	if(MSVC)
		target_link_options(MultiPadTester PRIVATE /DELAYLOAD:GameInput.dll)
	endif()
endif()
