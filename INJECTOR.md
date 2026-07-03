# INJECTOR-RELATED ROADMAP ITEMS AND BACKGROUND

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
| `ToolboxPlugin` base + `ToolboxPluginInstance()` export | plain `DllMain` → worker thread | ✔ (`dllmain.cpp`) |
| `Initialize(ImGuiContext* ctx, …)` hands you ImGui | `CreateContext` + `ImGui_ImplDX9/Win32_Init` yourself | ✔ (`Overlay.cpp`) |
| `Draw(IDirect3DDevice9*)` called per-frame | hook `EndScene` (vtbl 42) + `Reset` (vtbl 16) via MinHook | ✔ (`Overlay.cpp`) |
| GWCA init via `ToolboxPlugin::Initialize` | `GW::Initialize()`+`EnableHooks()` in the worker thread | ✔ (`Game.cpp`) |
| `LoadSettings/SaveSettings(folder)` + lifecycle | own settings dir (glaze JSON) + `FreeLibraryAndExitThread` unload | ✔ (`Settings.cpp`, `Overlay.cpp`) |

Everything else — `Capture`, `ObjWriter`, `TextureExport`, `GameState`, `GuildliteConfig`, and the
GWCA reads (`GW::Agents::*`, `GW::Map::*`, `GW::Chat::WriteChat`) — **lifted over unchanged** in
Phase 1. The four glue modules that replaced the toolbox seams are `Game` (GWCA lifecycle),
`Settings` (config persistence), `Exporter` (control panel + capture orchestration, the port of
`GuildlitePlugin::Draw`/`DrawControlPanel`/`BeginCapture`/`FlushCapture`), and the capture-hook
wiring inside `Overlay` (the `DrawIndexedPrimitive` hook now installs alongside `EndScene`). Every
GWCA read is gated on `Game::Ready()` so nothing touches game memory before GWCA has scanned it.
The device comes from our own `EndScene` hook, so `GW::Render::GetDevice()` is no longer needed.

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

- **Phase 0 — spike:** ✔ loader + payload that hooks `EndScene`, draws a hello ImGui window, and
  screenshots the backbuffer (hotkey, button, or `shot.request` file). Proves own-injection +
  own-overlay + screenshot-over-SSH end to end.
- **Phase 1 — lift the exporter:** ✔ ported the portable files (`Capture`, `ObjWriter`,
  `TextureExport`, `GameState`, `GuildliteConfig`), added the four glue modules (`Game`,
  `Settings`, `Exporter`, capture-hook wiring in `Overlay`), moved the `DrawIndexedPrimitive`
  capture hook alongside the `EndScene` overlay hook, and wired the control panel. Builds
  `guildlite.dll` (monolith) + `guildlite-inject.exe`. Exporter runs standalone.
- **Phase 2 — the dev loop:** ✔ `guildlite-stub.dll` (inject once, owns GWCA, watches the control
  file) + `guildlite-core.dll` (the reloadable overlay/exporter). Write `reload` to the control
  file to hot-swap a freshly-built core; write `capture`/`screenshot`/… to drive it — all over
  SSH, no re-inject. See **Phase 2 control file** below.
- **Phase 3 — the fun:** free camera, then shared-state / Prop Hunt off the ROADMAP.

## Phase 2 control file — driving the dev loop over SSH

Inject the stub once (interactive session): `guildlite-inject guildlite-stub.dll`. Thereafter the
stub watches `Documents\guildlite\control` (~2 Hz) and acts on each line, then deletes the file:

| Verb | Effect |
|---|---|
| `reload` | stop the running core, `FreeLibrary` it, load the freshly-built `guildlite-core.dll` |
| `unload` | stop the core, `GW::Terminate()`, unload the stub itself |
| `capture` | forward to the core: arm a model export |
| `capture-dry` | forward to the core: diagnostics only, writes no file |
| `screenshot` | forward to the core: backbuffer → PNG |
| `demo` | forward to the core: toggle the ImGui demo |
| *(anything else)* | forwarded verbatim to the core, so new verbs need no stub rebuild |

The core is loaded from a private **copy** (`core-live\guildlite-core-<n>.dll`) so the next build
can overwrite the canonical `guildlite-core.dll` while a core is live — the classic hot-reload
trick. GWCA lives in the **stub** (loaded once, hooks stay put across reloads); only the D3D9
overlay + exporter reload, so there is no GWCA re-init churn. Write atomically over SSH
(`control.tmp` → rename `control`) to avoid a torn read. Example:
`ssh win 'printf "reload\ncapture\n" > Documents/guildlite/control.tmp; mv Documents/guildlite/control.tmp Documents/guildlite/control'`.

## macOS control console (Gw.exe-less) — `gui/`

A native **ImGui + Metal** app (`gui/`, built with `./build.sh --macos`) that is a *better
producer* for the same control channel — the GUI replacement for the `scp … control`
one-liners. It needs no Guild Wars client and no Windows box to build or run: it drives
injected clients remotely and doubles as an ImGui sandbox for UI dev on the go.

