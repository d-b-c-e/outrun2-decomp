#pragma once
// The DirectInput 8 provider (windows/docs/INPUT-SHIM.md section 6), slice 3: the single owner of every device handle,
// behind the core's Provider interface. Tested over fake COM devices (windows/tests/shim_fake_com.hpp); no game, no
// real device in this slice.
//
// - enumerate: game controllers attached now, by instance and product GUID text (lower case).
// - open: CreateDevice by instance, c_dfDIJoystick2, background + non-exclusive, the 0..65535 range and zero dead
//   zone requested, then every object checked by its DIJOYSTATE2 offset (GetObjectInfo) and each axis's range read
//   back (GetProperty, by offset); the opened identity is read back from the new handle (GetDeviceInfo), so the core
//   can compare it with the listing. No force-feedback call is ever made.
// - poll / state: Poll and GetDeviceState are separate calls, each classified where it is made; the state buffer is
//   initialized and copied out only when the whole read succeeded.
// - close: refused while a read of that handle is in progress (a per-slot atomic: readers in, or closing), so a handle
//   is never released under a reader; the refusal is counted.
// - shutdown: releases every device not being read, waits a bounded time for reads in progress, and deliberately leaks
//   (never releases) a device still being read; the report says what was left.
#include "shim.hpp"
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <windows.h>
#include <dinput.h>
#include <array>
#include <atomic>
#include <string>
namespace dbce::shim {
std::string guid_text(const GUID&);
bool guid_parse(const std::string& text,GUID& out);

class DInputProvider final:public Provider {
public:
    static constexpr int MaxSlots=64;
    // Takes ownership of `input` (one reference): a real IDirectInput8W from DirectInput8Create, or a test fake.
    DInputProvider(IDirectInput8W* input,HWND window);
    ~DInputProvider() override;
    std::vector<DeviceInfo> enumerate() override;
    OpenResult open(const DeviceInfo& listed) override;
    Call reacquire(Handle) override;
    void close(Handle) override;
    Call poll(Handle) override;
    Call state(Handle,RawState& out) override;
    std::int64_t now_us() override;

    struct ShutdownReport { int released{},leaked{}; bool input_released{}; std::string detail; };
    ShutdownReport shutdown(int wait_ms);
    int misuse()const{return misuse_.load();}
    static Call classify(HRESULT);
private:
    // A handle is (generation << 8) | (slot + 1): a slot is reused after its close, and a stale handle from an earlier
    // open never reaches the new device.
    struct Slot {
        IDirectInputDevice8W* device{};
        std::atomic<int> state{0};       // > 0: readers inside; 0: idle; -1: closing or closed
        std::atomic<bool> used{false};
        std::atomic<std::uint32_t> generation{0};
    };
    IDirectInput8W* input_;
    HWND window_;
    std::array<Slot,MaxSlots> slots_;
    std::atomic<int> misuse_{0};
    bool shut_{};
    Slot* slot(Handle)const;
    static bool enter(Slot&);
    static void leave(Slot&);
};
}
