"""Read the live Alien locomotion state without changing game memory."""
import ctypes as c
import re
import json
import struct
import sys
import time
from pathlib import Path

pid = int(sys.argv[1])
snapshot_name = re.sub(r"[^a-zA-Z0-9_-]", "_", sys.argv[2]) if len(sys.argv) > 2 else None
snapshots = []
log = (Path(__file__).resolve().parents[1] / "AlienModUI.log").read_text(encoding="utf-8", errors="replace")
matches = re.findall(r"alien found ([0-9A-Fa-f]{8})", log)
if not matches:
    raise SystemExit("No Alien found in current log")
npc = int(matches[-1], 16)
k = c.WinDLL("kernel32", use_last_error=True)
k.OpenProcess.argtypes = [c.c_uint32, c.c_int, c.c_uint32]
k.OpenProcess.restype = c.c_void_p
k.ReadProcessMemory.argtypes = [c.c_void_p, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
k.CloseHandle.argtypes = [c.c_void_p]
h = k.OpenProcess(0x410, False, pid)
if not h:
    raise c.WinError(c.get_last_error())

def read(address, fmt):
    size = struct.calcsize(fmt)
    buf = c.create_string_buffer(size)
    got = c.c_size_t()
    if not k.ReadProcessMemory(h, address, buf, size, c.byref(got)) or got.value != size:
        raise c.WinError(c.get_last_error())
    values = struct.unpack(fmt, buf.raw)
    return values[0] if len(values) == 1 else values

try:
    for _ in range(5):
        loco = read(npc + 0x42a8, "<I")
        act = read(npc + 0x36c, "<I")
        mgr = read(npc + 0x728, "<I")
        if snapshot_name:
            snapshots.append({
                "time": time.time(), "npc": npc, "loco": loco, "action": act,
                "position": read(npc+0x70, "<3f"),
                "crouch": read(npc+0x2af, "<B"),
                "posture_flags": read(npc+0x3e2c, "<16B"),
                "loco_words": read(loco, "<128I"),
                "action_words": read(act, "<32I"),
                "fsm_words": read(npc+0x2cd0, "<208I"),
                "ctx_words": read(mgr, "<12I"),
            })
        print(f"npc={npc:08X} pos={read(npc+0x70, '<3f')} crouch={read(npc+0x2af, '<B')} "
              f"action={read(act+0x1a, '<B')} flags={read(act+8, '<I'):X} "
              f"loco={loco:08X} request={read(loco, '<2f')} fsm={read(npc+0x2cd0, '<12I')} modes={read(loco+8, '<2I')} speed/yaw={read(loco+0x10, '<5f')} "
              f"ctx={read(mgr+0x10, '<I'):08X}", flush=True)
        time.sleep(0.25)
finally:
    k.CloseHandle(h)
    if snapshot_name and snapshots:
        outdir = Path(__file__).resolve().parents[1] / "build-ghidra" / "captures"
        outdir.mkdir(parents=True, exist_ok=True)
        destination = outdir / (snapshot_name + ".json")
        destination.write_text(json.dumps(snapshots, indent=2), encoding="utf-8")
        print("Saved", destination)