- **Stack:** SDL2 (window/input) + Metal (`imgui_impl_sdl2` + `imgui_impl_metal`), Dear ImGui
  vendored at `third_party/imgui` (submodule). SDL2 was already `brew`-installed; nothing else
  to add. Chosen over GLFW/Cocoa because **SDL2 also runs on iOS** — see below.
- **Seams (why it's clean):** `ControlConsole` / `Transport` / `Proc` are portable C++; only
  `main.mm` is platform code. `ITransport` has a `MockTransport` (offline — logs what it would
  send, for pure UI dev) and an `SshTransport` (`scp` the payload to `<host>:control`, exactly
  the proven one-liner). ssh runs on a worker thread, so a dead host fails after
  `ConnectTimeout` instead of freezing the window.
- **Verbs:** the panel emits the exact vocabulary the stub/core already understand
  (`reload`/`reboot`/`off`/`on`/`unload`, `capture`/`capture-dry`/`screenshot`/`demo`,
  `profile clean-*`, and free-text `set <k> <v>` / `target target`). "also send capture"
  mirrors the `reboot\ncapture` habit.
- **Build:** `./build.sh --macos [--run] [--selftest] [--debug] [--clean]` →
  `tools/build_macos_gui.sh` (local CMake; no vcpkg/SSH). **Pins Apple's clang via `xcrun`** —
  Homebrew LLVM's libc++ can't resolve the macOS SDK (`mbstate_t`/`pthread`), and we need
  Apple's Obj-C++/Metal toolchain anyway; the script auto-cleans a cache configured with a
  different compiler. `Guildlite --selftest` renders headlessly (hidden window, 3 frames) and
  prints `GUILDLITE-GUI SELFTEST OK`, so the pipeline can verify it with no display.
- **iOS:** feasible ("yes, but harder"). Metal + SDL2 both run on iOS so `ControlConsole`
  recompiles; the work is an xcodeproj + signing and a new `Transport` (iOS can't spawn `ssh`
  → libssh2/relay). Everything above the `Transport`/`Proc` seam is already iOS-ready. See
  `gui/README.md`.

## Build & deploy integration

- `-Kind guildlite` in `tools/build_remote.ps1` configures/builds the `injector/` tree (its own
  `CMakePresets.json`) instead of `GWToolboxpp/`, builds targets `guildlite`, `guildlite-inject`,
  `guildlite-core`, `guildlite-stub`, verifies each is a valid PE, and installs them plus the
  staged **`gwca.dll`** to `Documents\guildlite\` (override with `--install-dir` /
  `GW_TOOLBOX_INSTALL_DIR`). Same verify/fetch/manifest machinery as the plugin.
- Deps via the same vcpkg toolchain + baseline as GWToolboxpp (`vcpkg-configuration.json`):
  **imgui** (dx9 + win32 bindings), **minhook**, **stb**, and **glaze** (the exporter's manifest +
  our settings; needs the same x86 constexpr/`ZMIJ_USE_SIMD=0` flags, mirrored in the CMake).
  **GWCA** is the prebuilt import lib + `gwca.dll` from `GWToolboxpp/Dependencies/GWCA` (not a
  vcpkg port), linked via an `IMPORTED` target and staged next to the payloads by a POST_BUILD
  copy. DirectXTex is **not** needed — `TextureExport` decodes DXT in software, dependency-free.
- `injector/{build,bin,vcpkg_installed}` are excluded from the source tarball (`windows-utils.sh`).

## Runtime risks to confirm on the live inject (authored blind on macOS)

Everything below compiles clean in the pipeline; only the interactive inject can confirm runtime.

- **GWCA vs. our D3D hooks.** GWCA uses its own (separate) MinHook inside `gwca.dll`; our overlay
  uses the payload's MinHook. The plugin's `DrawIndexedPrimitive` hook already coexisted with
  GWCA+GWToolbox, so DIP is known-safe. New in standalone: we hook `EndScene`/`Reset`. We never
  attach GWCA's render callback, so GWCA should not be hooking those — but if a future GWCA build
  does, two MinHooks on one prologue could clash. If the overlay never draws, suspect this.
- **`gwca.dll` load.** GWToolbox embeds gwca.dll as a resource; we instead deploy it next to the
  payload and let the import lib load it. If GWToolbox is *also* running, its already-loaded
  `gwca.dll` (same module name) is what binds — a coexistence case to test later. Standalone
  (guildlite alone) is the Phase-1/2 target and has no such ambiguity.
- **Hot-reload teardown.** The core removes its hooks + destroys its ImGui context on
  `GuildliteCoreStop` before the stub `FreeLibrary`s it; the drain is a `g_unload` flag + 200 ms
  sleep (same pattern as the spike's self-unload). A reload under heavy render load is the thing
  to watch.

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
