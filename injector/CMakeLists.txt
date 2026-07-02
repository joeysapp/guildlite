cmake_minimum_required(VERSION 3.29)
project(guildlite_injector CXX)

# Guild Wars is a 32-bit D3D9 process; the payload DLL MUST match it. The vcpkg preset
# passes -A Win32, so a 64-bit configure is a mistake -- fail loudly rather than build junk.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Configure 32-bit (the 'vcpkg' preset does: -A Win32). Got a ${CMAKE_SIZEOF_VOID_P}-byte pointer build.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# dll + exe (+ pdb) land in bin/<Config>/ -- matches the GWToolboxpp layout the pipeline expects.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin/")

add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)

find_package(imgui CONFIG REQUIRED)
find_package(minhook CONFIG REQUIRED)
find_path(STB_INCLUDE_DIRS "stb_image_write.h")

# --- payload: injected into Gw.exe, hooks D3D9 EndScene, draws the ImGui overlay ---------
add_library(guildlite SHARED
    src/dllmain.cpp
    src/Overlay.cpp
    src/Screenshot.cpp)
target_include_directories(guildlite PRIVATE src ${STB_INCLUDE_DIRS})
target_link_libraries(guildlite PRIVATE imgui::imgui minhook::minhook d3d9)

# --- loader: finds Gw.exe and LoadLibrary-injects guildlite.dll -------------------------
add_executable(guildlite-inject src/inject_main.cpp)
