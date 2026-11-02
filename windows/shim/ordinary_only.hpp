#pragma once
// The x86 build of dbce_shim serves the ordinary path only (shim_dll/README.md, "x86 build: ordinary only"): Core, the
// DirectInput provider and ProviderHost, opened with dbce_shim_open. The control journal, its observer transport
// (qualified for x64 only, observer_transport.hpp) and the frame recorder are not compiled in, so dbce_shim_open2,
// dbce_shim_record and dbce_shim_bridge_open refuse there. x64 is unaffected: DBCE_SHIM_ORDINARY_ONLY is 0.
#if defined(_M_IX86)
#define DBCE_SHIM_ORDINARY_ONLY 1
#else
#define DBCE_SHIM_ORDINARY_ONLY 0
#endif
