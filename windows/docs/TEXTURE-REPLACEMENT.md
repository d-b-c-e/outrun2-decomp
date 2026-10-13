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

## Open: device memory

`PcSoftD3D9Device::create_texture_from_file` keeps the DDS data and a decoded RGBA8 copy of every level for the life
of the texture; `D3D11D3D9Device::texture` creates the GPU texture (always R8G8B8A8) from that copy on first draw.
For the 160 textures replaced on the menu-to-race path that is 3.0 GB of CPU memory (vs about 0.2 GB stock) besides
up to 1.65 GB of GPU textures; private memory went 0.83 -> 4.23 GB. The device owner's preferred fix (zero): drop
the RGBA8 copy after the GPU upload and decode again if a CPU reader needs it; uploading DXT as BC formats is a larger,
separately reviewed change. Replacement stays opt-in until then.
