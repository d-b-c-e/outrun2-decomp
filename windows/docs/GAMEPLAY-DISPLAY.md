# Gameplay display at startup

F6 Cameras Advanced offers **Gameplay display**. It saves one next-start key
through the existing writer and failed-save rollback:

```ini
[Window]
MonitorDevicePath =
```

Empty or absent means the existing default: primary monitor for borderless,
Windows default placement for the bordered 1280x720 client, and the existing
primary-centered racing row for Separate monitors. No existing selection is
silently migrated from Tweaks WindowPositionX/Y, display number or coordinates.

An explicit choice stores the opaque UTF-8 Windows CCD monitorDevicePath. Native
startup reads active CCD target/source names and joins the source GDI name to
MONITORINFOEX.szDevice. It matches the saved path case-insensitively to exactly
one active, non-cloned target. Friendly name, dimensions and desktop coordinates
are choice labels only. Enumeration order, DISPLAYn and HMONITOR are never saved
as identities. The current topology is read once; no mode, primary, topology,
desktop position or driver setting is changed.

For an explicit available choice, borderless fills that monitor's full rectangle.
A bordered window uses the existing decoration calculation, centers in the
selected work area, and fits within it when smaller than the usual outer window.
The same startup snapshot supplies placement and resolution auto-detection;
an explicit game resolution still wins. Existing DPI awareness is unchanged.
Separate monitors passes a local copy of the rectangles to the existing shared
racing-row rule with the selected display as center. Equal left/right neighbors
are still required; extra displays in the row's band refuse the span. Refusal
keeps one window on the selected display and logs the reason. Original Windows
primary flags are never changed. This does not create three independent views.

A missing, ambiguous, cloned, unusable or unqueryable saved display falls back
to the existing default and logs the fallback. Its saved identity stays intact,
and F6 retains a Saved display (unavailable or ambiguous) choice until the user
explicitly chooses a replacement or Default placement. Cable/driver changes can
change device paths; no heuristic remapping is attempted. Choices reflect the
startup snapshot; restart after a topology change. Test processes ignore the
key and do not enumerate displays. Playback retains the existing owner-Window
presentation merge and disabled panel; recording uses ordinary presentation.

This implements the planned monitor-choice adaptation of WindowPositionX/Y,
not arbitrary corner placement. Wheelkit now offers a dynamic picker for the
same saved target identity; its 17 focused Core checks passed at main 9fbd3dc.
Rendered saving and actual window placement remain unqualified. A fixed
Display1/2 row would not preserve this identity contract and is excluded.

The affected ClangCL x64 Release desktop and new startup_monitor fixture build
at `bf37a264`; the single CTest passes all32 checks with zero skipped. Original
logs/result/JUnit and executable are in hub
`artifacts/outrun-startup-monitor-20261017/build-01`. The executable is23,963,648
bytes, SHA-256 `3E901074DAB24022E92EBC19EF21B113ABF04287BB7A3E50159B573EB474E37A`.
Owned children were quiescent, queries safe, environment restored and exact
lease released at2026-10-17T01:55:59.0523458Z, with literal lease absence. Direct
handoff to Gore preceded these docs. No live monitor query, window, game,
provider, UI or owner install occurred. The fixture covers reordered identities, case matching,
default/test policy, missing/duplicate/cloned/unusable identity refusal, negative
coordinates, alternate racing-row anchor and bounded bordered geometry. It
does not call a monitor/device/window API or qualify rendered F6 saving, actual
window placement, DPI transitions or runtime display behavior. Prior level-select
861, cache351, extraction782 and CD/course results remain retained without repeats.

Windows API references: [target device path](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-displayconfig_target_device_name),
[source GDI name](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/ns-wingdi-displayconfig_source_device_name),
[active path query](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-querydisplayconfig),
[monitor information](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getmonitorinfow).
