// DrawDistanceIncrease / DrawDistanceBehind: the merged node list of one stage object and the lods.ini exclusions.
#include "platform/race_draw_distance.hpp"
#include <cstdio>
#include <cstdlib>
#include <vector>
using namespace outrun::platform;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"draw distance line %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
namespace {
using Lists=std::vector<std::vector<std::uint16_t>>;
std::vector<std::uint16_t> merge(const Lists& sections,int at,int ahead,int behind,const std::bitset<16384>* excluded=nullptr,bool* ok=nullptr){
    std::vector<std::uint16_t> out;
    const bool done=draw_distance_merge(at,int(sections.size()),ahead,behind,ahead,excluded,
        [&](int s,std::size_t k)->std::uint16_t{const auto& l=sections.at(std::size_t(s));return k<l.size()?l[k]:0xffffu;},out);
    if(ok)*ok=done;
    return out;
}
using V=std::vector<std::uint16_t>;
}
int main(){
    const Lists sections{{1,2},{2,3},{3,4},{4,5},{5,6},{6,7}};   // section s lists nodes s+1, s+2
    CHECK(merge(sections,2,0,0)==(V{3,4,0xffff}));                 // off: the car's section alone
    CHECK(merge(sections,2,2,0)==(V{3,4,5,6,0xffff}));             // +2: sections 2..4 (4 = max-2 is the last)
    CHECK(merge(sections,2,9,0)==(V{3,4,5,6,0xffff}));             // stops before the stage's last section
    CHECK(merge(sections,2,0,1)==(V{2,3,4,0xffff}));               // 1 behind: section 1 first, then 2
    CHECK(merge(sections,0,0,3)==(V{1,2,0xffff}));                 // behind the start: nothing below section 0
    CHECK(merge(sections,5,3,0)==(V{6,7,0xffff}));                 // the last section itself is drawn (offset 0)
    // Exclusions: skipped ahead/behind, but the car's own section (offset 0) keeps them while increase > 0; an
    // excluded node first seen behind is marked seen there and so stays out (Tweaks' order).
    std::bitset<16384> ex;ex.set(4);ex.set(5);
    CHECK(merge(sections,2,2,0,&ex)==(V{3,4,6,0xffff}));           // 4 at offset 0 stays, 5 (offset 1) goes
    CHECK(merge(sections,3,0,1,&ex)==(V{3,0xffff}));               // increase 0: 4 seen behind (excluded), 5 too
    // A node id past Tweaks' seen table: the game's list is used instead.
    bool ok=true;CHECK(merge(Lists{{1},{4096},{2}},0,2,0,nullptr,&ok).empty()&&!ok);
    // The lods.ini parser.
    DrawDistanceSettings s;
    const auto n=draw_distance_read_exclusions(
        "# comment\n[Stage 0]\n0x1 = 0xD\n\n[Stage 3]\n0x1 = 0x1, 0xD, 0x22 ; trailing\nSkipQuickSort = true\n"
        "0x6=0x1E\n[Stage 200]\n0x1 = 5\n[Stage 4]\n300 = 1\n0x2 = bad, 7, 16384\n",s);
    CHECK(n==6);   // 1 + 3 + 1 (stages 0 and 3) + 1 (stage 4: object 300, "bad" and 16384 skipped)
    CHECK(s.excluded(0,1)&&s.excluded(0,1)->test(0xd)&&s.excluded(0,1)->count()==1);
    CHECK(s.excluded(3,1)->test(0x1)&&s.excluded(3,1)->test(0xd)&&s.excluded(3,1)->test(0x22)&&s.excluded(3,6)->test(0x1e));
    CHECK(!s.excluded(4,300)&&s.excluded(4,2)->count()==1&&s.excluded(4,2)->test(7)&&!s.excluded(0,2));
    // The Grand Canyon start limit.
    s.increase=200;
    CHECK(draw_distance_ahead(s,20,10)==80&&draw_distance_ahead(s,20,30)==200&&draw_distance_ahead(s,0,10)==200);
    std::printf("draw distance: %u checks\n",checks);
    return 0;
}
