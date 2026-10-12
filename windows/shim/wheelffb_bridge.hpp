#pragma once
// The WheelFfb bridge provider (INPUT-SHIM.md section 6, "Bridge provider"). The force-feedback wheel the player steers
// with has exactly one owner of its handles at any time:
//   Inner    the shim's DInputProvider reads it on its own non-exclusive handle, like every other device (the path
//            without force output, unchanged);
//   Shared   while WheelFfb.dll holds that wheel as its force device (the F4 delivery's open), the shim reads it through
//            WheelFfb's read slot, which shares the exclusive force handle: no handle of the shim's own exists.
// No phase change waits, and the lock is never held across a driver or WheelFfb call (romi 32201/32222):
//   begin_promotion  game thread, at each race sample of the delivery's open: the provider's wheel handle is retired
//                    (its reads are Gone, so Core's epoch ends) and the worker closes it when Core hands it back. Until
//                    that close has returned, and while any provider open or close of the wheel is still inside the
//                    driver (romi 32320), it answers false and the delivery keeps waiting; then true, and wheel opens
//                    are refused while the library opens the wheel.
//   end_promotion    game thread: held, the Shared phase (the worker then prepares the read slot once, see below); not
//                    held or cancelled, Inner again.
//   begin_release / shutdown   game thread: the phase changes and the Shared handle is invalidated at once; the slot is
//                    closed here when it is ready, or by the worker that is still preparing it when it returns.
// The Shared phase's read slot is prepared by the worker, outside the lock, once per phase: EnumerateAllDevices,
// GetAnyDeviceGuid, OpenReadDevice and GetReadSlotCaps (native 0.11.0: the identity, objects and ranges read back from
// the shared handle, which Core checks as it checks any open). Later opens hand out the same slot with a new handle
// generation and no library call. A worker counts itself in before it looks at the phase, so once a phase change has
// seen no worker inside, none can enter the library again: library_quiet() says when the library may be unloaded
// (never while a worker call may still be inside it). Reads are the game thread's.
// The library gate (romi 32320): WheelFfb serializes its exports, so a game-thread call would wait for a worker inside
// the library. The worker holds the gate for the whole preparation; the delivery only try-enters it (enter_library):
// when the worker is inside, that sample makes no library call (the library's hold watchdog, which does not wait,
// silences the wheel), and a release that cannot enter keeps the wheel the library's (see ffb_device.hpp).
// OutRun is WheelFfb's only read-slot user (CloseReadDevices closes them all).
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
    enum class Phase { Inner,Draining,Promoting,Shared,Releasing,Stopped };
    // `instance`: the wheel's instance GUID text, lower case (as the profile parser and DInputProvider write it).
    // `api`: the force output's loaded WheelFfb.dll; it stays loaded until library_quiet().
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

    // Game thread (the input thread). None of these waits.
    bool begin_promotion();       // true: the provider's wheel handle is closed; the library may open the wheel now
    void end_promotion(bool held);
    void begin_release();
    void end_release();
    void shutdown();              // after the input thread's last tick
    // No worker call is inside WheelFfb and none can start (outside the Shared phase): it may be unloaded.
    bool library_quiet()const;
    // Game thread: the library gate, never waited for (false: a worker is inside the library now).
    bool enter_library();
    void leave_library();
    Phase phase()const;
    struct Counts { unsigned inner_opens{},shared_opens{},refused{},late_closes{},stale_reads{},worker_slot_closes{}; };
    Counts counts()const;
private:
    static constexpr Handle Tag=Handle(1)<<63;
    OpenResult prepare_shared(const DeviceInfo& listed);
    std::shared_ptr<DInputProvider> inner_;
    const WheelFfbApi* api_;
    std::string instance_;
    GUID guid_{};
    mutable std::mutex m_;               // guards the fields below; never held across a driver or WheelFfb call
    Phase phase_{Phase::Inner};
    Handle inner_wheel_{};               // the provider handle Core holds for the wheel (0 none)
    int inner_busy_{};                   // provider opens and closes of the wheel still inside the driver
    bool prepared_{};                    // this Shared phase's slot preparation started (once per phase)
    int slot_{-1};                       // the Shared read slot, once prepared
    OpenResult shared_open_;             // its identity and objects
    std::uint32_t generation_{};
    Counts counts_{};
    std::atomic<int> library_calls_{0};  // workers inside (or about to enter) WheelFfb
    std::mutex library_gate_;            // held by the worker while it prepares the slot; the game thread only tries it
    std::atomic<Handle> retired_{0};     // the provider handle a promotion retired, until the worker closed it
    std::atomic<Handle> shared_{0};      // the Shared handle Core may read (0 none)
    std::atomic<unsigned> stale_reads_{0};
};
}
