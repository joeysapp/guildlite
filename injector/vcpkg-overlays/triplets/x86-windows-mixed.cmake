# Static libs + static CRT: the injected guildlite.dll and guildlite-inject.exe are
# self-contained, so they don't drag a vcredist dependency into Gw.exe's address space.
set(VCPKG_TARGET_ARCHITECTURE x86)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
