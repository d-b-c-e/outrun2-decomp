# Texture replacement (Tweaks packs): design notes

`[Graphics] SceneTextureReplacement` and `UITextureReplacement` (both off by default) read OutRun2006Tweaks texture
packs from `<game folder>/<TextureBaseFolder>/load`. ROADMAP.md has the status and the in-game results; this note
keeps the design and the open problems.

## Names and lookup

One folder index serves both kinds (`pc_pmt_loader.cpp`). A texture's names are the ones Tweaks dumps:
`<index>_<XXH32 of the stock DDS file>_<stock width>x<stock height>.dds`, then the same without the index, first in
the bank's folder, then at the root, case-insensitive. The folder is the bank's file stem: `cs_CS_BEAC_pmt` for a
PMT bank (the 633558 table), `spr_etc_xst` / `spr_sprani_game_cvt_Exst` for a sprite bank (the 639CB8 XST table).
The index is Tweaks' CurrentTextureIdx: the bank's texture position for a PMT bank, the count of textures the XST
created before it for a sprite bank (entries without a texture do not count). Tweaks' FXT hash remaps apply.

A file is offered to the device only if the device's DDS reader takes it (DXT1/3/5 or 32-bit RGB, the request's cube
kind, every level inside the file); A8B8G8R8 files are converted to A8R8G8B8 first, as Tweaks' loader does. Anything
else keeps the stock texture and is counted.

## Scene extraction (October 16 private candidate)

`[Graphics] SceneTextureExtract = true` opts into saving original scene PMT DDS files at the next ordinary start.
It defaults to false, works without either replacement flag or a `load` folder, and is disabled in latched tests,
playback and recording. The existing validated `TextureBaseFolder` applies (default `textures`): output goes to
`<game folder>/<TextureBaseFolder>/dump/<PMT bank stem>/<index>_<XXH32>_<width>x<height>.dds`.
Names share the replacement lookup's XXH32 and FXT remaps. UI extraction is described below.

Extraction runs only after successful stock device creation. A successfully loaded replacement suppresses the dump;
a missing, invalid or refused replacement keeps the existing stock fallback and may dump that original file.
Supported inputs are validated DXT1, DXT3, DXT5 and 32-bit RGB DDS with A8R8G8B8-compatible color masks, up to 128 MiB.
Both 2D and complete six-face cubes are supported, with every stored mip level present. The entire original DDS is
copied byte-for-byte; no generated mip levels, GPU readback, capture or re-encoding are involved.

Each valid output name is attempted once per configured session. Exclusive file creation preserves existing files,
including when another process creates the destination first. Directory/write failures and unsupported files are
counted without changing texture creation or rendering; an incomplete newly created file is removed. Shutdown
prints written/existing/failed/unsupported counts. DDS assets are already ignored by the repository. No owner pack,
configuration or installation is changed by enabling this source option in a private candidate.

The existing `pc_pmt_loader` fixture contains synthetic checks for all eight format/cube combinations, byte-identical
stored files and source buffers, device-reader acceptance of stored mip levels, unchanged stock requests, off and
dedup behavior, existing files, replacement precedence/fallback, resource refusal, blocked output and invalid input.
ClangCL Release OutRunDesktop linked at `9df8513`. The first batch's overall failure was the new fixture cleanup
calling the three-argument function with two arguments; test-only correction `e965695` uses the existing wrapper.
The affected fixture target then built and `pc_pmt_loader` passed once (315 checks, 1/1, zero skipped); the desktop
was retained without rebuilding. Original hub evidence is in `artifacts/outrun-scene-extraction-20261016/build-01`
(including the failed fixture compile) and `build-02` (passing continuation, JUnit and original LastTest.log).
The desktop is 23,880,192 bytes, SHA-256 `018dd406a23013143c1d119816ef2550166c77517e872967903a0751c3f7e95e`.
Both batches released the exact lease with owned children quiescent and process environment restored; the passing
continuation released at `2026-10-17T00:35:05.3224112Z`. Shader tables were read from the verified original EXE into
memory only, and synthetic outputs used isolated TEMP. No game, provider or install ran. Ordinary-game extraction
and rendered behavior remain unobserved. Canonical native checkout `235bbb3` and owner assets remain untouched.

## UI extraction (October 16 source successor, unbuilt)

`[Graphics] UITextureExtract = true` independently enables original UI DDS output, default false and next ordinary
start only. It shares scene extraction's path validation, format/size/mip validation, XXH32/FXT naming, dedup set and
exclusive writer. Tests, playback and recording disable both flags before configuration. Output is under the same
`TextureBaseFolder/dump/<XST bank stem>/` folders. Native F6 Cameras Advanced offers **Extract scene textures** and
**Extract UI textures** through the existing one-key writer/failed-save rollback; both describe disk-write/loading
cost, preserved existing files and replacement suppression. No generated assets, encoder or GPU readback is added.

The source-backed consumers are:

