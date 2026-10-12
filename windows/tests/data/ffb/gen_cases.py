import math, pathlib, subprocess, sys, hashlib
out = pathlib.Path(sys.argv[1]); exe = out / "signal-calculation.exe"
H = "dbce.outrun2006.calculation-input,1,legacy-defaults,60"
def row(i, speed, steer, rate, l1, l2, gear, flags, pedal, rough, water):
    f = lambda v: ("%.4f" % v).rstrip("0").rstrip(".") if isinstance(v, float) else str(v)
    return ",".join([str(i), str(i * 17), f(max(0.0, speed)), f(steer), f(rate), f(l1), f(l2), str(gear), str(flags), str(pedal), f(min(1.0, max(0.0, rough))), str(water)])
def case(rows): return "\n".join([H] + rows + ["complete,%d" % len(rows)]) + "\n"
cases = {}
# 0: the mod's own synthetic case (Test-SignalCalculation.ps1)
cases["synthetic-v1"] = case([row(i, 0.8 if i < 32 else 0.5, 0.2, 0.01, 15, 2, 2 if i < 20 else 3, 4096 if 32 <= i <= 36 else 0, 180, 0.8, 1) for i in range(48)])
# 1: wall hits by a speed drop (3% over 6 frames at speed), the cooldown, then a second hit after re-arm
r = []
for i in range(220):
    s = 0.9 if i < 40 else 0.82 if i < 150 else 0.74
    if 40 <= i < 43: s = 0.9 - 0.03 * (i - 39)
    if 150 <= i < 153: s = 0.82 - 0.03 * (i - 149)
    r.append(row(i, s, 0.15, 0.002, 10.0 if i < 100 else -10.0, 1.0, 4, 0, 200, 0.0, 0))
cases["crash-speed-drop"] = case(r)
# 2: the contact flag (state +8 bit 0x1000): onset, held, an edge inside the cooldown, a re-armed edge
r = []
for i in range(200):
    fl = 4096 if (40 <= i < 46) or (60 <= i < 62) or (140 <= i < 143) else 0
    r.append(row(i, 0.6, 0.1 * math.sin(i / 10), 0.003 * math.cos(i / 10), -14.0, 0.5, 3, fl | (0x200 if i % 7 == 0 else 0), 255, 0.0, 0))
cases["contact-edge"] = case(r)
# 3: gear shifts up and down while accelerating (the double-pulse thunk), one inside another's pulse
r = []
gears = [1] * 30 + [2] * 30 + [3] * 30 + [2] * 3 + [3] * 57 + [4] * 30
for i, g in enumerate(gears):
    r.append(row(i, min(1.0, 0.05 + i * 0.006), 0.05, 0.0, 3.0, 0.0, g, 0, 255, 0.0, 0))
cases["gear-shifts"] = case(r)
# 4: water at speed: splash bursts (roughness > 0.7, speed > 0.95), plus plain rough surfaces at lower speed
r = []
for i in range(160):
    rough, water = (0.9, 1) if i < 60 else (0.75, 1) if i < 100 else (0.85, 0)
    r.append(row(i, 0.97 if i < 80 else 0.6, -0.05, 0.0, 2.0, 0.0, 5, 0, 255, rough, water))
cases["splash-water"] = case(r)
# 5: drift (the slide past 12: slip chatter, grip loss), then idle with throttle at a standstill
r = []
for i in range(200):
    if i < 120: r.append(row(i, 0.7, 0.3, 0.01 * math.sin(i / 5), 20.0 + 15.0 * math.sin(i / 20), 5.0, 4, 0, 255, 0.0, 0))
    else: r.append(row(i, 0.02, 0.0, 0.0, 0.0, 0.0, 1, 0, int(120 + (i - 120) * 1.5), 0.0, 0))
cases["drift-idle"] = case(r)
# 6: a long mixed drive: steering waves, a speed ramp up and down, surfaces, gears, contact pulses
r = []
for i in range(800):
    t = i / 60.0
    s = min(1.05, 0.02 + i * 0.0012) if i < 560 else max(0.0, 0.69 - (i - 560) * 0.0028)
    steer = 0.6 * math.sin(t * 0.9) + 0.1 * math.sin(t * 4.3)
    rate = 0.6 * 0.9 / 60 * math.cos(t * 0.9)
    lat = 18.0 * math.sin(t * 0.9 + 0.4)
    g = min(6, 1 + int(s * 6))
    fl = 4096 if i in range(400, 403) or i in range(700, 704) else 0
    rough = 0.0 if (i // 150) % 3 == 0 else 0.45 if (i // 150) % 3 == 1 else 0.8
    r.append(row(i, s, steer, rate, lat, -lat * 0.1, g, fl, 255 if i < 560 else 0, rough, 1 if rough == 0.8 and s > 0.9 else 0))
cases["mixed-drive"] = case(r)
for name, text in cases.items():
    p = out / (name + ".csv"); p.write_bytes(text.encode("ascii"))
    if len(text) > 65536: sys.exit("%s too large for the fixture (%d)" % (name, len(text)))
    q = subprocess.run([str(exe), str(p)], capture_output=True)
    if q.returncode != 0: sys.exit("%s: fixture exit %d %s" % (name, q.returncode, q.stderr.decode(errors="replace")))
    (out / (name + ".expected")).write_bytes(q.stdout.replace(b"\r\n", b"\n"))   # the fixture's cout is text mode; its own comparison is LF
    print(name, "frames", text.count("\n") - 2, "observation lines", q.stdout.count(b"\n"), "sha", hashlib.sha256(q.stdout).hexdigest()[:16])
