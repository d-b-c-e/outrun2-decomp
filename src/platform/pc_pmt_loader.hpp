#pragma once
// PC PMT device-resource loader: 42E490 / 42E4F0 (objects: vertex and index
// buffers, material shaders) and 42E850 / 42E8C0 (textures). The PC runs the
// step functions once per frame until they return 1; the native port keeps the
// same steps. System-section pointer fields stay offsets natively (the PC
// relocation of object fields +04..+38 adds the system base); every device
// object pointer the PC stores is a PcD3D9Device handle here.
#include "platform/pc_vertex_shader_setup.hpp"
#include "platform/ui_prompt_family.hpp"
#include <map>
#include <string>
#include <vector>
namespace outrun::platform {
struct PcPmtResources {
    std::vector<std::uint8_t> system;   // PMT system section (mutable)
    const std::uint8_t* video{};         // PMT video section (buffer/texture sources)
    std::size_t video_size{};
    // Bank fields (PC 7C2800 + id*0x48) as system offsets.
    std::uint32_t object_count_0c{};    // [system+4]
    std::uint32_t object_table_20{};    // system+0x14
    std::uint32_t texture_table_24{};   // header of the texture handle table
    std::uint32_t texture_records_04{}; // 0x14-byte texture records
    std::string pack_name;               // the bank's file stem (cs_CS_BEAC_pmt): its texture replacement folder
    driving::Bytes view(){return driving::Bytes(system.data(),system.size());}
};
// Scene texture replacement in the OutRun2006Tweaks pack layout (SceneTextureReplacement): a bank texture whose
// replacement exists under the load folder is created from that DDS instead. Names as Tweaks writes them,
// "{XXH32 of the original DDS file, upper-case hex}_{width}x{height}.dds", tried as <pack>/<index>_<name>, <pack>/<name>,
// <index>_<name>, <name> (index = the texture's position in its bank). The folder is indexed once at configure time.
// Off until configured with enabled = true. A replacement is created with the game's own request (levels, filters),
// only the file differs, as Tweaks does. A file that is unreadable, too large or not a DDS the device reads, or whose
// creation fails, leaves the stock texture in place (counted as failed). A8B8G8R8 files are read as A8R8G8B8 (R and B
// swapped), as Tweaks' loader converts them. Tweaks' FXT hash remaps apply. The one-argument form configures the
// scene textures only.
void pmt_texture_replacement_configure(bool enabled,const std::string& load_folder);
// Scene (SceneTextureReplacement) and UI (UITextureReplacement) replacement from one indexed load folder.
// any_folder (TextureMatchAnyFolder, not a Tweaks key): when a texture has no file under Tweaks' four names, a file of
// the same name (XXH32 and size of the stock DDS, by the pack's naming; any index prefix) elsewhere in the pack is
// taken if it is the only one, or if all such files are byte-identical; several different files are ambiguous and keep
// the stock texture. Lets a stage variant (cs_CS_PALM_pmt) use files made for another (cs_CS_PALM_T_pmt). The name
// index is built at configure time, and the file chosen for a name (its path, not its bytes) is cached for the session:
// a lookup cache, with no hot reload and no guarantee if pack files change while the game runs.
// SceneTextureExtract: original validated DDS bytes under the sibling dump folder,
// only after successful stock creation; replacements and existing dumps are kept.
// Extraction is independently default-off; configure resets its per-session attempts/statistics.
void texture_replacement_configure(bool scene,bool ui,const std::string& load_folder,bool any_folder=false,bool scene_extract=false);
struct PmtTextureExtractionStats { std::uint32_t written{},existing{},failed{},unsupported{};std::string error; };
const PmtTextureExtractionStats& pmt_texture_extraction_stats();
// other: replaced from another bank's folder (part of replaced); ambiguous: same-name files that differ (kept stock).
struct PmtTextureReplacementStats { std::uint32_t files{},looked{},replaced{},failed{},other{},ambiguous{}; };
const PmtTextureReplacementStats& pmt_texture_replacement_stats();
// Per bank (by pack name) since configure: its lookups, and whether the pack has a folder for it, so a log can say
// which banks a race used that the pack does not cover (a stage variant such as cs_CS_BEAC_T_pmt, for example).
// `seconds`: wall time in pack lookups, reads and replacement creation (load cost; the stock creations not included).
struct PmtTextureReplacementBank { std::uint32_t looked{},replaced{},failed{},other{},ambiguous{};bool folder{};double seconds{}; };
const std::map<std::string,PmtTextureReplacementBank>& pmt_texture_replacement_banks();
// UI texture replacement: a sprite bank (XST) texture from the same pack, the folder being the XST file stem
// (spr_etc_xst, spr_sprani_game_cvt_Exst) and `index` Tweaks' count of the textures the bank created before it.
// Creates the stock texture when UI replacement is off, the pack has no usable file or the device refuses it;
// `replaced` is true only for a created replacement. Counted per bank as the scene textures are.
bool ui_texture_replacement_enabled();
std::uint32_t ui_texture_replacement_create(PcD3D9Device&,const std::string& pack,std::uint32_t index,const PcTextureFileRequest&,bool& replaced);
// The same replacement for a texture drawn outside the PC device (the START loading picture of the preview presenter):
// the same lookup, names, read and checks for the stock DDS file `stock`, then the file's top level decoded to RGBA8
// (R,G,B,A) by the device reader (pc_d3dx_texture_from_file, one level). Counted per bank as the sprite banks are.
// False, with `rgba` empty, when UI replacement is off, the pack has no usable file, it does not decode, or its
// RGBA8 size exceeds `max_bytes` (the caller keeps the stock texture).
bool ui_texture_replacement_rgba(const std::string& pack,std::uint32_t index,const std::uint8_t* stock,std::size_t size,
    std::size_t max_bytes,std::uint32_t& width,std::uint32_t& height,std::vector<std::uint8_t>& rgba);
const PmtTextureReplacementStats& ui_texture_replacement_stats();
const std::map<std::string,PmtTextureReplacementBank>& ui_texture_replacement_banks();
std::uint32_t pmt_xxh32(const std::uint8_t* data,std::size_t size,std::uint32_t seed=0);
struct PcPmtLoadCursor {
    std::uint32_t object_95b230{},offset_95b234{},texture_95b22c{},texture_record_957be0{};
};
void pmt_objects_begin_42e490(PcPmtResources&,PcPmtLoadCursor&);
// One object per call; true when every object is done.
bool pmt_objects_step_42e4f0(PcPmtResources&,PcPmtLoadCursor&,PcD3D9Device&,PcShaderCache&,const PcShaderGlobals&);
void pmt_textures_begin_42e850(PcPmtResources&,PcPmtLoadCursor&);
// One texture per call; true when every texture is done.
bool pmt_textures_step_42e8c0(PcPmtResources&,PcPmtLoadCursor&,PcD3D9Device&);
// PC 4103F0 on a loaded bank: SetVertexShader(NULL), then the shader record
// of every material of object `index` for `kind` (4104D0). 0 when the
// index is not below the object count.
// PC 4066D0 / 406730 bodies on a loaded bank (0 when index >= count).
std::uint32_t pmt_object_group_type_4066d0(PcPmtResources&,std::uint32_t index,std::uint32_t type);
std::uint32_t pmt_object_mesh_flags_406730(PcPmtResources&,std::uint32_t index,std::uint32_t and_mask,std::uint32_t or_mask);
std::uint32_t pmt_object_shaders_4103f0(PcPmtResources&,std::uint32_t index,std::uint32_t kind,PcD3D9Device&,PcShaderCache&,const PcShaderGlobals&);
}