- XST sprite uploads (`pc_sprite_bank_load_xst`): the original DDS pointer/size goes to the existing device, and only
  a successful stock handle triggers extraction. The index counts created textures, skipping empty XST entries,
  exactly as replacement lookup does. Successful replacement creation suppresses extraction; fallback keeps stock.
  Extraction leaves uploaded bytes, requests, logical stock sizes, draw geometry and UV behavior unchanged.
- The START preview builder (`build_retail_start_loading`): the original DDS spans remain available while the CPU
  pack is assembled. Only after successful complete pack assembly are its stock entries offered to the same writer;
  entries present in the successful HD map are omitted. This establishes CPU pack preparation, not later device
  creation, GPU upload or rendering. No pack version, dimensions, payloads, animation or cache semantics change.

UI DDS admitted by these 2D consumers is DXT1/3/5 or A8R8G8B8-compatible 32-bit RGB with all stored mip levels.
UI and scene statistics are separate; both are printed at shutdown. Replacement lookup stays disabled when only
extraction is enabled. Missing, blocked or pre-existing dump destinations cannot change stock creation/pack output.
The existing sprite and START fixtures add byte-preservation, empty-entry index, replacement/fallback, refused
resource, stored-mip, dedup/existing-file and blocked-output cases. Planned qualification is the affected desktop
build and these existing fixtures, plus the PMT fixture because the shared scene extractor is refactored here.
No build/test/runtime acceptance is claimed for this UI successor yet.

## Pad prompt folders (source candidate, unbuilt)

Tweaks reference: local dbce-mods-outrun-2006 commit
`51acc201d70ebb1ec1151f7fa2ebaa627be915df`, hooks_textures.cpp:623-669,
plugin.hpp:52-76 and input_manager.cpp:526-542,634-641. It has no INI key
for prompt family. UITextureReplacement remains the same off-by-default
boolean here; TextureBaseFolder and existing explicit TextureMatchAnyFolder
behavior are unchanged. No input setting or owner pack is written.

Only these **post-FXT-remap** UI hashes select a pad folder:
`455717B2`, `1F77CB88`, `FA7BBB13`, `39229D64`, `ACF61D7C`, `31C58963`.
The selected physical WGI pad supplies a presentation hint from the existing
cached identity() function, after the same choose_pad() isolation decision and
successful existing GetCurrentReading(). No second enumeration/reader is added.
An unknown identity, unsupported product, no pad or rejected selection supplies
PC. The shared DualSense-layout PadSample, wheel/shim actions, replay and
synthesized connected bits never determine this hint. Pad mapping and the input
filter remain byte-for-byte unchanged.

Conservative first-party identities are supported, not every SDL controller:
Microsoft045E products028E/02D1/02E0/02EA/02FD/0B12/0B13 -> Xbox;
Sony054C products0268/05C4/09CC/0BA0/0CE6/0DF2 -> PlayStation;
Nintendo057E products2006/2007/2008/2009 -> Switch. Exact products are checked;
an unknown product from the same manufacturer is still PC. These existing
family facts are in the local Tweaks dependency SDL preview-3.1.8
`22422f7748d5128135995ed34c8f8012861c7332`, controller_list.h:29,84-87,
153-154,171,309-321,551-561 (SHA256
`0D13188EF0B8C0911027419407DAC6F13F1FD4C5DA47023CF1D7AF54B314773C`).
No SDL dependency or controller database is copied into the decomp.

At an affected lookup, the first non-PC hint latches for that configured
session, as Tweaks does. Later disconnects/family changes keep that atlas family;
configure resets it. A preceding ordinary UI or scene texture does not latch.
The existing first frame services ReadIO453BB0 before display449050/present
(pc_common_control.cpp:5961-5978; first timing frame returns one tick at5914).
ReadIO calls GameHost input_sample -> diag.read_pad -> padUpdate -> read_pad,
so the physical hint exists before the lazy sprite-bank lookup. The setter is
before map_input/input_filter and any synthetic connected bit. open_frontend
binds owners/text/audio; it does not eagerly load the prompt sprite banks.
No new polling point, capture pipeline or hot texture switching is introduced.

For that family (Xbox, PlayStation or Switch; case-insensitive), try:

1. bank/family/index_hash_size.dds
2. bank/family/hash_size.dds
3. family/bank/index_hash_size.dds
4. family/bank/hash_size.dds
5. family/index_hash_size.dds
6. family/hash_size.dds
7. The existing four generic bank-index/bank-hash/root-index/root-hash names.

A missing selected-family folder falls back to the generic names, not another
family. If no file matches, original stock bytes and behavior remain. As in the
existing loader, a present but unusable first file fails to stock rather than
bypassing validation via another file. Existing explicit TextureMatchAnyFolder
still runs only after the named paths and retains its unambiguous/byte-identical
same-name policy over all indexed folders; it is a separate non-Tweaks opt-in,
not automatic pad-family inference. DDS conversion/validation, resource creation,
FXT remaps and logical stock texel/UV handling reuse the original path.

