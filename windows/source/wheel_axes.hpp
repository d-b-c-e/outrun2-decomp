#pragma once
// The DIJOYSTATE2 axes a DirectInput device really has, read once when the native reader opens it (wheel_input.cpp).
// DIDEVCAPS gives counts only, so presence is read by offset (GetObjectInfo), as the shared provider reads it
// (shim/dinput_provider.cpp). An axis is range-qualified only when its range reads back as the 0..65535 the reader
// requested, the only range its bindings are scaled from.
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <windows.h>
#include <dinput.h>
#include <cstdint>
namespace outrun::desktop {
struct AxisInventory { std::uint32_t present{},ranged{}; };   // bit i: DIJOYSTATE2 axis i (X Y Z Rx Ry Rz Slider0 Slider1)
inline AxisInventory axis_inventory(IDirectInputDevice8W* device){
    static constexpr DWORD Offsets[8]{DIJOFS_X,DIJOFS_Y,DIJOFS_Z,DIJOFS_RX,DIJOFS_RY,DIJOFS_RZ,DIJOFS_SLIDER(0),DIJOFS_SLIDER(1)};
    AxisInventory out;
    for(unsigned a=0;a<8;++a){
        DIDEVICEOBJECTINSTANCEW object{};object.dwSize=sizeof object;
        if(FAILED(device->GetObjectInfo(&object,Offsets[a],DIPH_BYOFFSET)))continue;
        out.present|=1u<<a;
        DIPROPRANGE range{};range.diph.dwSize=sizeof range;range.diph.dwHeaderSize=sizeof range.diph;
        range.diph.dwHow=DIPH_BYOFFSET;range.diph.dwObj=Offsets[a];
        if(SUCCEEDED(device->GetProperty(DIPROP_RANGE,&range.diph))&&range.lMin==0&&range.lMax==65535)out.ranged|=1u<<a;
    }
    return out;
}
}
