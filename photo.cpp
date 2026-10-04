// photo.cpp — фоторежим: свободная кино-камера поверх follow-камеры игры.
//   F9 — вкл/выкл. WASD — лететь (по взгляду), Q/E — вниз/вверх, мышь или стрелки — смотреть, Z/C — наклон по Z
//   (R — сброс наклона), колесо — FOV, Shift — быстрее, Ctrl — медленнее, X — стоп-время. Ввод в игру не идёт, HUD
//   мода скрыт. Во вкладке «Фоторежим»: FOV, наклон, скорость, стоп-время / слоу-мо.
//   Машинима: стрелки ведут персонажа (Чужой или лицехват/андроид, за которого играешь) относительно взгляда камеры,
//   Tab — кого (или стрелки — камере), PgUp/PgDn — скорость (правый Shift — бегом, правый Ctrl — тихо), Enter — атака,
//   G — голос, T — камера едет за персонажем / едет и смотрит на него / стоит.
//
// Камера (объект камеры, проверено по памяти): три одинаковых блока с шагом 0x28 —
//   +0x04 кватернион (x, y, z, w), +0x14 позиция, +0x20 FOV в градусах. Блок 1 уходит в рендер (FUN_00432300),
//   блок 2 — прошлый кадр (скорость камеры для motion blur, FUN_00429ad0).
// Кватернион: «вперёд» — +Z, курс — поворот вокруг +Y (сверено с курсом Аманды: q(0, 0.968, 0, -0.246) = -2.643 рад).
// Follow-камера после своего поведения строит взгляд look-at'ом (без крена), поэтому кадр пишем ещё раз — в самом
// конце: перед расчётом скорости камеры и перед передачей в рендер (проверено: крен в блоке 1 доходит до матрицы вида).
// Сущность камеры — Аманда (она в мире: камера над Чужим «вне мира» замирает и карта чернеет).
//
// Время (FUN_00773050): игровая дельта = реальная × *(double*)0x016410c8, если *(bool*)0x016410d8. Проверено по памяти:
// при ×0.00001 кости и анимации персонажей замирают (из 760 меняющихся полей остаются ~80 — камера/рендер).
// Родная пауза игры (биты 0x016f194c) не годится: на паузе рендер показывает закешированный кадр, камера не видна.
// Дельту для шейдеров подменяем на реальную (иначе motion blur делит на ~0 и кадр размазан).
// Анимация персонажей берёт дельту через FUN_0077d440 = clamp(дельта, 1/360, 1/24): нижнюю границу тоже масштабируем
// (проверено: без этого Чужой при «стоп-времени» шевелится на ~1/6 скорости, с этим поза замирает полностью).
// Физика Havok (хвост Чужого, тряпки) шагает с 1/FPS мимо масштаба: при стопе шаг пропускаем, в слоу-мо масштабируем
// (проверено: с пропущенным шагом все 7 тел хвоста стоят).
// Постобработка: после смешивания CATHODE-настроек (FUN_00a08c50) правим итог — экспозиция, цвет, виньетка, зерно...
#include "game.h"
#include "interceptor.h"
#include "state.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

static int s_prevCam = 0;
static ms_t s_last = 0, s_keyAt = 0, s_freezeKeyAt = 0;
static float s_gameFov = 47.f;          // FOV игры в момент входа
static float s_fovTarget = 0;           // куда плавно едет FOV после колеса (0 — не едет)
static bool s_timeSaved = false, s_timeOnWas = false, s_timeFixLogged = false;
static double s_timeScaleWas = 1.0, s_timeWant = 1.0;
static std::atomic<bool> s_timeOn{ false };  // наш масштаб времени сейчас в игре
static std::atomic<float> s_timeMul{ 1.f };   // он же для хуков в потоке игры

bool PhotoActive() { return photo.on; }

struct Quat { float x, y, z, w; };
static Quat Mul(const Quat& a, const Quat& b) {
  return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
           a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
}
// курс вокруг +Y, затем тангаж (вверх — положительный), затем наклон вокруг взгляда (+Z)
static Quat FromAngles(float yaw, float pitch, float roll) {
  const Quat qy{ 0, std::sin(yaw / 2), 0, std::cos(yaw / 2) };
  const Quat qp{ std::sin(-pitch / 2), 0, 0, std::cos(-pitch / 2) };
  const Quat qr{ 0, 0, std::sin(roll / 2), std::cos(roll / 2) };
  return Mul(Mul(qy, qp), qr);
}
static V3 Forward() {
  const float cp = std::cos(photo.pitch);
  return { std::sin(photo.yaw) * cp, std::sin(photo.pitch), std::cos(photo.yaw) * cp };
}

// кадр фоторежима в блоки камеры from..to (0 — текущий, 1 — в рендер, 2 — прошлый кадр)
static void WriteBlocks(addr cam, int from, int to) {
  const V3 p = photo.pos;
  const Quat q = FromAngles(photo.yaw, photo.pitch, photo.roll);
  for (int k = from; k <= to; k++) {
    const addr s = cam + k * 0x28;
    wf(s + 0x04, q.x), wf(s + 0x08, q.y), wf(s + 0x0c, q.z), wf(s + 0x10, q.w);
    wv3(s + 0x14, p.x, p.y, p.z);
    if (photo.fov > 0) wf(s + 0x20, photo.fov);
  }
}

