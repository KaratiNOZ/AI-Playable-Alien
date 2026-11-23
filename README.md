# Alien Playable

<p align="center">
  <a href="https://github.com/KaratiNOZ/AI-Playable-Alien">
    <img src="https://img.shields.io/badge/GitHub-Repository-181717?style=for-the-badge&logo=github" alt="GitHub">
  </a>
  <a href="https://www.nexusmods.com/alienisolation/mods/181">
    <img src="https://img.shields.io/badge/Nexus_Mods-Download-orange?style=for-the-badge&logo=nexusmods" alt="Nexus Mods">
  </a>
  <a href="https://discord.gg/tNUWtQ6m9m">
    <img src="https://img.shields.io/badge/Discord-Community-5865F2?style=for-the-badge&logo=discord&logoColor=white" alt="Discord">
  </a>
  <a href="https://www.youtube.com/@pirlet8014">
    <img src="https://img.shields.io/badge/YouTube-Videos-FF0000?style=for-the-badge&logo=youtube&logoColor=white" alt="YouTube">
  </a>
</p>

<p align="center">
  <a href="#english">English</a> | <a href="#русский">Русский</a>
</p>

---

<a name="english"></a>
## 🇬🇧 English

<p align="center">
  <b>Play as the Alien, Facehugger, and Android in Alien: Isolation.</b>
</p>

Gagen's Playable Alien is a mod that lets you take control of different creatures and characters from Alien: Isolation, using their native animations, movement and abilities.

> **Beta Notice:** The mod is still under development. Bugs and occasional crashes may occur. Please report issues in the repository.

**Requirements**
- Windows 10 / 11
- Steam version recommended (`AI.exe` from `17.02.2015`)
- No additional software required

### Update — 17 November 2026

**0.22.1-beta**

- **Horizontal ducts: crash and entry fix.** Entering a low duct no longer freezes the game when the Alien has no sensed target, and entry assistance works in that case again: a valid target record from a neighboring manager slot is borrowed for the path node's tick and restored afterwards. Verified in-game with full enter/exit cycles.
- **F: hiding-place inspection.** Hold F near a locker to play the native inspection sequence; the progress bar shows its actual stages. Releasing F, moving, or disabling actions cancels the attempt. A catalog of 4192 inspection points across 35 levels is built in. Tall lockers are confirmed working; small lockers and tables are not detected yet.
- **Forced flamethrower retreat.** The native retreat gauge sends the Alien backstage; movement, attacks, crawling, unstuck, teleport and character switching are locked during the retreat. In multiplayer a human player can trigger the Alien owner's retreat through the new W_RETREAT packet (host-relayed, with participant, role and level checks); all participants need updated builds.
- The T camera follows the Alien's actual turn motion; only the initial camera/body offset eases out by cubic Bézier, and the final course aligns with the model. Turn transitions are driven only by the Alien's own locomotion, not by other NPCs.
- First-person free view: the neck limit applies only when body turns are enabled; with turns off or in a duct the view rotates freely.
- Hiding-place context hints are independent of the status HUD; F2 hides both.

**Known Issues:** The Alien model can jitter during T turns (cause not yet proven). Small lockers/tables are not inspected. Multiplayer retreat has not been tested in a live two-player session. Duct visibility and lighting remain experimental. The stereo listener, audio and NPC reactions are still in development.

### 🚀 Quick Start

1. Unpack the archive **anywhere except `Program Files`**.
2. Close the game.
3. Run `AlienModSetup.exe`, open **Installation**, check the game folder and select **Install**. Keep `AlienModSetupWorker.exe` beside the installer.
4. Click **Play with mod**.
5. Press **Delete** or **Insert** in-game to open the mod menu.

> **Restore original files** rolls back the installed game files. **Verify files** checks the selected components. In **Diagnostics**, you can copy the full report and find the installer, game, and crash logs.

### ⌨️ Controls

| Key | Action |
| :--- | :--- |
| **B** | Play as the Alien / Return to Amanda |
| **WASD** | Movement |
| **LMB / RMB** | Attack / Select target |
| **E** | Vent interactions |
| **Q** | Emerge near Amanda |
| **V** | Switch camera view |
| **Z (hold)** | Turn towards camera crosshair |
| **G** | Alien voice |
| **H** | Switch to Facehugger / Android |
| **F6** | Toggle floor crawling |
| **F (hold)** | Inspect a hiding place (locker) |
| **F9** | Photo mode |
| **Delete / Insert** | Open / Close mod menu |

### 🦠 Other Features

