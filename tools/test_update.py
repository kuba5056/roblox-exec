#!/usr/bin/env python3
"""Smoke test for tools/update.py's scan + mask machinery."""
import sys, pathlib
sys.path.insert(0, str(pathlib.Path(__file__).parent))
from update import to_mask, scan

PATTERNS = {
    "luau_load": "48 89 5C 24 ?? 48 89 74 24 ?? 57 48 83 EC ?? 49 8B F0",
    "lua_settop": "48 8B 41 ?? 4C 8B C2",
}

def test_hit():
    blob = bytearray(b"\x90" * 0x400)
    blob += bytes.fromhex("48895C2408488974241057488 3EC40498BF0".replace(" ", ""))
    blob += b"\xCC" * 0x100
    needle, mask = to_mask(PATTERNS["luau_load"])
    hits = scan(bytes(blob), needle, mask)
    assert hits == [0x400], f"expected [0x400], got {hits}"
    print(f"PASS: hit at {hex(hits[0])}")

def test_miss():
    blob = b"\x00" * 1024
    needle, mask = to_mask(PATTERNS["luau_load"])
    hits = scan(blob, needle, mask)
    assert hits == [], f"expected miss, got {hits}"
    print("PASS: miss returns []")

def test_wildcard():
    blob = bytearray(b"\x90" * 32)
    blob += bytes.fromhex("488B4108" + "4C8BC2")  # lua_settop shape
    needle, mask = to_mask(PATTERNS["lua_settop"])
    hits = scan(bytes(blob), needle, mask)
    assert hits == [32], f"wildcard failed: {hits}"
    print(f"PASS: wildcard byte at offset {hex(hits[0])}")

if __name__ == "__main__":
    test_hit()
    test_miss()
    test_wildcard()
    print("all green")
