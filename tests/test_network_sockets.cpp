#include "platform/pc_network_sockets.hpp"
#include "platform/race_translated.hpp"
#include <cstdio>
#include <cstring>
#include <stdexcept>
using namespace outrun::platform;
namespace outrun::platform {extern const TranslatedFunction network_functions[];}
namespace {
unsigned checks{};
void req(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
struct Fixture {
    std::vector<int> closed;std::vector<std::string> names;
    std::uint32_t error{},send_address{},sent_size{},receive_address{0x026433c6u};
    std::uint16_t opened_port{},send_port{},receive_port{0xefa1u};
    bool broadcast{};int received{3},sent{3},opened{41},opens{};
    PcRaceMemory memory;std::array<std::uint8_t,4096> data{};std::array<std::uint32_t,20> iat{};
    PcNetworkSockets bridge;NativeNetworkStats stats;std::uint32_t pop{};
    static constexpr std::uint32_t base=0x20000000u,thunk=0x0fb10000u;
    explicit Fixture(bool exact_error=true){memory.map(base,data.data(),data.size());bridge.map_memory(memory);
        for(unsigned i=0;i<iat.size();++i)iat[i]=thunk+i*4u;
        memory.map(0x5961f8u,reinterpret_cast<std::uint8_t*>(iat.data()),sizeof iat);
        PcNetworkPlatform p;
        p.open_udp=[this](std::uint16_t port,bool b){++opens;opened_port=port;broadcast=b;return opened;};
        p.close=[this](int fd){closed.push_back(fd);};
        p.send_to=[this](int fd,const auto* bytes,auto size,auto address,auto port){req(fd==41&&bytes[0]==0x71,"fake send native token and guest bytes");sent_size=size;send_address=address;send_port=port;return sent;};
        p.receive_from=[this](int fd,auto* bytes,auto size,auto& address,auto& port){req(fd==41&&size==8,"fake receive buffer and token");bytes[0]=0x52;address=receive_address;port=receive_port;return received;};
        p.resolve=[this](const std::string& host){names.push_back(host);return error?std::vector<std::uint32_t>{}:std::vector<std::uint32_t>{0x090200c0u,0x026433c6u,0x090200c0u};};
        p.interfaces=[](){return std::vector<PcNetworkInterface>{{0x090200c0u,0xff0200c0u,0x00ffffffu}};};
        if(exact_error)p.last_error=[this]{return error;};bridge.set_platform(std::move(p));
    }
    void text(std::uint32_t at,const std::string& s){std::memcpy(memory.at(at,s.size()+1u,true),s.c_str(),s.size()+1u);}
    std::uint32_t call(unsigned import,std::initializer_list<std::uint32_t> args={}){
        const std::vector<std::uint32_t> values(args);return bridge.call(memory,import,[&](unsigned n){return n<values.size()?values[n]:0u;},pop,stats);
    }
    std::uint32_t original_resolve(const std::string& host,unsigned count){
        text(base,host);TranslatedModule module{network_functions,nullptr,{},nullptr,nullptr,nullptr,nullptr};
        module.special=[&](or2x86::Cpu& cpu,std::uint32_t target){
            if(target<thunk||target>=thunk+80u)return false;
            std::uint32_t popped=0;const auto value=bridge.call(memory,(target-thunk)/4u,[&](unsigned n){return translated_arg(cpu,n);},popped,stats);
            translated_return(cpu,value,popped);return true;
        };
        const PcRaceService service=[&](const PcRaceCall& c)->std::uint32_t{
            if(c.pc==0x58249bu)return bridge.name_isalpha(c.args[0])?1u:0u;
            throw std::runtime_error("unexpected original resolver external");
        };
        return translated_call(memory,service,module,0x5349b0u,0,{base,base+0x200u,count});
    }
};
}
int main(){try{
    for(const auto& host:{"0.0.0.0","255.255.255.255","192.0.2.9","001.002.003.004"}){std::uint32_t ip=0;req(network_ipv4(host,ip),"full dotted IPv4 accepted");}
    for(const auto& host:{"","1","1.2.3","1.2.3.4.5","256.0.0.1","1..2.3","-1.2.3.4","1.2.3.4:80","1.2.3.4 ","1234.2.3.4"}){std::uint32_t ip=0;req(!network_ipv4(host,ip),"malformed IPv4 rejected");}
    for(const auto& host:{"clarissa.example","2server.example","localhost","a-b.test","192.0.2.9"})req(network_host_valid(host),"DNS labels and numeric host accepted");
    for(const auto& host:{"http://a.test","a.test:80","a/b","user@a.test","a..test","-a.test","a-.test","a.test.","256.1.2.3","a test"})req(!network_host_valid(host),"invalid endpoint refused");
    req(!network_host_valid(std::string(64,'a')+".test")&&!network_host_valid(std::string(254,'a')),"host/label bounds");
    Fixture f;const auto b=Fixture::base;
    req(f.opens==0&&f.names.empty(),"bridge construction performs no transport operations");
    f.text(b,"2server.example");const auto h=f.call(GetHostByName,{b});
    req(h&&f.pop==4&&f.memory.u16(h+8)==2&&f.memory.u16(h+10)==4,"32-bit hostent family, length and stdcall pop");
    const auto list=f.memory.u32(h+12);req(f.memory.u32(f.memory.u32(list))==0x090200c0u&&f.memory.u32(f.memory.u32(list+4))==0x026433c6u&&!f.memory.u32(list+8),"guest address list, deduplication and terminator");
    req(f.original_resolve("original.example",8)==2&&f.memory.u32(b+0x200)==0x090200c0u,"actual translated5349B0 consumes guest hostent list");
    const auto queries=f.names.size();req(f.original_resolve("198.51.100.2",8)==1&&f.memory.u32(b+0x200)==0x026433c6u&&f.names.size()==queries,"stock numeric resolver avoids DNS");
    req(f.bridge.set_server_override("2server.example"),"opt-in override accepted");
    req(f.original_resolve("198.51.100.2",1)==1&&f.names.back()=="2server.example"&&f.memory.u32(b+0x200)==0x090200c0u,"actual translated numeric caller reaches digit-leading DNS override");
    req(f.bridge.resolve_server(f.memory,f.bridge.server_override(),b+0x200,2)==2,"native original-boundary consumer shares resolver");
    req(f.bridge.resolve_server(f.memory,f.bridge.server_override(),b+0x200,0)==0,"zero output capacity makes no write");
    req(!f.bridge.set_server_override("http://bad")&&f.bridge.server_override().empty(),"bad override clears prior route");
    req(f.bridge.set_server_override("203.0.113.8"),"numeric override accepted");const auto before=f.names.size();
    req(f.original_resolve("original.example",4)==1&&f.names.size()==before&&f.memory.u32(b+0x200)==0x087100cbu,"numeric override no DNS and network byte order");
    f.bridge.set_server_override("");f.error=11001;f.text(b,"missing.example");req(!f.call(GetHostByName,{b})&&f.call(WsaGetLastError)==11001,"DNS failure and exact error");f.error=0;
    f.text(b,"999.1.2.3");const auto count=f.names.size();req(f.call(InetAddr,{b})==0xffffffffu&&f.names.size()==count,"invalid numeric address never goes to DNS");
    const auto sock=f.call(Socket,{2,2,17});req(sock==1&&f.pop==12&&f.opens==0,"lazy UDP guest socket token");
    f.memory.put32(b+0x100,1);req(!f.call(SetSockOpt,{sock,0xffff,0x20,b+0x100,4}),"broadcast option stored");
    f.memory.put16(b+0x120,2);f.memory.put16(b+0x122,0xefa1u);req(!f.call(Bind,{sock,b+0x120,16})&&f.opened_port==41455&&f.broadcast,"bound native port uses host order");
    f.memory.put32(b+0x124,0x090200c0u);f.memory.put8(b+0x140,0x71);f.memory.put32(b+0x180,16);
    req(f.call(SendTo,{sock,b+0x140,3,0,b+0x120,16})==3&&f.send_address==0x090200c0u&&f.send_port==0xefa1u&&f.sent_size==3&&f.stats.sent==1,"datagram address/port stay network order");
    req(f.call(RecvFrom,{sock,b+0x150,8,0,b+0x160,b+0x180})==3&&f.memory.u8(b+0x150)==0x52&&f.memory.u32(b+0x164)==f.receive_address&&f.memory.u16(b+0x162)==f.receive_port&&f.memory.u32(b+0x180)==16,"receive guest sockaddr and bytes");
    f.received=0;f.error=10035;req(f.call(RecvFrom,{sock,b+0x150,8,0,0,0})==0xffffffffu&&f.call(WsaGetLastError)==10035,"nonblocking no-data mapping");
    f.error=0;req(f.call(RecvFrom,{sock,b+0x150,8,0,0,0})==0,"valid zero-byte datagram differs from no data");
    f.received=-1;f.error=10040;req(f.call(RecvFrom,{sock,b+0x150,8,0,0,0})==0xffffffffu&&f.call(WsaGetLastError)==10040,"receive preserves exact supplier error");
    f.sent=-1;f.error=10065;req(f.call(SendTo,{sock,b+0x140,3,0,b+0x120,16})==0xffffffffu&&f.call(WsaGetLastError)==10065,"send preserves exact supplier error");
    req(!f.call(WsaIoctl,{sock,0x4004747fu,0,0,b+0x300,0x4c,b+0x400,0,0})&&f.memory.u32(b+0x400)==0x4c&&f.memory.u32(b+0x308)==0x090200c0u,"interface guest layout");
    req(!f.call(CloseSocket,{sock})&&f.closed==std::vector<int>{41},"explicit close closes actual supplier token once");
    const auto second=f.call(Socket,{2,2,17});f.error=10048;f.opened=-1;req(f.call(Bind,{second,b+0x120,16})==0xffffffffu&&f.call(WsaGetLastError)==10048,"bind refusal preserves exact error");
    f.opened=41;f.error=0;req(!f.call(Bind,{second,b+0x120,16}),"failed lazy open can be retried by game");
    f.bridge.set_platform({});req(f.closed==std::vector<int>({41,41}),"platform replacement closes outstanding owned token");
    bool unsupported=false;try{f.call(Connect,{1,b,16});}catch(const std::runtime_error&){unsupported=true;}req(unsupported,"connected transport remains an explicit unimplemented boundary");
    {Fixture legacy(false);const auto socket=legacy.call(Socket,{2,2,17});legacy.received=0;
        req(legacy.call(RecvFrom,{socket,b+0x150,8,0,0,0})==0xffffffffu&&legacy.call(WsaGetLastError)==10035,"legacy supplier zero remains no pending data");}
    std::printf("network_sockets: %u checks; actual translated resolver plus fake DNS/UDP only, no Windows supplier or sockets\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL: %s\n",e.what());return 1;}}
