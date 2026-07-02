# PLUGIN TO STANDALONE TOOL ROADMAP

The plan (and current spike) for graduating Guildlite from a **GWToolbox plugin** into a
**standalone tool** we own end to end: our own injector, our own D3D9/ImGui overlay, built
and driven by the existing macOS→Windows SSH pipeline.

## Why standalone

The valuable substrate under GWToolbox isn't GWToolbox — it's **GWCA**, the reverse-engineered
Guild Wars API. GWToolbox is one (crufty) consumer of it; our exporter already talks to the game
through GWCA, not through anything toolbox-specific. Freecam is GWCA camera, party-game state is
GWCA agents/party/chat, model export is a D3D9 hook. None of it needs GWToolbox — it needs
`GWCA + a D3D9 hook + ImGui`, which is separable. Owning that layer also fixes two things the
plugin couldn't (see *Dev-loop superpowers*). We keep the **neutered GWToolbox** (see
`GWToolboxpp/GUILDLITE_FORK.md`) as a companion to run *alongside* for its genuinely-good bits
(bank / `/skill` commands, hotkeys, TexMod) — two DLLs in one `Gw.exe` is fine.

## Architecture

Two artifacts instead of a plugin-in-their-tree:

- **`guildlite.dll`** — the payload injected into `Gw.exe`. Hooks the live D3D9 device and draws
  the ImGui overlay. Owns its own ImGui context (the plugin was *handed* one).
- **`guildlite-inject.exe`** — a ~90-line loader. Finds `Gw.exe` and `LoadLibrary`-injects the
  payload (`VirtualAllocEx` → `WriteProcessMemory` → `CreateRemoteThread(LoadLibraryW)`). GW has
  no anti-cheat, so classic LoadLibrary injection is correct — no manual mapping.

The GWToolbox coupling was only 5 seams, all in `plugins/Guildlite/GuildlitePlugin.cpp`:

| Seam (plugin, from toolbox) | Standalone replacement | Status |
|---|---|---|
| `ToolboxPlugin` base + `ToolboxPluginInstance()` export | plain `DllMain` → worker thread | spike ✔ (`dllmain.cpp`) |
| `Initialize(ImGuiContext* ctx, …)` hands you ImGui | `CreateContext` + `ImGui_ImplDX9/Win32_Init` yourself | spike ✔ (`Overlay.cpp`) |
| `Draw(IDirect3DDevice9*)` called per-frame | hook `EndScene` (vtbl 42) + `Reset` (vtbl 16) via MinHook | spike ✔ (`Overlay.cpp`) |
| GWCA init via `ToolboxPlugin::Initialize` | `GW::Initialize()` in the worker thread | **Phase 1** |
| `LoadSettings/SaveSettings(folder)` + lifecycle | own settings dir + `FreeLibraryAndExitThread` unload | spike ✔ (unload), Phase 1 (settings) |

Everything else — `Capture`, `ObjWriter`, `TextureExport`, `GameState`, and the GWCA reads
(`GW::Agents::*`, `GW::Render::GetDevice`, `GW::Map::*`, `GW::Chat::WriteChat`) — **lifts over
unchanged** in Phase 1.

## Dev-loop superpowers (why owning the layer matters)

Both directly answer ROADMAP line 94 — *"disable/reenable remote dlls"* and *"get an image out of
the game"* can't be done over an SSH/session-0 shell. Owning the present hook fixes both:

1. **Self-screenshot** — we're inside `EndScene`, so we copy the backbuffer to a SYSTEMMEM
   surface and write a PNG *from inside the game* (`Screenshot.cpp`, same `GetRenderTargetData`
   readback the texture exporter already uses). No OS screenshot, no session-0 wall. The tool
   screenshots itself → SSH fetches it. **Shipped in the spike.**
2. **Reloadable core + control channel** (Phase 2) — inject a thin *stub* once (in the interactive
   session); the stub `LoadLibrary`s `guildlite-core.dll` and watches a control file. SSH writes
   `capture` / `reload` / `toggle freecam` to that file → the stub acts, `FreeLibrary`s the old
   core, loads the freshly-built one. **Hot-reload + full remote control over SSH without ever
   re-injecting.** The spike already previews this: a `shot.request` file triggers a screenshot.

## Phases

- **Phase 0 — spike (this commit):** loader + payload that hooks `EndScene`, draws a hello ImGui
  window, and screenshots the backbuffer (hotkey, button, or `shot.request` file). Proves
  own-injection + own-overlay + screenshot-over-SSH end to end.
- **Phase 1 — lift the exporter:** port the 6 portable files, implement `GW::Initialize()` +
  settings, move the `DrawIndexedPrimitive` capture hook alongside the `EndScene` overlay hook,
  wire the control panel. Exporter runs standalone.
- **Phase 2 — the dev loop:** stub + reloadable core + control file. Iterate from the Mac over
  SSH with visual verification, no manual re-inject.
- **Phase 3 — the fun:** free camera, then shared-state / Prop Hunt off the ROADMAP.

## Build & deploy integration

- `-Kind guildlite` in `tools/build_remote.ps1` configures/builds the `injector/` tree (its own
  `CMakePresets.json`) instead of `GWToolboxpp/`, builds targets `guildlite` + `guildlite-inject`,
  verifies each is a valid PE, and installs both to `Documents\guildlite\` (override with
  `--install-dir` / `GW_TOOLBOX_INSTALL_DIR`). Same verify/fetch/manifest machinery as the plugin.
- Deps via the same vcpkg toolchain + baseline as GWToolboxpp (`vcpkg-configuration.json`):
  **imgui** (dx9 + win32 bindings), **minhook**, **stb**. GWCA/DirectXTex join in Phase 1.
- `injector/{build,bin,vcpkg_installed}` are excluded from the source tarball (`windows-utils.sh`).

## First-build status

**Compiled clean on the first build (2026-07-02)** — vcpkg resolved imgui (dx9 + win32) / minhook
/ stb, and the loader + overlay + screenshot all built and verified over the SSH pipeline; none of
the authored-blind-on-macOS risks below bit. They're kept as notes in case an upstream vcpkg bump
moves things. The one thing only the interactive inject can confirm is **runtime** behaviour (the
overlay renders and the screenshot's colours are right):

- **ImGui backend header path** — `#include <imgui_impl_dx9.h>` may need to be
  `<backends/imgui_impl_dx9.h>` depending on the vcpkg imgui port version (same for win32).
- **Target names** — `imgui::imgui` / `minhook::minhook` are the expected vcpkg CMake targets; if
  config errors, check the port's `usage` (minhook is sometimes `unofficial::minhook`).
- **`ImGui_ImplWin32_WndProcHandler`** — forward-declared in `Overlay.cpp`; if the installed
  backend header already prototypes it, the redundant declaration is harmless.
- **Backbuffer format** — the screenshot assumes `X8R8G8B8`/`A8R8G8B8` (BGRA); if GW renders a
  different format the PNG still writes but channels could swap — a one-line swizzle fix.
