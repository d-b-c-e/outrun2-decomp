// Synthetic screen bytes/services only; no retail data, renderer or device.
#include "platform/level_select.hpp"
#include "platform/pc_screen.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>
using namespace outrun::platform;
using outrun::driving::Bytes;
namespace {
unsigned checks{};
void check(bool ok,const char* what){++checks;if(!ok){std::fprintf(stderr,"level select FAIL: %s\n",what);std::exit(1);}}
struct Screen {
    std::vector<std::uint8_t> bytes=std::vector<std::uint8_t>(0x130e5u,0x5a);
    std::uint32_t action{12u},held{},mask{};
    std::array<bool,2> table{{true,true}};
    unsigned commits{},table_reads{};
    std::int32_t committed{};
    bool secondary{},move_in_action{};
    std::string order;
    Bytes object(){return {bytes.data(),bytes.size()};}
    Screen(){object().put32(0x34u,0u);}
    LevelSelectServices services(){return {
        this,
        [](void* p){auto& s=*static_cast<Screen*>(p);s.order+='A';
            if(s.move_in_action)s.object().put32(0x34u,40u);return s.action;},
        [](void* p,std::uint32_t mask){auto& s=*static_cast<Screen*>(p);s.order+='S';s.mask=mask;return s.held&mask;},
        [](void* p,unsigned table,unsigned index){auto& s=*static_cast<Screen*>(p);s.order+=table?'1':'0';
            check(table<2u&&index<20u,"bounded classification table access");++s.table_reads;return s.table.at(table);},
        [](void* p,std::int32_t course,bool secondary){auto& s=*static_cast<Screen*>(p);s.order+='C';
            ++s.commits;s.committed=course;s.secondary=secondary;}};}
    std::uint32_t tick(bool on=true){return level_select_update_4e7800(object(),on,services());}
};
void mapping(){
    // Authored row -> course mapping independently enumerated from the PC's
    // two columns and six extra courses, rather than copying its arithmetic.
    constexpr std::int32_t expected[66]{
        0,30,1,31,2,32,3,33,4,34,5,35,6,36,7,37,8,38,9,39,
        10,40,11,41,12,42,13,43,14,44,15,45,16,46,17,47,18,48,19,49,
        20,50,21,51,22,52,23,53,24,54,25,55,26,56,27,57,28,58,29,59,
        60,61,62,63,64,65};
    for(unsigned row=0;row<66u;++row){
        Screen s;s.object().put32(0x34u,row);s.action=0u;
        check(s.tick()==1u&&s.commits==1u&&s.committed==expected[row],"all 66 authored rows commit the matching course");
        check(s.object().u32(4u)==28u&&!s.secondary&&s.object().u8(0x130e4u)==0u,"confirm advances to car menu without an alternate");
        s=Screen{};s.object().put32(0x34u,row);s.held=0x100u;s.action=0u;
        const bool alternate=expected[row]>=10&&expected[row]<=29;
        check(s.tick()==1u&&s.secondary==alternate&&bool(s.object().u8(0x130e4u))==alternate,"LS alternate allowed only for the classification range");
        check(s.table_reads==(alternate?2u:0u)&&s.mask==0x110u,"no out-of-range table lookup, local alias mask");
    }
}
void actions(){
    for(const auto on:{false,true}){
        Screen s;s.object().put32(0x34u,20u);s.held=0x100u;
        check(s.tick(on)==0u&&s.object().u8(0x130e4u)==unsigned(on),"off keeps native X-only mask; on also accepts LS");
        s.held=0x10u;check(s.tick(on)==0u&&s.object().u8(0x130e4u)==1u,"native X remains accepted");
        s.held=0u;check(s.tick(on)==0u&&s.object().u8(0x130e4u)==0u,"release clears alternate immediately");
    }
    Screen s;s.object().put32(0x34u,20u);s.held=0x100u;s.action=0u;
    check(s.tick()==1u&&s.order=="AS01C"&&s.secondary,"native callback and table/commit order");
    s=Screen{};s.object().put32(0x34u,20u);s.held=0x100u;s.action=0u;s.table[0]=false;
    check(s.tick()==1u&&!s.secondary&&s.object().u8(0x130e4u)==0u,"first table refuses alternate");
    s=Screen{};s.object().put32(0x34u,20u);s.held=0x100u;s.action=0u;s.table[1]=false;
    check(s.tick()==1u&&!s.secondary&&s.object().u8(0x130e4u)==1u,"second table gates committed secondary, not highlight");
    for(const auto action:{1u,3u,5u,12u,~0u}){
        s=Screen{};s.object().put32(0x34u,21u);s.action=action;
        const auto expected=action==1u?2u:0u;
        check(s.tick()==expected&&s.commits==0u&&s.object().u32(4u)==0x5a5a5a5au&&s.object().u32(0x34u)==21u,"back/idle/other actions never commit or change row");
    }
    struct Move {unsigned row,action,next;};
    for(const auto m:{Move{0,2,65},Move{65,4,0},Move{21,2,20},Move{21,4,22}}){
        s=Screen{};s.object().put32(0x34u,m.row);s.action=m.action;
        check(s.tick()==0u&&s.object().u32(0x34u)==m.next&&s.commits==0u,"native previous/next and wrap");
    }
    s=Screen{};s.move_in_action=true;s.action=2u;
    check(s.tick()==0u&&s.object().u32(0x34u)==39u,"navigation reloads row after inherited action");
    s=Screen{};s.move_in_action=true;s.action=0u;
    check(s.tick()==1u&&s.committed==0,"confirmation uses course captured before inherited action");
    for(const auto row:{std::numeric_limits<std::int32_t>::min(),-1,std::numeric_limits<std::int32_t>::max()}){
        s=Screen{};s.object().puti(0x34u,row);s.held=0x100u;
        check(s.tick()==0u&&s.table_reads==0u&&s.commits==0u,"invalid signed rows cannot index a classification table");
    }
    s=Screen{};const auto before=s.bytes;bool refused=false;
    try{(void)level_select_update_4e7800(Bytes(s.bytes.data(),0x130e4u),true,s.services());}
    catch(const std::out_of_range&){refused=true;}
    check(refused&&s.order.empty()&&s.bytes==before,"short object refuses before callbacks or mutation");
    auto incomplete=s.services();incomplete.commit=nullptr;refused=false;
    try{(void)level_select_update_4e7800(s.object(),true,incomplete);}
    catch(const std::invalid_argument&){refused=true;}
    check(refused&&s.order.empty()&&s.bytes==before,"incomplete services refuse before callbacks or mutation");
}
}
int main(){
    check(!g_pc_enable_level_select,"setting defaults off");
    check(level_select_initial_key(false)==0u&&level_select_initial_key(true)==27u,"exact first-menu key; no history rewrite");
    mapping();actions();
    std::printf("Level select: %u checks PASS (synthetic screen/services only)\n",checks);return 0;
}
