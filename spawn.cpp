// spawn.cpp — спавн персонажей уровня (всё, что сделано сущностью Character).
//   F7 — найти (первый раз) / следующий, Shift+F7 — предыдущий, Ctrl+F7 — искать заново,
//   F8 — заспавнить выбранного, Shift+F8 — тот же объект ещё раз.
// Объекты: экземпляры сущностей Character (+0 vt 0x011d08d8, +0x14 интерфейс vt 0x011d0540, +8 handle).
// Поиск — по всей куче процесса в фоновом потоке (без общего замка, игра не тормозит), без дублей.
// Имя объекта игра до спавна не отдаёт (find_parameter(display_model) у этих сущностей пустой) — подписываем
// объект теми, кто появился после спавна.
// Источник: hot.js ct2, sp4, sp5 (finished_spawning), lockCtl, cname, лимит пула.
#include "game.h"
#include "interceptor.h"
#include "state.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwchar>

static ms_t s_startAt = 0;
static uint32_t s_guidFinished = 0;  // ShortGuid("finished_spawning"), передаётся игре по ссылке
static bool s_guidsReady = false;
static addr s_eiThis = 0, s_ctEcx = 0, s_lastTI = 0;

struct SpawnObj {
  addr o, h;         // объект сущности и её handle
  std::string label; // кто появился после спавна
  int spawned;       // сколько раз заспавнили мы
  bool gone;         // объект освобождён/переиспользован
  bool hugger;       // после спавна появился лицехват (тип 5) — источник для «спавн лицехвата»
};
static std::vector<SpawnObj> s_objs;
static size_t s_sel = 0;
static std::atomic<int> s_scanPct{ -1 };  // -1 — поиск не идёт
static long s_scanGen = 0;               // меняется при сбросе уровня: результаты старого поиска выбрасываем
static bool s_scanDone = false;
static ms_t s_keysAt = 0, s_recentUntil = 0, s_msgUntil = 0;
static std::string s_msg;

struct SpawnPend { addr o, h; };
struct FinPend { addr h; ms_t at; };
struct LabelPend { addr o; std::vector<addr> before; ms_t at; };
// поставить заспавненного в точку: считаем её в потоке игры в момент спавна (луч из центра камеры), переносим, как только
// новый персонаж появился в списке активных (и ещё пару раз — пока физика его не «поймала»)
struct PlacePend { std::vector<addr> before; ms_t until; bool ready; V3 at; float yaw; addr who; int moves; ms_t nextAt; };
static std::vector<PlacePend> s_placeQ;
static std::vector<SpawnPend> s_spawnQ;

static std::vector<FinPend> s_finQ;
static std::vector<LabelPend> s_labelQ;

static void Say(const char* m) {
  s_msg = m;
  s_msgUntil = NowMs() + 4000;
  Log("spawn: %s", m);
}
// объект ещё тот же (не освобождён/не переиспользован) — проверяем перед каждым вызовом игры с его handle
static bool ObjValid(const SpawnObj& o) {
  bool ok = false;
  Safe([&] { ok = rd<uint32_t>(o.o) == VA(va::VT_ENT) && rd<uint32_t>(o.o + 0x14) == VA(va::VT_ENT_IFACE) && rp(o.o + 8) == o.h; });
  return ok;
}

void SpawnReset() {
  s_objs.clear();
  s_sel = 0;
  s_scanGen++;
  s_scanPct = -1;
  s_scanDone = false;
  s_spawnQ.clear();
  s_finQ.clear();
  s_labelQ.clear();
}

static void GuidsTick() {
  // .asi грузится при старте игры (а не в главном меню, как инжектор): ждём, пока появятся глобалы игры
  if (s_guidsReady || NowMs() - s_startAt < 5000) return;
  addr g = 0;
  if (!SafeRead(VA(va::GameGlobals), g) || !g) return;
  uint32_t out[2] = {};
  Fn<ShortGuidFn>(va::ShortGuid)(addr(out), "finished_spawning");
  s_guidFinished = out[0];
  s_guidsReady = true;
  Log("guid finished_spawning %08X", s_guidFinished);
}

