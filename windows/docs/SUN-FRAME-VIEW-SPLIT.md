# Sun preparation and per-view render state

This is step1 of the native sun/lens-flare correction. The runtime still uses one legacy view, and the side-visible losses documented in SUN-PROJECTION-GATE.md remain. No side-view loop, new draw phase, viewport routing or developer flag is enabled.

`native_sun_prepare_frame` resolves the stage, lens-flare offsets and shared light direction/world point, captures size/intensity/pause/native camera inputs, and advances the authored+12 countdown once when unpaused. The caller prepares once for a frame. `native_sun_render_view` consumes that immutable frame, explicit view data, one NativeSunViewState and a caller-owned two-byte flicker span. It does not rerun stage/direction/blended-eye/countdown preparation.

Each view state owns visibility calibration, first-result state, previous samples, pending record/matrix, saved occlusion render states and its logical query handle. The device owns the underlying query resource, as in the legacy path. Flicker storage is explicit: the legacy wrapper aliases the original+10 work word, preserving its update timing; future panel contexts can supply separate storage. The native wrapper remains one preparation plus one render. Existing center visibility, edge fade, native lighting/queue/state/math and fallback behavior remain.

A preparation is not deduplicated automatically by frame number: its caller owns the once-per-frame contract. The current runtime has one call. The public functions are game-internal source seams, not a new C ABI or user setting. View fields currently describe the existing native camera; the future source-eye/off-axis projection and per-panel routing still need integration.

## Offline evidence

Before changing production source, the actual caller fixture captured34 legacy indexed draws with real Common PMT data and synthetic query samples. It records draw descriptors, World/View/Projection matrices, render and texture states, shader/declaration/index/stream IDs and VS/PS constants; no rasterizer is invoked. The190128 bytes have SHA256 **651B43D44924F7681FC5E0EEB45D79D45FC028B9FB159FF3B57E5917DEF2CE91**. After the split, all bytes compare exactly with this frozen baseline.

The same fixture prepares once, then renders three independent query/flicker contexts using the same legacy view. It verifies distinct query IDs and results, own flicker decrements from the prepared pause state, no shared countdown/work/light/camera/car changes during rendering, stack/Z-state restoration, and a pending result retaining only its own prior samples. This establishes state isolation; it does not prove side-panel geometry, routing or pixels. The final retail run passes165 assertions, including the existing8 side-visible caller rejection cases and34-packet equality. Gate-only69 and scene46 checks pass2/2; named Release Desktop/fixture/scene builds pass. An initial std::array-to-pointer compile correction is retained in the failed log.

Local baseline, source/blob provenance and logs: work/validation-20261102/sun-split-*. The baseline was captured from bbb6baa's unchanged sun producer with the new packet spy. GO49529712:02:50->12:02:56, failed compile49532412:09:10->12:09:13, final49534012:11:40->12:11:57 all released immediately, within caps, BelowNormal parent/jobs1 or2. No game/GPU/backend/window/real-device initialization, owner install, compositor flag enable or topology change.

## Next integration

Use an explicit per-view sun phase that bypasses GS replication, with each panel's source-eye view/projection and viewport. It must stay inside the world-source compositor lifetime: current first-non-world drawing finishes the curve scene, so ordinary HUD classification would composite too early. Keep query/flicker/fade state independent, retain shared preparation once, and gate the new route to Angled under the existing developer opt-in with default behavior unchanged. Qualify CPU projection/routing first, then actual GPU state/occlusion/curve/HUD/performance separately. Do not invoke the existing World pass three times under the replicating GS.
