// gamefont.cpp — шрифты интерфейса самой игры для ImGui: Isolation (меню игры) и Jixellation (терминалы Seegson).
// Лежат в DATA/UI.PAK -> DATA/UI/FONTS_RU.GFX (Scaleform, несжатый SWF) как DefineFont3 — векторные глифы.
// Переводим их в минимальный TrueType прямо в памяти (glyf/loca/cmap/hmtx/...) и отдаём ImGui. Файлы шрифтов
// в папку мода не копируются — всё берётся из установленной игры при запуске.
// Формат: PAK2 = "PAK2", u32, u32 число файлов, u32; имена (с нулём); u32[n] — абсолютные концы файлов.
// DefineFont3: EM = 1024*20 единиц, y вниз; кривые — квадратичные, как в TrueType.
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Pt { int x, y; bool on; };
typedef std::vector<Pt> Contour;
struct GFont {
  std::string name;
  bool bold = false;
  std::vector<std::vector<Contour>> glyphs;
  std::vector<uint16_t> codes;
  int asc = 0, desc = 0, lead = 0;
  std::vector<int> adv;
};

struct Bits {
  const uint8_t* d;
  size_t n, p;
  int b = 0;
  bool bad = false;
  uint32_t ub(int k) {
    uint32_t v = 0;
    for (int i = 0; i < k; i++) {
      if (p >= n) return bad = true, 0;
      v = (v << 1) | ((d[p] >> (7 - b)) & 1);
      if (++b == 8) b = 0, p++;
    }
    return v;
  }
  int sb(int k) {
    uint32_t v = ub(k);
    if (k && (v & (1u << (k - 1)))) return int(v) - int(1u << k);
    return int(v);
  }
};

static uint16_t U16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
static uint32_t U32(const uint8_t* p) { return uint32_t(p[0] | (p[1] << 8) | (p[2] << 16) | (uint32_t(p[3]) << 24)); }

