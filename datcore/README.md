# datcore — portable Guild Wars `Gw.dat` extractor

A dependency-free C++17/20 library + `datcli` tool that reads Guild Wars' `Gw.dat`
archive and exports models and textures to cross-platform formats. No DirectX,
Win32, or MFC — builds and runs on macOS/Linux/Windows. Ported from the parse
layer of `third_party/GuildWarsMapBrowser` (and `GWDatBrowser`), lifted off their
D3D renderer.

## Build

**macOS / Linux** (models + ~6% of textures; see the ATEX note):
```
cd datcore
cmake -B build -DCMAKE_C_COMPILER=/usr/bin/clang -DCMAKE_CXX_COMPILER=/usr/bin/clang++
cmake --build build -j
# -> bin/datcli   (MUST use Apple clang, not Homebrew LLVM — SDK header errors)
```

**Windows** (full-fidelity textures — the 32-bit MSVC build compiles the real
ATEX x86 asm): from the repo root, `./build.sh --datcore` → builds `datcli.exe`
and deploys it to `Documents\guildlite` on the box.

## `datcli` commands

```
datcli info    <dat>                       master-file-table summary
datcli census  <dat> [limit]               decompress + classify entries (type counts)
datcli scan    <dat> [limit]               model geometry coverage (0xFA0 parse rate)
datcli texscan <dat> [limit]               texture decode coverage (native vs asm-needed)
datcli extract <dat> <index> <out>         write one decompressed entry to disk
datcli obj     <dat> <index> <out.obj>     FFNA-2 model -> Wavefront OBJ (High LOD)
datcli tex     <dat> <index> <out.png>     ATEX/ATTX texture -> PNG
datcli objtex  <dat> <index> <outdir>      model + its textures -> OBJ + MTL + PNG bundle
```

## Status (2026-07-07)

| Layer | State | Coverage |
|---|---|---|
| Archive (MFT) + `xentax` decompress | done, proven | 100% (214,823 entries) |
| FFNA-2 model geometry -> OBJ | done, proven | ~46% of models (the `0xFA0` container) |
| ATEX/ATTX texture decode -> PNG | done, proven | ~92% on Windows (asm); ~6% native (clang) |

## Known issues / not done yet

- **Texture SELECTION is wrong — models often get the same/shared texture.**
  Texture *decode* is correct (verified pixel-for-pixel), but `objtex` picks which
  texture goes on a submesh with a crude heuristic (the model's `0xFA5` texture-
  filename refs, in order). GW's real per-submodel texture assignment lives in
  GWMB's `GetMesh()` — the per-submodel `tex_indices`, the old-vs-new-model
  `pixel_shader_type` split, and the **AMAT material file** (`0xFAD`, gives texture
  ordering) — which this port dropped to stay DirectX-free.
  **Evidence (2026-07-07, box `objtex` run in `../datcore-objtex-output`):** models
  22/23/30/711/712/763/764/811/812/819 all emit a byte-identical `tex0.png`
  (md5 `e3a7…`) because they all parse the same first ref `(44508,257) → mft 21`
  (a lightmap/environment-looking texture, not a diffuse); 851/852 differ
  `(8452,256) → 557`. So the parse reads model-specific data (not a constant bug),
  but the *first* `0xFA5` ref is frequently a shared texture, not the model's skin.
  Many models have 0 `0xFA5` entries entirely (textures only via AMAT) → no texture.
  **Fix: port `GetMesh`'s texture-index resolution + the AMAT (`0xFAD`) parse**
  (and sanity-check the raw `0xFA5` chunk bytes for a couple of these models while
  doing it, to rule out any residual offset error).
- **`0xBB8` model family (~54% of models) is undecoded** — a second FFNA-2 container
  (`0xBB8/0xBBA/0xBBB`) that GWMB doesn't parse either. Likely where character /
  creature models live. Raw RE needed (check OpenTyria / Headquarter).
- **No skeleton / animation** — DAT models are static geometry. Rigging is a live-
  capture-path strength (the injector's `#vbld` + bone palette), not the DAT's.
- **DDS textures** bypass ATEX and are not handled.
- **ATEX SubCode4/5/7** are stubbed on non-Windows (the ~6% native figure); a C
  port of those x86-asm stages would make full textures native on macOS.

## Next

- Add a glTF-binary (`.glb`) writer (self-contained geometry + embedded texture)
  for `~/etc/git/topic-get` (its GLTFLoader is present but unwired) and Blender.
- Fix texture selection (port `GetMesh` + AMAT) — the top correctness gap.
