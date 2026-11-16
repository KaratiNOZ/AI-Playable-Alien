# Alien Playable

<p align="center">
  <b>Play as the Alien, Facehugger, and Android in Alien: Isolation</b>
</p>

<p align="center">
  <a href="#english">English</a> | <a href="#русский">Русский</a>
</p>

---

<a name="english"></a>
## EN English Description

> **Beta Notice:** This mod may occasionally crash or behave unexpectedly. Please report bugs in the repository comments or issues.

Designed for the Steam version of the game (`AI.exe` from `17.02.2015`). On other builds (GOG, Epic, patches), the installer will warn you, but will still install and run the mod.

* **OS:** Windows 10 / 11 (Windows 7 should work, but is untested).
* **Requirements:** No extra software required — everything is packed into the mod files.

### Update — 16 November 2026

**0.21-beta (interim build)**

* T camera rotation follows the Alien's actual turn direction using cubic Bézier ease-in-out; third-person crawling also supports T.
* Faster owned standing-turn animations; “snappy Alien” skips the native walking start as well as the stop.
* Free first-person view reads the head position directly. Camera offsets use fixed character axes and apply immediately.
* Multiplayer menus are temporarily hidden. The backend is retained; network Alien idle look follows the camera and releases on movement input.
* Amanda following is now menu-only. F is reserved for hiding-place inspection, which is not implemented in this interim build.
* Known issues: the third-person T camera can jitter and reset its orbit after turning; Bézier synchronization is unfinished. Duct visibility and lighting remain experimental. Forced flamethrower retreat, hiding-place inspection and the audio/NPC work are still pending.

* **F6: floor crawling.** Uses the game's native crouched posture; normal walking speed returns after standing up.
* **Horizontal ducts:** keep moving into an opening to enter or leave automatically. The manual C path mode has been removed; C still rolls the camera in photo mode.
* Fixed the camera briefly jumping into the Alien's head when changing posture.
* Removed hidden recovery turns and interference with native animation completion that could block movement.
* Fixed menu scroll jumps, including the photo-mode film/grain controls; pages retain separate scroll positions.
* Hot reload now restores floor-crawl posture before detaching and distinguishes failure from an already detached mod.

### 🚀 Quick Start

1. Unpack the archive **anywhere except `Program Files`**.
2. Close the game.
3. Run `AlienModSetup.exe` and click **"Install"** (it detects the game automatically; use "Browse" if needed).
4. Start the game.
5. Run `runUI.bat` (or `AlienInjectorUI.exe`) — or use the desktop shortcut if you checked that option during installation.
6. Press **Delete** (or **Insert**) in-game to open the mod menu.

> *To uninstall:* Run `AlienModSetup.exe` -> **"Remove from game"** to restore original files.

### ⌨️ Controls & Features

#### 🕹️ Main Controls (Always Active)
| Key | Action |
| :--- | :--- |
| **B** | Play as the Alien / Switch back to Amanda Ripley |
| **WASD** | Movement (*Ctrl* — sneak, *Shift* — fast walk, *Space* — run) |
| **Mouse** | Look around / Camera control |
| **Mouse Wheel** | Camera distance (*Shift/Ctrl + wheel* — height/sideways, *Middle button* — reset) |
| **LMB / RMB** | Attack / Change target |
| **E** | Vents: hide, come out here, or cancel |
| **Q** | Come out near Amanda |
| **V** | Toggle camera view (third-person over-the-shoulder / first-person head view) |
| **Z (hold)** | LOOK: The Alien turns its head and body towards the camera crosshair |
| **G** | Alien voice (*Shift* — random ambient sound) |
| **H** | Switch to a Facehugger (*Press again for the next one; B to return to Amanda*) |
| **Esc** | Game pause (the menu stays active, control remains with your character) |
| **F2 / F3** | On-screen hints / Vent map |
| **F6** | Toggle floor crawling |
| Keep moving into an opening | Enter or leave a horizontal duct automatically |
| **F9** | Photo mode |
| **Delete / Insert** | Open / Close mod menu |

