"""A test's own Forza listener (desktop telemetry, source/telemetry_forza): records every packet on 127.0.0.1:<port> and,
optionally, polls the named shared-memory block, until a stop file appears or the time runs out. Never port 8000
(SimHub and the MOZA dash listen there): the game is pointed here with [Telemetry] Port / OR2_TELEMETRY_PORT.

    py -3 -B telemetry_listen.py --port 18765 --shm OutRun2006Telemetry-test --seconds 150 --out <folder> [--stop <file>]

Writes packets.tsv (receive time, length, IsRaceOn, game packet ms, Speed m/s, Gear, Accel, Brake, signed Steer,
local AccX, RPM, sentinel), shared.tsv and
summary.json (counts per length, IsRaceOn transitions, maxima, the shared block's last values).
"""
import argparse, json, mmap, pathlib, socket, struct, sys, time

SHM_FIELDS = ("version", "packetId", "speed", "steeringAngle", "lateralG1", "lateralG2", "impactForce", "gear", "prevGear",
              "stateFlags", "carFlags", "s0", "s1", "s2", "s3", "vibrationLeft", "vibrationRight", "gameMode", "isInGameplay")
SHM_FORMAT = "<II5f4I4I2fIB3x"   # OutRun2006TelemetryData, 76 bytes


def decode(p):
    """Horizon 324: dash at 244; FM7 311: dash at 232 (Speed +12, Accel +71, Brake +72, Gear +75)."""
    dash = 244 if len(p) == 324 else 232 if len(p) == 311 else None
    if dash is None: return None
    race_on, packet_ms = struct.unpack_from("<iI", p, 0)
    rpm, = struct.unpack_from("<f", p, 16)
    acc_x, = struct.unpack_from("<f", p, 20)
    speed, = struct.unpack_from("<f", p, dash + 12)
    steer, = struct.unpack_from("<b", p, dash + 76)
    return dict(raceOn=race_on, packetMs=packet_ms, speed=speed, gear=p[dash + 75], accel=p[dash + 71],
                brake=p[dash + 72], steer=steer, accX=acc_x, rpm=rpm,
                sentinel=chr(p[323]) if len(p) == 324 else "")


def main():
    a = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    a.add_argument("--port", type=int, required=True)
    a.add_argument("--shm", help="the shared-memory name to poll (the game's [Telemetry] SharedMemName)")
    a.add_argument("--seconds", type=float, default=150.0)
    a.add_argument("--out", type=pathlib.Path, required=True)
    a.add_argument("--stop", type=pathlib.Path, help="stop when this file exists")
    o = a.parse_args()
    if o.port == 8000: sys.exit("refused: port 8000 is SimHub's / the MOZA dash's")
    o.out.mkdir(parents=True, exist_ok=False)
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM); s.bind(("127.0.0.1", o.port)); s.settimeout(0.05)
    shm = mmap.mmap(-1, 76, tagname=o.shm) if o.shm else None   # opens the game's block, or creates it first
    t0 = time.time(); end = t0 + o.seconds
    lengths, transitions, last_on, maxima, n = {}, [], None, dict(speed=0.0, rpm=0.0, gear=0, accel=0, brake=0), 0
    shared_last, shared_n, next_shm = None, 0, t0
    with open(o.out / "packets.tsv", "w", encoding="utf-8") as pf, open(o.out / "shared.tsv", "w", encoding="utf-8") as sf:
        pf.write("t\tlength\traceOn\tpacketMs\tspeed\tgear\taccel\tbrake\tsteer\taccX\trpm\tsentinel\n")
        sf.write("t\t" + "\t".join(SHM_FIELDS) + "\n")
        while time.time() < end and not (o.stop and o.stop.exists()):
            try:
                p, _ = s.recvfrom(2048)
                t = round(time.time() - t0, 3); n += 1
                lengths[len(p)] = lengths.get(len(p), 0) + 1
                d = decode(p)
                if d:
                    if d["raceOn"] != last_on: transitions.append(dict(t=t, raceOn=d["raceOn"])); last_on = d["raceOn"]
                    for k in maxima: maxima[k] = max(maxima[k], d[k])
                    pf.write("%s\t%d\t%d\t%d\t%.3f\t%d\t%d\t%d\t%d\t%.6f\t%.0f\t%s\n" %
                             (t, len(p), d["raceOn"], d["packetMs"], d["speed"], d["gear"], d["accel"],
                              d["brake"], d["steer"], d["accX"], d["rpm"], d["sentinel"]))
            except socket.timeout:
                pass
            if shm and time.time() >= next_shm:
                next_shm = time.time() + 0.5
                v = struct.unpack(SHM_FORMAT, shm[:76])
                if v[0] == 1:
                    shared_n += 1; shared_last = dict(zip(SHM_FIELDS, v))
                    sf.write("%.3f\t" % (time.time() - t0) + "\t".join("%g" % x for x in v) + "\n")
    summary = dict(packets=n, startedEpochSeconds=t0, lengths={str(k): v for k, v in sorted(lengths.items())}, raceOnTransitions=transitions, maxima=maxima,
                   sharedSamples=shared_n, sharedLast=shared_last, seconds=round(time.time() - t0, 1))
    (o.out / "summary.json").write_text(json.dumps(summary, indent=1), encoding="utf-8")
    print(json.dumps(summary, indent=1))


if __name__ == "__main__":
    main()
