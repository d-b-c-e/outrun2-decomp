"""Strict reader for controls-journal.jsonl (windows/docs/CONTROL-JOURNAL.md section 7). Prints a JSON verdict.

`qualifies` means a structurally valid, complete journal; for a latched journal also exactly one terminal RunChanged
event. Completeness is not success: a complete journal whose terminal is `failed:` is a complete failed record.
Qualifying a test additionally needs the observer, the host report and the supervisor (the three-fact rule).
"""
import json, sys

LINE_BYTES = 8192
TERMINAL = {"completed", "expired", "failed", "inert"}
IDENTITY = ("v", "pid", "created", "profileSha256", "build")
COUNTERS = ("statusDropped", "statusCoalesced", "eventsDropped", "controlEventsDropped", "batchesOffered",
            "identicalSkipped", "emptyDrainsSkipped")


def read(path):
    v = {"complete": False, "incomplete": True, "torn": False, "qualifies": False, "errors": [], "terminal": 0,
         "terminalDetail": None, "endReason": None, "devices": None, "events": 0, "statuses": 0, "unavailable": [],
         "reasons": [], "deviceNames": [], "emptySkipped": None}
    err = v["errors"].append
    try:
        data = open(path, "rb").read()
    except OSError as e:
        err("unreadable: %s" % e)
        return v
    parts = data.split(b"\n")
    if parts and parts[-1] != b"":
        v["torn"] = True          # a torn last line can only follow a failed write; the journal is incomplete
    rows = []
    for n, raw in enumerate(parts[:-1]):
        if len(raw) + 1 > LINE_BYTES:
            err("line %d above %d bytes" % (n, LINE_BYTES))
            return v
        try:
            rows.append(json.loads(raw.decode("utf-8", errors="strict")))
        except (UnicodeDecodeError, ValueError) as e:
            err("line %d is not strict UTF-8 JSON: %s" % (n, e))
            return v
    if not rows or rows[0].get("type") != "header":
        err("no header first")
        return v
    head = rows[0]
    latched = head.get("mode") == "latched"
    ident = {k: head.get(k) for k in IDENTITY}
    if "inputSource" in head:
        if head["inputSource"] != "external-physical-input@1":
            err("unsupported physical input source")
            return v
        ident["inputSource"] = head["inputSource"]
    if any(ident[k] is None for k in IDENTITY) or (latched and not head.get("nonce")):
        err("identity fields missing from the header")
        return v
    for n, r in enumerate(rows):
        if not isinstance(r, dict) or r.get("v") != 1:
            err("line %d is not a v1 object" % n)
            return v
        if r.get("seq") != n:
            err("seq gap or reorder at line %d" % n)
            return v
        if any(r.get(k) != ident[k] for k in ident) or (latched and r.get("nonce") != head.get("nonce")):
            err("identity differs at line %d" % n)
            return v
    devices = head.get("devices")
    v["devices"] = devices
    if not isinstance(devices, int) or devices < 0 or devices > 31:
        err("device count invalid")
        return v
    for d in range(devices):
        r = rows[1 + d] if 1 + d < len(rows) else {}
        if r.get("type") != "device" or r.get("index") != d:
            err("device row %d missing" % d)
            return v
        v["deviceNames"].append(r.get("name", "").encode("utf-8").hex())
    begun, last_epoch, last_counters, ended, drain = set(), {}, None, None, 0
    pending_status = []

    def settle_drain():
        for s in pending_status:
            if s["epoch"] != 0 and (s["device"], s["epoch"]) not in begun:
                err("%s epoch %d of device %d has no EpochBegin in this or an earlier drain" % (s["what"], s["epoch"], s["device"]))
        pending_status.clear()

    for n, r in enumerate(rows[1 + devices:], start=1 + devices):
        t = r.get("type")
        if ended is not None:
            err("line %d after the end" % n)
            return v
        d = r.get("drain")
        if t == "startline":
            if d != 0:
                err("start-line row outside the header")
            continue
        if not isinstance(d, int) or d < drain:
            err("drain number decreases at line %d" % n)
            return v
        if d != drain:
            settle_drain()
            drain = d
        if t == "event":
            v["events"] += 1
            dev, ep = r.get("device"), r.get("epoch")
            if r.get("kindName") == "epoch-begin":
                if ep <= last_epoch.get(dev, 0):
                    err("epoch of device %s does not increase at line %d" % (dev, n))
                last_epoch[dev] = ep
                begun.add((dev, ep))
            elif isinstance(ep, int) and ep != 0:
                # Every other epoch-bearing event (epoch end, read pending, ...) needs its epoch begun too (Astra 16397).
                pending_status.append({"device": dev, "epoch": ep, "what": "event"})
            if r.get("kindName") == "run-changed" and str(r.get("detail", "")).split(":")[0] in TERMINAL:
                v["terminal"] += 1
                v["terminalDetail"] = r.get("detail")
            if r.get("kindName") == "batch-refused" and len(v["reasons"]) < 4:
                v["reasons"].append(str(r.get("detail", "")).encode("utf-8").hex())
        elif t == "status":
            v["statuses"] += 1
            dev = r.get("device")
            if not isinstance(dev, int) or dev < -1 or dev >= devices:
                err("status device index invalid at line %d" % n)
            pending_status.append({"device": dev, "epoch": r.get("epoch", 0), "what": "status"})
            if r.get("of") == "action" and len(v["reasons"]) < 2:
                v["reasons"].append(str(r.get("reason", "")).encode("utf-8").hex())
        elif t == "counters":
            vals = [r.get(k) for k in COUNTERS]
            if any(not isinstance(x, int) for x in vals):
                err("counter missing at line %d" % n)
            elif last_counters is not None and any(a < b for a, b in zip(vals, last_counters)):
                err("a counter decreased at line %d" % n)
            last_counters = vals
            v["emptySkipped"] = r.get("emptyDrainsSkipped")
            for k in ("recorderOverruns", "observerPublished"):
                x = r.get(k)
                if isinstance(x, dict) and x.get("available") is False:
                    if k not in v["unavailable"]:
                        v["unavailable"].append(k)
                elif not isinstance(x, int):
                    err("%s neither a value nor explicitly unavailable" % k)
        elif t == "end":
            ended = r
        else:
            err("unknown line type %r at line %d" % (t, n))
    settle_drain()
    if ended is None:
        return v
    v["endReason"] = ended.get("reason")
    if ended.get("lines") != len(rows):
        err("end line count differs")
    tail = len(parts[-2]) + 1 if len(parts) >= 2 else 0
    if ended.get("bytes") != len(data) - tail:
        err("end byte count differs")
    v["complete"] = ended.get("complete") is True and not v["torn"]
    v["incomplete"] = not v["complete"]
    if latched and v["terminal"] != 1:
        err("a latched journal needs exactly one terminal RunChanged event, found %d" % v["terminal"])
    v["qualifies"] = v["complete"] and not v["errors"]
    return v


if __name__ == "__main__":
    verdict = read(sys.argv[1])
    print(json.dumps(verdict))
    sys.exit(0 if verdict["qualifies"] else 1)
