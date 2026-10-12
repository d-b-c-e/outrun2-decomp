"""Display cadence from a run_perf.py --etw trace (zero 25531): how many vblanks each frame stayed on screen, from the
DxgKrnl vsync events, and the game's own Present calls, over the measured race window.

    py -3 -B present_cadence.py <result folder>        (reads trace.etl and perf.json there; writes cadence.json)

- The trace is dumped once with the Windows Performance Toolkit's xperf (-a dumper) to trace-dump.csv beside it.
- Scan-out: every DxgKrnl VSyncDPC event names the surface scanned out at that vblank (ScannedPhysicalAddress). A new
  address is a new frame on screen, so the vblanks between address changes are how long each frame was shown. At a
  steady 60 fps on a 180 Hz panel every frame is held 3 vblanks; a 4 or more is a frame shown late (a missed interval).
  The target is the one with the most address changes while the game runs (the game's display).
- Present calls: the game process's DXGI Present starts, their intervals in vblank periods (CPU-side cadence).
- The race window: from the game's first Present + measure_from to + seconds (perf.json), less 1 s at each end, as the
  harness measures from the window's appearance, which the first Present follows closely.
"""
import collections, json, pathlib, statistics, subprocess, sys

XPERF = pathlib.Path(r"C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\xperf.exe")


def dump(etl, out):
    if not out.exists():
        q = subprocess.run([str(XPERF), "-i", str(etl), "-o", str(out), "-a", "dumper"], capture_output=True, text=True, timeout=1800)
        if q.returncode != 0 or not out.exists(): raise RuntimeError("xperf dumper failed (%d): %s" % (q.returncode, (q.stdout + q.stderr)[-400:]))


def fields(line):
    return [f.strip() for f in line.rstrip("\r\n").split(",")]


def read(path):
    """Header field names per event type, then the rows of the event types used here."""
    header, rows, counts = {}, collections.defaultdict(list), collections.Counter()
    in_header = False
    wanted = lambda name: name.endswith("/VSyncDPC/win:Info") or name.endswith("/VSyncDPCMultiPlane/win:Info") or \
        name.startswith("Microsoft-Windows-DXGI/Present/") or \
        name.startswith("Microsoft-Windows-DXGI/GetFrameStatistics/")
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            s = line.strip()
            if s == "BeginHeader": in_header = True; continue
            if s == "EndHeader": in_header = False; continue
            if not s: continue
            v = fields(line)
            if in_header: header[v[0]] = v; continue
            counts[v[0]] += 1
            if wanted(v[0]): rows[v[0]].append(v)
    return header, rows, counts


def column(header, name, key):
    names = header.get(name, [])
    for i, n in enumerate(names):
        if n.lower() == key.lower(): return i
    return None


def histogram(values):
    c = collections.Counter(values)
    return {str(k): c[k] for k in sorted(c)}