// поток игры, после апдейта follow-камеры (camera.cpp): точка взгляда и позиция для самой follow-камеры
void PhotoCamera(addr b) {
  const V3 p = photo.pos, f = Forward();
  wv3(b + 0x1c0, p.x + f.x * 2.f, p.y + f.y * 2.f, p.z + f.z * 2.f);
  wv3(b + 0x1c, p.x, p.y, p.z);
  addr cam = rp(b + 0x34);
  if (!cam) return;
  WriteBlocks(cam, 0, 2);
  wv3(cam + 0x10c, p.x + f.x * 10.f, p.y + f.y * 10.f, p.z + f.z * 10.f);
  wr<uint8_t>(cam + 0x153, 1);
}

// FUN_00429ad0 (ecx = камера): скорость камеры = блок 1 − блок 2. Блок 1 — наш кадр (follow-камера только что
// перезаписала его look-at'ом без крена), блок 2 — наш прошлый кадр: motion blur честный, без рывков.
static void OnCamVelocityEnter(Invocation& inv) {
  if (!photo.on) return;
  Safe([&] {
    if (inv.ecx == rp(CamG() + 0x1d8)) WriteBlocks(inv.ecx, 0, 1);
  });
}
// FUN_00432300 (ecx = менеджер камер): блок 1 текущей камеры уходит в рендер — последний шанс поставить крен и FOV
// (после тряски и прочих эффектов камеры, которые игра накладывает на блок 1)
static void FocusPick();
static void OnCamSubmitEnter(Invocation& inv) {
  if (!photo.on) return;
  if (photoFocusPick.load()) Safe([] { FocusPick(); });  // клик / F — навести фокус (луч — только из потока игры)
  Safe([&] {
    const addr G = inv.ecx;
    if (G != CamG()) return;
    addr cam = rp(G + 0x1d8);
    if (rd<uint8_t>(G + 0x221) && rp(G + 0x1f0)) cam = rp(G + 0x1f0);  // так выбирает сама игра
    if (cam) WriteBlocks(cam, 0, 2);
  });
}
// комната для отрисовки: игра берёт её у персонажа, за которым следит камера (Аманда), — улетел фотокамерой в другую
// комнату, а рисуется то, что видно из комнаты Аманды. Снимаем флаг «комната задана» ([0x01758144] — его читает
// FUN_009b45a0 уже после этой функции): зону активной точки зрения игра ищет сама по позиции камеры (FUN_0074c870)
// Второй флаг — «зону по позиции не искать» ([0x01758145] = камера+0x150, у камеры следования 1): без него игра
// ставила зону −1 и не рисовала ничего (тестер: «карта вообще не рендерится»). Если зона активной точки зрения всё же
// −1 (камера вне уровня) — этот кадр оставляем комнату Аманды, а не пустоту.
static void OnCamSubmitLeave(Invocation& inv) {
  if (!photo.on || inv.ecx != CamG()) return;
  const addr mgr = rp(VA(va::PovManager)), pov = mgr ? rp(mgr + 0x8c) : 0;
  if (pov && rd<uint32_t>(pov + 0x330) == 0xFFFFFFFFu) return;
  wr<uint8_t>(VA(va::RenderRoomForced), 0);
  wr<uint8_t>(VA(va::RenderNoZoneMatch), 0);
}

// FUN_0099db50(float время, float дельта): пока наш масштаб времени включён, шейдерам — реальная дельта кадра
// (motion blur делит на неё), а игровой глобал 0x0175814c (его читают эффекты на CPU) — снова масштабированный.
static void OnShaderClockEnter(Invocation& inv) {
  if (!s_timeOn) return;
  const float real = float(rd<double>(VA(va::RealDelta)));
  if (!(real > 0.f && real < 1.f)) return;
  std::memcpy(inv.text + 4, &inv.args[1], 4);
  std::memcpy(&inv.args[1], &real, 4);
  inv.text[0] = 1;
}
static void OnShaderClockLeave(Invocation& inv) {
  if (inv.text[0] != 1) return;
  float game;
  std::memcpy(&game, inv.text + 4, 4);
  wf(VA(va::GameDeltaF), game);
}
// физика: при остановленном времени задачу шага мира не зовём вовсе (как на паузе игры — хвост и тряпки замирают),
// в слоу-мо шаг идёт с дельтой × масштаб
static void OnPhysicsTaskEnter(Invocation& inv) {
  if (s_timeOn && s_timeMul < 0.02f) inv.skip = true;  // cdecl, без аргументов
}
static void OnPhysicsStepEnter(Invocation& inv) {  // FUN_00b42850(this, очередь, float dt)
  if (!s_timeOn) return;
  float dt;
  std::memcpy(&dt, &inv.args[1], 4);
  dt *= s_timeMul;
  std::memcpy(&inv.args[1], &dt, 4);
}

