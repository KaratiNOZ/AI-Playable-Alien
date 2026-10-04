// common.h — общие помощники мода: адреса, чтение/запись памяти, защищённые вызовы, время, клавиши, лог, планировщик.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <cmath>
#include <string>
#include <type_traits>
#include <atomic>

static_assert(sizeof(void*) == 4, "AlienMod собирается только под x86: AI.exe 32-битный");

using addr = uintptr_t;
using ms_t = long long;

// ---- адреса: Ghidra показывает VA с базой 0x400000, в процессе база другая (ASLR) ----
extern addr g_base, g_modEnd;
inline addr VA(addr va) { return g_base + (va - 0x400000); }
inline addr ToVA(addr p) { return p - g_base + 0x400000; }
inline bool InGame(addr p) { return p >= g_base && p < g_modEnd; }

// ---- память (аналог ptr.readFloat()/writeFloat() во Frida; ошибку доступа ловит Safe) ----
template <class T> inline T rd(addr a) { return *reinterpret_cast<volatile T*>(a); }
template <class T> inline void wr(addr a, T v) { *reinterpret_cast<volatile T*>(a) = v; }
inline addr rp(addr a) { return rd<addr>(a); }
inline float rf(addr a) { return rd<float>(a); }
inline void wf(addr a, float v) { wr<float>(a, v); }
struct V3 { float x, y, z; };
inline V3 rv3(addr a) { return { rf(a), rf(a + 4), rf(a + 8) }; }
inline void wv3(addr a, float x, float y, float z) { wf(a, x); wf(a + 4, y); wf(a + 8, z); }

// ---- Safe(f): выполнить f, а ошибку доступа к памяти превратить в false (как try/catch вокруг чтений во Frida).
// Работает через векторный обработчик исключений и маленькую заглушку в машинных кодах (runtime.cpp),
// поэтому одинаково собирается и MSVC, и MinGW. Деструкторы внутри прерванного f не вызываются — только чтения/записи.
typedef void (*SafeFn)(void*);
typedef int (*SafeCallFn)(SafeFn, void*);
extern SafeCallFn g_safeCall;
template <class F> bool Safe(F&& f) {
  using L = typename std::remove_reference<F>::type;
  L* pf = &f;
  return g_safeCall([](void* p) { (*static_cast<L*>(p))(); }, const_cast<void*>(static_cast<const void*>(pf))) != 0;
}
template <class T> bool SafeRead(addr a, T& out) { return Safe([&] { out = rd<T>(a); }); }
bool Readable(const MEMORY_BASIC_INFORMATION& m);
void LogStackChain(const char* tag, uint32_t esp);  // runtime.cpp: адреса возврата в коде игры — в лог

constexpr float kPi = 3.14159265358979f;
inline float WrapPi(float a) { return std::remainder(a, 2 * kPi); }
// ---- клавиши и ImGui ----
// g_uiKeysBlocked: ImGui набирает текст (поле имени уровня) — клавиатура мода молчит, иначе B/V/1/2... сработают при наборе.
// g_uiMouseBlocked: открыто меню DevTools — ЛКМ не атакует (клик по кнопке меню не должен запускать атаку).
// g_uiMenuShown: блок ввода Аманды пропускает сообщения окна, чтобы ImGui их получил (иначе меню не нажать).
// g_hotkeysOn: галочка в меню — выключить горячие клавиши мода (движение WASD/стрелки остаётся).
// g_simpleKeys: «простое управление» — работают только главные клавиши (B, WASD, мышь, E Q V, F2 F3 F9),
// остальные (R F C, цифровой блок, F1 F4 F5, спавн F7 F8) молчат, чтобы не нажать случайно; всё это — в меню.
inline std::atomic<bool> g_uiKeysBlocked{ false }, g_uiMouseBlocked{ false }, g_uiMenuShown{ false }, g_hotkeysOn{ true };
inline std::atomic<bool> g_simpleKeys{ true };
// Esc, пока играешь за Чужого или лицехвата: камера и управление остаются у него, но на время паузы игры ввод идёт
// игре (меню паузы), а клавиши мода молчат. Снимается, когда игра снова пошла (EscTick в control.cpp).
inline std::atomic<bool> g_pausePass{ false };
// отцепление мода (reload.cpp): 1 — поток рендера должен выключить ImGui и вернуть Present/WndProc, 2 — сделал,
// 3 — рендер не ответил, рисовать больше нельзя
inline std::atomic<int> g_uiUnload{ 0 };
// отладка (dev\cmd.txt: shot [сколько] [мс]): снимки кадра игры прямо из back buffer — dev\shot_N.bmp (standalone.cpp)
inline std::atomic<int> g_shotLeft{ 0 }, g_shotEvery{ 300 }, g_shotIdx{ 0 };
inline std::atomic<bool> g_photoOn{ false };  // фоторежим: ввод в игру не идёт, мышь — нашей камере
// язык интерфейса: 0 — русский, 1 — английский (по умолчанию — по языку Windows, переключается в меню)
inline std::atomic<int> g_lang{ 0 };
inline const char* TR(const char* ru, const char* en) { return g_lang ? en : ru; }
inline const wchar_t* TRW(const wchar_t* ru, const wchar_t* en) { return g_lang ? en : ru; }
ms_t NowMs();
// окно игры сейчас активно (после Alt+Tab мод не читает клавиатуру и мышь — нажатия в других окнах ничего не делают).
// Проверка по процессу активного окна, раз в 50 мс (Down зовут очень часто).
inline bool GameFocused() {
  static std::atomic<ms_t> at{ 0 };
  static std::atomic<bool> focused{ true };
  const ms_t t = NowMs();
  if (t - at.load() >= 50) {
    DWORD pid = 0;
    if (HWND w = GetForegroundWindow()) GetWindowThreadProcessId(w, &pid);
    focused = pid == GetCurrentProcessId();
    at = t;
  }
  return focused;
}
inline bool Down(int vk) {
  if (!GameFocused() || g_pausePass) return false;
  if (vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON) {
    if (g_uiMouseBlocked) return false;
  } else if (g_uiKeysBlocked) {
    return false;
  }
  return (GetAsyncKeyState(vk) & 0x8000) != 0;
}
// ---- переназначаемые клавиши (меню «Клавиши», сохраняются в .ini). Движение (WASD, Ctrl/Shift/Пробел), мышь и
// клавиши фоторежима — постоянные ----
enum KeyAct {
  K_SWAP, K_VIEW, K_VENT, K_VENT_AMANDA, K_UNSTUCK,  // главные
  K_STRAFE, K_FOLLOW, K_PATH,                        // за Чужого, дополнительные
  K_CHARS, K_HUD, K_MAP, K_AI, K_PANIC, K_SPAWN_FIND, K_SPAWN, K_PHOTO,
  K_LOOK, K_SOUND,                                   // за Чужого: держать — осматривается; звук (рык/шипение)
  K_HUGGER,                                          // играть за лицехвата (следующего)
  K_MENU,                                            // меню мода (Insert открывает его всегда)
  K_TURN180,                                         // за Чужого: резкий разворот на 180 (анимацией, камера — вместе с телом)
  K_COUNT
};
inline constexpr int kKeyDefault[K_COUNT] = { 'B', 'V', 'E', 'Q', 'X', 'R', 'F', 'C',
                                              VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F7, VK_F8, VK_F9, 'Z', 'G', 'H', VK_DELETE, 'T' };
