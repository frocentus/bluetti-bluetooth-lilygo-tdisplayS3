"""Fetch and decode the raw page-0 register dump exposed by GET /rawPage.

Used to reason about the Bluetti field map directly instead of inferring it from
published values. Captures are cached per label so two states can be compared.

    python tools/decode_page.py fetch baseline      # capture the current state
    python tools/decode_page.py show  baseline      # print the register table
    python tools/decode_page.py diff  baseline both # only what changed

Registers are addressed by their protocol offset (0x0A + n). The names/scales
below come from Device_AC200M.h, which is community-sourced and known to be
imperfect - that is the point of being able to see the raw page.
"""
import json
import os
import re
import sys
import tempfile
import time
import urllib.request

# The bridge's address comes from the environment so a public checkout carries no
# private network details:  export BLUETTI_BRIDGE=http://192.168.1.50
_bridge = os.environ.get("BLUETTI_BRIDGE", "")
if not _bridge:
    raise SystemExit("set BLUETTI_BRIDGE, e.g. BLUETTI_BRIDGE=http://192.168.1.50")
HOST = _bridge if _bridge.startswith("http") else "http://" + _bridge

# offset -> (name, divisor) straight from the AC200M device table
KNOWN = {
    0x24: ("dc_input_power", 1),
    0x25: ("ac_input_power", 1),
    0x26: ("ac_output_power", 1),
    0x27: ("dc_output_power", 1),
    0x29: ("power_generation", 10),
    0x2B: ("total_battery_percent", 1),
    0x30: ("ac_output_on", 1),
    0x31: ("dc_output_on", 1),
    0x47: ("internal_ac_voltage", 10),
    0x4A: ("internal_ac_frequency", 10),
    0x4D: ("ac_input_voltage", 10),
    0x56: ("internal_dc_input_voltage", 10),
    0x5B: ("pack_max_num", 1),
    0x5C: ("internal_pack_voltage", 100),
}


def cache_path(label):
    return os.path.join(tempfile.gettempdir(), "rawpage-%s.json" % label)


def fetch():
    last = None
    for _ in range(8):
        try:
            body = urllib.request.urlopen(HOST + "/rawPage", timeout=25).read().decode()
            head = re.search(r"offset=0x([0-9a-f]+) bytes=(\d+)", body)
            if not head:
                raise ValueError("unexpected response: %r" % body[:80])
            offset = int(head.group(1), 16)
            hexblob = "".join(re.findall(r"\b[0-9a-f]{2,}\b", body.split("\n", 2)[2]))
            frame = bytes.fromhex(hexblob)
            # The endpoint returns the frame verbatim, including the 3 byte
            # header (prefix, command, declared length); register data starts
            # after it. Getting this wrong shifts every value by 1.5 registers
            # and still produces plausible-looking numbers.
            if len(frame) < 4 or frame[0] != 0x01 or frame[1] != 0x03:
                raise ValueError("not a 01/03 frame: %s" % frame[:4].hex())
            return {"offset": offset, "header": list(frame[:3]),
                    "data": list(frame[3:]), "when": time.time()}
        except Exception as exc:
            last = exc
            time.sleep(5)
    raise SystemExit("could not fetch /rawPage: %r" % last)


def reg(cap, off):
    k = (off - cap["offset"]) * 2
    d = cap["data"]
    if k < 0 or k + 1 >= len(d):
        return None
    return (d[k] << 8) | d[k + 1]


def anchors_ok(cap):
    """Independent fields that must look right for the decode to be trusted."""
    d = cap["data"]
    if len(d) < 2 or bytes(d[0:2]) != b"AC":
        return False, "device_type=%r" % bytes(d[0:7])
    pack = reg(cap, 0x5C)
    if pack is None or not 4000 <= pack <= 6000:
        return False, "pack=%s" % pack
    return True, "device_type=AC200M pack=%.2fV" % (pack / 100)


def load(label):
    with open(cache_path(label)) as fh:
        return json.load(fh)


def show(cap):
    d = cap["data"]
    span = cap["offset"] + (len(d) // 2) - 1
    print("header=%s page 0x00 offset=0x%02X %d data bytes -> registers 0x%02X..0x%02X"
          % (bytes(cap["header"]).hex(" "), cap["offset"], len(d), cap["offset"], span))
    ok, why = anchors_ok(cap)
    print("anchors: %s  (%s)" % ("OK" if ok else "FAILED - do not trust this decode", why))
    if not ok:
        return

    print("\n== named AC200M fields ==")
    for off, (name, div) in sorted(KNOWN.items()):
        r = reg(cap, off)
        if r is None:
            continue
        print("  0x%02X %-26s raw=%-6d %s" % (off, name, r, "%.2f" % (r / div)))

    print("\n== every non-zero register ==")
    for off in range(cap["offset"], span + 1):
        r = reg(cap, off)
        if r:
            print("  0x%02X = %-6d /100 = %-8.2f /10 = %.2f" % (off, r, r / 100, r / 10))


def diff(a, b):
    da, db = a["data"], b["data"]
    for cap, name in ((a, sys.argv[2]), (b, sys.argv[3])):
        ok, why = anchors_ok(cap)
        if not ok:
            print("WARNING: capture '%s' failed anchors (%s)" % (name, why))
    span = min(a["offset"] + len(da) // 2, b["offset"] + len(db) // 2)
    print("registers that changed (0x%02X..0x%02X):" % (a["offset"], span))
    changed = 0
    for off in range(a["offset"], span + 1):
        ra, rb = reg(a, off), reg(b, off)
        if ra == rb:
            continue
        changed += 1
        name = KNOWN.get(off, ("?", 1))[0]
        print("  0x%02X %-26s %-6s -> %-6s" % (off, name, ra, rb))
    if not changed:
        print("  nothing changed")


def regs(cap, start, end):
    ok, why = anchors_ok(cap)
    print("anchors: %s (%s)" % ("OK" if ok else "FAILED - do not trust", why))
    for off in range(start, end + 1):
        r = reg(cap, off)
        name = KNOWN.get(off, ("", 1))[0]
        if r is None:
            print("  0x%02X %-24s (outside this capture)" % (off, name))
            continue
        print("  0x%02X %-24s raw=%-6d /100=%-8.2f /10=%.2f" % (off, name, r, r / 100, r / 10))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return
    cmd = sys.argv[1]
    if cmd == "fetch":
        label = sys.argv[2]
        cap = fetch()
        with open(cache_path(label), "w") as fh:
            json.dump(cap, fh)
        print("captured '%s': %d bytes, offset 0x%02X" % (label, len(cap["data"]), cap["offset"]))
        show(cap)
    elif cmd == "show":
        show(load(sys.argv[2]))
    elif cmd == "diff":
        diff(load(sys.argv[2]), load(sys.argv[3]))
    elif cmd == "regs":
        regs(load(sys.argv[2]), int(sys.argv[3], 0), int(sys.argv[4], 0))
    else:
        print(__doc__)


main()