// ---------------- постобработка ----------------
// итог смешивания в менеджере (раскладки пакетов сверены с Apply-функциями сущностей и значениями в памяти):
//   +0x190c цвет: +8 яркость, +0xc контраст, +0x10 насыщенность, +0x14/+0x18/+0x1c тинт RGB (множители)
//   +0x192c bloom: +0x10 bloom_scale      +0x1980 HighSpec motion blur: +8 contribution
//   +0x19a8 зерно: +8/+0xc/+0x10 амплитуды (низ/сред/верх), +0x28/+0x2c/+0x30 они же без адаптации
//   +0x19e0 виньетка: +8 сила, +0xc хром. аберрация по краям    +0x1a04 резкость: +8 local_contrast_factor
//   +0x1a2c размытие кадра: +8 contribution    +0x1a58 хроматическая аберрация: +8 aberration_scalar
static void OnPostBlendLeave(Invocation& inv) {
  if (!photo.on || !photoPost.on) return;
  const PhotoPost p = photoPost;
  const addr m = inv.ecx;
  auto add = [](addr a, float v) { wf(a, rf(a) + v); };
  auto mul = [](addr a, float v) { wf(a, rf(a) * v); };
  const addr c = m + 0x190c;
  add(c + 0x08, p.brightness);
  mul(c + 0x0c, p.contrast);
  mul(c + 0x10, p.saturation);
  const float ev = std::exp2(p.exposure);
  mul(c + 0x14, ev * (1.f + 0.3f * p.temperature) * (1.f + 0.15f * p.tint));
  mul(c + 0x18, ev * (1.f - 0.2f * p.tint));
  mul(c + 0x1c, ev * (1.f - 0.3f * p.temperature) * (1.f + 0.15f * p.tint));
  // свечение: проверено по кадру — сила даёт bloom_gather_scale (+0x18), порог — bloom_gather_exponent (+0x14);
  // bloom_scale (+0x10) на картинку почти не влияет (поэтому раньше «bloom не работал»)
  mul(m + 0x192c + 0x18, p.bloom);
  mul(m + 0x192c + 0x14, p.bloomThreshold);
  if (p.noMotionBlur) wf(m + 0x1980 + 0x08, 0.f);
  // зерно фоторежима — целиком своё: зерно уровня выключаем, ставим то, что на ползунке (0 — чистый кадр)
  for (addr o : { 0x08, 0x0c, 0x10, 0x28, 0x2c, 0x30 }) wf(m + 0x19a8 + o, p.grain);
  add(m + 0x19e0 + 0x08, p.vignette);
  add(m + 0x19e0 + 0x0c, p.aberration * 0.01f);
  add(m + 0x19f0 + 0x08, p.distortion);  // радиальное искажение линзы
  if (p.blur > rf(m + 0x1a2c + 0x08)) wf(m + 0x1a2c + 0x08, p.blur);
  add(m + 0x1a58 + 0x08, p.aberration);
  // глубина резкости (bokeh игры, FUN_009f1ee0): включается, когда фокусное > 0; +8 фокусное (м), +0xc плоскость
  // резкости (м), +0x10 диафрагма
  if (p.dof) {
    const addr d = m + 0x1a38;
    wf(d + 0x04, 1.f);
    wf(d + 0x08, std::max(0.001f, p.focal * 0.001f));
    wf(d + 0x0c, std::max(0.05f, p.focus));
    wf(d + 0x10, std::max(0.5f, p.fstop));
  }
}
float PhotoSharpen() { return photo.on && photoPost.on ? photoPost.sharpness : 0.f; }