// this нужных объектов игры ловим с живых вызовов
static void OnFindParamStringEnter(Invocation& inv) {
  if (s_eiThis) return;
  s_eiThis = inv.ecx;
  Log("EntityInterface %08X", unsigned(s_eiThis));
}
// call_triggers: thiscall(this = EntityInterface, &ent, &guid, &TriggerInfo*, double) — берём живой TriggerInfo
static void OnCallTriggersEnter(Invocation& inv) {
  s_ctEcx = inv.ecx;
  s_lastTI = rp(inv.arg[2]);
}

// ---------------- поиск по всей куче (фоновый поток) ----------------
static DWORD WINAPI ScanThread(void* param) {
  Interceptor::IgnoreCurrentThread();
  const long gen = long(intptr_t(param));
  const uint32_t VT0 = uint32_t(VA(va::VT_ENT)), VT5 = uint32_t(VA(va::VT_ENT_IFACE));
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  // адреса — в 64 битах (иначе у верхних 4 ГБ счётчик переполняется в 0 и поиск идёт по кругу, см. treescan.cpp)
  const uint64_t lo = uintptr_t(si.lpMinimumApplicationAddress), hi = uintptr_t(si.lpMaximumApplicationAddress);
  std::vector<addr> found;
  uint64_t bytes = 0;
  ms_t t0 = NowMs();
  for (uint64_t a = lo; a < hi;) {
    MEMORY_BASIC_INFORMATION m;
    if (!VirtualQuery(reinterpret_cast<void*>(uintptr_t(a)), &m, sizeof m)) break;
    const uint64_t base = uintptr_t(m.BaseAddress), end = base + m.RegionSize;
    if (m.Type == MEM_PRIVATE && Readable(m) && (m.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE))) {
      for (uint64_t c = base; c < end; c += 0x400000) {
        const uint64_t ce = std::min<uint64_t>(c + 0x400000, end);
        Safe([&] {  // память может освободиться на ходу — тогда просто пропускаем кусок
          for (uint64_t pp = c; pp + 0x18 <= end && pp < ce; pp += 4) {
            const addr p = addr(pp);
            if (rd<uint32_t>(p) == VT0 && rd<uint32_t>(p + 0x14) == VT5) found.push_back(p);
          }
        });
        bytes += ce - c;
        Sleep(0);
      }
    }
    s_scanPct = int(uint64_t(a - lo) * 100 / (hi - lo));
    if (end <= a) break;
    a = end;
  }
  std::sort(found.begin(), found.end());
  found.erase(std::unique(found.begin(), found.end()), found.end());
  JsLock();
  if (gen == s_scanGen) {
    s_objs.clear();
    for (addr o : found) {
      addr h = 0;
      if (SafeRead(o + 8, h) && h) s_objs.push_back({ o, h, std::string(), 0, false, false });
    }
    s_sel = 0;
    s_scanPct = -1;
    s_scanDone = true;
    s_recentUntil = NowMs() + 8000;
    Log("scan: %u objects in %.0f MB, %.1f s", unsigned(s_objs.size()), double(bytes) / 1048576.0, double(NowMs() - t0) / 1000.0);
  }
  JsUnlock();
  return 0;
}