#### 🦠 Facehugger & Android Mechanics
* **Facehugger:** Press **RMB** to select a nearby target, then **LMB** to leap and attack. Victims die using the game's native facehugger animation (`ACT_DEATH_FROM_FACEHUGGER`).
* **Android (Working Joe):** Switch via **H** (shares the cycle with facehuggers). Move with **WASD**, attack with **LMB** (hit/strangle). Press **G** to play authentic game voice lines.
* **AI Modes:** Other facehuggers and androids can be set to different AI behaviors via the menu: *Manual (idle)*, *Hunt anyone except Amanda*, or *Full AI*.

#### 📷 Photo Mode (F9)
| Key / Control | Action |
| :--- | :--- |
| **WASD / Q / E** | Free camera movement |
| **Mouse / Arrow Keys** | Look around |
| **X** | Stop / Resume time (freezes animations, physics, and the tail) |
| **F** | Auto-focus on center (depth of field) |
| **Mouse Wheel** | Smooth FOV adjustment (*Ctrl + wheel* for fine-tuning) |

* **Post-Processing (in menu):** Presets (Noir, Warm, Cold, 1979 film look), exposure, glow, grain, vignette, chromatic aberration, lens distortion, and aspect ratio crops (2.39:1, 4:3, 1:1, etc.).
* **Machinima tip:** You can move the Alien, Facehugger, or Android using arrow keys while in Photo Mode to set up custom scenes.

---
---

<a name="русский"></a>
## 🇷🇺 Описание на русском

<p align="center">
  <b>Играйте за Чужого, Лицехвата и Андроида в Alien: Isolation</b>
</p>

> **Бета:** Мод находится в разработке, возможны баги и вылеты. Пожалуйста, сообщайте о проблемах в комментариях или issues.

Сделано под Steam-версию игры (`AI.exe` от `17.02.2015`). На других сборках (GOG, Epic, патчи) установщик предупредит вас, но всё равно установит и запустит мод.

* **ОС:** Windows 10 / 11 (Windows 7 должна работать, но не тестировалась).
* **Требования:** Ничего доустанавливать не нужно — всё уже вшито в файлы мода.

### Обновление — 16 ноября 2026

**0.21-beta — промежуточная сборка**

* Камера при T следует направлению разворота Чужого по кривой Bézier ease-in-out. T доступна и при ползании от третьего лица.
* Ускорены собственные анимации разворота. «Удобный Чужой» пропускает начало ходьбы, как и остановку.
* Свободный вид от первого лица берёт позицию головы напрямую. Смещения задаются в постоянных осях тела и применяются сразу.
* Мультиплеер временно скрыт из меню, его код сохранён. В сетевой игре осмотр Чужого включается в покое и отключается кнопкой движения.
* Следование Аманды осталось только в меню. F освобождена для осмотра укрытий; сам осмотр в эту сборку ещё не вошёл.
* Известные ограничения: камера T за спиной может дёргаться и сбрасывать ракурс после разворота; синхронизация Bézier ещё не закончена. Видимость и освещение в шахтах остаются экспериментальными. Неотменяемое отступление от огнемёта, осмотр укрытий, звук и реакции NPC ещё в работе.

* **F6: ползание по полу.** Использует штатную позу игры; после вставания возвращается обычная скорость ходьбы.
* **Горизонтальная вентиляция:** продолжайте двигаться в отверстие, чтобы автоматически войти или выйти. Ручной режим пути на C убран; C в фоторежиме по-прежнему наклоняет камеру.
* Исправлен кратковременный скачок камеры в голову Чужого при смене позы.
* Убраны скрытые восстановительные развороты и вмешательство в завершение анимаций, способные блокировать движение.
* Исправлены скачки прокрутки меню, в том числе при настройке зерна в фоторежиме; у страниц отдельные позиции прокрутки.
* При горячей замене сначала восстанавливается обычная поза; ошибка отключения больше не считается успешным отключением.