// пресеты «плёнки»: только цвет и зерно, камеру и DOF не трогают
static void ApplyPreset(int i) {
  PhotoPost& p = photoPost;
  const PhotoPost keep = p;
  p.exposure = 0, p.brightness = 0, p.contrast = 1, p.saturation = 1, p.temperature = 0, p.tint = 0;
  p.vignette = 0, p.grain = 0, p.aberration = 0, p.bloom = 1, p.bloomThreshold = 1, p.distortion = 0;
  switch (i) {
    case 1: p.saturation = 0, p.contrast = 1.35f, p.grain = 0.35f, p.vignette = 1.2f; break;               // нуар
    case 2: p.temperature = 0.6f, p.saturation = 0.75f, p.contrast = 1.1f, p.grain = 0.15f; break;         // тёплый
    case 3: p.temperature = -0.6f, p.tint = -0.15f, p.saturation = 0.8f, p.contrast = 1.1f; break;         // холодный
    case 4: p.grain = 0.5f, p.saturation = 0.85f, p.contrast = 1.15f, p.bloom = 2.f, p.vignette = 0.8f,    // плёнка 1979
            p.aberration = 0.6f, p.temperature = 0.15f; break;
    case 5: p.saturation = 1.4f, p.contrast = 1.2f, p.bloom = 1.3f; break;                                 // сочно
    case 6: p.saturation = 0.15f, p.tint = -0.5f, p.contrast = 1.25f, p.grain = 0.3f, p.vignette = 1.f; break;  // терминал
  }
  p.on = keep.on, p.noMotionBlur = keep.noMotionBlur, p.sharpness = keep.sharpness, p.blur = keep.blur;
  p.dof = keep.dof, p.focus = keep.focus, p.focal = keep.focal, p.fstop = keep.fstop;
  p.letterbox = keep.letterbox, p.grid = keep.grid, p.hideGameHud = keep.hideGameHud, p.showAmanda = keep.showAmanda;
}
void ActPhotoPost(int field, float v) {
  PhotoPost& p = photoPost;
  auto cl = [](float x, float lo, float hi) { return std::max(lo, std::min(hi, x)); };
  switch (field) {
    case PF_ON: p.on = v != 0; break;
    case PF_EXPOSURE: p.exposure = cl(v, -4.f, 4.f); break;
    case PF_BRIGHTNESS: p.brightness = cl(v, -1.f, 1.f); break;
    case PF_CONTRAST: p.contrast = cl(v, 0.f, 3.f); break;
    case PF_SATURATION: p.saturation = cl(v, 0.f, 3.f); break;
    case PF_TEMPERATURE: p.temperature = cl(v, -1.f, 1.f); break;
    case PF_TINT: p.tint = cl(v, -1.f, 1.f); break;
    case PF_VIGNETTE: p.vignette = cl(v, 0.f, 5.f); break;
    case PF_GRAIN: p.grain = cl(v, 0.f, 3.f); break;
    case PF_ABERRATION: p.aberration = cl(v, 0.f, 5.f); break;
    case PF_SHARPNESS: p.sharpness = cl(v, 0.f, 3.f); break;
    case PF_BLUR: p.blur = cl(v, 0.f, 1.f); break;
    case PF_BLOOM: p.bloom = cl(v, 0.f, 20.f); break;
    case PF_BLOOM_THRESHOLD: p.bloomThreshold = cl(v, 0.1f, 3.f); break;
    case PF_DISTORTION: p.distortion = cl(v, -0.5f, 0.5f); break;
    case PF_NO_MOTION_BLUR: p.noMotionBlur = v != 0; break;
    case PF_DOF: p.dof = v != 0; break;
    case PF_FOCUS: p.focus = cl(v, 0.1f, 200.f); break;
    case PF_FOCAL: p.focal = cl(v, 10.f, 400.f); break;
    case PF_FSTOP: p.fstop = cl(v, 0.7f, 32.f); break;
    case PF_LETTERBOX: p.letterbox = int(cl(v, 0.f, 5.f)); break;
    case PF_GRID: p.grid = v != 0; break;
    case PF_HIDE_HUD: p.hideGameHud = v != 0; break;
    case PF_SHOW_AMANDA: p.showAmanda = v != 0; break;
    case PF_PRESET: ApplyPreset(int(v)); break;
    case PF_RESET: {
      const PhotoPost keep = p;
      p = PhotoPost{};
      p.on = keep.on;
      break;
    }
  }
}

// точка фокуса: луч из камеры фоторежима — по центру кадра или через точку клика (UV 0..1), поток игры
static V3 Rot(const Quat& q, V3 v) {  // q · v · q*
  const Quat p{ v.x, v.y, v.z, 0 }, qc{ -q.x, -q.y, -q.z, q.w };
  const Quat r = Mul(Mul(q, p), qc);
  return { r.x, r.y, r.z };
}
static void FocusPick() {
  const int mode = photoFocusPick.exchange(0);
  if (!mode) return;
  const Quat q = FromAngles(photo.yaw, photo.pitch, photo.roll);
  const V3 f = Rot(q, { 0, 0, 1 }), r = Rot(q, { 1, 0, 0 }), u = Rot(q, { 0, 1, 0 });
  V3 dir = f;
  if (mode == 2) {
    const float fovY = (photo.fov > 0 ? photo.fov : s_gameFov) * kPi / 180.f, t = std::tan(fovY * 0.5f);
    const float aspect = photoFocusUV[2] > 0.1f ? photoFocusUV[2] : 16.f / 9.f;
    const float sx = (photoFocusUV[0] * 2.f - 1.f) * t * aspect, sy = (1.f - photoFocusUV[1] * 2.f) * t;
    dir = { f.x + r.x * sx + u.x * sy, f.y + r.y * sx + u.y * sy, f.z + r.z * sx + u.z * sy };
  }
  const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
  if (len < 1e-4f) return;
  const V3 a = photo.pos, b{ a.x + dir.x / len * 200.f, a.y + dir.y / len * 200.f, a.z + dir.z / len * 200.f };
  V3 hit{};
  if (!RayHit(a, b, hit)) {
    photoFocusHit = 0.f;
    Log("photo focus: nothing hit");
    return;
  }
  const float depth = (hit.x - a.x) * f.x + (hit.y - a.y) * f.y + (hit.z - a.z) * f.z;  // глубина по оси взгляда
  photoPost.focus = std::max(0.1f, depth);
  photoPost.dof = true;
  photoFocusHit = photoPost.focus;
  Log("photo focus: %.2f m", photoPost.focus);
}

