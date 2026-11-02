#pragma once
// The DirectInput 8 provider (windows/docs/INPUT-SHIM.md section 6), slice 3: the single owner of every device handle,
// behind the core's Provider interface. Tested over fake COM devices (windows/tests/shim_fake_com.hpp); no game, no
// real device in this slice.
//
// - enumerate: game controllers attached now, by instance and product GUID text (lower case).
// - open: CreateDevice by instance, c_dfDIJoystick2, background + non-exclusive, the 0..65535 range and zero dead
//   zone requested, then every object checked by its DIJOYSTATE2 offset (GetObjectInfo) and each axis's range read
//   back (GetProperty, by offset); the opened identity is read back from the new handle (GetDeviceInfo). Any failure
//   after CreateDevice releases the handle. No force-feedback call is ever made.
// - poll / state: Poll and GetDeviceState are separate calls, each classified where it is made (E_PENDING, "data not
//   yet available", is Call::Pending: transient, not a lost device); nothing is copied from a failed read.
//
// Admission (slice 3 review, findings 1 and 3). Each slot has one 64-bit atomic word: the generation in the high half,
// and in the low half a CLOSED bit, a RESERVED bit and the reader count. A read is admitted by one compare-exchange
// that checks the handle's generation, that the slot is open, and adds a reader, so a stale handle can never be
// admitted to a reused slot. A slot is published (its generation advanced and CLOSED cleared) only after its device is
// stored. close() is one compare-exchange from "open, no readers" to "closing". After shutdown(), nothing new is
// admitted: reads, opens, listings and reacquisitions are refused. An open publishes its slot and then looks for a stop
// (slice 3.1 review): an open in flight when shutdown began either fails and releases its own device, or published
// first and is accounted for by shutdown. It never returns a handle shutdown missed. A handle with bits above its
// 32-bit generation is refused as misuse.
//
// Lifetime (finding 2). All mutable state lives in a shared State; every call holds a reference to it for its whole
// duration. Destroying the provider drops only the provider's reference: a read still inside a driver call keeps the
// State alive, and whatever shutdown could not release (a device being read, DirectInput while an open or listing is
// in flight) is released by the State's destructor once the last call has left. The owner must not destroy the
// provider while one of its threads may still START a call (the core's own shutdown gate); calls already started are
// covered here. The code module is the game executable (never unloaded).
#include "shim.hpp"
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <windows.h>
#include <dinput.h>
#include <memory>
#include <string>
namespace dbce::shim {
std::string guid_text(const GUID&);
bool guid_parse(const std::string& text,GUID& out);

class DInputProvider final:public ProviderOwner {
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

    using ShutdownReport=ProviderShutdownReport;
    // Stops admission at once, releases what is idle, waits at most wait_ms for calls in progress. Never releases
    // anything a call is using; what remains is released when the last call leaves (see Lifetime above).
    ShutdownReport shutdown(int wait_ms) override;
    int misuse()const;            // stale handles, closes refused during a read
    int refused_after_stop()const;
    static Call classify(HRESULT);

    // Tests only: called inside admission between reading a slot's word and the compare-exchange (null in production),
    // to schedule a close / reopen exactly there.
    using AdmissionHook=void(*)(void* user);
    static void set_admission_hook_for_tests(AdmissionHook,void* user);
    // Tests only: called inside open once a slot is reserved and its device stored, just before and just after the slot
    // is published (null in production), to run a shutdown exactly there.
    enum OpenPoint:int{BeforePublish=0,AfterPublish=1};
    using OpenHook=void(*)(int point,void* user);
    static void set_open_hook_for_tests(OpenHook,void* user);
private:
    struct State;
    std::shared_ptr<State> state_;
    bool shut_{};
};
}