// ---------------- действия (клавиши и кнопки ImGui) ----------------
void ActSpawnScan() {
  if (s_scanPct >= 0) return;
  s_objs.clear();
  s_scanDone = false;
  s_scanPct = 0;
  s_scanGen++;
  s_recentUntil = NowMs() + 8000;
  HANDLE t = CreateThread(nullptr, 0, ScanThread, reinterpret_cast<void*>(intptr_t(s_scanGen)), 0, nullptr);
  if (t) CloseHandle(t), Log("scan start");
  else s_scanPct = -1;
}
void ActSpawnSelect(int i) {
  if (i < 0 || size_t(i) >= s_objs.size()) return;
  s_sel = size_t(i);
  s_recentUntil = NowMs() + 6000;
}
void ActSpawn(bool again) {
  if (s_objs.empty()) return;
  ms_t t = NowMs();
  s_recentUntil = t + 6000;
  SpawnObj& o = s_objs[s_sel % s_objs.size()];
  if (o.gone || !ObjValid(o)) {
    o.gone = true;
    return Say(TR("объект исчез — Ctrl+F7, искать заново", "object is gone — Ctrl+F7 to search again"));
  }
  // тот же объект = та же сущность: повторы плодили копии (два BackstageAlien) и роняли дерево поведения
  if (o.spawned && !again) return Say(TR("уже заспавнен — Shift+F8, если точно ещё раз", "already spawned — Shift+F8 to spawn it again"));
  std::vector<addr> active = ActiveChars();
  if (active.size() + s_spawnQ.size() >= 16) return Say(TR("лимит: 16 персонажей (дальше игра падает)", "limit: 16 characters (the game crashes beyond)"));
  // спавн создаёт новые деревья: наши узлы/рубильники закрепляем, иначе они «переедут» на клона -> вылет
  if (!locked && gate && moveNode) {
    locked = true;
    Log("control locked: gate %08X move %08X npc %08X", unsigned(gate), unsigned(moveNode), unsigned(npc));
  }
  o.spawned++;
  if (spawnWhere != 0) s_placeQ.push_back({ active, t + 4000, false, {}, 0.f, 0, 0, 0 });
  s_spawnQ.push_back({ o.o, o.h });
  s_finQ.push_back({ o.h, t + 1500 });
  s_labelQ.push_back({ o.o, active, t + 2000 });
  Log("spawn obj %08X #%u (active %u)", unsigned(o.o), unsigned(s_sel % s_objs.size() + 1), unsigned(active.size()));
}
SpawnInfo SpawnSnapshot() {
  SpawnInfo r;
  r.scanning = s_scanPct >= 0;
  r.pct = s_scanPct;
  r.done = s_scanDone;
  r.sel = s_objs.empty() ? -1 : int(s_sel % s_objs.size());
  for (const SpawnObj& o : s_objs) {
    r.found.push_back(o.o);
    r.labels.push_back(o.label);
    r.spawned.push_back(o.spawned);
    r.gone.push_back(o.gone);
    r.hugger.push_back(o.hugger);
  }
  r.recent = NowMs() < s_recentUntil;
  if (NowMs() < s_msgUntil) r.msg = s_msg;
  return r;
}

static void SpawnKeysTick() {
  ms_t t = NowMs();
  if (t - s_keysAt < 350) return;
  const bool shift = Down(VK_SHIFT), ctrl = Down(VK_CONTROL);
  if (HotAdvK(K_SPAWN_FIND)) {
    s_keysAt = t;
    s_recentUntil = t + 6000;
    if (ctrl || (!s_scanDone && s_scanPct < 0) || (s_scanDone && s_objs.empty())) return ActSpawnScan();
    if (s_objs.empty()) return;
    size_t n = s_objs.size();
    ActSpawnSelect(int(shift ? (s_sel + n - 1) % n : (s_sel + 1) % n));
  } else if (HotAdvK(K_SPAWN) && !s_objs.empty()) {
    s_keysAt = t;
    ActSpawn(shift);
  }
}