// FUN_0077d440(float lo, float hi): дельта игры, зажатая снизу lo (анимация: 1/360 с). Пока наш масштаб включён,
// нижняя граница масштабируется тем же множителем — иначе при остановленном времени анимация идёт на lo за кадр.
static void OnDeltaClampEnter(Invocation& inv) {
  if (!s_timeOn) return;
  float lo;
  std::memcpy(&lo, &inv.args[0], 4);
  lo *= s_timeMul;
  std::memcpy(&inv.args[0], &lo, 4);
}

// ---------------- время ----------------
bool FreezeAvailable() { return true; }
static void ApplyTime() {
  const bool want = photo.on && (photo.freeze || std::fabs(photo.timeScale - 1.f) > 0.001f);
  if (want) {
    if (!s_timeSaved) {  // запомнить, что было у игры
      s_timeOnWas = rd<uint8_t>(VA(va::TimeScaleOn)) != 0;
      s_timeScaleWas = rd<double>(VA(va::TimeScale));
      s_timeSaved = true;
      s_timeFixLogged = false;
    }
    // ~0, а не 0: дельта не ноль — делений на 0 нет (фонарик и прочее при дельте < 1e-4 просто не считаются)
    s_timeWant = photo.freeze || photo.timeScale < 0.005f ? 1e-5 : double(photo.timeScale);
    s_timeMul = float(s_timeWant);
    wr<double>(VA(va::TimeScale), s_timeWant);
    wr<uint8_t>(VA(va::TimeScaleOn), 1);
    s_timeOn = true;
  } else if (s_timeSaved) {
    s_timeOn = false;
    wr<double>(VA(va::TimeScale), s_timeScaleWas);
    wr<uint8_t>(VA(va::TimeScaleOn), s_timeOnWas ? 1 : 0);
    s_timeSaved = false;
  }
}
// игра сама сбрасывает масштаб (FUN_007468d0: конец своего слоу-мо -> флаг 0, масштаб 1) — держим свой
static void KeepTime() {
  if (!s_timeOn) return;
  if (rd<uint8_t>(VA(va::TimeScaleOn)) && rd<double>(VA(va::TimeScale)) == s_timeWant) return;
  wr<double>(VA(va::TimeScale), s_timeWant);
  wr<uint8_t>(VA(va::TimeScaleOn), 1);
  if (!s_timeFixLogged) {
    s_timeFixLogged = true;
    Log("photo: game reset the time scale — restored");
  }
}
float PhotoTimeLive() {  // что сейчас в игре: множитель игрового времени
  float v = 1.f;
  Safe([&] { v = rd<uint8_t>(VA(va::TimeScaleOn)) ? float(rd<double>(VA(va::TimeScale))) : 1.f; });
  return v;
}
void ActFreezeTime(bool on) {
  photo.freeze = on;
  ApplyTime();
  Log("photo: time %s", on ? "frozen" : "running");
}
void ActTimeScale(float s) {
  photo.timeScale = std::max(0.f, std::min(2.f, s));
  ApplyTime();
}
void ActPhotoParams(float fov, float rollDeg) {
  photo.fov = fov <= 0 ? 0 : std::max(5.f, std::min(140.f, fov));
  s_fovTarget = 0;  // из меню — сразу, без плавного хода
  photo.roll = rollDeg * kPi / 180.f;
}

void ActPhoto(bool on) {
  if (on == photo.on) return;
  if (on) {
    if (!player) FindPlayer();  // уровень без Чужого: Аманду мог ещё не найти никто
    if (!player) return Log("photo: no player yet");
    addr G = CamG(), follow = rp(G + 0x1dc), cur = rp(G + 0x1d8);
    if (!camSaved) camSaved = cur;  // чтобы потом вернуть камеру Аманды
    // старт — из текущей камеры игры: её позиция, курс и тангаж из кватерниона, её FOV
    V3 start = rv3(player + 0x70);
    start.y += 1.6f;
    float yaw = rf(player + 0x174), pitch = 0;
    Safe([&] {
      const addr s = cur;
      Quat q{ rf(s + 0x04), rf(s + 0x08), rf(s + 0x0c), rf(s + 0x10) };
      // вперёд = q · (0,0,1) · q*
      const float fx = 2 * (q.x * q.z + q.w * q.y), fy = 2 * (q.y * q.z - q.w * q.x), fz = 1 - 2 * (q.x * q.x + q.y * q.y);
      V3 p = rv3(s + 0x14);
      if (std::isfinite(fx) && std::isfinite(p.x) && std::fabs(fx) + std::fabs(fz) > 0.01f) {
        start = p;
        yaw = std::atan2(fx, fz);
        pitch = std::asin(std::max(-1.f, std::min(1.f, fy)));
      }
      const float fov = rf(s + 0x20);
      if (fov > 5 && fov < 140) s_gameFov = fov;
    });
    photo.pos = start;
    photo.yaw = yaw;
    photo.pitch = pitch;
    photo.roll = 0;
    s_prevCam = camMode;
    // follow-камера над Амандой (она в мире — движок камеру обновляет; над Чужим за кулисами кадр чернеет)
    Fn<CamSetEntityFn>(va::CamSetEntity)(follow, player);
    if (cur != follow) Fn<CamActivateFn>(va::CamActivate)(G, follow);
    photo.on = true;
    g_photoOn = true;
    if (photoActor.load() != PA_CAMERA) photoActor = hugger ? PA_NPC : PA_ALIEN;  // стрелки — тому, за кого играешь
    photoMouseDX = photoMouseDY = photoWheel = 0;
    s_fovTarget = 0;
    ApplyTime();
    Log("photo on at %.1f %.1f %.1f yaw %.2f pitch %.2f fov %.0f", start.x, start.y, start.z, yaw, pitch, s_gameFov);
  } else {
    photo.on = false;
    g_photoOn = false;
    ApplyTime();  // вернуть время игры
    if (s_prevCam == 0) CamRestore("photo off");
    else ActCamera(s_prevCam);
    Log("photo off");
  }
}

