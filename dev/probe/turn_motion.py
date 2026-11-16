"""Read-only actor/render-camera measurements; no screenshots or game input."""
from pathlib import Path
exec(Path(__file__).with_name('camera_posture.py').read_text().split('name = b"FollowBehavior"')[0])
import math

rows = []
t0 = time.monotonic()
try:
    while time.monotonic()-t0 < float(sys.argv[2] if len(sys.argv)>2 else 20):
        try:
            manager = rd(rd(base+0x016f0c88-0x400000)+0x58)
            cam = rd(manager+0x1d8)
            action = rd(npc+0x36c)
            world = rd(npc+0x400, '<16f')
            rows.append({'t':time.monotonic()-t0,
                         'body':rd(npc+0x174,'<f'),
                         'render_yaw':math.atan2(world[8],world[10]),
                         'pos':rd(npc+0x70,'<3f'), 'render_pos':world[12:15],
                         'primary':rd(action+0x1a,'<B') if action else -1,
                         'camera_pos':rd(cam+0x14,'<3f'),
                         'camera_rot':rd(cam+4,'<4f')})
        except OSError:
            pass  # transitions may invalidate an actor/camera; never write back
        time.sleep(.008)
finally:
    k.CloseHandle(h)
    out=root/'build-ghidra/captures/turn_motion.json'
    out.parent.mkdir(parents=True,exist_ok=True)
    out.write_text(json.dumps(rows),encoding='utf-8')
    turning=[r for r in rows if r['primary']==57]
    print(f'Captured {len(rows)} measurements, {len(turning)} during native turns: {out}')
    if turning:
        wrap=lambda a:(a+math.pi)%(2*math.pi)-math.pi
        print('Maximum actor/render yaw difference (deg):',
              max(abs(wrap(r['render_yaw']-r['body'])) for r in turning)*180/math.pi)
