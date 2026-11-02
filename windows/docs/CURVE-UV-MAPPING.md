# Native curve UV/source calculation

`windows/source/curved_panel_warp.hpp` is an allocation-free native adapter for the settled `CurvedPanelWarp.WithSourceOn` calculation. It uses the game's existing `RigPanel`/`RigVec` geometry. **It is not connected to the renderer or settings, and enables no curved presentation.** The world-particle and target/resolve/HUD integration gaps in [CURVE-SCENE-BOUNDARY.md](CURVE-SCENE-BOUNDARY.md) remain.

All positions use millimetres in right-handed physical space: X right, Y up, negative Z into the screen. Eye distance remains eye to the plane through the center panel's edges; the curved glass bows away behind that chord. No sagitta is subtracted from the measured distance. UV has its origin at the lower left; the D3D adapter must explicitly handle texture orientation and pixel-center conventions later.

`curved_panel_warp(real_panel, real_eye, radius, rendered_panel, out)` builds the source rectangle from 256 samples on each physical panel edge. Both panels must have the same width/height; the rendered panel may belong to a virtual-eye Game-FOV rig. The **real** eye/panel determine the UV mapping. Moving the source onto the rendered panel leaves those UVs unchanged, while positioning its widened rectangle on that rendered panel. Radius zero retains the rendered surface exactly and maps UV identically. `curved_panel_point` provides the physical arc point, and `curved_source_uv` maps a physical panel UV to that source. The intended presenter grid is64x36 cells, matching the canonical core. No source target is allocated here.

The functions reject nonfinite/degenerate panels, unsupported radii, an eye on/behind the physical panel, differing source-panel dimensions and invalid UVs. Output arguments remain unchanged on failure; the caller must handle the error before using a plan. Existing Planar/Angled choices, framing sets, camera input and owner INIs are untouched.

`test_curved_panel_warp` passes **34,364 assertions**:270 canonical managed reference rows plus dense three-panel64x36 grids, real/virtual UV invariance, pitched-coordinate equivalence, coverage bounds, exact flat identity, chord distance and refusal/output-retention cases. The canonical rows cover radius0/400/1500/20000mm, virtual distances313/900/1200mm, asymmetric angles, bezel/height and eye offsets. Coordinate tolerance is1e-8mm; UV tolerance1e-10. These are numerical assertions, not images, visual calibration, interpolation-error bounds, rendered seams or GPU coverage.

## Native clip-correction consumer

`curved_source_views(original_projection, render_eye, warps, out)` consumes three source rectangles into the existing native `TripleViews` row-vector layout. It preserves the original RH perspective's near/far and computes each correction as `inverse(Poriginal) * Rsource * Psource`. A widened center source can require a nonidentity correction. The function accepts finite off-axis native perspectives, rejects unsupported/singular/nonfinite projections or invalid source plans, and publishes no partial output. It does not install those matrices into any renderer callback.

The extended standalone fixture passes **34,888 total assertions**. Additional checks prove radius-zero parity with legacy `triple_views`, source corners at clip edges, direct projection versus homogeneous correction (including corners behind the center eye), original near/far, off-axis center projections, both pixel-center-fix cases, displaced render eye and failed-output retention. Logs `curve-source-views-{build,build-final,conformance,conformance-final}.log`. Curve/triples CTest2/2 passes. These matrix assertions do not prove GPU admission, source target ownership, texture orientation or visual curve correction; those integration gates remain open.

Canonical source pin: private DBCE triple-screen toolkit **149cc66ff34feb773afc66c615f3bcddb2dac406**, its unmodified `Vector3d`, `DisplaySurface`, `TripleRigDefinition`, `TripleRigBuilder` and `CurvedPanelWarp` sources. The oracle compiles those files directly; it does not reimplement the formula. The normal native test uses the committed numeric TSV and needs neither that private checkout nor .NET. No toolkit source is vendored or mutated by this increment.

To regenerate deliberately from that exact source:

```powershell
dotnet build windows/tests/oracles/curved-warp/Oracle.csproj -c Release -maxcpucount:1 -p:TripleToolkitSource='<toolkit>/src/Dbce.TripleScreen.Core'
dotnet work/validation-20261102/curve-oracle/bin/net8.0/Oracle.dll
```

The second command writes270 rows to stdout; preserve the TSV's provenance comment when replacing its numeric rows. The oracle has no NuGet dependencies/sources and keeps outputs under ignored project work. It built with0 warnings/errors after correcting the initial intermediate-output/nullable project settings. Private logs `curve-oracle-build{,-final}.log`, `curve-warp-{build,build-final,conformance,ctest}.log` are under `work/validation-20261102/`. Only the standalone math target was built (jobs1); no native-runtime rebuild, renderer/device creation, game launch, rig lease or owner install occurred.