New cases are authored inside test_pc_sprite_replacement only: conservative
families/unknown products, six hashes, non-prompt/scene non-latch, first non-PC
latch/disconnect/configure reset, all ten path positions, missing/other-family
folders, invalid file, off/stock bytes, and pre-whitelist FXT remapping. Synthetic
DDS hash-tail inversion supplies exact prompt hashes without retail assets.
No test, compilation or runtime was executed for this source slice. Lola's
frozen c5d3a9f checkout, CMake/startup/Shift/highway work and active batch remain
unchanged. Woden's queued ordinary run takes priority over qualification here.

## Logical size (UI)

The XST image tables and the SPRANI frames address texels of the stock texture. A sprite texture created from a pack
keeps its stock size (`PcSpriteBank::stock_sizes`); the draws use it where the original code divides by the texture
size: footage UVs (42DDF0), the 42C0F0 quad's UVs, and the ID3DXSprite path (42A0A0), whose source rectangle is moved
onto the replacement with the scale divided by the extent ratio. The half-texel offsets still follow the texture
sampled, as in Tweaks. Stock textures take the original code unchanged.

## Open: the front-end bank stalls the first menu frame (design only)

Sprite banks load when a draw first needs them (`ensure_bank` in sprite_2d_runtime.cpp), on the main thread, the
whole bank at once. With an HD pack the front-end bank (0x44, `spr_sprani_sumo_fe_cvt_Exst`, 123 files, 1.08 GB in
the owner pack) costs, measured 2026-10-12:

| Part | Cost |
| --- | --- |
| Pack reads, A8B8G8R8 conversion, checks (offline, no device) | 8.6 s cold file cache, 1.0 s warm |
| The same plus device creation, in game (warm cache) | 1.6-1.9 s |

In game the title logo is lost behind it. Tweaks has the same stall (its UI replacement reads files inside the
game's texture creation), but the original game loads that bank during its own loading.

Options, cheapest first:

1. **Prefetch into the file cache.** At configure time a low-priority thread reads (and discards) the files of the
   front-end, font and HUD folders. Removes the cold-cache part (8.6 s -> about 1 s) without holding memory; the warm
   cost stays on the main thread. Pack-loader only.
2. **Stock first, HD in steps.** Load the bank with its stock textures (as with replacement off), then swap in
   replacements under a per-frame time budget, as the PMT loader already creates one texture per step. Safe between
   frames: queued 2D records live for one frame and reach textures through the bank (token -> handle) when queued.
   The replaced stock texture is released after the frame that last drew it. Visible as HD art arriving over about
   two seconds on the first menu. Sprite module plus a deferred release.
3. **Decode off the main thread.** A worker reads, converts and decodes (`pc_d3dx_texture_from_file`, `pc_dds_rgba`);
   the main thread only adds the finished object. Needs a device entry point for a decoded texture: the device is
   zero's (PcSoftD3D9Device / D3D11D3D9Device), so a joint change.

Recommendation: 1 now (small, no visible change), 2 if the warm stall still matters to the owner, 3 together with
the device memory work below.

## Coverage of a pack (scene)

`tools/texture_pack_coverage.cpp <game folder> <pack load folder> [any]` runs every PMT bank of the game through the
scene replacement path (a counting device, nothing created) and explains each texture it does not replace: no file,
a file only in another bank's folder, a file under another index prefix, or a file the device would refuse (cube
request with a 2D file, pixel format, shorter than its levels); then the pack folders and files nothing uses. With
`any` it runs with TextureMatchAnyFolder and counts what that option takes and what it leaves ambiguous. A file stem
named by several table entries (cs_LRBK_PALM_pmt) is counted per entry.

Against an owner Tweaks pack (2026-10-12): 401 banks with textures, 14,395 textures; the pack's 41 scene folders all
name a bank, no index mismatch; Tweaks' names replace 1,277; 16 cube requests have 2D files; 2,398 textures have
their file only in another bank's folder, mostly stage variants (`_R`, `_T`, and the plain cs_CS_PALM of the OutRun
mode, whose pack folder is cs_CS_PALM_T); TextureMatchAnyFolder takes 2,058 of them and leaves 300 ambiguous.

## Open: device memory

`PcSoftD3D9Device::create_texture_from_file` keeps the DDS data and a decoded RGBA8 copy of every level for the life
of the texture; `D3D11D3D9Device::texture` creates the GPU texture (always R8G8B8A8) from that copy on first draw.
For the 160 textures replaced on the menu-to-race path that is 3.0 GB of CPU memory (vs about 0.2 GB stock) besides
up to 1.65 GB of GPU textures; private memory went 0.83 -> 4.23 GB. The device owner's fix (zero, e167f58 on the
graphics-parity branch) drops the RGBA8 copy after the GPU upload and decodes again if a CPU reader needs it. Measured
with one executable on the same route (HD UI, race interval): private 4,250 MB with OR2_KEEP_TEXTURE_RGBA=1, 3,711 MB
without (544.6 MB released); the rest stays because textures decoded at bank load but never drawn are never uploaded.
Decoding at first upload instead (moving the cost to the first draw) or BC uploads are the further levers, the
device's to decide. Replacement stays opt-in.
