#pragma once
// PC PMT device-resource loader: 42E490 / 42E4F0 (objects: vertex and index
// buffers, material shaders) and 42E850 / 42E8C0 (textures). The PC runs the
// step functions once per frame until they return 1; the native port keeps the
// same steps. System-section pointer fields stay offsets natively (the PC
// relocation of object fields +04..+38 adds the system base); every device
// object pointer the PC stores is a PcD3D9Device handle here.
#include "platform/pc_vertex_shader_setup.hpp"
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
// creation fails, leaves the stock texture in place (counted as failed).
void pmt_texture_replacement_configure(bool enabled,const std::string& load_folder);
struct PmtTextureReplacementStats { std::uint32_t files{},looked{},replaced{},failed{}; };
const PmtTextureReplacementStats& pmt_texture_replacement_stats();
// Per bank (by pack name) since configure: its lookups, and whether the pack has a folder for it, so a log can say
// which banks a race used that the pack does not cover (a stage variant such as cs_CS_BEAC_T_pmt, for example).
struct PmtTextureReplacementBank { std::uint32_t looked{},replaced{},failed{};bool folder{}; };
const std::map<std::string,PmtTextureReplacementBank>& pmt_texture_replacement_banks();
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
