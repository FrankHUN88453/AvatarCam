cmake_minimum_required(VERSION 3.24)
project(AvatarCam LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Statikus C++ futtatókörnyezet: az exe-hez nem kell külön VC++ Redistributable
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(EXT ${CMAKE_SOURCE_DIR}/external)

add_library(imgui STATIC
  ${EXT}/imgui/imgui.cpp
  ${EXT}/imgui/imgui_draw.cpp
  ${EXT}/imgui/imgui_tables.cpp
  ${EXT}/imgui/imgui_widgets.cpp
  ${EXT}/imgui/imgui_demo.cpp
  ${EXT}/imgui/backends/imgui_impl_win32.cpp
  ${EXT}/imgui/backends/imgui_impl_dx11.cpp
  ${EXT}/imgui/misc/cpp/imgui_stdlib.cpp)
target_include_directories(imgui PUBLIC ${EXT}/imgui ${EXT}/imgui/backends ${EXT}/imgui/misc/cpp)

add_library(thirdparty STATIC
  ${EXT}/ufbx/ufbx.c
  src/thirdparty_impl.cpp)
target_include_directories(thirdparty PUBLIC ${EXT}/ufbx ${EXT}/cgltf ${EXT}/stb ${EXT}/stb/deprecated ${EXT}/json)

# Spout2 SDK (DirectX küldő), BSD 2-Clause
add_library(spout STATIC
  ${EXT}/spout/SpoutDX.cpp ${EXT}/spout/SpoutCopy.cpp ${EXT}/spout/SpoutDirectX.cpp ${EXT}/spout/SpoutFrameCount.cpp
  ${EXT}/spout/SpoutSenderNames.cpp ${EXT}/spout/SpoutSharedMemory.cpp ${EXT}/spout/SpoutUtils.cpp)
target_include_directories(spout PUBLIC ${EXT}/spout)
target_compile_definitions(spout PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
if(MSVC)
  target_compile_options(spout PRIVATE /W0 /utf-8)
endif()

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS src/*.cpp src/*.h)
list(REMOVE_ITEM APP_SOURCES ${CMAKE_SOURCE_DIR}/src/thirdparty_impl.cpp)

add_executable(AvatarCam WIN32 ${APP_SOURCES} src/app.rc)
target_include_directories(AvatarCam PRIVATE src ${EXT}/onnxruntime/include)
target_link_libraries(AvatarCam PRIVATE imgui thirdparty spout d3d11 dxgi d3dcompiler dwmapi ws2_32 winmm ole32 mfplat mf mfreadwrite mfuuid shlwapi comdlg32 shell32)
target_compile_definitions(AvatarCam PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
if(MSVC)
  target_compile_options(AvatarCam PRIVATE /utf-8 /W3 /permissive- /Zc:__cplusplus /MP /Zi)
  target_compile_options(thirdparty PRIVATE /utf-8 /W0)
  target_compile_options(imgui PRIVATE /utf-8)
  target_link_options(AvatarCam PRIVATE /MANIFEST:NO /DEBUG /OPT:REF /OPT:ICF)
endif()

set_target_properties(AvatarCam PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
