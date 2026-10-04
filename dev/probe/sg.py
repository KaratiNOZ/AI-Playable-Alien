# sg.py <имя...> — ShortGuid CATHODE так, как его считает игра (FUN_008bf7f0): SHA1 имени -> 16-байтный GUID (байты
# хеша переставлены словами: d12..15, d8..11, d4..7, d0..3) -> его hex заглавными с последнего байта -> SHA1 -> первые
# 4 байта (little-endian). Проверено: finished_spawning = DEF8F174 (так же пишет мод в лог).
import hashlib, struct, sys


def sg(name):
    d = hashlib.sha1(name.encode()).digest()
    g = d[12:16] + d[8:12] + d[4:8] + d[0:4]
    hexs = ''.join('%02X' % b for b in reversed(g))
    d2 = hashlib.sha1(hexs.encode()).digest()
    return struct.unpack('<I', d2[:4])[0]


if __name__ == '__main__':
    for n in sys.argv[1:]:
        print(n, '%08X' % sg(n))