// ---------------- поток игры: спавн (по одному за кадр) и finished_spawning ----------------
// точка для спавна (поток игры): луч из центра текущей камеры (блок 1: кватернион +0x04, позиция +0x14) до 25 м, чуть
// назад от стены, вниз — на пол; или 2 м перед тем, за кого играешь. Лицом — к камере / к персонажу
static bool SpawnPoint(V3& at, float& yaw) {
  bool ok = false;
  Safe([&] {
    if (spawnWhere == 2) {
      const addr ref = CamTarget() ? CamTarget() : player;
      if (!ref) return;
      const V3 p = rv3(ref + 0x70);
      const float y = rf(ref + 0x174);
      at = { p.x + std::sin(y) * 2.f, p.y, p.z + std::cos(y) * 2.f };
      yaw = WrapPi(y + kPi);
    } else {
      const addr cam = rp(CamG() + 0x1d8);
      if (!cam) return;
      const float qx = rf(cam + 0x04), qy = rf(cam + 0x08), qz = rf(cam + 0x0c), qw = rf(cam + 0x10);
      const V3 pos = rv3(cam + 0x14);
      V3 f{ 2 * (qx * qz + qw * qy), 2 * (qy * qz - qw * qx), 1 - 2 * (qx * qx + qy * qy) };
      const float len = std::sqrt(f.x * f.x + f.y * f.y + f.z * f.z);
      if (!(len > 0.1f) || !std::isfinite(pos.x)) return;
      f = { f.x / len, f.y / len, f.z / len };
      V3 hit{};
      if (RayHit(pos, { pos.x + f.x * 25.f, pos.y + f.y * 25.f, pos.z + f.z * 25.f }, hit))
        at = { hit.x - f.x * 0.6f, hit.y - f.y * 0.6f + 0.3f, hit.z - f.z * 0.6f };
      else
        at = { pos.x + f.x * 6.f, pos.y + f.y * 6.f, pos.z + f.z * 6.f };
      yaw = std::atan2(-f.x, -f.z);
    }
    float floorY;
    if (RayDown(at.x, at.y + 0.5f, at.z, 8.f, floorY)) at.y = floorY;
    ok = std::isfinite(at.x) && std::isfinite(at.y) && std::isfinite(at.z);
  });
  return ok;
}
static void PlaceTick() {  // 30 мс, под замком
  const ms_t t = NowMs();
  for (size_t i = 0; i < s_placeQ.size();) {
    PlacePend& p = s_placeQ[i];
    if (t > p.until || (p.who && p.moves >= 4)) {
      s_placeQ.erase(s_placeQ.begin() + ptrdiff_t(i));
      continue;
    }
    if (p.ready && !p.who)
      for (addr e : ActiveChars())
        if (std::find(p.before.begin(), p.before.end(), e) == p.before.end()) {
          p.who = e;
          break;
        }
    if (p.who && t >= p.nextAt) {
      TpMove(p.who, p.at.x, p.at.y, p.at.z);
      Safe([&] { wf(p.who + 0x174, p.yaw); });
      if (!p.moves) Log("spawn: %s placed at %.1f %.1f %.1f", CharName(p.who).c_str(), p.at.x, p.at.y, p.at.z);
      p.moves++;
      p.nextAt = t + 120;
    }
    i++;
  }
}
static void OnLocoUpdateSpawn(Invocation&) {
  if (!s_spawnQ.empty()) {
    SpawnPend p = s_spawnQ.front();
    s_spawnQ.erase(s_spawnQ.begin());
    size_t act = ActiveChars().size();
    bool valid = false;
    for (const SpawnObj& o : s_objs)
      if (o.o == p.o) valid = ObjValid(o);
    if (!valid) {
      Log("spawn skipped: obj %08X gone", unsigned(p.o));
    } else if (act >= 16) {
      Log("spawn blocked: pool limit");
    } else {
      addr hh = p.h, tt = s_lastTI;
      for (PlacePend& pl : s_placeQ)  // точка — сейчас, в потоке игры (лучи Havok)
        if (!pl.ready) {
          pl.ready = SpawnPoint(pl.at, pl.yaw);
          if (!pl.ready) pl.until = 0;
          break;
        }
      uint32_t r = Fn<SpawnFn>(va::SpawnImpl)(p.o + 0x14, addr(&hh), addr(&tt));
      Log("SPAWN obj %08X ret %u (active before %u)", unsigned(p.o), r, unsigned(act));
    }
  }
  if (!s_finQ.empty() && NowMs() >= s_finQ.front().at) {  // через 1.5 с — выход finished_spawning (оружие, ИИ)
    FinPend p = s_finQ.front();
    s_finQ.erase(s_finQ.begin());
    addr ei = s_ctEcx ? s_ctEcx : s_eiThis;
    if (s_lastTI && ei && s_guidsReady) {
      addr hh = p.h, tt = s_lastTI;
      Fn<CallTriggersFn>(va::CallTriggers)(ei, addr(&hh), addr(&s_guidFinished), addr(&tt), 0.0);
      Log("finished_spawning fired h %08X", unsigned(p.h));
    }
  }
}

