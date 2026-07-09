cmake_minimum_required(VERSION 3.29)
project(guildlite_injector CXX)

# Guild Wars is a 32-bit D3D9 process; the payload DLLs MUST match it. The vcpkg preset
# passes -A Win32, so a 64-bit configure is a mistake -- fail loudly rather than build junk.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Configure 32-bit (the 'vcpkg' preset does: -A Win32). Got a ${CMAKE_SIZEOF_VOID_P}-byte pointer build.")
endif()

# glaze (via GameState/Settings) wants a recent standard; mirror GWToolboxpp exactly so it
# builds the same way here -- C++26, but MSVC (non-clang) falls back to C++23.
set(CMAKE_CXX_STANDARD 26)
if(MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_STANDARD 23)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# dll + exe (+ pdb) land in bin/<Config>/ -- matches the GWToolboxpp layout the pipeline expects.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/bin/")

add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)

# The exporter's manifest + our settings serialise through glaze, which needs deeper
# constexpr/template limits and its x64-only SIMD float writer disabled on x86 -- mirror
# GWToolboxpp/CMakeLists.txt so glaze builds the same way in this (separate) tree.
add_compile_definitions(ZMIJ_USE_SIMD=0)
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options("SHELL:-Xclang -fconstexpr-depth=2048" "SHELL:-Xclang -fconstexpr-steps=10000000" /bigobj)
else()
    add_compile_options(/constexpr:depth2048 /constexpr:steps10000000 /templateDepth:2048 /bigobj)
endif()

find_package(imgui CONFIG REQUIRED)
find_package(minhook CONFIG REQUIRED)
find_package(glaze CONFIG REQUIRED)
find_path(STB_INCLUDE_DIRS "stb_image_write.h")

# GWCA: the reverse-engineered Guild Wars API. Prebuilt import lib + runtime dll from the
# GWCA submodule at third_party/GWCA -- a pinned "ghost" mirror we own (see its README),
# NOT the GWToolbox tree. This is what decouples the injector from GWToolboxpp: GWCA is the
# real substrate, GWToolbox was just one consumer of it.
set(GWCA_FOLDER "${PROJECT_SOURCE_DIR}/../third_party/GWCA")
add_library(gwca SHARED IMPORTED GLOBAL)
set_target_properties(gwca PROPERTIES
    IMPORTED_IMPLIB "${GWCA_FOLDER}/lib/gwca.lib"
    IMPORTED_LOCATION "${GWCA_FOLDER}/bin/gwca.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${GWCA_FOLDER}/include")
target_link_libraries(gwca INTERFACE minhook::minhook)
# GWCA_API resolves to __declspec(dllimport) only when GWCA_IMPORT is defined (Export.h);
# GWToolboxdll defines it the same way. Propagate to every consumer of this imported target.
target_compile_definitions(gwca INTERFACE GWCA_IMPORT)

# --- shared engine: the overlay/ImGui host + the exporter + the ported plugin files ------
# Compiled into both the Phase-1 monolith and the Phase-2 reloadable core; the only
# difference between those two is the entry translation unit and who owns GWCA.
set(GUILDLITE_ENGINE
    src/Overlay.cpp
    src/Screenshot.cpp
    src/Exporter.cpp
    src/Freecam.cpp
    src/Editor.cpp
    src/AppearanceApply.cpp
    src/Info.cpp
    src/Commands.cpp
    src/Capture.cpp
    src/ObjWriter.cpp
    src/TextureExport.cpp
    src/GameState.cpp
    src/Settings.cpp
    src/Game.cpp)

set(GUILDLITE_ENGINE_LIBS imgui::imgui minhook::minhook glaze::glaze gwca d3d9)

# gwca.dll must sit next to whatever we inject (the import lib binds it by name at load); drop
# a copy into the output dir so the loader and the deploy step both find it.
function(guildlite_stage_gwca tgt)
    add_custom_command(TARGET ${tgt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${GWCA_FOLDER}/bin/gwca.dll" "$<TARGET_FILE_DIR:${tgt}>/gwca.dll"
        VERBATIM)
endfunction()

# --- Phase 1: monolith payload -- direct-injected, self-bootstraps + owns GWCA -----------
add_library(guildlite SHARED src/dllmain.cpp ${GUILDLITE_ENGINE})
target_include_directories(guildlite PRIVATE src ${STB_INCLUDE_DIRS})
target_link_libraries(guildlite PRIVATE ${GUILDLITE_ENGINE_LIBS})
guildlite_stage_gwca(guildlite)

# --- loader: finds Gw.exe and LoadLibrary-injects a payload (guildlite.dll or the stub) ---
add_executable(guildlite-inject src/inject_main.cpp)

# --- Phase 2: reloadable core -- same engine, C-ABI entry, GWCA owned by the stub --------
add_library(guildlite-core SHARED src/core_main.cpp ${GUILDLITE_ENGINE})
target_include_directories(guildlite-core PRIVATE src ${STB_INCLUDE_DIRS})
target_link_libraries(guildlite-core PRIVATE ${GUILDLITE_ENGINE_LIBS})
guildlite_stage_gwca(guildlite-core)

# --- Phase 2: hot-reload stub -- injected once, owns GWCA, (re)loads the core ------------
add_library(guildlite-stub SHARED src/stub_main.cpp src/Game.cpp)
target_include_directories(guildlite-stub PRIVATE src)
target_link_libraries(guildlite-stub PRIVATE gwca)
guildlite_stage_gwca(guildlite-stub)
