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
import bisect, collections, json, pathlib, statistics, subprocess, sys

XPERF = pathlib.Path(r"C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\xperf.exe")


def dump(etl, out):
    if not out.exists():
        q = subprocess.run([str(XPERF), "-i", str(etl), "-o", str(out), "-a", "dumper"], capture_output=True, text=True, timeout=1800)
        if q.returncode != 0 or not out.exists(): raise RuntimeError("xperf dumper failed (%d): %s" % (q.returncode, (q.stdout + q.stderr)[-400:]))


# DxgKrnl events that carry the present id a later VSyncDPCMultiPlane names, with the field holding it. Logged in the
# presenting thread while its DXGI Present runs (independent flip / multiplane overlay), they tie a Present call to its
# id; which ones a driver emits varies, so every one found is tried.
PRESENT_ID_EVENTS = {"MMIOFlipMultiPlaneOverlay3": "PresentId[PlaneCount]", "MMIOFlipMultiPlaneOverlay": "FlipPresentId"}


def as_int(text):
    try: return int(text.strip().strip("[]").split(":")[0].strip(), 0)
    except ValueError: return None


def percentile(values, q):
    v = sorted(values)
    return v[min(len(v) - 1, int(round(q * (len(v) - 1))))] if v else None


def fields(line):
    return [f.strip() for f in line.rstrip("\r\n").split(",")]


def read(path):
    """Header field names per event type, then the rows of the event types used here."""
    header, rows, counts = {}, collections.defaultdict(list), collections.Counter()
    in_header = False
    wanted = lambda name: name.endswith("/VSyncDPC/win:Info") or name.endswith("/VSyncDPCMultiPlane/win:Info") or \
        any(name == "Microsoft-Windows-DxgKrnl/%s/win:Info" % e for e in PRESENT_ID_EVENTS) or \
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


