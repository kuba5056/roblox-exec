#!/usr/bin/env python3
"""rbx-exec offset scraper.

Reads offsets/patterns.yaml and scans a live RobloxPlayerBeta.exe,
writing offsets.db keyed by sha256(exe). Run after every Roblox push.
"""
from __future__ import annotations
import argparse, hashlib, json, re, sys, pathlib

try:
    import yaml  # type: ignore
except Exception:
    sys.stderr.write("need pyyaml — nix-shell -p python3Packages.pyyaml\n")
    sys.exit(2)

HEX = re.compile(r"([0-9A-Fa-f]{2}|\?\?)")

def to_mask(pat: str):
    b = bytearray()
    m = bytearray()
    for tok in HEX.findall(pat):
        if tok == "??":
            b.append(0); m.append(ord("?"))
        else:
            b.append(int(tok, 16)); m.append(ord("x"))
    return bytes(b), bytes(m)

def scan(blob: bytes, needle: bytes, mask: bytes) -> list[int]:
    n = len(needle); hits = []
    for i in range(0, len(blob) - n + 1):
        for j in range(n):
            if mask[j] == ord("x") and blob[i+j] != needle[j]:
                break
        else:
            hits.append(i)
    return hits

def section_text(pe: bytes) -> tuple[int, int, bytes]:
    if pe[:2] != b"MZ": raise SystemExit("not PE")
    e_lfanew = int.from_bytes(pe[0x3c:0x40], "little")
    coff = e_lfanew + 4
    num_sections = int.from_bytes(pe[coff+2:coff+4], "little")
    opt_size = int.from_bytes(pe[coff+16:coff+18], "little")
    sec0 = coff + 20 + opt_size
    for i in range(num_sections):
        s = sec0 + i*40
        name = pe[s:s+8].rstrip(b"\x00")
        if name == b".text":
            vsize = int.from_bytes(pe[s+ 8:s+12], "little")
            va    = int.from_bytes(pe[s+12:s+16], "little")
            rsize = int.from_bytes(pe[s+16:s+20], "little")
            roff  = int.from_bytes(pe[s+20:s+24], "little")
            return va, vsize, pe[roff:roff+rsize]
    raise SystemExit("no .text")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--patterns", default="offsets/patterns.yaml")
    ap.add_argument("--db", default="offsets/offsets.db")
    args = ap.parse_args()

    pe = pathlib.Path(args.exe).read_bytes()
    sha = hashlib.sha256(pe).hexdigest()
    va, _, text = section_text(pe)

    cfg = yaml.safe_load(pathlib.Path(args.patterns).read_text())
    out = {"sha256": sha, "symbols": {}}
    for name, spec in cfg["signatures"].items():
        needle, mask = to_mask(spec["pattern"])
        hits = scan(text, needle, mask)
        if not hits:
            out["symbols"][name] = None
        elif len(hits) == 1:
            out["symbols"][name] = va + hits[0]
        else:
            out["symbols"][name] = {"ambiguous": [va + h for h in hits]}
    db = pathlib.Path(args.db)
    try:
        existing = json.loads(db.read_text())
    except Exception:
        existing = {}
    existing[sha] = out
    db.write_text(json.dumps(existing, indent=2))
    print(f"wrote {db} for sha {sha}")

if __name__ == "__main__":
    main()
