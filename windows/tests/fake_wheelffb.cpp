// A fake WheelFfb.dll for test_ffb_wiring: every export the F4 gates use, touching no device. Its attach count and
// each call it receives go into the process environment (FAKE_WHEELFFB_ATTACH, FAKE_WHEELFFB_CALLS), so the test can
// read them after the module was unloaded. Built into its own folder beside test_ffb_wiring only.
#include <windows.h>
#include <string>
namespace {
void append(const wchar_t* call){
    wchar_t buf[8192]{};
    const DWORD n=GetEnvironmentVariableW(L"FAKE_WHEELFFB_CALLS",buf,8192);
    std::wstring s=n&&n<8192?std::wstring(buf,n):std::wstring();
    s+=call;s+=L";";
    SetEnvironmentVariableW(L"FAKE_WHEELFFB_CALLS",s.c_str());
}
}
BOOL WINAPI DllMain(HINSTANCE,DWORD reason,LPVOID){
    if(reason==DLL_PROCESS_ATTACH){
        wchar_t buf[16]{};
        const DWORD n=GetEnvironmentVariableW(L"FAKE_WHEELFFB_ATTACH",buf,16);
        const int count=(n&&n<16?_wtoi(buf):0)+1;
        SetEnvironmentVariableW(L"FAKE_WHEELFFB_ATTACH",std::to_wstring(count).c_str());
    }
    return TRUE;
}
extern "C" {
__declspec(dllexport) int __cdecl GetWheelFfbVersion(){append(L"GetWheelFfbVersion");return 1000;}
__declspec(dllexport) void __cdecl SetHoldTimeoutMs(int){append(L"SetHoldTimeoutMs");}
__declspec(dllexport) void __cdecl SetPreferredDevice(const char*){append(L"SetPreferredDevice");}
__declspec(dllexport) void __cdecl SetPreferredDeviceIndex(int){append(L"SetPreferredDeviceIndex");}
__declspec(dllexport) void __cdecl SetPreferredDeviceGuid(const void*){append(L"SetPreferredDeviceGuid");}
__declspec(dllexport) void __cdecl SetStrictDeviceSelection(int){append(L"SetStrictDeviceSelection");}
__declspec(dllexport) int __cdecl EnumerateDevices(){append(L"EnumerateDevices");return 0;}
__declspec(dllexport) int __cdecl GetDeviceGuid(int,void*){append(L"GetDeviceGuid");return 0;}
__declspec(dllexport) int __cdecl InitDirectInput(int){append(L"InitDirectInput");return 0;}
__declspec(dllexport) int __cdecl InstallExitGuards(){append(L"InstallExitGuards");return 0;}
__declspec(dllexport) int __cdecl SetDeviceForcesXY(int,int){append(L"SetDeviceForcesXY");return 0;}
__declspec(dllexport) int __cdecl GetLastHResult(){append(L"GetLastHResult");return 0;}
__declspec(dllexport) void __cdecl ZeroForces(){append(L"ZeroForces");}
__declspec(dllexport) void __cdecl PanicStop(){append(L"PanicStop");}
__declspec(dllexport) void __cdecl FreeDirectInput(){append(L"FreeDirectInput");}
}
