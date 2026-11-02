# Coherent triple correction profile invalidation

A future curve-to-flat session latch can retain the same correction callback and native projection while changing the callback's immutable profile. Previously the camera's panel frusta only rebuilt when the camera stored a projection. View-only replay retained them. D3D11 also retained both an active52-float upload and a projection/fix correction LRU. An active-upload invalidation alone would reload stale values from that second cache.

`pc_triple_profile_changed()` publishes a monotonically increasing correction revision. A profile owner must finish publishing its immutable configuration and announce at an owned frame boundary before the next tick/culling/draw. This is an invalidation signal, not thread/hardware custody, a transaction lock or permission to mutate a callback's data concurrently. No producer/latch/mode is enabled here; future integration must call it explicitly. Raw callback-pointer changes are detected too.

PcSceneRenderer retains the actual authored projection independently of temporary device/global transforms. Render-view/queue access and frame entry lazily refresh panel frusta once when the provider/revision changes; a camera projection store still refreshes normally. Native center planes and authored camera/projection values stay untouched. With no authored projection, manually supplied/uninitialized views retain their existing behavior. Refused/absent corrections clear the optional panel frusta and use existing native fallback rules; a failed revision is not recomputed per node.

The temporary sun near-plane scope snapshots/restores the retained original projection, provider/revision and frusta together. If a revision changes during that scope, restoring it refreshes the original projection under the new profile instead of resurrecting old panels. Normal profile publication must still happen at a coherent frame boundary; the mid-scope fixture is a restoration stress case.

D3D11 compares provider/revision before world correction use and invalidates both its active upload and all projection/fix LRU entries, retaining existing point-mode and WorldCoverage cache behavior. Native vertex-program exact/affine classification depends on its own projection/constants and remains unchanged. Actual GPU uploads/culling parity on a live transition are not qualified by the CPU test.

## Checks

GO494197 named OutRunDesktop/test_camera_controls/test_pc_scene_renderer builds passed jobs2 BelowNormal. Focused camera/scene CTest2/2 passed (182 inert controller and46 scene checks); the controller additionally passed212 checks with the actual retail model.12 new real-scene assertions check same-provider changes without a new projection, stable repeated access, queue-consumer refresh, refusal/recovery, raw disable/enable, unchanged native projection, and the actual frame sun near-plane scope restoring coherently after a revision change. No private state/test-only setter is used.

CPU custody ENTER08:48:53, EXIT2026-11-02T08:51:59.9995426-06:00 immediately after focused results; exact receipt work/validation-20261102/profile-cpu-exit.json. Own token removed; no mode enable, window/backend/GPU/game/audio/input/physical-device initialization or owner install. Build/CTest/retail-model logs and entry/exit/process receipts use the profile-* prefix. A successful build is not an actual GPU cache/upload test.

The curve mesh/shader/inner-fallback helpers remain unconnected; resource readiness, explicit developer opt-in, session latch, source projection/profile selection, resolve/composite/HUD ordering and GPU/state/runtime qualification remain separate work.
