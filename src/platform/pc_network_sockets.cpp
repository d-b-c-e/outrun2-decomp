#include "pc_network_sockets.hpp"
#include "race_translated.hpp"
#include "translated_crt.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <utility>
namespace outrun::platform {
namespace {
constexpr std::uint32_t WsaEWouldBlock=10035u;
std::uint16_t swap16(std::uint32_t v){return std::uint16_t(((v&0xffu)<<8)|((v>>8)&0xffu));}
}
bool network_ipv4(const std::string& host,std::uint32_t& address){
    std::uint32_t result=0;std::size_t pos=0;
    for(unsigned part=0;part<4;++part){
        unsigned value=0,digits=0;
        while(pos<host.size()&&host[pos]>='0'&&host[pos]<='9'){
            value=value*10u+unsigned(host[pos++]-'0');if(++digits>3u||value>255u)return false;
        }
        if(!digits)return false;result|=std::uint32_t(value)<<(part*8u);
        if(part<3u){if(pos==host.size()||host[pos++]!='.')return false;}
    }
    if(pos!=host.size())return false;address=result;return true;
}
bool network_host_valid(const std::string& host){
    if(host.empty()||host.size()>253u)return false;
    std::uint32_t address=0;if(network_ipv4(host,address))return true;
    bool alpha=false;std::size_t label=0;char previous=0;
    for(char c:host){
        const bool letter=(c>='a'&&c<='z')||(c>='A'&&c<='Z');alpha|=letter;
        if(c=='.'){if(!label||previous=='-')return false;label=0;}
        else{
            if(!letter&&(c<'0'||c>'9')&&c!='-')return false;
            if((!label&&c=='-')||++label>63u)return false;
        }
        previous=c;
    }
    return alpha&&label&&previous!='-'; // invalid dotted numeric input never falls through to DNS
}
std::vector<std::uint32_t> network_resolve(const PcNetworkPlatform& p,const std::string& host){
    std::uint32_t address=0;if(network_ipv4(host,address))return {address};
    if(!network_host_valid(host)||!p.resolve)return {};
    const auto found=p.resolve(host);std::vector<std::uint32_t> unique;
    for(auto ip:found){if(std::find(unique.begin(),unique.end(),ip)==unique.end())unique.push_back(ip);if(unique.size()==16u)break;}
    return unique;
}
void PcNetworkSockets::map_memory(PcRaceMemory& m){m.map(0x0ef90000u,ntoa.data(),ntoa.size());m.map(0x0ef90100u,dns.data(),dns.size());}
void PcNetworkSockets::close_all() noexcept{
    for(auto& s:sockets){if(s.used&&s.fd>=0&&platform.close)try{platform.close(s.fd);}catch(...){}s={};}
}
PcNetworkSockets::~PcNetworkSockets(){close_all();}
void PcNetworkSockets::set_platform(PcNetworkPlatform p){close_all();platform=std::move(p);last_error=0;}
bool PcNetworkSockets::set_server_override(const std::string& name){
    override_.clear();if(name.empty())return true;
    if(!network_host_valid(name))return false;override_=name;return true;
}
std::uint32_t PcNetworkSockets::platform_error(std::uint32_t fallback)const{
    const auto error=platform.last_error?platform.last_error():0u;return error?error:fallback;
}
std::uint32_t PcNetworkSockets::resolve_server(PcRaceMemory& m,const std::string& host,std::uint32_t out,std::uint32_t count){
    if(!count)return 0;
    const auto addresses=network_resolve(platform,host);
    const auto n=std::min<std::size_t>(count,addresses.size());
    for(std::size_t i=0;i<n;++i)m.put32(out+static_cast<std::uint32_t>(i*4u),addresses[i]);
    if(!n)last_error=platform_error(11001u);
    return static_cast<std::uint32_t>(n);
}
std::uint32_t PcNetworkSockets::call(PcRaceMemory& m,std::uint32_t index,const std::function<std::uint32_t(unsigned)>& a,std::uint32_t& pop,NativeNetworkStats& stats_){
    auto& s=*this;auto& p=platform;
    auto socket_of=[&](std::uint32_t h)->VirtualSocket*{return h>=1u&&h<=s.sockets.size()&&s.sockets[h-1].used?&s.sockets[h-1]:nullptr;};
    auto bound=[&](VirtualSocket& v)->bool{
        if(v.fd>=0)return true;
        if(!p.open_udp)return false;
        v.fd=p.open_udp(v.port,v.broadcast);
        if(std::getenv("OR2_NET_TRACE"))std::fprintf(stderr,"[net] socket port %u broadcast %d -> fd %d\n",v.port,int(v.broadcast),v.fd);
        return v.fd>=0;};
    std::uint32_t eax=0;pop=0;
    switch(index){
    case WsaStartup:pop=8;{auto* d=m.at(a(1),0x190,true);std::memset(d,0,0x190);d[0]=2;d[1]=2;d[2]=2;d[3]=2;}eax=0;break;
    case Socket:pop=12;
        eax=0xffffffffu;
        if(a(0)==2u&&a(1)==2u)for(std::uint32_t k=0;k<s.sockets.size();++k)if(!s.sockets[k].used){s.sockets[k]={true,false,-1,0};eax=k+1u;break;}
        if(eax==0xffffffffu)s.last_error=10024u;   // WSAEMFILE / not UDP
        break;
    case SetSockOpt:pop=20;{auto* v=socket_of(a(0));
        if(!v){eax=0xffffffffu;s.last_error=10038u;break;}
        if(a(1)!=0xffffu||a(2)!=0x20u){eax=0xffffffffu;s.last_error=10042u;break;}
        if(a(4)!=1u&&a(4)!=4u){eax=0xffffffffu;s.last_error=10022u;break;}
        v->broadcast=(a(4)==1u?m.u8(a(3)):m.u32(a(3)))!=0u;eax=0;}break;
    case Bind:pop=12;{auto* v=socket_of(a(0));
        if(!v){eax=0xffffffffu;s.last_error=10038u;break;}
        if(a(2)<16u){eax=0xffffffffu;s.last_error=10014u;break;}
        if(m.u16(a(1))!=2u){eax=0xffffffffu;s.last_error=10047u;break;}
        v->port=swap16(m.u16(a(1)+2));
        eax=bound(*v)?0u:0xffffffffu;if(eax)s.last_error=platform_error(10048u);}break;
    case IoctlSocket:pop=12;eax=socket_of(a(0))?0u:0xffffffffu;break;          // FIONBIO: the platform sockets are non-blocking
    case CloseSocket:pop=4;if(auto* v=socket_of(a(0))){if(v->fd>=0&&p.close)p.close(v->fd);*v={};}eax=0;break;
    case Htons:case Ntohs:pop=4;eax=swap16(a(0));break;
    case SendTo:{pop=24;auto* v=socket_of(a(0));
        if(!v||!p.send_to){eax=0xffffffffu;s.last_error=10038u;break;}
        if(a(3)){eax=0xffffffffu;s.last_error=10045u;break;}
        if(a(5)<16u){eax=0xffffffffu;s.last_error=10014u;break;}
        if(!bound(*v)){eax=0xffffffffu;s.last_error=platform_error(10048u);break;}
        const std::uint32_t to=a(4);
        const int n=p.send_to(v->fd,m.at(a(1),a(2)),a(2),m.u32(to+4),std::uint16_t(m.u16(to+2)));
        if(n<0)s.last_error=platform_error(10054u);
        eax=n<0?0xffffffffu:std::uint32_t(n);if(n>0)++stats_.sent;
        if(std::getenv("OR2_NET_TRACE")){const auto ad=m.u32(to+4);
            std::fprintf(stderr,"[net] sendto %u.%u.%u.%u:%u %u bytes from port %u\n",ad&0xffu,(ad>>8)&0xffu,(ad>>16)&0xffu,ad>>24,swap16(m.u16(to+2)),a(2),v->port);}
        break;}
    case RecvFrom:{pop=24;auto* v=socket_of(a(0));
        if(v&&std::getenv("OR2_NET_TRACE")){static std::uint32_t polled=0;const std::uint32_t bit=1u<<(v->port&31u);
            static std::uint32_t polls=0;++polls;
            if(!(polled&bit)||polls%500u==0u){polled|=bit;std::fprintf(stderr,"[net] recvfrom polls port %u (tick %u, poll %u)\n",v->port,stats_.ticks,polls);}}
        if(!v||!p.receive_from){eax=0xffffffffu;s.last_error=10038u;break;}
        if(a(3)){eax=0xffffffffu;s.last_error=10045u;break;}
        if(a(4)&&(!a(5)||m.u32(a(5))<16u)){eax=0xffffffffu;s.last_error=10014u;break;}
        if(!bound(*v)){eax=0xffffffffu;s.last_error=platform_error(10048u);break;}
        std::uint32_t address=0;std::uint16_t port=0;
        const int n=p.receive_from(v->fd,m.at(a(1),a(2),true),a(2),address,port);
        if(n<0||(n==0&&(!p.last_error||p.last_error()!=0u))){eax=0xffffffffu;s.last_error=platform_error(n==0?WsaEWouldBlock:10054u);break;}
        if(a(4)){auto* sa=m.at(a(4),16,true);std::memset(sa,0,16);sa[0]=2;std::memcpy(sa+2,&port,2);std::memcpy(sa+4,&address,4);}
        if(a(5))m.put32(a(5),16);
        eax=std::uint32_t(n);++stats_.received;
        if(std::getenv("OR2_NET_TRACE"))std::fprintf(stderr,"[net] recvfrom %u.%u.%u.%u:%u %d bytes on port %u\n",
            address&0xffu,(address>>8)&0xffu,(address>>16)&0xffu,address>>24,swap16(port),n,v->port);
        break;}
    case GetHostByName:{
        pop=4;const auto host=override_.empty()?guest_string(m,a(0)):override_;const auto addresses=network_resolve(p,host);
        if(addresses.empty()){eax=0;s.last_error=platform_error(11001u);break;}
        dns.fill(0);constexpr std::uint32_t base=0x0ef90100u;
        // 32-bit hostent: h_name, empty alias list, AF_INET, length4,
        // null-terminated h_addr_list. The PC reads +0C then dereferences.
        m.put32(base,base+0x100u);m.put32(base+4u,base+0x18u);m.put16(base+8u,2);m.put16(base+10u,4);
        m.put32(base+12u,base+0x20u);std::memcpy(dns.data()+0x100u,host.c_str(),host.size()+1u);
        for(std::size_t i=0;i<addresses.size();++i){const auto k=static_cast<std::uint32_t>(i);
            m.put32(base+0x20u+k*4u,base+0x80u+k*4u);m.put32(base+0x80u+k*4u,addresses[i]);}
        eax=base;break;}
    case InetAddr:{pop=4;std::uint32_t address=0;
        const auto host=override_.empty()?guest_string(m,a(0)):override_;
        if(!override_.empty()){
            const auto addresses=network_resolve(p,host);
            if(addresses.empty()){eax=0xffffffffu;s.last_error=platform_error(11001u);}else eax=addresses.front();
        }else if(network_ipv4(host,address))eax=address;
        else{eax=0xffffffffu;s.last_error=10022u;}break;}
    case Recv:case Send:case Connect:case Listen:case Select:
        if(std::getenv("OR2_NET_TRACE"))std::fprintf(stderr,"[net] winsock %u\n",index);
        {char t[64];std::snprintf(t,sizeof t,"Winsock import %u not bridged",index);throw std::runtime_error(t);}
    case InetNtoa:{pop=4;const std::uint32_t v=a(0);char t[20];
        std::snprintf(t,sizeof t,"%u.%u.%u.%u",v&0xffu,(v>>8)&0xffu,(v>>16)&0xffu,v>>24);
        std::memcpy(s.ntoa.data(),t,std::strlen(t)+1);eax=0x0ef90000u;break;}
    case WsaIoctl:{pop=36;
        if(a(1)!=0x4004747fu||!p.interfaces){eax=0xffffffffu;s.last_error=10045u;break;}   // SIO_GET_INTERFACE_LIST only
        const auto list=p.interfaces();const std::uint32_t room=a(5)/0x4cu;std::uint32_t n=0;
        for(const auto& i:list){
            if(n>=room)break;
            auto* e=m.at(a(4)+n*0x4cu,0x4c,true);std::memset(e,0,0x4c);
            const std::uint32_t flags=1u|2u;std::memcpy(e,&flags,4);           // IFF_UP | IFF_BROADCAST
            auto addr=[&](std::size_t off,std::uint32_t v){e[off]=2;std::memcpy(e+off+4,&v,4);};
            addr(4,i.address);addr(0x1c,i.broadcast);addr(0x34,i.netmask);++n;
        }
        if(a(6))m.put32(a(6),n*0x4cu);
        eax=0;break;}
    case WsaGetLastError:pop=0;eax=s.last_error;break;
    default:{char t[64];std::snprintf(t,sizeof t,"Winsock import %u not bridged",index);throw std::runtime_error(t);}
    }
    return eax;
}
}