static bool ShapeContours(const uint8_t* d, size_t n, size_t p, std::vector<Contour>& out) {
  Bits bs{ d, n, p };
  const int nfill = int(bs.ub(4)), nline = int(bs.ub(4));
  int x = 0, y = 0;
  Contour cur;
  for (int guard = 0; guard < 100000; guard++) {
    if (bs.bad) return false;
    if (bs.ub(1) == 0) {
      const uint32_t flags = bs.ub(5);
      if (flags == 0) break;
      if (flags & 1) {
        const int k = int(bs.ub(5));
        x = bs.sb(k), y = bs.sb(k);
        if (!cur.empty()) out.push_back(cur), cur.clear();
        cur.push_back({ x, y, true });
      }
      if (flags & 2) bs.ub(nfill);
      if (flags & 4) bs.ub(nfill);
      if (flags & 8) bs.ub(nline);
      if (flags & 16) return false;  // новые стили внутри глифа — не бывает
    } else {
      const bool straight = bs.ub(1) != 0;
      const int k = int(bs.ub(4)) + 2;
      if (cur.empty()) cur.push_back({ x, y, true });
      if (straight) {
        int dx = 0, dy = 0;
        if (bs.ub(1)) dx = bs.sb(k), dy = bs.sb(k);
        else if (bs.ub(1)) dy = bs.sb(k);
        else dx = bs.sb(k);
        x += dx, y += dy;
        cur.push_back({ x, y, true });
      } else {
        const int cx = x + bs.sb(k), cy = y + bs.sb(k);
        x = cx + bs.sb(k), y = cy + bs.sb(k);
        cur.push_back({ cx, cy, false });
        cur.push_back({ x, y, true });
      }
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return !bs.bad;
}

static bool ParseFonts(const uint8_t* d, size_t n, std::vector<GFont>& fonts) {
  if (n < 32 || memcmp(d, "GFX", 3) != 0) return false;
  const int nbits = d[8] >> 3;
  size_t p = 8 + (5 + 4 * nbits + 7) / 8 + 4;
  while (p + 2 <= n) {
    const uint16_t cl = U16(d + p);
    p += 2;
    const uint32_t code = cl >> 6;
    uint32_t len = cl & 0x3f;
    if (len == 0x3f) {
      if (p + 4 > n) return false;
      len = U32(d + p), p += 4;
    }
    if (p + len > n) return false;
    if (code == 75) {  // DefineFont3
      const uint8_t* t = d + p;
      size_t q = 0;
      const uint8_t flags = t[2];
      const uint8_t nl = t[4];
      q = 5;
      GFont f;
      f.name.assign(reinterpret_cast<const char*>(t + q), nl);
      while (!f.name.empty() && f.name.back() == 0) f.name.pop_back();
      f.bold = flags & 1;
      q += nl;
      const uint16_t ng = U16(t + q);
      q += 2;
      const bool wide = flags & 0x08;
      const size_t tbl = q, sz = wide ? 4 : 2;
      auto off = [&](size_t i) -> uint32_t { return wide ? U32(t + tbl + i * 4) : U16(t + tbl + i * 2); };
      const uint32_t codeOff = off(ng);
      f.glyphs.resize(ng);
      for (size_t i = 0; i < ng; i++)
        if (!ShapeContours(t, len, tbl + off(i), f.glyphs[i])) return false;
      size_t cq = tbl + codeOff;
      for (size_t i = 0; i < ng; i++) f.codes.push_back(U16(t + cq + i * 2));
      cq += size_t(ng) * 2;
      f.adv.assign(ng, 1024 * 20);
      if (flags & 0x80) {
        f.asc = U16(t + cq), f.desc = U16(t + cq + 2), f.lead = int16_t(U16(t + cq + 4));
        cq += 6;
        for (size_t i = 0; i < ng; i++) f.adv[i] = int16_t(U16(t + cq + i * 2));
      }
      (void)sz;
      fonts.push_back(std::move(f));
    }
    p += len;
    if (code == 0) break;
  }
  return !fonts.empty();
}

// ---------------- TrueType ----------------
struct BE {
  std::vector<uint8_t> b;
  void u8(uint32_t v) { b.push_back(uint8_t(v)); }
  void u16(uint32_t v) { u8(v >> 8), u8(v); }
  void s16(int v) { u16(uint32_t(uint16_t(int16_t(v)))); }
  void u32(uint32_t v) { u16(v >> 16), u16(v & 0xffff); }
  void raw(const std::vector<uint8_t>& o) { b.insert(b.end(), o.begin(), o.end()); }
  void pad4() { while (b.size() % 4) u8(0); }
};
static uint32_t Checksum(const std::vector<uint8_t>& t) {
  uint32_t s = 0;
  for (size_t i = 0; i < t.size(); i += 4) {
    uint32_t v = 0;
    for (size_t k = 0; k < 4; k++) v = (v << 8) | (i + k < t.size() ? t[i + k] : 0);
    s += v;
  }
  return s;
}

static std::vector<uint8_t> BuildTtf(const GFont& f) {
  const int upem = 2048;
  const double k = upem / 20480.0;
  std::vector<size_t> order(f.codes.size());
  for (size_t i = 0; i < order.size(); i++) order[i] = i;
  std::sort(order.begin(), order.end(), [&](size_t a, size_t b) { return f.codes[a] < f.codes[b]; });
  BE glyf, hmtx;
  std::vector<uint32_t> loca{ 0 };
  int xmin = 32767, ymin = 32767, xmax = -32768, ymax = -32768, maxpts = 0, maxcont = 0, advMax = 0;
  hmtx.u16(upem / 2), hmtx.s16(0);  // .notdef
  loca.push_back(0);
  std::vector<std::pair<uint16_t, uint16_t>> cmap;
  uint16_t gi = 1;
  for (size_t i : order) {
    cmap.push_back({ f.codes[i], gi++ });
    std::vector<std::vector<Pt>> conts;
    for (const Contour& c : f.glyphs[i]) {
      std::vector<Pt> pts;
      for (const Pt& q : c) pts.push_back({ int(std::lround(q.x * k)), int(std::lround(-q.y * k)), q.on });
      if (pts.size() > 1 && pts.front().x == pts.back().x && pts.front().y == pts.back().y && pts.back().on) pts.pop_back();
      if (pts.size() >= 2) conts.push_back(pts);
    }
    const int adv = std::max(0, int(std::lround(f.adv[i] * k)));
    advMax = std::max(advMax, adv);
    if (conts.empty()) {
      hmtx.u16(adv), hmtx.s16(0);
      loca.push_back(uint32_t(glyf.b.size()));
      continue;
    }
    int gx0 = 32767, gy0 = 32767, gx1 = -32768, gy1 = -32768, npts = 0;
    for (auto& c : conts)
      for (auto& q : c) gx0 = std::min(gx0, q.x), gy0 = std::min(gy0, q.y), gx1 = std::max(gx1, q.x), gy1 = std::max(gy1, q.y), npts++;
    xmin = std::min(xmin, gx0), ymin = std::min(ymin, gy0), xmax = std::max(xmax, gx1), ymax = std::max(ymax, gy1);
    maxpts = std::max(maxpts, npts), maxcont = std::max(maxcont, int(conts.size()));
    glyf.s16(int(conts.size())), glyf.s16(gx0), glyf.s16(gy0), glyf.s16(gx1), glyf.s16(gy1);
    int e = -1;
    for (auto& c : conts) e += int(c.size()), glyf.u16(uint32_t(e));
    glyf.u16(0);  // инструкций нет
    for (auto& c : conts)
      for (auto& q : c) glyf.u8(q.on ? 1 : 0);
    int px = 0, py = 0;
    for (auto& c : conts)
      for (auto& q : c) glyf.s16(q.x - px), px = q.x;
    for (auto& c : conts)
      for (auto& q : c) glyf.s16(q.y - py), py = q.y;
    glyf.pad4();
    hmtx.u16(adv), hmtx.s16(gx0);
    loca.push_back(uint32_t(glyf.b.size()));
  }
  const int ng = int(order.size()) + 1;
  const int asc = f.asc ? int(std::lround(f.asc * k)) : upem * 8 / 10, desc = f.desc ? int(std::lround(f.desc * k)) : upem / 5;
  if (xmin > xmax) xmin = ymin = 0, xmax = ymax = upem;

  BE head;
  head.u32(0x00010000), head.u32(0x00010000), head.u32(0), head.u32(0x5F0F3CF5), head.u16(0x000B), head.u16(upem);
  head.u32(0), head.u32(0), head.u32(0), head.u32(0);  // даты
  head.s16(xmin), head.s16(ymin), head.s16(xmax), head.s16(ymax);
  head.u16(f.bold ? 1 : 0), head.u16(8), head.s16(2), head.s16(1), head.s16(0);
  BE hhea;
  hhea.u32(0x00010000), hhea.s16(asc), hhea.s16(-desc), hhea.s16(int(std::lround(f.lead * k))), hhea.u16(uint32_t(advMax));
  hhea.s16(0), hhea.s16(0), hhea.s16(xmax), hhea.s16(1), hhea.s16(0), hhea.s16(0);
  for (int i = 0; i < 5; i++) hhea.s16(0);
  hhea.u16(uint32_t(ng));
  BE maxp;
  maxp.u32(0x00010000), maxp.u16(uint32_t(ng)), maxp.u16(uint32_t(maxpts)), maxp.u16(uint32_t(maxcont));
  maxp.u16(0), maxp.u16(0), maxp.u16(2);
  for (int i = 0; i < 8; i++) maxp.u16(0);
  // cmap формат 4
  struct Seg { uint16_t a, b, g; };
  std::vector<Seg> segs;
  for (auto& cg : cmap) {
    if (!segs.empty() && cg.first == segs.back().b + 1 && cg.second == segs.back().g + (cg.first - segs.back().a)) segs.back().b = cg.first;
    else if (segs.empty() || cg.first != segs.back().b) segs.push_back({ cg.first, cg.first, cg.second });
  }
  segs.push_back({ 0xFFFF, 0xFFFF, 0 });
  const int nseg = int(segs.size());
  int pw = 1, es = 0;
  while (pw * 2 <= nseg) pw *= 2, es++;
  BE sub;
  sub.u16(4), sub.u16(0), sub.u16(0), sub.u16(uint32_t(2 * nseg)), sub.u16(uint32_t(2 * pw)), sub.u16(uint32_t(es)),
      sub.u16(uint32_t(2 * nseg - 2 * pw));
  for (auto& sg : segs) sub.u16(sg.b);
  sub.u16(0);
  for (auto& sg : segs) sub.u16(sg.a);
  for (auto& sg : segs) sub.u16(sg.a == 0xFFFF ? 1 : uint32_t(uint16_t(sg.g - sg.a)));
  for (int i = 0; i < nseg; i++) sub.u16(0);
  sub.b[2] = uint8_t(sub.b.size() >> 8), sub.b[3] = uint8_t(sub.b.size());
  BE cmapt;
  cmapt.u16(0), cmapt.u16(1), cmapt.u16(3), cmapt.u16(1), cmapt.u32(12);
  cmapt.raw(sub.b);
  BE locat;
  for (uint32_t o : loca) locat.u32(o);
  BE post;
  post.u32(0x00030000);
  for (int i = 0; i < 7; i++) post.u32(0);
  BE os2;
  os2.u16(1), os2.s16(upem / 2), os2.u16(f.bold ? 700 : 400), os2.u16(5), os2.u16(0);
  for (int i = 0; i < 11; i++) os2.s16(0);
  for (int i = 0; i < 26; i++) os2.u8(0);  // panose + unicode ranges
  os2.u8('N'), os2.u8('O'), os2.u8('N'), os2.u8('E');
  os2.u16(0x40), os2.u16(0x20), os2.u16(0xFFFF), os2.s16(asc), os2.s16(-desc), os2.s16(0), os2.u16(uint32_t(asc)), os2.u16(uint32_t(desc));
  os2.u32(1), os2.u32(0);
  std::vector<uint16_t> nm;
  for (char ch : f.name) nm.push_back(uint16_t(uint8_t(ch)));
  BE name;
  name.u16(0), name.u16(2), name.u16(6 + 24);
  for (int id : { 1, 4 }) name.u16(3), name.u16(1), name.u16(0x409), name.u16(uint32_t(id)), name.u16(uint32_t(nm.size() * 2)), name.u16(0);
  for (uint16_t ch : nm) name.u16(ch);

  struct Tab { const char* tag; std::vector<uint8_t>* d; };
  Tab tabs[] = { { "OS/2", &os2.b }, { "cmap", &cmapt.b }, { "glyf", &glyf.b }, { "head", &head.b }, { "hhea", &hhea.b },
                 { "hmtx", &hmtx.b }, { "loca", &locat.b }, { "maxp", &maxp.b }, { "name", &name.b }, { "post", &post.b } };
  const int nt = int(sizeof tabs / sizeof tabs[0]);
  pw = 1, es = 0;
  while (pw * 2 <= nt) pw *= 2, es++;
  BE out;
  out.u32(0x00010000), out.u16(uint32_t(nt)), out.u16(uint32_t(16 * pw)), out.u16(uint32_t(es)), out.u16(uint32_t(nt * 16 - 16 * pw));
  uint32_t offset = 12 + 16 * uint32_t(nt), headAt = 0;
  for (auto& t : tabs) {
    for (int i = 0; i < 4; i++) out.u8(uint8_t(t.tag[i]));
    out.u32(Checksum(*t.d)), out.u32(offset), out.u32(uint32_t(t.d->size()));
    if (!strcmp(t.tag, "head")) headAt = offset;
    offset += uint32_t((t.d->size() + 3) & ~size_t(3));
  }
  for (auto& t : tabs) out.raw(*t.d), out.pad4();
  const uint32_t adj = 0xB1B0AFBA - Checksum(out.b);
  out.b[headAt + 8] = uint8_t(adj >> 24), out.b[headAt + 9] = uint8_t(adj >> 16), out.b[headAt + 10] = uint8_t(adj >> 8),
  out.b[headAt + 11] = uint8_t(adj);
  return out.b;
}

static bool ReadAll(const std::wstring& path, std::vector<uint8_t>& out) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz{};
  bool ok = GetFileSizeEx(h, &sz) && sz.QuadPart > 0 && sz.QuadPart < (256LL << 20);
  if (ok) {
    out.resize(size_t(sz.QuadPart));
    DWORD got = 0;
    ok = ReadFile(h, out.data(), DWORD(out.size()), &got, nullptr) && got == out.size();
  }
  CloseHandle(h);
  return ok;
}

static std::vector<GFont>& Fonts() {  // один раз: UI.PAK -> FONTS_RU.GFX -> DefineFont3
  static std::vector<GFont> fonts;
  static bool tried = false;
  if (tried) return fonts;
  tried = true;
  wchar_t exe[MAX_PATH] = {};
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  std::wstring dir = exe;
  dir.resize(dir.find_last_of(L"\\/") + 1);
  std::vector<uint8_t> pak;
  if (!ReadAll(dir + L"DATA\\UI.PAK", pak) || pak.size() < 16 || memcmp(pak.data(), "PAK2", 4) != 0) return fonts;
  const uint32_t cnt = U32(pak.data() + 8);
  size_t p = 16;
  int want = -1;
  for (uint32_t i = 0; i < cnt && p < pak.size(); i++) {
    const char* s = reinterpret_cast<const char*>(pak.data() + p);
    const size_t len = strnlen(s, pak.size() - p);
    if (len == 20 && !_strnicmp(s, "DATA/UI/FONTS_RU.GFX", 20)) want = int(i);
    p += len + 1;
  }
  if (want < 1 || p + size_t(cnt) * 4 > pak.size()) return fonts;
  const uint32_t a = U32(pak.data() + p + (want - 1) * 4), b = U32(pak.data() + p + want * 4);
  if (a >= b || b > pak.size()) return fonts;
  size_t s = a;
  while (s < a + 16 && s + 3 < b && memcmp(pak.data() + s, "GFX", 3) != 0) s++;  // выравнивание перед файлом
  ParseFonts(pak.data() + s, b - s, fonts);
  return fonts;
}

}  // namespace

namespace AlienMod {
// TTF шрифта игры по имени (Isolation_Bold_RU, JixellationRU, Nostromo Cond, JectRex); false — нет игры / не разобрался
bool GameFontTTF(const char* name, bool bold, std::vector<unsigned char>& out) {
  for (const GFont& f : Fonts())
    if (f.name == name && f.bold == bold && !f.codes.empty()) {
      out = BuildTtf(f);
      return !out.empty();
    }
  return false;
}
}  // namespace AlienMod
