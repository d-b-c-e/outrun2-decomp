# Native world fallbacks inside the widened curve source

`curve_inner_rect.hpp` derives the original flat panel's normalized inner rectangle inside an existing CurvedPanelWarp source. It uses the admitted warp interval and real-panel dimensions; no new rig measurement, eye-distance convention, ray approximation or settings are introduced. Coordinates are D3D top-left:

- x = -min_x / span_x; width = panel_width / span_x.
- y = 1 - (panel_height - min_y) / span_y; height = panel_height / span_y.

Radius0 is exactly (0,0,1,1). Invalid/nonfinite/out-of-source intervals leave output unchanged. This small helper is not yet called by the renderer and does not enable curve mode.

## Why the world fallback belongs here

Coordinator review494131 proposes a developer opt-in world/source/composite/HUD separation, matching the existing Art design. A center-only world fallback (ordinary RHW, unmatched/custom XYZ) should draw in the original panel's inner sub-rectangle of the widened center source. The subsequent warp then treats it as part of the center world, retaining native depth and draw order; sides still lack that effect as they do now. Moving it to a final overlay would lose world occlusion/order. HUD/frontend/non-world must instead draw after the world composite and stay unwarped. This is engineering direction for an explicit default-off developer path, not rendered owner acceptance.

A future backend must compose this rect with its existing native viewport (including offsets/sub-viewports) and center source origin, use the resulting float viewport, apply a conservative source-clamped scissor and keep the original MinDepth/MaxDepth. Do not nearest-round the viewport, stretch the fallback across the widened source, move it to an independent final layer, or reinterpret native screen coordinates as source-wide coordinates. Actual scissor rounding, interpolation/edges, depth occlusion and ordering still need backend/pixel qualification.

## Numerical contract

The standalone actual-source test passes13,121 assertions. For asymmetric61/73-degree panels,85mm eye height,8mm bezel, radius0/400/1500/20000, three render-eye positions and a relocated rendered plane, it builds the existing admitted curve/source geometry. It compares original panel projection plus the inner viewport against the widened source projection at5x4 panel locations and3 ray depths. All3 panels are exercised, including future center use. Their homogeneous W and Z/W are unchanged; normalized XY agrees within1e-12. Float viewport arithmetic for widths256/2560/5461 and height16384 stays within.002 pixel of the double reference. This is CPU arithmetic, not a rasterizer or filtering bound. Refusal cases preserve output.

GO494150 jobs1 BelowNormal standalone build/CTest1/1 passed. ENTER08:40:02 local, EXIT2026-11-02T08:40:07.4649218-06:00 immediately after result, own token removed. No runtime/Desktop rebuild, shader compile, window/backend/GPU/game/audio/input/device/init/install. Evidence: work/validation-20261102/inner-rect-{build,ctest}.log, inner-rect-cpu-{enter,exit}.json and inner-rect-exit-processes.json. Latest actual private desktop remains0be0720/5DD09A29, installed2dd9886 unchanged.

Readiness still needs a session latch and explicit projection/culling cache invalidation at a coherent frame boundary when switching to flat after refusal. A boolean changed only after begin_frame is insufficient because view/frustum data may already be retained. No readiness/profile/global callback behavior changes here.