def main():
    res = pathlib.Path(sys.argv[1]).resolve()
    receipt = json.loads((res / "perf.json").read_text(encoding="utf-8"))
    pid = receipt.get("pid")
    dump(res / "trace.etl", res / "trace-dump.csv")
    header, rows, counts = read(res / "trace-dump.csv")
    report = dict(pid=pid, eventTypes={k: n for k, n in counts.most_common() if "DxgKrnl" in k or "DXGI" in k}, problems=[])

    # The game's Present calls (DXGI Present start events of its process).
    presents = []
    mine = lambda v: len(v) > 2 and "(%s)" % pid in v[2].replace(" ", "")
    for v in rows.get("Microsoft-Windows-DXGI/Present/win:Start", []):
        if mine(v): presents.append(int(v[1]))
    presents.sort()
    if not presents: report["problems"].append("no DXGI Present events for pid %s" % pid); print(json.dumps(report, indent=1)); return 1
    start = presents[0] + int(1e6 * (float(receipt["plan"]["measureFrom"]) + 1))
    end = presents[0] + int(1e6 * (float(receipt["plan"]["measureFrom"]) + float(receipt["plan"]["seconds"]) - 1))
    report["windowUs"] = [start, end]

    # Vblanks per display: (time, what was scanned out). With multiplane overlays (this rig) the VSyncDPCMultiPlane
    # event names plane 0's present id per VidPnSourceId (VSyncDPC's ScannedPhysicalAddress then stays constant);
    # otherwise VSyncDPC's ScannedPhysicalAddress per VidPnTargetId.
    targets = collections.defaultdict(list)
    mp = "Microsoft-Windows-DxgKrnl/VSyncDPCMultiPlane/win:Info"
    if rows.get(mp):
        s_col, a_col = column(header, mp, "VidPnSourceId"), column(header, mp, "PresentIdOrPhysicalAddress[PlaneCount]")
        for v in rows[mp]:
            ts = int(v[1])
            if start <= ts <= end: targets[(mp, "source " + v[s_col])].append((ts, v[a_col].strip("[]").split(":")[0].strip()))
    else:
        for name, rs in rows.items():
            if not name.endswith("/VSyncDPC/win:Info"): continue
            t_col, a_col = column(header, name, "VidPnTargetId"), column(header, name, "ScannedPhysicalAddress")
            if a_col is None: report["problems"].append("%s has no ScannedPhysicalAddress field: %s" % (name, header.get(name))); continue
            for v in rs:
                ts = int(v[1])
                if start <= ts <= end: targets[(name, v[t_col] if t_col is not None else "?")].append((ts, v[a_col]))
    if not targets: report["problems"].append("no vsync events in the window"); print(json.dumps(report, indent=1)); return 1
    key = max(targets, key=lambda k: sum(1 for a, b in zip(targets[k], targets[k][1:]) if a[1] != b[1]))
    vs = targets[key]
    period = statistics.median(b[0] - a[0] for a, b in zip(vs, vs[1:]))
    held, run = [], 1
    for a, b in zip(vs, vs[1:]):
        if b[1] == a[1]: run += 1
        else: held.append(run); run = 1
    held = held[1:]   # the first run started before the window
    mode = collections.Counter(held).most_common(1)[0][0] if held else None
    seconds = (vs[-1][0] - vs[0][0]) / 1e6
    report["scanout"] = dict(event=key[0], target=key[1], vblanks=len(vs), periodUs=period, refreshHz=round(1e6 / period, 2),
                             frames=len(held), fps=round(len(held) / seconds, 2) if seconds else None, heldVblanks=histogram(held),
                             usual=mode, late=sum(1 for h in held if mode and h > mode), early=sum(1 for h in held if mode and h < mode),
                             latePerMinute=round(60 * sum(1 for h in held if mode and h > mode) / seconds, 2) if seconds else None,
                             longestHeldVblanks=max(held) if held else None)
    report["otherTargets"] = {"%s %s" % k: dict(vblanks=len(v), changes=sum(1 for a, b in zip(v, v[1:]) if a[1] != b[1]))
                              for k, v in targets.items() if k != key}
    # The longest holds: when they happened (seconds into the window) and for how many vblanks.
    holds, run, since = [], 1, vs[0][0]
    for a, b in zip(vs, vs[1:]):
        if b[1] == a[1]: run += 1
        else: holds.append((run, since)); run, since = 1, b[0]
    report["scanout"]["longestHolds"] = [dict(vblanks=h, atSeconds=round((t - start) / 1e6, 3)) for h, t in sorted(holds[1:], reverse=True)[:8]]

    # Present-call intervals in vblank periods.
    win = [t for t in presents if start <= t <= end]
    gaps = [b - a for a, b in zip(win, win[1:])]
    report["presentCalls"] = dict(count=len(win), intervalVblanks=histogram(round(g / period) for g in gaps),
                                  maxIntervalMs=round(max(gaps) / 1000, 2) if gaps else None,
                                  over20ms=sum(1 for g in gaps if g > 20000), over33ms=sum(1 for g in gaps if g > 33333))
    # The game's own GetFrameStatistics calls (DXGI logs each): presents against the refreshes they took, as the exe's
    # display cadence line counts them (zero 25625). The same source as that line, so a check of its arithmetic only.
    name = "Microsoft-Windows-DXGI/GetFrameStatistics/win:Info"
    cols = {k: column(header, name, k) for k in ("ReturnValue", "PresentCount", "PresentRefreshCount")}
    stats = []
    if None not in cols.values():
        for v in rows.get(name, []):
            ts = int(v[1])
            if mine(v) and start <= ts <= end and int(v[cols["ReturnValue"]], 0) == 0:
                stats.append((ts, int(v[cols["PresentCount"]], 0), int(v[cols["PresentRefreshCount"]], 0)))
    late = sum(1 for a, b in zip(stats, stats[1:]) if b[1] > a[1] and b[2] - a[2] > 3 * (b[1] - a[1]))
    report["frameStatistics"] = dict(samples=len(stats), presents=stats[-1][1] - stats[0][1] if len(stats) > 1 else 0,
                                     refreshes=stats[-1][2] - stats[0][2] if len(stats) > 1 else 0, samplesLate=late)
    (res / "cadence.json").write_text(json.dumps(report, indent=1), encoding="utf-8")
    s = report["scanout"]
    print("scan-out %s target %s: %.2f Hz, %d frames (%.2f fps); held vblanks %s; late %d (%.2f/min), early %d, longest %d" %
          (s["event"], s["target"], s["refreshHz"], s["frames"], s["fps"] or 0, s["heldVblanks"], s["late"], s["latePerMinute"] or 0, s["early"], s["longestHeldVblanks"] or 0))
    p = report["presentCalls"]
    print("present calls: %d, intervals in vblanks %s, max %.2f ms, >20 ms %d, >33 ms %d" % (p["count"], p["intervalVblanks"], p["maxIntervalMs"] or 0, p["over20ms"], p["over33ms"]))
    f = report["frameStatistics"]
    print("frame statistics (the game's own calls): %d samples, %d presents over %d refreshes, %d samples late" % (f["samples"], f["presents"], f["refreshes"], f["samplesLate"]))
    if report["problems"]: print("problems: %s" % report["problems"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