// ---------------- персонаж в кадре (машинима) ----------------
static std::atomic<int> s_attackReq{ PA_CAMERA };  // Enter: чья атака (PA_*), забирает тот, кого ведут
static std::atomic<int> s_toastKind{ 0 };          // 1 — кого ведут, 2 — скорость, 3 — слежение, 4 — ИИ Чужого мешает
static std::atomic<ms_t> s_toastAt{ 0 };
static void Toast(int kind) { s_toastKind = kind, s_toastAt = NowMs(); }
static addr ActorEntity(int a) { return a == PA_ALIEN ? npc : a == PA_NPC ? hugger : 0; }
int PhotoActorNow() {
  if (!photo.on) return PA_CAMERA;
  int a = photoActor.load();
  if (a == PA_NPC && !hugger) a = npc ? PA_ALIEN : PA_CAMERA;  // выбранного нет — стрелки у того, кто есть
  if (a == PA_ALIEN && !npc) a = hugger ? PA_NPC : PA_CAMERA;
  return a;
}
PhotoDrive PhotoDriveFor(int kind) {
  PhotoDrive d{};
  if (kind == PA_CAMERA || PhotoActorNow() != kind) return d;
  d.on = true;
  d.dx = int(Down(VK_RIGHT)) - int(Down(VK_LEFT));
  d.dy = int(Down(VK_UP)) - int(Down(VK_DOWN));
  d.speed = Down(VK_RSHIFT) ? 3 : Down(VK_RCONTROL) ? 0 : std::max(0, std::min(3, photoGear.load()));
  d.yaw = photo.yaw;
  return d;
}
bool PhotoTakeAttack(int kind) {
  int k = kind;
  return kind != PA_CAMERA && s_attackReq.compare_exchange_strong(k, PA_CAMERA);
}
static const char* ActorName(int a) {
  if (a == PA_ALIEN) return TR("Чужой", "the Alien");
  if (a == PA_NPC) return NpcCtlKind() == 2 ? TR("андроид", "the android") : TR("лицехват", "the facehugger");
  return TR("камера (смотреть)", "camera (look)");
}
std::string PhotoToast() {
  const int k = s_toastKind.load();
  if (!photo.on || !k || NowMs() - s_toastAt.load() > 1500) return "";
  static const char* kGearRu[] = { "тихо", "шагом", "быстро", "бегом" };
  static const char* kGearEn[] = { "sneak", "walk", "fast", "run" };
  char b[160];
  switch (k) {
    case 1: std::snprintf(b, sizeof b, TR("Стрелки: %s", "Arrows: %s"), ActorName(PhotoActorNow())); break;
    case 2: {
      const int g = std::max(0, std::min(3, photoGear.load()));
      std::snprintf(b, sizeof b, TR("Скорость: %s", "Speed: %s"), g_lang ? kGearEn[g] : kGearRu[g]);
      break;
    }
    case 3: {
      const int f = photoFollow.load();
      std::snprintf(b, sizeof b, "%s", f == 2 ? TR("Камера: едет за персонажем и смотрит на него", "Camera: tracks and aims at the actor")
                                      : f == 1 ? TR("Камера: едет за персонажем", "Camera: tracks the actor")
                                               : TR("Камера: стоит", "Camera: static"));
      break;
    }
    case 4: std::snprintf(b, sizeof b, "%s", TR("ИИ Чужого включён — стрелки не ведут его (раздел «Атака и ИИ» → «Ручное»)",
                                                 "The Alien's AI is on — arrows can't steer it (Attack & AI → Manual)")); break;
    default: return "";
  }
  return b;
}
void ActPhotoActor(int kind) {
  photoActor = std::max(0, std::min(2, kind));
  Toast(1);
}
void ActPhotoFollow(int mode) {
  photoFollow = std::max(0, std::min(2, mode));
  Toast(3);
}
static int NextActor(int a) {  // Tab: камера -> Чужой -> лицехват/андроид -> камера (кого нет — пропускаем)
  for (int i = 0; i < 3; i++) {
    a = (a + 1) % 3;
    if (a == PA_CAMERA || ActorEntity(a)) return a;
  }
  return PA_CAMERA;
}
// за кем едет камера: за тем, кого ведут; стрелки у камеры — за Чужим (или за тем, за кого играешь)
static addr FollowEntity(float* aimH) {
  int a = PhotoActorNow();
  if (a == PA_CAMERA) a = npc ? PA_ALIEN : hugger ? PA_NPC : PA_CAMERA;
  *aimH = a == PA_ALIEN ? 1.2f : NpcCtlKind() == 2 ? 1.45f : 0.2f;
  return ActorEntity(a);
}
static bool s_followInit = false;
static addr s_followWho = 0;
static V3 s_followOff{};
static float s_aimYawOff = 0, s_aimPitchOff = 0;
static void ActorKeys(ms_t t) {
  static ms_t tabAt = 0, gearAt = 0, followAt = 0;
  static bool entWas = false;
  if (Down(VK_TAB) && t - tabAt > 300) {
    tabAt = t;
    photoActor = NextActor(PhotoActorNow());
    Toast(1);
    Log("photo: arrows -> %s", ActorName(PhotoActorNow()));
  }
  if ((Down(VK_PRIOR) || Down(VK_NEXT)) && t - gearAt > 250) {
    gearAt = t;
    photoGear = std::max(0, std::min(3, photoGear.load() + (Down(VK_PRIOR) ? 1 : -1)));
    Toast(2);
  }
  if (Down('T') && t - followAt > 350) {
    followAt = t;
    photoFollow = (photoFollow.load() + 1) % 3;
    s_followInit = false;
    s_aimYawOff = s_aimPitchOff = 0;
    Toast(3);
  }
  const bool ent = Down(VK_RETURN);
  if (ent && !entWas && PhotoActorNow() != PA_CAMERA) s_attackReq = PhotoActorNow();
  entWas = ent;
  // Чужой на ИИ стрелок не слушает — подсказать один раз на нажатие
  static bool arrowsWas = false;
  const bool arrows = Down(VK_UP) || Down(VK_DOWN) || Down(VK_LEFT) || Down(VK_RIGHT);
  if (arrows && !arrowsWas && PhotoActorNow() == PA_ALIEN && aiMode != 0) Toast(4);
  arrowsWas = arrows;
}