// через 2 с после спавна смотрим, какие персонажи появились — это подпись объекта
static void LabelTick() {
  if (s_labelQ.empty() || NowMs() < s_labelQ.front().at) return;
  LabelPend p = s_labelQ.front();
  s_labelQ.erase(s_labelQ.begin());
  std::string names;
  std::vector<addr> now = ActiveChars();
  for (addr e : now)
    if (std::find(p.before.begin(), p.before.end(), e) == p.before.end()) names += (names.empty() ? "" : ", ") + CharName(e);
  Log("after spawn obj %08X: active %u, new: %s", unsigned(p.o), unsigned(now.size()), names.empty() ? "-" : names.c_str());
  bool hug = false;
  for (addr e : now)
    if (std::find(p.before.begin(), p.before.end(), e) == p.before.end() && IsHugger(e)) hug = true;
  for (SpawnObj& o : s_objs)
    if (o.o == p.o) {
      o.label = names.empty() ? (o.label.empty() ? TR("никто не появился", "nobody appeared") : o.label) : names;
      if (hug && !o.hugger) o.hugger = true, Log("spawn: obj %08X gives a facehugger — Ctrl+H spawns more of them", unsigned(o.o));
    }
}

// ещё один лицехват туда, куда смотрит камера (спавн сущности, из которой уже появлялся лицехват — копия)
void ActSpawnHugger() {
  if (s_scanPct >= 0) return Say(TR("ищу персонажей уровня — подожди пару секунд", "looking for level characters — wait a couple of seconds"));
  if (!s_scanDone) {
    ActSpawnScan();
    return Say(TR("ищу персонажей уровня... потом ещё раз Ctrl+H", "looking for level characters... then Ctrl+H again"));
  }
  for (size_t i = 0; i < s_objs.size(); i++)
    if (s_objs[i].hugger && !s_objs[i].gone && ObjValid(s_objs[i])) {
      const int keep = spawnWhere;
      if (!spawnWhere) spawnWhere = 1;  // «лицехват сюда» — всегда туда, куда смотришь
      s_sel = i;
      ActSpawn(true);
      spawnWhere = keep;
      return;
    }
  Say(TR("лицехват ещё не найден: F7 — выбрать объект в списке, F8 — заспавнить (мод запомнит, из какого появился лицехват)",
         "no facehugger known yet: F7 — pick an object, F8 — spawn it (the mod remembers which one gives a facehugger)"));
}

void InstallSpawn() {
  using Interceptor::Attach;
  s_startAt = NowMs();
  Attach(VA(va::FindParamString), OnFindParamStringEnter, nullptr);
  Attach(VA(va::CallTriggers), OnCallTriggersEnter, nullptr, false);  // горячий (все выходы всех сущностей): без замка
  Attach(VA(va::LocoUpdate), OnLocoUpdateSpawn, nullptr);
  Every(100, "guids", GuidsTick);
  Every(60, "spawn-keys", SpawnKeysTick);
  Every(100, "spawn-label", LabelTick);
  Every(30, "spawn-place", PlaceTick);
}
