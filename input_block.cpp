// input_block.cpp — пока камера у Чужого, Аманда не получает ввод (кроме Esc): GetAsyncKeyState из кода игры,
// WM_KEY*/WM_*BUTTON* из очереди сообщений, Raw Input клавиатура и кнопки мыши (движение мыши остаётся — им крутится камера).
// Источник: hot.js §3. Хуки без общего замка: читают только camMode (atomic).
// Сборка с DevTools: пока открыто меню ImGui (Delete), сообщения окна не трогаем — их получает ImGui через свой WndProc,
// а до игры их не пускает уже Menu.cpp. Delete (открыть/закрыть меню) и Esc не глушим никогда.
#include "interceptor.h"
#include "state.h"

// Esc, клавиша меню (Delete, её можно сменить) и Insert, F12 (скриншот Steam) и Print Screen до игры доходят всегда
static bool PassKey(uint32_t vk) {
  return vk == VK_ESCAPE || vk == uint32_t(g_keys[K_MENU].load()) || vk == VK_INSERT || vk == VK_F12 || vk == VK_SNAPSHOT;
}

// камера у Чужого или фоторежим — ввод не для Аманды (после Esc — снова для неё, см. EscTick в control.cpp)
static bool Blocking() { return (camMode != 0 && !g_amandaInput && !g_pausePass) || g_photoOn; }  // на паузе — игре
bool GameInputBlocked() { return Blocking(); }
static void OnGetAsyncKeyStateLeave(Invocation& inv) {
  if (!Blocking() || !InGame(inv.ra) || PassKey(inv.arg[0])) return;
  inv.replaceRet(0);
}

static void OnGetMessageLeave(Invocation& inv) {  // PeekMessage/GetMessage: MSG* = arg0
  addr ret = inv.retval();
  if (!Blocking() || g_uiMenuShown || ret == 0 || ret == addr(-1) || !inv.arg[0]) return;
  addr msg = inv.arg[0];
  uint32_t m = rd<uint32_t>(msg + 4);
  bool key = m >= WM_KEYDOWN && m <= WM_SYSKEYUP, mouse = m >= WM_LBUTTONDOWN && m <= 0x20d;
  if (!key && !mouse) return;
  if (key && PassKey(rd<uint32_t>(msg + 8))) return;
  wr<uint32_t>(msg + 4, WM_NULL);
}