inline std::atomic<int> g_keys[K_COUNT] = { 'B', 'V', 'E', 'Q', 'X', 'R', 'F', 'C',
                                            VK_F1, VK_F2, VK_F3, VK_F4, VK_F5, VK_F7, VK_F8, VK_F9, 'Z', 'G', 'H', VK_DELETE, 'T' };
// в меню ждём новую клавишу — горячие клавиши мода молчат, чтобы нажатие не сработало как действие
inline std::atomic<bool> g_rebinding{ false };
// Esc, пока играешь за Чужого: камера возвращается Аманде и ввод сразу идёт игре (меню паузы), не дожидаясь смены камеры.
// Сбрасывается, когда снова берёшь Чужого.
inline std::atomic<bool> g_amandaInput{ false };

// горячая клавиша мода (переключатели режимов); движение и модификаторы скорости читаются через Down
inline bool Hot(int vk) { return g_hotkeysOn && !g_rebinding && Down(vk); }
// Нажатия клавиш действий ловит отдельный опрос раз в 5 мс (KeyPollTick в ui.cpp) и держит 300 мс: задачи, которые их
// читают, тикают реже (до 200 мс), и короткое нажатие между их тиками раньше терялось. Старое нажатие не срабатывает
// «потом», когда действие стало доступно.
inline std::atomic<ms_t> g_keyHitAt[K_COUNT];
inline bool KeyHit(int act) {
  const ms_t at = g_keyHitAt[act].exchange(0);
  return at && NowMs() - at < 300;
}
// в фоторежиме действия мода молчат (F1–F8, B, E...) — работает только сама клавиша фоторежима
inline bool HotK(int act) {
  if (g_photoOn && act != K_PHOTO) return false;
  const bool hit = KeyHit(act);  // забираем нажатие в любом случае
  return g_hotkeysOn && !g_rebinding && (hit || Down(g_keys[act]));
}
// «дополнительные» клавиши работают всегда: назначенная клавиша должна срабатывать, а «Простое управление» только
// сокращает подсказки на экране (раньше оно глушило R/F/C/F1/F4/F5/F7/F8 — выглядело как «кнопки не работают»)
inline bool HotAdv(int vk) { return Hot(vk); }
inline bool HotAdvK(int act) { return HotK(act); }
// клавиша, которую держат (осмотреться): без фронта нажатия, просто «зажата ли сейчас»
inline bool HeldK(int act) { return !g_photoOn && g_hotkeysOn && !g_rebinding && Down(g_keys[act]); }
inline std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }  // только ASCII

void Log(const char* fmt, ...);

// ---- один общий замок: во Frida весь JS шёл под одной блокировкой (хуки в потоке игры ждали таймеры и наоборот),
// на этом держится вся логика с общими указателями — сохраняем ту же модель ----
void JsLock();
void JsUnlock();

// ---- setInterval: периодические задачи в своём потоке, под замком и в Safe ----
void Every(int periodMs, const char* name, void (*fn)());