static void PhotoTick() {  // 16 мс
  ms_t t = NowMs();
  const float dt = std::min(0.05f, float(t - (s_last ? s_last : t)) / 1000.f);
  s_last = t;
  if (HotK(K_PHOTO) && t - s_keyAt > 400) {
    s_keyAt = t;
    ActPhoto(!photo.on);
  }
  if (!photo.on) return;
  if (GameMenuPaused()) {  // игра на паузе (меню Esc): камера стоит, ввод — меню (тестер: камера летала на паузе)
    photoMouseDX = photoMouseDY = photoWheel = 0;
    return;
  }
  if (Hot('X') && t - s_freezeKeyAt > 400) {
    s_freezeKeyAt = t;
    ActFreezeTime(!photo.freeze);
  }
  static ms_t hugKeyAt = 0;  // H (своя клавиша лицехвата) — лицехват туда, куда смотрит фрикам
  if (Hot(g_keys[K_HUGGER]) && t - hugKeyAt > 400) {
    hugKeyAt = t;
    ActSpawnHugger();
  }
  static ms_t focusKeyAt = 0;
  if (Hot('F') && t - focusKeyAt > 300) {  // F — фокус (DOF) на то, что в центре кадра
    focusKeyAt = t;
    photoFocusPick = 1;
  }
  KeepTime();
  ActorKeys(t);
  const int follow = photoFollow.load();
  // взгляд: мышь (Raw Input перехвачен в input_block.cpp) и стрелки (если они не у персонажа). Камера смотрит на
  // персонажа — мышь сдвигает взгляд относительно него
  const int mx = photoMouseDX.exchange(0), my = photoMouseDY.exchange(0), wheel = photoWheel.exchange(0);
  float& lookYaw = follow == 2 ? s_aimYawOff : photo.yaw;
  float& lookPitch = follow == 2 ? s_aimPitchOff : photo.pitch;
  lookYaw += mx * photo.sens * 0.0025f;
  lookPitch -= my * photo.sens * 0.0025f;
  const float turn = 1.6f * dt;
  if (PhotoActorNow() == PA_CAMERA) {
    if (Down(VK_LEFT)) lookYaw -= turn;
    if (Down(VK_RIGHT)) lookYaw += turn;
    if (Down(VK_UP)) lookPitch += turn;
    if (Down(VK_DOWN)) lookPitch -= turn;
  }
  s_aimYawOff = WrapPi(s_aimYawOff);
  s_aimPitchOff = std::max(-1.2f, std::min(1.2f, s_aimPitchOff));
  // наклон по Z
  if (Down('Z')) photo.roll -= 0.8f * dt;
  if (Down('C')) photo.roll += 0.8f * dt;
  if (Down('R')) photo.roll = 0;
  photo.roll = std::max(-kPi, std::min(kPi, photo.roll));
  // FOV колесом (шаг колеса 120)
  // FOV колесом: шаг — доля от текущего (на широком угле крупнее, на зуме мельче), Ctrl — совсем мелко, Shift — крупно;
  // сам FOV плавно доезжает до цели
  if (wheel) {
    const float from = s_fovTarget > 0 ? s_fovTarget : photo.fov > 0 ? photo.fov : s_gameFov;
    const float k = Down(VK_CONTROL) ? 0.005f : Down(VK_SHIFT) ? 0.06f : 0.02f;
    s_fovTarget = std::max(5.f, std::min(140.f, from * (1.f - wheel / 120.f * k)));
    if (photo.fov <= 0) photo.fov = s_gameFov;
  }
  if (s_fovTarget > 0) {
    const float d = s_fovTarget - photo.fov;
    if (std::fabs(d) < 0.02f) photo.fov = s_fovTarget, s_fovTarget = 0;
    else photo.fov += d * std::min(1.f, dt * 10.f);
  }
  photo.yaw = WrapPi(photo.yaw);
  photo.pitch = std::max(-1.55f, std::min(1.55f, photo.pitch));
  // полёт
  float sp = photo.speed * (Down(VK_SHIFT) ? 3.f : Down(VK_CONTROL) ? 0.25f : 1.f) * dt;
  const V3 f = Forward();
  const float rx = std::cos(photo.yaw), rz = -std::sin(photo.yaw);  // вправо
  float mvF = float(Down('W') - Down('S')), mvR = float(Down('D') - Down('A')), mvU = float(Down('E') - Down('Q'));
  const V3 step{ (f.x * mvF + rx * mvR) * sp, (f.y * mvF + mvU) * sp, (f.z * mvF + rz * mvR) * sp };
  photo.pos.x += step.x, photo.pos.y += step.y, photo.pos.z += step.z;
  // слежение: камера держит смещение от персонажа (WASD его меняет), плавно; в режиме 2 ещё и смотрит на него
  if (follow) {
    float aimH = 1.f;
    const addr e = FollowEntity(&aimH);
    V3 ap{};
    if (e && Safe([&] { ap = rv3(e + 0x70); }) && std::isfinite(ap.x)) {
      if (!s_followInit || e != s_followWho) {
        s_followInit = true, s_followWho = e;
        s_followOff = { photo.pos.x - ap.x, photo.pos.y - ap.y, photo.pos.z - ap.z };
      } else {
        s_followOff.x += step.x, s_followOff.y += step.y, s_followOff.z += step.z;
      }
      const float k = 1.f - std::exp(-dt * 6.f);
      photo.pos.x += (ap.x + s_followOff.x - photo.pos.x) * k;
      photo.pos.y += (ap.y + s_followOff.y - photo.pos.y) * k;
      photo.pos.z += (ap.z + s_followOff.z - photo.pos.z) * k;
      if (follow == 2) {
        const float dx = ap.x - photo.pos.x, dy = ap.y + aimH - photo.pos.y, dz = ap.z - photo.pos.z;
        if (dx * dx + dz * dz > 1e-4f) {
          photo.yaw = WrapPi(std::atan2(dx, dz) + s_aimYawOff);
          photo.pitch = std::max(-1.55f, std::min(1.55f, std::atan2(dy, std::sqrt(dx * dx + dz * dz)) + s_aimPitchOff));
        }
      }
    }
  } else {
    s_followInit = false;
  }
}