def present_to_scanout(header, rows, mine, key, start, end):
    mp = "Microsoft-Windows-DxgKrnl/VSyncDPCMultiPlane/win:Info"
    if key[0] != mp: return dict(measured=False, why="no multiplane vsync events (needs VSyncDPCMultiPlane present ids)")
    s_col, a_col = column(header, mp, "VidPnSourceId"), column(header, mp, "PresentIdOrPhysicalAddress[PlaneCount]")
    shown = {}
    for v in rows[mp]:
        if "source " + v[s_col] != key[1]: continue
        i = as_int(v[a_col])
        if i is not None and i not in shown: shown[i] = int(v[1])
    # The game's Present calls with their threads: start / stop paired per thread in order.
    calls, open_ = [], {}
    for v in sorted(rows.get("Microsoft-Windows-DXGI/Present/win:Start", []) + rows.get("Microsoft-Windows-DXGI/Present/win:Stop", []), key=lambda v: int(v[1])):
        if not mine(v): continue
        tid, ts = v[3], int(v[1])
        if v[0].endswith("win:Start"): open_[tid] = ts
        elif tid in open_: calls.append((open_.pop(tid), ts, tid))
    by_thread = collections.defaultdict(list)
    for c in calls: by_thread[c[2]].append(c)
    found, used = {}, collections.Counter()
    for name, field in PRESENT_ID_EVENTS.items():
        full = "Microsoft-Windows-DxgKrnl/%s/win:Info" % name
        col = column(header, full, field)
        if col is None: continue
        for v in rows.get(full, []):
            ts, tid = int(v[1]), v[3]
            for c in by_thread.get(tid, []):
                if c[0] <= ts <= c[1] and c[0] not in found:
                    i = as_int(v[col])
                    if i is not None: found[c[0]] = i; used[name] += 1
                    break
    lat = [shown[i] - t for t, i in found.items() if start <= t <= end and i in shown and shown[i] >= t]
    inwin = sum(1 for c in calls if start <= c[0] <= end)
    # A few chance ties (an id event of another context inside a call) are not a measurement: at least half the calls.
    if len(lat) < max(1, inwin // 2): return dict(measured=False, why="%d of %d calls tied to a shown present id (needs half)" % (len(lat), inwin), calls=inwin, tiedCalls=len(found), idEvents=dict(used))
    ms = [x / 1000 for x in lat]
    return dict(measured=True, calls=inwin, matched=len(lat), idEvents=dict(used), medianMs=round(statistics.median(ms), 2),
                p95Ms=round(percentile(ms, 0.95), 2), maxMs=round(max(ms), 2), minMs=round(min(ms), 2))


def whole_run(header, rows, key, first, usual):
    mp = "Microsoft-Windows-DxgKrnl/VSyncDPCMultiPlane/win:Info"
    if key[0] != mp or not usual: return dict(measured=False)
    s_col, a_col = column(header, mp, "VidPnSourceId"), column(header, mp, "PresentIdOrPhysicalAddress[PlaneCount]")
    vs = [(int(v[1]), v[a_col].strip("[]").split(":")[0].strip()) for v in rows[mp] if "source " + v[s_col] == key[1] and int(v[1]) >= first]
    holds, run, since = [], 1, vs[0][0] if vs else 0
    for x, y in zip(vs, vs[1:]):
        if y[1] == x[1]: run += 1
        else: holds.append((run, since)); run, since = 1, y[0]
    holds = holds[1:]
    long_ = [(h, t) for h, t in holds if h > usual]
    return dict(measured=True, frames=len(holds), held=histogram(min(h, 12) for h, _ in holds), longerThanUsual=len(long_),
                vblanksLost=sum(h - usual for h, _ in long_),
                longest=[dict(vblanks=h, atSeconds=round((t - first) / 1e6, 2)) for h, t in sorted(long_, reverse=True)[:12]])


def frame_statistics_latency(header, rows, mine, presents, period_us, start, end):
    """Present to scan-out from the game's own GetFrameStatistics calls (DXGI logs each): PresentCount names the swap
    chain's k-th Present (GetLastPresentCount's count), PresentRefreshCount the vblank that showed it, and SyncQPCTime the
    QPC time of vblank SyncRefreshCount. So present k was shown at SyncQPCTime - (Sync - PresentRefresh) periods. The
    k-th DXGI Present_Start of the game is the call (the trace starts before the launch, so it holds every Present).
    QPC is mapped to trace time from the VSyncDPC events' FrameQPCTime. Composed or independent flip alike."""
    vname = "Microsoft-Windows-DxgKrnl/VSyncDPC/win:Info"
    qcol = column(header, vname, "FrameQPCTime")
    pairs = [(int(v[1]), int(v[qcol], 0)) for v in rows.get(vname, []) if qcol is not None and v[qcol].strip()]
    if len(pairs) < 3: return dict(measured=False, why="no VSyncDPC FrameQPCTime to map QPC to trace time")
    (t1, q1), (t2, q2) = pairs[0], pairs[-1]
    slope = (t2 - t1) / (q2 - q1)                      # trace us per QPC tick
    to_us = lambda q: t1 + (q - q1) * slope
    mid = pairs[len(pairs) // 2]
    fit_error_us = abs(to_us(mid[1]) - mid[0])
    name = "Microsoft-Windows-DXGI/GetFrameStatistics/win:Info"
    c = {k: column(header, name, k) for k in ("ReturnValue", "PresentCount", "PresentRefreshCount", "SyncRefreshCount", "SyncQPCTime")}
    if None in c.values(): return dict(measured=False, why="GetFrameStatistics fields missing")
    period_q = period_us / slope
    shown, offsets = {}, []
    for v in rows.get(name, []):
        ts = int(v[1])
        if not mine(v) or int(v[c["ReturnValue"]], 0) != 0: continue
        k, prc, src, sq = (int(v[c[f]], 0) for f in ("PresentCount", "PresentRefreshCount", "SyncRefreshCount", "SyncQPCTime"))
        if k < 1 or k > len(presents) or src < prc: continue
        offsets.append(bisect.bisect_right(presents, ts) - k)   # presents made but not yet shown (>= 0 if k is the count)
        shown.setdefault(k, to_us(sq - (src - prc) * period_q))
    if not shown: return dict(measured=False, why="no GetFrameStatistics samples of the game")
    lo, hi = min(offsets), max(offsets)
    if lo < 0 or hi > 8:
        return dict(measured=False, why="PresentCount does not index the game's Present calls (offset %d..%d)" % (lo, hi))
    lat = [shown[k] - presents[k - 1] for k in shown if start <= presents[k - 1] <= end]
    if not lat: return dict(measured=False, why="no shown present in the window")
    ms = [x / 1000 for x in lat]
    return dict(measured=True, method="DXGI frame statistics (PresentCount -> its vblank)", matched=len(ms),
                calls=sum(1 for t in presents if start <= t <= end), queuedPresents="%d..%d" % (lo, hi), qpcFitErrorUs=round(fit_error_us, 1),
                medianMs=round(statistics.median(ms), 2), p95Ms=round(percentile(ms, 0.95), 2), maxMs=round(max(ms), 2), minMs=round(min(ms), 2),
                negative=sum(1 for x in ms if x < 0))


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

    # The whole run on the game's display, from its first Present (menus, car select, the race start included): the
    # frames held longer than usual, with their time since the first Present, to compare startup freezes (cold caches).
    report["wholeRun"] = whole_run(header, rows, key, presents[0], mode)

    # Present to scan-out: each Present call of the game, tied to its present id by an id event logged on the same thread
    # inside that call, against the first vblank whose plane 0 shows that id on the game's display. Measured only when
    # ids tie; otherwise reported as not measured (order matching is ambiguous by whole frames with a queue).
    report["presentToScanout"] = present_to_scanout(header, rows, mine, key, start, end)
    if not report["presentToScanout"].get("measured"):
        report["presentToScanout"] = dict(byPresentId=report["presentToScanout"], **frame_statistics_latency(header, rows, mine, presents, period, start, end))

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
    w = report["wholeRun"]
    if w.get("measured"): print("whole run from the first Present: %d frames, %d held longer than usual (%d vblanks lost); longest %s" %
                               (w["frames"], w["longerThanUsual"], w["vblanksLost"], ", ".join("%d@%.1fs" % (x["vblanks"], x["atSeconds"]) for x in w["longest"][:8])))
    q = report["presentToScanout"]
    if q.get("measured"): print("present to scan-out (%s): %d of %d calls: median %.2f ms, p95 %.2f, max %.2f, min %.2f; queued %s, QPC fit error %s us" %
                               (q.get("method", "present id"), q["matched"], q["calls"], q["medianMs"], q["p95Ms"], q["maxMs"], q["minMs"],
                                q.get("queuedPresents", "-"), q.get("qpcFitErrorUs", "-")))
    else: print("present to scan-out: not measured: %s" % q["why"])
    if report["problems"]: print("problems: %s" % report["problems"])
    return 0


if __name__ == "__main__":
    sys.exit(main())