static void OnGetRawInputDataLeave(Invocation& inv) {  // (hRawInput, uiCommand, pData, pcbSize, cbSizeHeader)
  addr data = inv.arg[2];
  // открыто меню: движение мыши до игры не доходит — камера (и фоторежим) стоит, пока крутишь ползунки
  if (g_uiMenuShown && InGame(inv.ra) && data && inv.arg[1] == RID_INPUT && int(inv.retval()) > 0 &&
      rd<uint32_t>(data) == RIM_TYPEMOUSE && !(rd<uint16_t>(data + sizeof(RAWINPUTHEADER)) & MOUSE_MOVE_ABSOLUTE)) {
    wr<int32_t>(data + sizeof(RAWINPUTHEADER) + 12, 0);
    wr<int32_t>(data + sizeof(RAWINPUTHEADER) + 16, 0);
  }
  // открыто меню: кнопки и клавиши до Аманды тоже не пускаем (тестер: клик мимо окна меню — Рипли бьёт); ImGui
  // читает сообщения окна, а не Raw Input — ему это не мешает. Колесо в меню — прокрутка меню, не камере
  if (!Blocking() || inv.arg[1] != RID_INPUT || !data || int(inv.retval()) <= 0) return;
  uint32_t type = rd<uint32_t>(data);
  addr body = data + sizeof(RAWINPUTHEADER);
  if (type == RIM_TYPEKEYBOARD) {
    if (PassKey(rd<uint16_t>(body + 6))) return;
    wr<uint16_t>(body + 2, RI_KEY_BREAK);  // Flags: отпускание
    wr<uint16_t>(body + 6, 0);             // VKey
    wr<uint32_t>(body + 8, WM_KEYUP);      // Message
  } else if (type == RIM_TYPEMOUSE) {
    if (!g_uiMenuShown && (rd<uint16_t>(body + 4) & RI_MOUSE_WHEEL)) {
      if (g_photoOn) photoWheel += int16_t(rd<uint16_t>(body + 6));  // фоторежим: колесо — FOV
      else camWheel += int16_t(rd<uint16_t>(body + 6));              // камера у Чужого: колесо — настройка камеры
    }
    wr<uint32_t>(body + 4, 0);  // usButtonFlags/usButtonData
    wr<uint32_t>(body + 8, 0);  // ulRawButtons
    if (g_photoOn && !g_uiMenuShown && !(rd<uint16_t>(body) & MOUSE_MOVE_ABSOLUTE)) {  // фоторежим: движение мыши — нашей камере
      photoMouseDX += rd<int32_t>(body + 12);
      photoMouseDY += rd<int32_t>(body + 16);
      wr<int32_t>(body + 12, 0);
      wr<int32_t>(body + 16, 0);
    } else if (!g_photoOn && !g_uiMenuShown && FpFreeActive() && !(rd<uint16_t>(body) & MOUSE_MOVE_ABSOLUTE)) {
      // свободный вид из головы: мышь крутит наш взгляд (camera.cpp), Аманда (её невидимый взгляд) стоит
      fpMouseDX += rd<int32_t>(body + 12);
      fpMouseDY += rd<int32_t>(body + 16);
      wr<int32_t>(body + 12, 0);
      wr<int32_t>(body + 16, 0);
    }
  }
}

// ---- курсор для меню ImGui ----
// Игра держит системный курсор: запирает его (ClipCursor) и возвращает в центр (SetCursorPos) — тогда курсор ImGui
// (он рисуется по позиции системного) стоит на месте, и ползунки меню не потянуть. Пока меню открыто, игре это не даём,
// а её последнее ограничение запоминаем и возвращаем, когда меню закрыли.
static RECT s_gameClip;
static bool s_gameClipOn = false;
static void OnClipCursorEnter(Invocation& inv) {  // BOOL ClipCursor(const RECT*)
  if (!InGame(inv.ra)) return;
  const RECT* r = reinterpret_cast<const RECT*>(inv.args[0]);
  s_gameClipOn = r != nullptr;
  if (r) s_gameClip = *r;
  if (g_uiMenuShown) inv.args[0] = 0;  // вместо ограничения игры — свободный курсор
}
static void OnSetCursorPosEnter(Invocation& inv) {  // BOOL SetCursorPos(int, int)
  if (!InGame(inv.ra) || !g_uiMenuShown) return;
  inv.cpu->eax = TRUE;
  inv.skip = true;
  inv.skipPop = 8;  // stdcall, 2 аргумента
}
void MenuCursor(bool menuShown) {  // из SetUiInput (поток рендера): меню открыли/закрыли
  if (menuShown) ClipCursor(nullptr);
  else if (s_gameClipOn) ClipCursor(&s_gameClip);
}

void InstallInputBlock() {
  HMODULE u32 = GetModuleHandleW(L"user32.dll");
  auto fn = [&](const char* name) { return addr(GetProcAddress(u32, name)); };
  using Interceptor::Attach;
  Attach(fn("GetAsyncKeyState"), nullptr, OnGetAsyncKeyStateLeave, false);
  for (const char* n : { "PeekMessageW", "PeekMessageA", "GetMessageW", "GetMessageA" }) Attach(fn(n), nullptr, OnGetMessageLeave, false);
  Attach(fn("GetRawInputData"), nullptr, OnGetRawInputDataLeave, false);
  Attach(fn("ClipCursor"), OnClipCursorEnter, nullptr, false);
  Attach(fn("SetCursorPos"), OnSetCursorPosEnter, nullptr, false);
}
