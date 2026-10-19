"""The native controls consumer (controls_consumer.cpp) on its fixtures: what the decomp takes from OutRunDesktop.ini.

  py -3 -B test_controls_consumer.py <outrun_controls_consumer.exe> <tests/controls_consumer>
"""
import json, pathlib, subprocess, sys

exe, folder = str(pathlib.Path(sys.argv[1]).resolve()), pathlib.Path(sys.argv[2])
checks = failed = 0
ffb_capability = {"models": ["outrun-legacy@1", "outrun-producer@2"],
                  "wheelOutput": ["outrun-producer@2"], "legacy": "trace-only"}

def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError("duplicate native consumer key: " + key)
        result[key] = value
    return result
def check(ok, what):
    global checks, failed
    checks += 1
    if not ok: failed += 1; print("FAIL:", what)
def read(name):
    r = subprocess.run([exe, str(folder / name)], capture_output=True, text=True)
    check(r.returncode == 0, "%s: exit %d %s" % (name, r.returncode, r.stderr.strip()))
    native = json.loads(r.stdout, object_pairs_hook=unique_object)
    check(native["capability"].get("ffb") == ffb_capability, name + ": exact build-owned FFB capability")
    check(list(native["capability"]["ffb"]) == list(ffb_capability), name + ": FFB capability field order")
    return native

a = read("shim-hpattern.ini")
check(a["schema"] == "outrun.controls-consumer@1", "schema")
check(a["backend"] == {"text": "shim", "shim": True}, "Backend = shim (any case) selects the shim")
check(a["profile"]["ok"] and a["profile"]["error"] == "" and len(a["profile"]["sha256"]) == 64, "the profile is admitted")
s = a["section"]
check(s["schema"] == 1 and s["profile"] == "Fixture Rig" and s["revision"] == "fixture-1" and s["transmission"] == "H-pattern" and s["hpattern"], "the header lines")
bound = {b["action"]: b for b in a["bound"]}
check(bound["steer"]["delivered"] and bound["steer"]["why"] is None and bound["steer"]["binding"].startswith("axis 0 dev={d71b8350"), "steer delivered, its binding echoed")
check(bound["gear1"]["delivered"] and bound["throttle"]["delivered"], "gear1 and throttle delivered")
check(bound["clutch"]["delivered"] is False and bound["clutch"]["why"] == "OutRun 2006 has no clutch", "clutch bound but not delivered, with the native reason")
check(bound["reverse"]["delivered"] is False and "reverse" in bound["reverse"]["why"], "reverse not delivered")
check("FixtureUnknown" not in bound, "an unknown key is not a binding")
cap = a["capability"]
want = {"steer", "throttle", "brake", "shiftUp", "shiftDown", "confirm", "back", "start", "navUp", "navDown", "navLeft", "navRight", "camera",
        "gear1", "gear2", "gear3", "gear4", "gear5", "gear6", "horn"}
check(set(cap["delivered"]) == want, "the delivered set (%s)" % sorted(set(cap["delivered"]) ^ want))
check(set(cap["unsupported"]) == {"clutch", "handbrake", "neutral", "reverse", "gear7", "gear8", "select", "lookBack", "reset", "modMenu", "muteFfb"},
      "the unsupported set (%s)" % sorted(cap["unsupported"]))
check(len(cap["delivered"]) + len(cap["unsupported"]) == 31, "every contract action is either delivered or unsupported")
check(set(a["wheel"]) == {"sensitivity", "deadZone"} and abs(a["wheel"]["sensitivity"] - 1.25) < 1e-6 and abs(a["wheel"]["deadZone"] - 0.05) < 1e-6,
      "[Wheel] as the game reads it (floats: %s)" % a["wheel"])

b = read("native-duplicate.ini")
check(b["backend"] == {"text": "", "shim": False}, "no [Input]: the Native backend")
check(b["profile"]["ok"] is False and 'duplicate key "steer"' in b["profile"]["error"], "two [Controls] copies with one key refuse the shim profile")

print("controls consumer: %d checks, %d failed" % (checks, failed))
sys.exit(1 if failed else 0)
