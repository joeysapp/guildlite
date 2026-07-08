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

`<sel>` = an MFT index, or `hash:<n>` (the stable ANet file hash).

```
datcli info    <dat>                       master-file-table summary
datcli census  <dat> [limit]               decompress + classify entries (type counts)
datcli scan    <dat> [limit]               model geometry coverage (0xFA0 parse rate)
datcli texscan <dat> [limit]               texture decode coverage (native vs asm-needed)
datcli index   <dat> [out.tsv] [limit]     build the searchable catalog (~42s full)
datcli search  <catalog.tsv> [filters]     --type T --min-tris N --max-tris N
                                           --dim N --fmt C --hash H --limit N
datcli show    <catalog.tsv> <mft|hash:N|murmur:HEX>   full record + cross-refs
datcli extract <dat> <sel> <out>           write one decompressed entry to disk
datcli obj     <dat> <sel> <out.obj>       FFNA-2 model -> Wavefront OBJ (High LOD)
datcli tex     <dat> <sel> <out.png>       ATEX/ATTX texture -> PNG
datcli objtex  <dat> <sel> <outdir>        model + its textures -> OBJ + MTL + PNG bundle
```

## Catalog (`Gw.dat.catalog.tsv`)

`datcli index` writes a tab-separated, **greppable** record per entry:
`mft · hash · murmur · type · usize · w · h · fmt · nsub · nverts · ntris · amat · texrefs`.
Stable ids are `hash` (ANet file number, when present) and `murmur` (content hash) —
`mft` is a per-dat-version handle, not stable. Full run: **77,411 models (32,467 with
geometry), 65,455 textures**. Use `search`/`show`, plain `grep`/`awk`, or address any
export by `hash:` so you never guess an MFT index. `texrefs` are the referenced
textures' file-hashes (join back with `show`).

## Status (2026-07-07)

| Layer | State | Coverage |
|---|---|---|
| Archive (MFT) + `xentax` decompress | done, proven | 100% (214,823 entries) |
| FFNA-2 model geometry -> OBJ | done, proven | ~46% of models (the `0xFA0` container) |
| ATEX/ATTX texture decode -> PNG | done, proven | ~92% on Windows (asm); ~6% native (clang) |

## Known issues / not done yet

- **Texture selection — FIXED for the main model population (GetMesh + AMAT ported, 2026-07-07).**
  `objtex` now picks each submesh's diffuse via GW's real per-submodel logic:
  `FFNA_ModelFile::submodel_texture_indices()` (a faithful trim of GWMB's `GetMesh`
  — old models read `texture_index_UV_mapping_maybe`, new models read
  `unknown_tex_stuff1` reordered by the **AMAT `0xFAD`** file's `tex_infos`, resolved
  through the DAT in `parse_model(..., Dat*)`). Verified: model 16509 assigns two
  *distinct* textures to its two submeshes; larger scenery models export correctly
  (previously every model emitted a byte-identical `tex0.png`).
  **Remaining edge case:** simple low-index props (e.g. 819, 851) hit GW's
  off-by-one / null-`(0,0)`-ref quirk — they request `texref 1` when only `texref 0`
  is valid. `objtex` applies a defensive clamp + fallback to the nearest decodable
  texture so they still get a reasonable diffuse, but their true intended texture
  may differ, and some low-index items genuinely share one texture. Models with 0
  `0xFA5` entries (textures only via AMAT-listed refs) still get no texture.
  Also note: `objtex` exports only the *first* texture of GW's multitexture stack as
  `map_Kd`; the blend flags / extra layers are not represented in OBJ/MTL.
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
