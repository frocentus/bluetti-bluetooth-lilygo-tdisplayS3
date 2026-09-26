#!/usr/bin/env python3
"""Walk a Bluetti power station's whole register space through the bridge.

The bridge polls only the blocks its field map names, three seconds apart. This
asks it for one 10-register block at a time instead (GET /readRegs) and collects
the raw answers - which is how registers nobody has identified yet get found, as
the map can only name what somebody already recognised.

  BLUETTI_BRIDGE=http://192.168.178.232 python tools/sweep_regs.py
  ... --from 0x0b00 --to 0x0bff --dump settings.txt

The default range is the 0x0000-0x1fff address space bluetti-bt-lib sweeps
(pages 0x00-0x1f). Each block is one request that waits for the BLE answer, so the
full range is ~800 requests and takes a few minutes.

Writes a JSON dump (address -> hex, the shape bluetti-bt-lib's readall uses) and a
text hexdump, then prints every non-zero register - that list is the part worth
reading. Addresses the station refuses are recorded, not treated as failures.
"""
import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

BLOCK = 10  # SWEEP_QTY in BTooth.h
HEADER_RE = re.compile(r"page=0x([0-9a-fA-F]+) offset=0x([0-9a-fA-F]+) qty=(\d+) bytes=(\d+)")


def fetch(bridge, page, offset, timeout):
    query = urllib.parse.urlencode({"page": hex(page), "offset": hex(offset)})
    with urllib.request.urlopen("%s/readRegs?%s" % (bridge.rstrip("/"), query), timeout=timeout) as response:
        return response.status, response.read().decode("utf-8", "replace")


def parse_block(body):
    """-> (page, offset, qty, words, raw); words is None for a refused address.

    A refused address still gets an answer: a MODBUS exception (01 83 <code>),
    which is why this is not an error but a result worth recording.
    """
    match = HEADER_RE.search(body)
    if match is None:
        return None
    page, offset, qty = int(match.group(1), 16), int(match.group(2), 16), int(match.group(3))
    _, _, hexdump = body.partition("\n")
    raw = bytes.fromhex(re.sub(r"[^0-9a-fA-F]", "", hexdump))
    if len(raw) < 3 or raw[0] != 0x01 or raw[1] != 0x03:
        return page, offset, qty, None, raw
    data = raw[3:]
    words = [(data[i] << 8) | data[i + 1] for i in range(0, len(data) - 1, 2)]
    return page, offset, qty, words, raw


def sweep(args):
    first, last = args.first + (BLOCK - args.first % BLOCK) % BLOCK, args.last
    blocks, no_answer = [], []
    total = 0
    addr = first
    while addr <= last:
        page, offset = addr >> 8, addr & 0xFF
        if offset + BLOCK > 256:
            addr = (addr + 256) & ~0xFF  # the firmware refuses a block spanning two pages
            continue
        total += 1
        parsed = None
        for attempt in range(args.retries + 1):
            try:
                _, body = fetch(args.bridge, page, offset, args.timeout)
                parsed = parse_block(body)
            except Exception:
                parsed = None
                if attempt < args.retries:
                    time.sleep(0.2)
            if parsed is not None:
                break
        if parsed is None:
            no_answer.append(addr)
        else:
            blocks.append((addr, parsed[3], parsed[4]))
        if total % 25 == 0:
            print("  ... %d blocks, %d answered" % (total, len(blocks)), flush=True)
        addr += BLOCK

    register_hex = {"%06x" % a: raw.hex() for a, words, raw in blocks if words is not None}
    json.dump({"bridge": args.bridge, "first": "%06x" % first, "last": "%06x" % last,
               "blocks": register_hex, "no_answer": ["%06x" % a for a in no_answer]},
              open(args.out, "w"), indent=1)

    with open(args.dump, "w") as handle:
        for a, words, raw in blocks:
            if words is None:
                handle.write("%06x\trefused\t%s\n" % (a, raw.hex()))
                continue
            printable = "".join(chr(b) if 32 <= b < 127 else "." for b in raw[3:])
            handle.write("%06x\t%s\t%s\n" % (a, " ".join("%04x" % w for w in words), printable))

    non_zero = [(a + i, w) for a, words, _ in blocks if words for i, w in enumerate(words) if w]
    print("\nblocks asked %d, answered %d, refused %d, no answer %d"
          % (total, sum(1 for _, w, _ in blocks if w is not None),
             sum(1 for _, w, _ in blocks if w is None), len(no_answer)))
    print("registers seen %d, non-zero %d" % (len(register_hex) * BLOCK, len(non_zero)))
    print("wrote %s and %s" % (args.out, args.dump))
    print("\nnon-zero registers:")
    for a, w in non_zero:
        print("  0x%04x = %-6d 0x%04x" % (a, w, w))
    return 0


def selftest():
    page, offset, qty, words, raw = parse_block(
        "page=0x00 offset=0x0a qty=6 bytes=15\n"
        "0103 0c41 4332 3030 4d00 0000 0000 0000\n")
    assert (page, offset, qty) == (0, 0x0A, 6), (page, offset, qty)
    assert words[0] == 0x4143 and words[2] == 0x304D, words[:3]
    assert len(words) == 6, len(words)
    refused = parse_block("page=0x1f offset=0x00 qty=10 bytes=5\n0183 02 c1a3 \n")
    assert refused[3] is None and refused[4][:2] == b"\x01\x83", refused
    print("selftest ok: header parsing, word decoding, exception handling")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--bridge", default=os.environ.get("BLUETTI_BRIDGE", "http://bluetti.local"))
    parser.add_argument("--from", dest="first", type=lambda v: int(v, 0), default=0x0000)
    parser.add_argument("--to", dest="last", type=lambda v: int(v, 0), default=0x1FFF)
    parser.add_argument("--out", default="sweep.json")
    parser.add_argument("--dump", default="sweep.txt")
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--retries", type=int, default=1)
    parser.add_argument("--selftest", action="store_true")
    args = parser.parse_args()
    return selftest() if args.selftest else sweep(args)


if __name__ == "__main__":
    sys.exit(main())
