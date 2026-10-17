# Opt-in Windows DNS and UDP transport

`[Misc] DemonwareServerOverride` now has an actual resolver consumer and Windows
supplier. It defaults to an empty string, preserving the native desktop's
previous empty Windows network adapter. A valid nonempty value opts into the
supplier at the next ordinary start. Recording, playback and fenced tests ignore
it. No UI, account handling, UPnP, automatic server discovery or port forwarding
is added. A hostname is a plain ASCII DNS name (including digit-leading labels)
or a complete dotted-decimal IPv4 address; URLs, ports, user-info, spaces,
invalid labels and malformed numeric addresses are refused. A refused value
clears the override and leaves the ordinary Windows adapter empty.

## Actual game call semantics

The reconstructed closure's `5349B0` is cdecl `(name, out_addresses, capacity)`.
It calls CRT `isalpha` at `58249B`, then `gethostbyname` (`596234`) for a leading
letter or `inet_addr` (`596238`) otherwise. Its hostent consumer reads the
32-bit address-list pointer at offset `0C`, dereferences each IPv4 address and
writes at most the supplied capacity. Actual socket wrappers `491990` and
`534580` create IPv4 UDP sockets; `5345C0` binds. The datagram bridge uses host
order for the bind-port callback and network order for address/port in send and
receive callbacks, matching the preexisting contract.

`PcNetworkSockets` is the existing import bridge extracted from `pc_network.cpp`
so the actual consumer can run over injected transport. It owns the same guest
socket bank plus guest-layout DNS/inet_ntoa scratch. Both the native `5349B0`
entry and translated internal calls consume the configured override. The CRT
classification seam forces an override through the DNS/address-list path, and
the actual DNS/IPv4 imports also substitute the configured host. This matters:
translated functions can call their original `5349B0` directly instead of
returning through native dispatch. Digit-leading hostnames are resolved through
the supplier rather than misclassified as numeric addresses. Results are
deduplicated and bounded to 16 IPv4 addresses with a null-terminated guest list.

The Windows supplier uses lazy `WSAStartup`, synchronous IPv4 `getaddrinfo`,
nonblocking UDP bind/send/receive and `SIO_GET_INTERFACE_LIST`. Construction and
configuration perform no socket, lookup or enumeration operation. Original
guest handles and native Windows `SOCKET` values remain separate integer-token
banks; there is no 64-bit handle truncation. Open failure closes the partial
socket, replacement closes owned outstanding tokens, and supplier destruction
closes any remainder and balances Winsock initialization. It does not set
`SO_REUSEADDR` or take over another process's bound port. It exposes exact
Winsock failures, including no-data `10035`, bind refusal and receive truncation.
Zero-length received datagrams are distinguished from no pending data; old
suppliers without the error callback retain their existing zero-means-no-data
contract. Bind/sockaddr lengths and supported datagram flags are checked before
invoking the supplier. The original one-byte SO_BROADCAST form is supported.

The relevant API contracts are Microsoft's
[getaddrinfo](https://learn.microsoft.com/en-us/windows/win32/api/ws2tcpip/nf-ws2tcpip-getaddrinfo),
[recvfrom](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-recvfrom)
and [ioctlsocket](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-ioctlsocket).
Configured DNS latency/timeouts have not been measured; resolution follows the
original synchronous call boundary.

## Bounded delivery and remaining dependency

The connected imports `Recv`, `Send`, `Connect`, `Listen` and `Select` remain
explicitly unsupported. A source search found no calls to their IAT slots
`59621C` through `59622C` in any reconstructed source closure. Adding generic
TCP callbacks alone would not provide an actual game consumer. If later online
services require those calls, their real call closure and lifecycle/readiness
semantics need to be recovered and integrated before implementation/testing.
No server login or end-to-end Demonware protocol acceptance is claimed here.

The native login-protection candidate and its default remain intact. See
[ONLINE-LOGIN-PROTECTION.md](ONLINE-LOGIN-PROTECTION.md); its first network-gap
description records the earlier login-only build, before this successor.

## Source-only qualification

The affected fixture runs the real translated `5349B0` and the actual extracted
import bridge using fake DNS/UDP callbacks and synthetic guest memory. It checks
stock/numeric and digit-leading override routes, hostent pointers/counts,
network byte order, bind/send/receive, exact errors, zero-byte datagrams,
legacy no-data semantics and token cleanup. It never constructs the Windows
supplier or calls Winsock, external sockets, DNS, accounts, credentials, games
or owner settings. The desktop build compiles and links the real supplier.
Rendered behavior, Windows/network operation, online protocol and physical
acceptance remain unqualified. Original results belong to the hub artifact.
