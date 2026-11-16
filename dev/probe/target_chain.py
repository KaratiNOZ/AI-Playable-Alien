"""Read the Alien's target-manager records (brain +0x34a8) and the PathTarget inputs; no game calls."""
from pathlib import Path
exec(Path(__file__).with_name('camera_posture.py').read_text().split('name = b"FollowBehavior"')[0])

def srd(a, fmt="<I"):
    try:
        return rd(a, fmt)
    except OSError:
        return None

try:
    game = srd(base + 0x016f0c88 - 0x400000)
    player = srd(srd(srd(game + 8) + 0x80)) if game else None
    brain = srd(npc + 0x34a8)
    out = {'npc': hex(npc), 'player': hex(player) if player else None,
           'brain': hex(brain) if brain else None,
           'ai_flag_016f0c8c': srd(base + 0x016f0c8c - 0x400000, '<B'), 'records': {}}
    for off in (0x08, 0x0c, 0x10, 0x14, 0x18, 0x1c, 0x20):
        rec = srd(brain + off) if brain else 0
        if not rec:
            out['records'][hex(off)] = None
            continue
        t = srd(rec + 0x42c)
        r = {'rec': hex(rec), 'vt': srd(rec), 'refs': srd(rec + 0x14),
             'target': hex(t) if t is not None else None,
             'num_430': srd(rec + 0x430, '<i'), 'idx_434': srd(rec + 0x434, '<i'),
             'idx_438': srd(rec + 0x438, '<i')}
        if t:
            r['target_num_50'] = srd(t + 0x50, '<i')
            r['target_pos'] = srd(t + 0x70, '<3f')
            r['is_player'] = t == player
        out['records'][hex(off)] = r
    print(json.dumps(out, indent=1))
finally:
    k.CloseHandle(h)