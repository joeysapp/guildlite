cmake_minimum_required(VERSION 3.16)
project(datcore LANGUAGES CXX)

# Portable Guild Wars .dat extraction core.
# Builds with Apple clang / gcc / MSVC — no DirectX, Win32, or MFC.
# The x86-asm ATEX texture stage (AtexAsm) is NOT part of this target yet; the
# archive + decompression + model-geometry layers are fully cross-platform.

set(CMAKE_CXX_STANDARD 20)   # the FFNA parser uses std::span
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(datcore STATIC
  src/dat.cpp
  src/xentax.cpp
  src/murmur3.cpp
  src/model.cpp
  src/atex_reader.cpp
  src/atex_decompress.cpp
  src/texture.cpp
)
target_include_directories(datcore PUBLIC include)
target_include_directories(datcore PRIVATE third_party)   # vendored stb_image_write.h

# ATEX pre-DXT unpack: real 32-bit x86 asm under MSVC (full-fidelity textures),
# portable stubs everywhere else (~6% native coverage; rest flagged needs-asm).
if(MSVC)
  target_sources(datcore PRIVATE src/atex_asm_win.cpp src/atex_flag_win.cpp)
  target_compile_definitions(datcore PRIVATE DATCORE_HAVE_ATEX_ASM)
else()
  target_sources(datcore PRIVATE src/atex_stub.cpp)
endif()
if(NOT MSVC)
  # multi-char literals are intentional (FourCC type tags), match GWMB byte-for-byte
  target_compile_options(datcore PRIVATE -Wno-multichar)
endif()

add_executable(datcli tools/datcli.cpp)
target_link_libraries(datcli PRIVATE datcore)
