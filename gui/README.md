# Guildlite — Control (macOS, SDL2 + Metal)

A native **ImGui + Metal** app that runs **without a Guild Wars client**. Two jobs:

1. **Drive injected Windows clients from macOS.** It produces control-file verbs
   (`reload`, `capture`, `screenshot`, `set up_axis 2`, …) and ships them to a client over
   SSH — the GUI replacement for the `scp … control` one-liners. The injected stub polls
   `Documents\guildlite\control` ~2 Hz and acts on each line (see `../INJECTOR.md`).
2. **An ImGui/Metal sandbox** for building and testing UI on the go, no game required
   (demo window, metrics, style editor built in).

The window/render stack is **SDL2 + Metal** on purpose: that same stack also runs on iOS, so
the portable C++ console here (`ControlConsole`, `Transport`, `Proc`) is what an eventual
iPhone build would reuse — swapping only the transport (iOS can't spawn `ssh`) and the
windowing host. See "iOS" below.

## Build & run

```sh
./build.sh --macos              # configure + build gui/ into gui/build/Guildlite.app
./build.sh --macos --run        # …and launch it
./build.sh --macos --selftest   # …and verify headlessly (hidden window, render, exit 0)
./build.sh --macos --debug --clean
```

Local tools only — **no Windows box, no SSH pipeline, no vcpkg.** Requirements (all already
present on the dev Mac): CMake, `pkg-config`, SDL2 (`brew install sdl2`), Xcode Command Line
Tools, and the vendored Dear ImGui submodule:

```sh
git submodule update --init third_party/imgui
```

The build pins **Apple's** clang (via `xcrun`), not Homebrew LLVM — the latter's libc++ can't
resolve the macOS SDK, and we need Apple's Objective-C++/Metal toolchain regardless.

## Using it

- **Connection**: pick `mock` (offline; logs what it *would* send — pure UI dev) or `ssh`
  (drives a live client). Set the `host` (an ssh Host alias in `~/.ssh/config`, default
  `guildlite-win`) and control path. ssh sends run on a worker thread, so an unreachable host
  fails after `ConnectTimeout` instead of freezing the window.
- **Lifecycle / Actions / Profiles**: one-click verbs. "also send 'capture'" mirrors the
  `reboot\ncapture` habit from the one-liners.
- **Custom verb**: anything the core/exporter understands — `set <key> <val>`, `target target`,
  `profile clean-solo`, etc. Forwarded verbatim (INJECTOR.md "control file").
- **Fetch remote log**: best-effort `ssh <host> <logCommand>` peek at the DLL log.

## Layout

| File | Role |
|---|---|
| `src/main.mm` | SDL2 + Metal host + ImGui loop; `--selftest` headless verify |
| `src/ControlConsole.{h,cpp}` | the panel + async dispatch + log (portable C++/ImGui) |
| `src/Transport.{h,cpp}` | `ITransport` seam: `MockTransport` + `SshTransport` (scp/ssh) |
| `src/Proc.{h,cpp}` | `posix_spawn` run-with-capture (the one file iOS replaces) |
| `CMakeLists.txt` | macOS-only; ImGui core + sdl2/metal backends + our sources |

## iOS — "yes, but harder"

The rendering is free (Metal is identical on iOS; `imgui_impl_metal` runs unchanged) and SDL2
runs on iOS, so `ControlConsole` recompiles. The real work is (1) an Xcode project +
code-signing (a free Apple ID sideloads to your own device; $99/yr for TestFlight/longevity),
and (2) a new `Transport`: iOS forbids spawning `ssh`, so it needs an in-process SSH lib
(libssh2/NMSSH) or a small relay on the Windows box. Everything above the `Transport`/`Proc`
seam is already iOS-ready.
