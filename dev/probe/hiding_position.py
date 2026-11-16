"""Read player/Alien positions and the game's last hiding target; no game calls."""
from pathlib import Path
exec(Path(__file__).with_name('camera_posture.py').read_text().split('name = b"FollowBehavior"')[0])
try:
    game = rd(base + 0x016f0c88 - 0x400000)
    player = rd(rd(rd(game + 8) + 0x80))
    print(json.dumps({'player': rd(player + 0x70, '<3f'),
                      'alien': rd(npc + 0x70, '<3f'),
                      'last_hiding_target': rd(base + 0x016b18a8 - 0x400000, '<3f')}))
finally:
    k.CloseHandle(h)