### 🚀 Быстрый старт

1. Распакуйте архив **куда угодно, кроме `Program Files`**.
2. Закройте игру.
3. Запустите `AlienModSetup.exe` и нажмите **«Установить»** (папку найдёт сама; при необходимости укажите вручную через «Обзор»).
4. Запустите игру.
5. Запустите `runUI.bat` (or `AlienInjectorUI.exe`) — либо используйте ярлык с рабочего стола, если включили его создание при установке.
6. В игре нажмите **Delete** (или **Insert**), чтобы открыть меню мода.

> *Для удаления:* Запустите `AlienModSetup.exe` -> **«Удалить из игры»** для восстановления оригинальных файлов.

### ⌨️ Управление и Возможности

#### 🕹️ Основное управление (Работает всегда)
| Клавиша | Действие |
| :--- | :--- |
| **B** | Играть за Чужого / вернуться к Аманде Рипли |
| **WASD** | Движение (*Ctrl* — тихо, *Shift* — быстро, *Пробел* — бег) |
| **Мышь** | Осмотр / управление камерой |
| **Колесо мыши** | Расстояние камеры (*Shift/Ctrl + колесо* — высота/сдвиг, *Средняя кнопка* — сброс) |
| **ЛКМ / ПКМ** | Атака / смена цели |
| **E** | Вентиляция: спрятаться, вылезти здесь или отменить |
| **Q** | Вылезти рядом с Амандой |
| **V** | Переключение вида (от третьего лица из-за спины / из головы) |
| **Z (удерживать)** | LOOK: Чужой поворачивает голову и корпус туда, куда смотрит прицел камеры |
| **G** | Голос Чужого (*Shift* — случайный звук по обстановке) |
| **H** | Переключиться на Лицехвата (*Ещё раз — на следующего; B — вернуться к Аманде*) |
| **Esc** | Пауза игры (меню активно, управление персонажем сохраняется) |
| **F2 / F3** | Подсказки на экране / Карта вентиляции |
| **F6** | Включить / выключить ползание по полу |
| Продолжать движение в отверстие | Автоматический вход / выход из горизонтальной вентиляции |
| **F9** | Фоторежим |
| **Delete / Insert** | Открыть / Закрыть меню мода |

#### 🦠 Лицехват и Андроид
* **Лицехват:** Нажмите **ПКМ**, чтобы выбрать ближайшую цель, затем **ЛКМ** для прыжка и атаки. Жертвы погибают с оригинальной анимацией игры (`ACT_DEATH_FROM_FACEHUGGER`).
* **Андроид (Рабочий Джо):** Переключение через **H** (в общем цикле с лицехватами). Движение на **WASD**, атака на **ЛКМ** (удар/удушение). Нажатие **G** воспроизводит настоящие реплики андроидов из игры.
* **Режимы ИИ:** Остальным лицехватам и андроидам можно задать поведение через меню: *Ручное (стоят)*, *Охота на всех, кроме Аманды* или *Полный ИИ*.

#### 📷 Фоторежим (F9)
| Клавиша / Управление | Действие |
| :--- | :--- |
| **WASD / Q / E** | Свободное перемещение камеры |
| **Мышь / Стрелки** | Вращение камеры |
| **X** | Остановить / Возобновить время (замораживает анимации, физику и хвост) |
| **F** | Автофокус на центр (глубина резкости) |
| **Колесо мыши** | Плавная регулировка FOV (*Ctrl + колесо* — для точной настройки) |

* **Постобработка (в меню):** Пресеты (нуар, тёплый, холодный, плёнка 1979 года), экспозиция, свечение, зерно, виньетка, хроматическая аберрация, искажение линзы и обрезка кадра (каше 2.39:1, 4:3, 1:1 и т.д.).
* **Совет по мачиниме:** Находясь в фоторежиме, вы можете двигать Чужого, Лицехвата или Андроида с помощью клавиш со стрелками для настройки сцен.
