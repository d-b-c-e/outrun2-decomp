#pragma once
// [Graphics] DrawDistanceIncrease / DrawDistanceBehind (the OutRun2006Tweaks keys; 0 = the game's own, the default).
// The stage display 44DE10 draws each stage object with the culling-node list of the car's track section (one section
// per metre or so of track, in cs_CS_<stage>_bin). With these set, it draws the union of the lists of the sections from
// `behind` before to `increase` after the car's section, each node once, less the per-stage exclusions of a Tweaks
// lods.ini (LOD models and stage geometry that look wrong from afar). This is Tweaks' hooks_drawdistance.cpp, its patch
// at 44DF6D, transliterated: the visit order, the deduplication, the offset-0 exemption from exclusions, the section
// bounds and the Grand Canyon start limit are its own. More nodes need room in the render queues
// (render_queues_extended, Tweaks' DrawBufferExtension).
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>
namespace outrun::platform {
struct DrawDistanceSettings {
    int increase{},behind{};
    std::map<std::pair<int,int>,std::bitset<16384>> exclusions;   // (stage, object) -> excluded node ids
    bool active()const{return increase>0||behind>0;}
    const std::bitset<16384>* excluded(int stage,int object)const{
        const auto it=exclusions.find({stage,object});return it==exclusions.end()?nullptr:&it->second;}
};
DrawDistanceSettings& draw_distance();
// A Tweaks lods.ini: "[Stage N]" sections (N from the name's first digits, 0..127) with "object = node, node, ..."
// keys (decimal or 0x hex; '#' and ';' start comments; SkipQuickSort is read by Tweaks only for a disabled hack and
// ignored here). Entries outside Tweaks' bounds (object 0..255, node 0..16383) are skipped. Returns the nodes read.
std::size_t draw_distance_read_exclusions(const std::string& text,DrawDistanceSettings&);
// Tweaks' Grand Canyon override: stage 20 before section 30 (the car in the branch) keeps at most 80 sections ahead,
// so far parts of the canyon do not hang in the sky.
int draw_distance_ahead(const DrawDistanceSettings&,int stage,int section);

// One object's merged list (0xFFFF-terminated) from the section lists. read(s, k) is node k of section s's list,
// 0xFFFF at its end. Offsets run from -behind to `ahead`; offset 0 is always visited, the others only for sections in
// 0 .. max_sections-2. A node is taken the first time it is seen; an excluded node is skipped (but still counts as
// seen) unless it is seen at offset 0 with increase > 0. Returns false, leaving the game's list in use, when a node
// id is 4096 or more (Tweaks' seen-table size).
template<class Read>
bool draw_distance_merge(int section,int max_sections,int ahead,int behind,int increase,const std::bitset<16384>* excluded,
    Read read,std::vector<std::uint16_t>& out){
    std::bitset<4096> seen;out.clear();
    for(int offset=-behind;offset<ahead+1;++offset){
        if(offset!=0){
            if(section+offset<0)continue;
            if(section+offset>=max_sections-1)break;
        }
        for(std::size_t k=0;;++k){
            const std::uint16_t id=read(section+offset,k);
            if(id==0xffffu)break;
            if(id>=4096u){out.clear();return false;}
            if(seen[id])continue;
            seen[id]=true;
            if((offset==0&&increase>0)||!excluded||!(*excluded)[id])out.push_back(id);
        }
    }
    out.push_back(0xffffu);
    return true;
}
}
