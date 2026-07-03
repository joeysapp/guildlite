cmake_minimum_required(VERSION 3.20)
project(guildlite_gui LANGUAGES C CXX OBJCXX)

# This is the macOS (SDL2 + Metal) build of the Guildlite control console. It is intentionally
# separate from injector/ (which is 32-bit MSVC via vcpkg on the Windows box) -- different
# toolchain, different platform, no shared build. Drive it with `./build.sh --macos`.
if(NOT APPLE)
    message(FATAL_ERROR "gui/ is the macOS build. Use ./build.sh (no --macos) for the Windows payloads.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # so clangd/editors resolve imgui.h include paths

set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/imgui")
if(NOT EXISTS "${IMGUI_DIR}/imgui.h")
    message(FATAL_ERROR
        "Dear ImGui submodule missing at ${IMGUI_DIR}.\n"
        "Run: git submodule update --init third_party/imgui")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)

set(IMGUI_METAL_BACKEND "${IMGUI_DIR}/backends/imgui_impl_metal.mm")

# Dear ImGui core + exactly the two backends we use: SDL2 (windowing/input) + Metal (render).
set(IMGUI_SOURCES
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/imgui_demo.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_sdl2.cpp"
    "${IMGUI_METAL_BACKEND}")

set(APP_SOURCES
    src/main.mm
    src/ControlConsole.cpp
    src/Transport.cpp
    src/Proc.cpp)

add_executable(Guildlite MACOSX_BUNDLE ${APP_SOURCES} ${IMGUI_SOURCES})

set_target_properties(Guildlite PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Guildlite"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.guildlite.gui"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0")

# ARC only on the Objective-C++ translation units (our host + the imgui Metal backend).
# The imgui Metal backend is written to support ARC and non-ARC both.
set_source_files_properties(src/main.mm "${IMGUI_METAL_BACKEND}"
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")

target_include_directories(Guildlite PRIVATE
    src
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
    ${SDL2_INCLUDE_DIRS})

target_compile_options(Guildlite PRIVATE ${SDL2_CFLAGS_OTHER})
target_link_directories(Guildlite PRIVATE ${SDL2_LIBRARY_DIRS})

# We link the SDL2 dylib (which pulls its own framework deps) plus the frameworks OUR code
# touches directly: Metal + QuartzCore for the layer/encoder, Foundation/Cocoa for the app.
target_link_libraries(Guildlite PRIVATE
    ${SDL2_LIBRARIES}
    "-framework Metal"
    "-framework MetalKit"
    "-framework QuartzCore"
    "-framework Foundation"
    "-framework Cocoa")
