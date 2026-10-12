#pragma once
// The WheelFfb bridge provider (INPUT-SHIM.md section 6, "Bridge provider"). The force-feedback wheel the player steers
// with has exactly one owner of its handles at any time:
//   Inner    the shim's DInputProvider reads it on its own non-exclusive handle, like every other device (the path
//            without force output, unchanged);
//   Shared   while WheelFfb.dll holds that wheel as its force device (the F4 delivery's open), the shim reads it through
//            WheelFfb's read slot, which shares the exclusive force handle: no handle of the shim's own exists.
// The hand-over runs on the game thread inside the delivery's open and release (ffbdev::Owner): the handle of the phase
// that ends is closed first and marked stale, so Core's next read of it is Gone and its epoch ends; wheel opens are
// refused until the next phase, then Core reopens the wheel on its worker as a new epoch. The identity and objects in
// the Shared phase are WheelFfb's read-back of the shared handle (GetReadSlotCaps, native 0.11.0), which Core checks as
// it checks any open. Every other device goes to the DInputProvider unchanged.
// WheelFfb is called only in the Shared phase: by the worker under the bridge's lock (open, close), and on the game
// thread (reads, the hand-over). Once end_release or shutdown returns, no call into WheelFfb is in flight or can start,
// so the library may be freed then. OutRun is WheelFfb's only read-slot user (CloseReadDevices closes them all).
#include "shim.hpp"
#include "dinput_provider.hpp"
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"   // the vendored header's static loader helpers
#endif
#include "wheelffb.h"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
namespace dbce::shim {
class WheelFfbBridge final:public Provider {
public:
    enum class Phase { Inner,Promoting,Shared,Releasing,Stopped };
    // `instance`: the wheel's instance GUID text, lower case (as the profile parser and DInputProvider write it).
    // `api`: the force output's loaded WheelFfb.dll; it outlives every Shared-phase call (see above).
    WheelFfbBridge(std::shared_ptr<DInputProvider> inner,const WheelFfbApi* api,const std::string& instance);
    // True when `api` has what the Shared phase calls (GetReadSlotCaps is optional in older libraries).
    static bool usable(const WheelFfbApi&);
    std::vector<DeviceInfo> enumerate() override;
    OpenResult open(const DeviceInfo& listed) override;
    Call reacquire(Handle) override;
    void close(Handle) override;
    Call poll(Handle) override;
    Call state(Handle,RawState& out) override;
    std::int64_t now_us() override;

    // Game thread (the input thread), inside the delivery's open, before the library opens the wheel: the Inner handle
    // closed and stale; wheel opens refused.
    void begin_promotion();
    // held: the library holds the wheel as its force device (Shared: Core reopens it through the library); else Inner.
    void end_promotion(bool held);
    // Game thread, before the library frees the wheel: the Shared slot closed and stale; wheel opens refused.
    void begin_release();
    void end_release();        // Inner again
    // After the input thread's last tick: the Shared slot closed, nothing opened through the library again.
    void shutdown();
    Phase phase()const;
    struct Counts { unsigned inner_opens{},shared_opens{},shared_refused{},stale_reads{}; };
    Counts counts()const;
private:
    static constexpr Handle Tag=Handle(1)<<63;
    OpenResult open_shared(const DeviceInfo& listed);
    void close_shared_locked();
    std::shared_ptr<DInputProvider> inner_;
    const WheelFfbApi* api_;
    std::string instance_;
    GUID guid_{};
    mutable std::mutex m_;
    Phase phase_{Phase::Inner};         // under m_
    Handle inner_wheel_{};               // under m_: the Inner handle Core holds for the wheel (0 none)
    std::uint32_t generation_{};         // under m_: the Shared handles' generations
    std::atomic<Handle> shared_{0};      // the Shared handle Core may read (0 none)
    std::atomic<Handle> retired_{0};     // the Inner handle closed by a promotion: its reads are Gone, its close a no-op
    Counts counts_{};                    // under m_ (stale_reads from stale_reads_)
    std::atomic<unsigned> stale_reads_{0};   // reads of a handle a hand-over closed (the game thread)
};
}
