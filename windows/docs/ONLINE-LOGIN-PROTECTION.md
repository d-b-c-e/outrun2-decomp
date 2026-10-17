# Saved login protection and online transport

`[Misc] ProtectLoginData = true` opts into the Windows adapter at the next
ordinary start. Missing, false or invalid values remain Off, preserving the
native port's existing default. Tests, recording and playback exclude this
adapter. F6 exposes the same next-start key under Controls / Advanced / Menus
and license, using the existing settings writer and failed-save rollback.

The actual consumers are the shared profile loader `416380` and both common
writers `4164D0` / `416420` in `src/platform/frontend_profiles.cpp`. They use the
same common-memory block as the translated sign-in screens. The login region
is original address `7C205C`, offset `10864` in the `7B17F8` common payload,
192 bytes for eight names and passwords. Protection leaves live memory intact
and zeros only that region of a successful new `common.dat` disk payload.
All other common bytes, its PC size envelope and license writes are preserved.

The raw encrypted sidecar is `SaveGame/OnlineLoginData.dat` in the caller's
existing save directory. Unlike Tweaks' executable-directory file, it follows
the native application's save root; it does not discover or import a PC install.
Windows uses `CryptProtectData` / `CryptUnprotectData`, the `OnlineLoginData`
description, no optional entropy, user scope and `CRYPTPROTECT_UI_FORBIDDEN`.
The adapter scrubs and frees DPAPI output buffers; the shared loader scrubs its
temporary plaintext vector. The bounded decrypted payload must be exactly
192 bytes; raw encrypted input is limited to 64 KiB.

The encrypted sidecar is checked and replaced before writing the sanitized
common payload. Both use the existing flushed `.new` / `.bak` replacement path
and refuse recovery-name collisions. Failed protection or sidecar writing
refuses the common save; there is no plaintext fallback. An unreadable sidecar
clears the live login, reports an invalid file and latches a refusal to replace
it until a later successful/missing-sidecar reload. The original `416420`
license-write behavior and PC scalar return remain independent of common-save
failure. This is not a transaction spanning the two files: if only the common
replace fails, the new encrypted sidecar can coexist with the prior common.

Loading never writes, migrates or scrubs files. A missing sidecar preserves a
legacy login in memory until an ordinary explicit save creates the encrypted
file. Existing files/backups remain as they were on a failed save. Successful
replacement removes its temporary backup; it does not securely erase old disk
sectors, external backups or previously copied files. Keep the sidecar with
your saves. Turning protection Off ignores it and may require entering the
login again. DPAPI is generally tied to the same Windows user and computer;
this file is not a portable credential export. See Microsoft's
[CryptProtectData](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata)
and [CryptUnprotectData](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptunprotectdata)
contracts for user/roaming-profile behavior and buffer ownership.

## Server override: actual missing dependencies

`DemonwareServerOverride` is not exposed or consumed in this candidate. Tweaks
reference `51acc201`, `src/hooks_misc.cpp`, redirects `bdPlatformSocket`
resolution at `5349B0` to its configured hostname (default `clarissa.port0.org`)
and adds UPnP mapping during `45ACB0`. Native `pc_network.cpp::winsock_call`
throws for `GetHostByName`, `InetAddr`, `Connect`, `Send`, `Recv`, `Select` and
`Listen`; `Socket` accepts only IPv4 UDP. `PcNetworkPlatform` supplies only
bound UDP, datagrams and interfaces. Its BSD supplier returns an empty adapter
on Windows. The translated Demonware service/client scaffolding therefore has
no complete resolver plus connected transport supplier on this platform.

A usable override needs the actual DNS/IPv4 guest-memory result bridge,
connected/nonblocking socket lifecycle, send/receive/readiness/error semantics
and a Windows platform supplier before routing the override at the original
resolver boundary. Native LAN translation alone does not establish online
play. No placeholder saved key, automatic UPnP, live DNS lookup, server contact,
account login or owner credential/config operation was added or performed.

## Verification boundary

The focused `login_protection` fixture calls the actual shared loader and both
save consumers using disposable synthetic files and fake reversible transforms.
It covers raw sidecar format, exact round trips, legacy/default behavior,
excluded writes, unchanged bytes outside the login block, crypto failures,
malformed payloads and recovery-file collisions. `frontend_profiles` retains
the existing profile/writer regression coverage. These fixtures never link the
DPAPI adapter or access a game, network, account or owner save. Desktop build,
test results and the executable identity belong to the hub artifact record;
DPAPI operation, rendered F6 saving and online behavior remain runtime gates.


Qualified source-only build: product commit `5bb80ce1a9b40494ca38204053df91cba4551d0e`.
Hub `artifacts/outrun-login-protection-20261017/build-01` retains the original
build output, JUnit, LastTest.log and cleanup result. Desktop Release and both
affected CTests passed: `desktop_login_protection` 79 checks and
`desktop_profile_writes` 51 checks (130 total, 2/2 tests, zero skipped).
Executable: 24,023,040 bytes, SHA-256
`0DD7BBD4DDDDE9C0C3FD15E9460D438D2828525B77EDBAF0EF3BA5C50F49CAD0`.
Actual safe release: `2026-10-17T04:07:52.2522165Z`; direct Hera handoff
preceded documentation. No DPAPI operation or account/network/device access
occurred. Previous vibration/presentation evidence remains retained.
