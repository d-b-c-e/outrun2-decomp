#pragma once
#include "pc_network.hpp"
#include <array>
namespace outrun::platform {
enum PcNetworkImport : std::uint32_t {WsaStartup,SendTo,RecvFrom,Ntohs,Socket,Htons,Bind,IoctlSocket,CloseSocket,Recv,Send,Connect,Listen,Select,
    InetNtoa,GetHostByName,InetAddr,WsaIoctl,WsaGetLastError,SetSockOpt};
bool network_ipv4(const std::string&,std::uint32_t&);
bool network_host_valid(const std::string&);
std::vector<std::uint32_t> network_resolve(const PcNetworkPlatform&,const std::string&);
// Existing native/translated Winsock import consumer, separated for injection.
// Socket numbers remain guest tokens, never cast/truncated Windows handles.
class PcNetworkSockets {
public:
    PcNetworkSockets()=default;
    ~PcNetworkSockets();
    PcNetworkSockets(const PcNetworkSockets&)=delete;
    PcNetworkSockets& operator=(const PcNetworkSockets&)=delete;
    void map_memory(PcRaceMemory&);
    void set_platform(PcNetworkPlatform);
    bool set_server_override(const std::string&);
    const std::string& server_override()const{return override_;}
    bool name_isalpha(std::uint32_t c)const{return !override_.empty()||(c>='A'&&c<='Z')||(c>='a'&&c<='z');}
    std::uint32_t call(PcRaceMemory&,std::uint32_t,const std::function<std::uint32_t(unsigned)>&,std::uint32_t& pop,NativeNetworkStats&);
    // 5349B0: explicit override bypasses the original first-character isalpha
    // test; names beginning with a digit reach the actual DNS supplier.
    std::uint32_t resolve_server(PcRaceMemory&,const std::string&,std::uint32_t out,std::uint32_t count);
private:
    struct VirtualSocket {bool used{},broadcast{};int fd{-1};std::uint16_t port{};};
    std::array<VirtualSocket,16> sockets{};
    PcNetworkPlatform platform;
    std::uint32_t last_error{};
    std::string override_;
    std::array<std::uint8_t,0x20> ntoa{};
    std::array<std::uint8_t,0x400> dns{};
    void close_all() noexcept;
    std::uint32_t platform_error(std::uint32_t fallback)const;
};
}
