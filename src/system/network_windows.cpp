#include "network_windows.hpp"
#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <array>
#include <climits>
#include <memory>
namespace outrun::platform {
namespace {
struct WindowsNetwork {
    std::array<SOCKET,16> sockets;
    bool initialized{};
    std::uint32_t error{};
    WindowsNetwork(){sockets.fill(INVALID_SOCKET);}
    ~WindowsNetwork(){for(auto s:sockets)if(s!=INVALID_SOCKET)closesocket(s);if(initialized)WSACleanup();}
    bool ready(){
        if(initialized)return true;WSADATA data{};const int e=WSAStartup(MAKEWORD(2,2),&data);
        if(e){error=static_cast<std::uint32_t>(e);return false;}initialized=true;return true;
    }
    SOCKET get(int token){
        if(token<1||static_cast<std::size_t>(token)>sockets.size()||sockets[token-1]==INVALID_SOCKET){error=WSAENOTSOCK;return INVALID_SOCKET;}
        return sockets[token-1];
    }
    int fail(){error=static_cast<std::uint32_t>(WSAGetLastError());return -1;}
};
}
PcNetworkPlatform pc_network_windows_platform(){
    auto state=std::make_shared<WindowsNetwork>();PcNetworkPlatform p;
    p.open_udp=[state](std::uint16_t port,bool broadcast)->int{
        if(!state->ready())return -1;
        std::size_t token=0;while(token<state->sockets.size()&&state->sockets[token]!=INVALID_SOCKET)++token;
        if(token==state->sockets.size()){state->error=WSAEMFILE;return -1;}
        const SOCKET s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(s==INVALID_SOCKET)return state->fail();
        struct Close {SOCKET socket;~Close(){if(socket!=INVALID_SOCKET)closesocket(socket);}} cleanup{s};
        const BOOL one=TRUE;u_long nonblocking=1;
        if((broadcast&&setsockopt(s,SOL_SOCKET,SO_BROADCAST,reinterpret_cast<const char*>(&one),sizeof one)==SOCKET_ERROR)||
           ioctlsocket(s,FIONBIO,&nonblocking)==SOCKET_ERROR)return state->fail();
        sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(port);address.sin_addr.s_addr=htonl(INADDR_ANY);
        // No SO_REUSEADDR: do not share a game's bound port with another process.
        if(bind(s,reinterpret_cast<const sockaddr*>(&address),sizeof address)==SOCKET_ERROR)return state->fail();
        state->sockets[token]=s;cleanup.socket=INVALID_SOCKET;return static_cast<int>(token+1u);
    };
    p.close=[state](int token){const auto s=state->get(token);if(s!=INVALID_SOCKET){closesocket(s);state->sockets[token-1]=INVALID_SOCKET;}};
    p.send_to=[state](int token,const std::uint8_t* data,std::uint32_t size,std::uint32_t address,std::uint16_t port)->int{
        const auto s=state->get(token);if(s==INVALID_SOCKET)return -1;
        if(size>INT_MAX){state->error=WSAEMSGSIZE;return -1;}
        sockaddr_in to{};to.sin_family=AF_INET;to.sin_port=port;to.sin_addr.s_addr=address;
        const int n=sendto(s,reinterpret_cast<const char*>(data),static_cast<int>(size),0,reinterpret_cast<const sockaddr*>(&to),sizeof to);
        return n==SOCKET_ERROR?state->fail():n;
    };
    p.receive_from=[state](int token,std::uint8_t* data,std::uint32_t size,std::uint32_t& address,std::uint16_t& port)->int{
        const auto s=state->get(token);if(s==INVALID_SOCKET)return -1;
        if(size>INT_MAX){state->error=WSAEMSGSIZE;return -1;}
        sockaddr_in from{};int length=sizeof from;
        const int n=recvfrom(s,reinterpret_cast<char*>(data),static_cast<int>(size),0,reinterpret_cast<sockaddr*>(&from),&length);
        if(n==SOCKET_ERROR){state->fail();return state->error==WSAEWOULDBLOCK?0:-1;}
        state->error=0;address=from.sin_addr.s_addr;port=from.sin_port;return n;
    };
    p.resolve=[state](const std::string& host){
        std::vector<std::uint32_t> out;if(!state->ready())return out;
        addrinfo hints{},*list=nullptr;hints.ai_family=AF_INET;hints.ai_socktype=SOCK_DGRAM;
        const int e=getaddrinfo(host.c_str(),nullptr,&hints,&list);
        if(e){state->error=static_cast<std::uint32_t>(e);return out;}
        struct Free {addrinfo* list;~Free(){freeaddrinfo(list);}} cleanup{list};
        for(auto* a=list;a&&out.size()<16u;a=a->ai_next)
            if(a->ai_family==AF_INET&&a->ai_addrlen>=sizeof(sockaddr_in))out.push_back(reinterpret_cast<const sockaddr_in*>(a->ai_addr)->sin_addr.s_addr);
        if(out.empty())state->error=WSANO_DATA;return out;
    };
    p.interfaces=[state]{
        std::vector<PcNetworkInterface> out;if(!state->ready())return out;
        const SOCKET s=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(s==INVALID_SOCKET){state->fail();return out;}
        struct Close {SOCKET socket;~Close(){closesocket(socket);}} cleanup{s};
        std::array<INTERFACE_INFO,64> list{};DWORD bytes=0;
        if(WSAIoctl(s,SIO_GET_INTERFACE_LIST,nullptr,0,list.data(),sizeof list,&bytes,nullptr,nullptr)==SOCKET_ERROR){state->fail();return out;}
        for(std::size_t i=0;i<bytes/sizeof(INTERFACE_INFO)&&i<list.size();++i){const auto& a=list[i];
            if((a.iiFlags&IFF_UP)&&!(a.iiFlags&IFF_LOOPBACK))out.push_back({a.iiAddress.AddressIn.sin_addr.s_addr,a.iiBroadcastAddress.AddressIn.sin_addr.s_addr,a.iiNetmask.AddressIn.sin_addr.s_addr});}
        return out;
    };
    p.last_error=[state]{return state->error;};return p;
}
}
#else
namespace outrun::platform {PcNetworkPlatform pc_network_windows_platform(){return {};}}
#endif
