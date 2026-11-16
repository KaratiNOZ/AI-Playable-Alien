"""Read-only camera/posture transition capture; never sends input to the game."""
import ctypes as c
import json
import re
import struct
import sys
import time
from pathlib import Path

root = Path(__file__).resolve().parents[2]
log = (root / "AlienModUI.log").read_text(encoding="utf-8", errors="replace")
base = int(re.findall(r"AI.exe base ([0-9A-Fa-f]+)", log)[-1], 16)
npc = int(re.findall(r"alien found ([0-9A-Fa-f]{8})", log)[-1], 16)
k = c.WinDLL("kernel32", use_last_error=True)
k.OpenProcess.argtypes = [c.c_uint32, c.c_int, c.c_uint32]
k.OpenProcess.restype = c.c_void_p
k.ReadProcessMemory.argtypes = [c.c_void_p, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
k.CloseHandle.argtypes = [c.c_void_p]
h = k.OpenProcess(0x410, False, int(sys.argv[1]))
if not h:
    raise c.WinError(c.get_last_error())

def rd(a, fmt="<I"):
    n = struct.calcsize(fmt)
    buf = c.create_string_buffer(n)
    got = c.c_size_t()
    if not k.ReadProcessMemory(h, a, buf, n, c.byref(got)) or got.value != n:
        raise c.WinError(c.get_last_error())
    v = struct.unpack(fmt, buf.raw)
    return v[0] if len(v) == 1 else v

name = b"FollowBehavior"
hash_follow = sum(name[i % len(name)] << (i & 31) for i in range(256)) & 0xFFFFFFFF
rows = []
t0 = time.monotonic()
try:
    while time.monotonic() - t0 < float(sys.argv[2] if len(sys.argv) > 2 else 25):
        g = rd(rd(base + 0x016F0C88 - 0x400000) + 0x58)
        follow = rd(g + 0x1DC)
        current = rd(g + 0x1D8)
        arr = rd(follow + 0xBC)
        count = min(rd(follow + 0xC0), 256)
        b = next((p for i in range(count) if (p := rd(arr + i * 4)) and rd(p + 8) == hash_follow), 0)
        if b:
            mgr = rd(base + 0x01766A00 - 0x400000)
            pov = rd(mgr + 0x8C) if mgr else 0
            rows.append({"t": time.monotonic() - t0, "crouch": rd(npc + 0x2AF, "<B"),
                         "room": rd(npc + 0x1B0),
                         "render_room": rd(base + 0x01758140 - 0x400000),
                         "room_flags": rd(base + 0x01758144 - 0x400000, "<2B"),
                         "pov_zone": rd(pov + 0x330) if pov else None,
                         "npc": rd(npc + 0x70, "<3f"), "current": current, "follow": follow,
                         "bpos": rd(b + 0x1C, "<3f"), "base": rd(b + 0x1C0, "<3f"),
                         "offset": rd(b + 0x9C, "<3f"), "target": rd(b + 0xA8, "<3f"),
                         "campos": rd(current + 0x14, "<3f"),
                         "renderpos": rd(current + 0x3C, "<3f"),
                         "nextpos": rd(current + 0x64, "<3f")})
        time.sleep(0.004)
finally:
    k.CloseHandle(h)
    out = root / "build-ghidra" / "captures" / (re.sub(r"[^a-zA-Z0-9_-]", "_", sys.argv[3]) + ".json" if len(sys.argv) > 3 else "camera_posture.json")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(rows), encoding="utf-8")
    print(f"Captured {len(rows)} frames to {out}")
    changes = [i for i in range(1, len(rows)) if rows[i]["crouch"] != rows[i-1]["crouch"]]
    for i in changes:
        print("POSTURE CHANGE", rows[i]["t"], "->", rows[i]["crouch"])
        for j in range(max(0, i-2), min(len(rows), i+4)):
            print(json.dumps(rows[j]))
