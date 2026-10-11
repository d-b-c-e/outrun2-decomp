#pragma once
// Not part of the ABI: the test entry (tests/test_shim_abi.cpp) that opens a host on a given DirectInput object, such
// as a fake, which the host takes over exactly as dbce_shim_open takes over the one it creates.
#include "dbce_shim.h"
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <windows.h>
#include <dinput.h>
int32_t dbce_shim_open_with_input(IDirectInput8W* input, const wchar_t* folder, const char* nonce, const char* profile,
                                  int32_t profile_len, const char* unsupported, void* hwnd, void** out, char* why, int32_t why_len);
int32_t dbce_shim_open2_with_input(IDirectInput8W* input, const dbce_shim_open_args* args, void** out, char* why, int32_t why_len);