// отцепление мода (reload.cpp): фоторежим выключить, не трогая камеры игры из чужого потока — камеру Аманде вернёт
// CamDo в потоке игры (camWant = 3)
void PhotoOffForUnload() {
  if (!photo.on) return;
  photo.on = false;
  g_photoOn = false;
  ApplyTime();
  camWant = 3, camKeyAt = NowMs();
  Log("photo off (unload)");
}

void ActPhotoToPlayer() {  // вернуть камеру к Аманде
  if (!player) return;
  photo.pos = rv3(player + 0x70);
  photo.pos.y += 1.6f;
  photo.yaw = rf(player + 0x174);
  photo.pitch = photo.roll = 0;
}
float PhotoGameFov() { return s_gameFov; }

void PhotoReset() {  // смена уровня / Чужой уничтожен
  if (photo.on) {
    photo.on = false;
    g_photoOn = false;
  }
  photo.freeze = false;
  photo.timeScale = 1.f;
  Safe([] { ApplyTime(); });  // время игры — как было
}

void InstallPhoto() {
  using Interceptor::Attach;
  Attach(VA(va::CamVelocity), OnCamVelocityEnter, nullptr);
  Attach(VA(va::CamSubmit), OnCamSubmitEnter, OnCamSubmitLeave);
  Attach(VA(va::ShaderClock), OnShaderClockEnter, OnShaderClockLeave, false);  // только атомики и глобалы игры
  Attach(VA(va::DeltaClamp), OnDeltaClampEnter, nullptr, false);
  Attach(VA(va::PhysicsTask), OnPhysicsTaskEnter, nullptr, false);
  Attach(VA(va::PhysicsStep), OnPhysicsStepEnter, nullptr, false);
  // итог сбрасывается в начале каждого смешивания — наши + и × не копятся; без замка (может идти в потоке рендера)
  Attach(VA(va::PostBlend), nullptr, OnPostBlendLeave, false);
  Every(16, "photo", PhotoTick);
}