- **Facehugger:** Select nearby targets and leap to attack.
- **Android:** Play as a Working Joe, move and attack.
- **AI Modes:** Configure behaviors of other Facehuggers and androids.
- **Photo Mode:** Free camera, time control and post-processing effects.

---

<a name="русский"></a>
## 🇷🇺 Русский

<p align="center">
  <b>Играйте за Чужого, Лицехвата и Андроида в Alien: Isolation.</b>
</p>

Gagen's Playable Alien — мод, позволяющий управлять разными существами и персонажами из Alien: Isolation, используя их оригинальные анимации, передвижение и способности.

> **Бета:** Мод находится в разработке. Возможны баги и вылеты. Пожалуйста, сообщайте о проблемах в репозитории.

**Требования**
- Windows 10 / 11
- Рекомендуется Steam-версия (`AI.exe` от `17.02.2015`)
- Дополнительное ПО не требуется

### Обновление — 17 ноября 2026

**0.22.1-beta**

- **Исправлен вход в горизонтальные шахты.** Игра больше не виснет, когда Чужой входит в низкую шахту без «почувствованной» цели, и помощь при входе снова работает в этом случае: годная запись цели временно берётся из соседнего слота менеджера на время тика узла пути и возвращается обратно. Проверено в игре полными циклами вход/выход.
- **F: осмотр укрытий.** Удерживайте F рядом со шкафом, чтобы прошла нативная последовательность осмотра; полоса прогресса показывает её реальные этапы. Отпускание F, движение или отключение действий отменяет попытку. Каталог из 4192 точек осмотра на 35 уровнях встроен в мод. Высокие шкафы подтверждены игроком; малые шкафы и столы пока не определяются.
- **Неотменяемое отступление от огнемёта.** Нативная шкала отступления уводит Чужого за кулисы; на время ухода заблокированы движение, атака, ползание, unstuck, телепорт и смена персонажа. В мультиплеере человек может запросить отступление владельцу Чужого новым пакетом W_RETREAT (ретрансляция хостом, проверка участников, ролей и уровня); всем участникам нужны обновлённые сборки.
- Камера T следует фактическому движению разворота Чужого; по Bézier сглаживается только начальное смещение камеры относительно тела, в конце курс выравнивается по модели. Переходы разворота запускаются только собственной локомоцией Чужого, а не чужими NPC.
- Свободный вид от первого лица: предел шеи действует только при включённых разворотах тела; при выключенных разворотах или в шахте взгляд вращается свободно.
- Контекстные подсказки укрытий независимы от статусного HUD; F2 скрывает обе.

**Известные проблемы:** Модель Чужого может дрожать при разворотах T (причина пока не доказана). Малые шкафы/столы не осматриваются. Отступление в мультиплеере не проверено в живой игре с двумя игроками. Видимость и освещение в шахтах остаются экспериментальными. Стереослушатель, звук и реакции NPC ещё в разработке.

### 🚀 Быстрый старт

1. Распакуйте архив **куда угодно, кроме `Program Files`**.
2. Закройте игру.
3. Запустите `AlienModSetup.exe`, откройте **«Установка»**, проверьте папку игры и нажмите **«Установить»**. Оставьте `AlienModSetupWorker.exe` рядом с установщиком.
4. Нажмите **«Играть с модом»**.
5. В игре нажмите **Delete** или **Insert**, чтобы открыть меню мода.

> **«Вернуть оригиналы»** восстанавливает изменённые файлы игры. **«Проверить файлы»** проверяет выбранные компоненты. В разделе **«Диагностика»** можно скопировать полный отчёт и найти логи установщика, игры и вылетов.

### ⌨️ Управление

| Клавиша | Действие |
| :--- | :--- |
| **B** | Играть за Чужого / вернуться к Аманде |
| **WASD** | Движение |
| **ЛКМ / ПКМ** | Атака / выбор цели |
| **E** | Взаимодействие с вентиляцией |
| **Q** | Вылезти рядом с Амандой |
| **V** | Переключение вида камеры |
| **Z (удерживать)** | Поворот в сторону прицела камеры |
| **G** | Голос Чужого |
| **H** | Переключение на Лицехвата / Андроида |
| **F6** | Включить / выключить ползание по полу |
| **F (удерживать)** | Осмотр укрытия (шкаф) |
| **F9** | Фоторежим |
| **Delete / Insert** | Открыть / закрыть меню мода |

### 🦠 Другие возможности

- **Лицехват:** Выбор ближайших целей и прыжок для атаки.
- **Андроид:** Управление Рабочим Джо, передвижение и атаки.
- **Режимы ИИ:** Настройка поведения других лицехватов и андроидов.
- **Фоторежим:** Свободная камера, управление временем и эффекты постобработки.
