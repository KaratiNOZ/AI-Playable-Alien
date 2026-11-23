#pragma once
#include <cstddef>
enum class Text { Title, Language, Game, Components, Actions, Browse, Detect, Pick, Tree, Levels, MissingLevels, Shortcut, Install, Update, Restore, Verify, Play, ModFolder, Logs, ShowErrors, HideErrors, Copy, Open, MissingGame, Ready, Installed, Different, ConfirmContinue, Finding, Backup, Writing, Restoring, Checking, Done, Restored, Verified, Failed, Running, FileMissing, Mismatch, NoBackup, FileOperation, Admin, AdminError, ShortcutError, OpenError, LogGuide, InstallerLog, Local, LogPath, Home, Links, Credits, Releases, Theme, Amber, Terminal, Ice, Diagnostics, Count };
inline constexpr int kLanguageCount = 6;
inline constexpr const wchar_t* kLanguageNames[] = {L"English", L"Русский", L"Español", L"日本語", L"简体中文", L"Français"};
inline constexpr const wchar_t* kText[][kLanguageCount] = {
  {L"AlienMod Setup", L"Установка AlienMod", L"Instalador de AlienMod", L"AlienMod セットアップ", L"AlienMod 安装程序", L"Installation d’AlienMod"}, // Title
  {L"Language", L"Язык", L"Idioma", L"言語", L"语言", L"Langue"}, // Language
  {L"Game folder", L"Папка игры", L"Carpeta del juego", L"ゲームフォルダー", L"游戏文件夹", L"Dossier du jeu"}, // Game
  {L"Components", L"Компоненты", L"Componentes", L"コンポーネント", L"组件", L"Composants"}, // Components
  {L"Installation", L"Установка", L"Instalación", L"インストール", L"安装", L"Installation"}, // Actions
  {L"Browse…", L"Обзор…", L"Examinar…", L"参照…", L"浏览…", L"Parcourir…"}, // Browse
  {L"Find game", L"Найти игру", L"Buscar juego", L"ゲームを検索", L"查找游戏", L"Trouver le jeu"}, // Detect
  {L"Select the Alien: Isolation folder", L"Выберите папку Alien: Isolation", L"Selecciona la carpeta de Alien: Isolation", L"Alien: Isolation のフォルダーを選択", L"选择 Alien: Isolation 文件夹", L"Sélectionner le dossier d’Alien: Isolation"}, // Pick
  {L"Mod behaviour tree", L"Дерево поведения мода", L"Árbol de comportamiento", L"MOD のビヘイビアツリー", L"模组行为树", L"Arbre de comportement"}, // Tree
  {L"Level packages", L"Пакеты уровней", L"Paquetes de niveles", L"レベルパッケージ", L"关卡包", L"Packs de niveaux"}, // Levels
  {L"Level packages aren’t included in this archive.", L"В этом архиве нет пакетов уровней.", L"Este archivo no incluye paquetes de niveles.", L"このアーカイブにレベルパッケージはありません。", L"此压缩包未包含关卡包。", L"Cette archive ne contient aucun pack de niveaux."}, // MissingLevels
  {L"Desktop shortcut", L"Ярлык на рабочем столе", L"Acceso directo en el escritorio", L"デスクトップのショートカット", L"桌面快捷方式", L"Raccourci sur le bureau"}, // Shortcut
  {L"Install", L"Установить", L"Instalar", L"インストール", L"安装", L"Installer"}, // Install
  {L"Update / repair", L"Обновить / починить", L"Actualizar / reparar", L"更新・修復", L"更新 / 修复", L"Mettre à jour / réparer"}, // Update
  {L"Restore original files", L"Вернуть оригиналы", L"Restaurar originales", L"元のファイルに戻す", L"恢复原始文件", L"Restaurer les originaux"}, // Restore
  {L"Verify files", L"Проверить файлы", L"Verificar archivos", L"ファイルを確認", L"验证文件", L"Vérifier les fichiers"}, // Verify
  {L"Play with mod", L"Играть с модом", L"Jugar con el mod", L"MOD でプレイ", L"启动游戏及模组", L"Jouer avec le mod"}, // Play
  {L"Mod folder", L"Папка мода", L"Carpeta del mod", L"MOD フォルダー", L"模组文件夹", L"Dossier du mod"}, // ModFolder
  {L"Logs and diagnostics", L"Логи и диагностика", L"Registros y diagnóstico", L"ログと診断", L"日志和诊断", L"Journaux et diagnostic"}, // Logs
  {L"Show errors", L"Показать ошибки", L"Mostrar errores", L"エラーを表示", L"显示错误", L"Afficher les erreurs"}, // ShowErrors
  {L"Hide errors", L"Скрыть ошибки", L"Ocultar errores", L"エラーを隠す", L"隐藏错误", L"Masquer les erreurs"}, // HideErrors
  {L"Copy details", L"Копировать детали", L"Copiar detalles", L"詳細をコピー", L"复制详情", L"Copier les détails"}, // Copy
  {L"Open folder", L"Открыть папку", L"Abrir carpeta", L"フォルダーを開く", L"打开文件夹", L"Ouvrir le dossier"}, // Open
  {L"Choose a folder containing AI.exe and the game data.", L"Укажите папку с AI.exe и файлами игры.", L"Elige la carpeta con AI.exe y los datos del juego.", L"AI.exe とゲームデータのあるフォルダーを選択してください。", L"请选择包含 AI.exe 和游戏数据的文件夹。", L"Choisissez le dossier contenant AI.exe et les données du jeu."}, // MissingGame
  {L"Game found. Ready to install.", L"Игра найдена. Можно устанавливать.", L"Juego encontrado. Listo para instalar.", L"ゲームが見つかりました。インストールできます。", L"已找到游戏，可以安装。", L"Jeu trouvé. Prêt à installer."}, // Ready
  {L"Mod behaviour tree is installed.", L"Дерево поведения мода установлено.", L"El árbol de comportamiento está instalado.", L"MOD のビヘイビアツリーはインストール済みです。", L"已安装模组行为树。", L"L’arbre de comportement du mod est installé."}, // Installed
  {L"This AI.exe differs from the supported Steam build.", L"Версия AI.exe отличается от поддерживаемой сборки Steam.", L"Este AI.exe difiere de la versión compatible de Steam.", L"この AI.exe は対応する Steam 版と異なります。", L"此 AI.exe 与支持的 Steam 版本不同。", L"Cet AI.exe diffère de la version Steam prise en charge."}, // Different
  {L"Continue anyway?", L"Всё равно продолжить?", L"¿Continuar de todos modos?", L"続行しますか？", L"仍要继续吗？", L"Continuer quand même ?"}, // ConfirmContinue
  {L"Looking for the game…", L"Поиск игры…", L"Buscando el juego…", L"ゲームを検索中…", L"正在查找游戏…", L"Recherche du jeu…"}, // Finding
  {L"Backing up originals…", L"Резервное копирование…", L"Guardando los originales…", L"元のファイルをバックアップ中…", L"正在备份原始文件…", L"Sauvegarde des originaux…"}, // Backup
  {L"Installing files…", L"Установка файлов…", L"Instalando archivos…", L"ファイルをインストール中…", L"正在安装文件…", L"Installation des fichiers…"}, // Writing
  {L"Restoring originals…", L"Восстановление оригиналов…", L"Restaurando originales…", L"元のファイルを復元中…", L"正在恢复原始文件…", L"Restauration des originaux…"}, // Restoring
  {L"Verifying files…", L"Проверка файлов…", L"Verificando archivos…", L"ファイルを確認中…", L"正在验证文件…", L"Vérification des fichiers…"}, // Checking
  {L"Installation complete.", L"Установка завершена.", L"Instalación completada.", L"インストールが完了しました。", L"安装完成。", L"Installation terminée."}, // Done
  {L"Original files restored.", L"Оригинальные файлы восстановлены.", L"Archivos originales restaurados.", L"元のファイルを復元しました。", L"已恢复原始文件。", L"Fichiers originaux restaurés."}, // Restored
  {L"All selected files passed verification.", L"Выбранные файлы прошли проверку.", L"Se verificaron todos los archivos seleccionados.", L"選択したファイルの確認が完了しました。", L"所选文件均已通过验证。", L"Tous les fichiers sélectionnés ont été vérifiés."}, // Verified
  {L"Operation failed. See the details below.", L"Операция не выполнена. Подробности ниже.", L"La operación falló. Consulta los detalles.", L"処理に失敗しました。下の詳細を確認してください。", L"操作失败，请查看下方详情。", L"Échec de l’opération. Consultez les détails ci-dessous."}, // Failed
  {L"Close Alien: Isolation before changing its files.", L"Закройте Alien: Isolation перед изменением файлов.", L"Cierra Alien: Isolation antes de cambiar sus archivos.", L"ファイルを変更する前に Alien: Isolation を終了してください。", L"更改文件前请关闭 Alien: Isolation。", L"Fermez Alien: Isolation avant de modifier ses fichiers."}, // Running
  {L"Required file is missing", L"Нет необходимого файла", L"Falta un archivo necesario", L"必要なファイルがありません", L"缺少必需文件", L"Fichier requis introuvable"}, // FileMissing
  {L"File differs from the selected package", L"Файл отличается от выбранного пакета", L"El archivo difiere del paquete seleccionado", L"ファイルが選択したパッケージと一致しません", L"文件与所选安装包不匹配", L"Le fichier diffère du pack sélectionné"}, // Mismatch
  {L"Original backup is missing or incompatible", L"Нет подходящей копии оригинала", L"Falta una copia original compatible", L"元のファイルのバックアップがないか互換性がありません", L"原始文件备份缺失或不兼容", L"La sauvegarde d’origine est absente ou incompatible"}, // NoBackup
  {L"File operation failed", L"Ошибка при работе с файлом", L"Error al procesar el archivo", L"ファイル操作に失敗しました", L"文件操作失败", L"Échec de l’opération sur le fichier"}, // FileOperation
  {L"Waiting for administrator approval…", L"Ожидание разрешения администратора…", L"Esperando autorización de administrador…", L"管理者の承認を待っています…", L"正在等待管理员批准…", L"En attente de l’autorisation administrateur…"}, // Admin
  {L"Administrator operation failed", L"Операция с правами администратора не выполнена", L"Falló la operación de administrador", L"管理者権限での処理に失敗しました", L"管理员操作失败", L"Échec de l’opération administrateur"}, // AdminError
  {L"Could not create the desktop shortcut", L"Не удалось создать ярлык", L"No se pudo crear el acceso directo", L"ショートカットを作成できませんでした", L"无法创建快捷方式", L"Impossible de créer le raccourci"}, // ShortcutError
  {L"Could not open the path or start the application", L"Не удалось открыть путь или запустить приложение", L"No se pudo abrir la ruta o la aplicación", L"パスを開くかアプリを起動できませんでした", L"无法打开路径或启动应用", L"Impossible d’ouvrir le chemin ou de lancer l’application"}, // OpenError
  {L"Game · AlienModUI.log / .prev.log\r\nCrashes and hangs · AlienModUI.diagnostics.log\r\nMultiplayer · AlienModUI.mp.log / .mp.prev.log", L"Игра · AlienModUI.log / .prev.log\r\nВылеты и зависания · AlienModUI.diagnostics.log\r\nМультиплеер · AlienModUI.mp.log / .mp.prev.log", L"Juego · AlienModUI.log / .prev.log\r\nCuelgues y fallos · AlienModUI.diagnostics.log\r\nMultijugador · AlienModUI.mp.log / .mp.prev.log", L"ゲーム · AlienModUI.log / .prev.log\r\nクラッシュ・フリーズ · AlienModUI.diagnostics.log\r\nマルチプレイ · AlienModUI.mp.log / .mp.prev.log", L"游戏 · AlienModUI.log / .prev.log\r\n崩溃和卡死 · AlienModUI.diagnostics.log\r\n多人游戏 · AlienModUI.mp.log / .mp.prev.log", L"Jeu · AlienModUI.log / .prev.log\r\nPlantages et blocages · AlienModUI.diagnostics.log\r\nMultijoueur · AlienModUI.mp.log / .mp.prev.log"}, // LogGuide
  {L"Installer log", L"Лог установщика", L"Registro del instalador", L"インストーラーのログ", L"安装日志", L"Journal d’installation"}, // InstallerLog
  {L"Local build", L"Локальная сборка", L"Compilación local", L"ローカルビルド", L"本地构建", L"Version locale"}, // Local
  {L"Log location", L"Расположение логов", L"Ubicación de los registros", L"ログの場所", L"日志位置", L"Emplacement des journaux"}, // LogPath
  {L"Home", L"Главная", L"Inicio", L"ホーム", L"主页", L"Accueil"}, // Home
  {L"Links", L"Ссылки", L"Enlaces", L"リンク", L"链接", L"Liens"}, // Links
  {L"Photo and font", L"Фото и шрифт", L"Foto y fuente", L"写真とフォント", L"照片和字体", L"Photo et police"}, // Credits
  {L"GitHub releases", L"Релизы на GitHub", L"Versiones en GitHub", L"GitHub のリリース", L"GitHub 发行版", L"Versions sur GitHub"}, // Releases
  {L"Theme", L"Тема", L"Tema", L"テーマ", L"主题", L"Thème"}, // Theme
  {L"Amber", L"Янтарь", L"Ámbar", L"アンバー", L"琥珀", L"Ambre"}, // Amber
  {L"Terminal", L"Терминал", L"Terminal", L"ターミナル", L"终端", L"Terminal"}, // Terminal
  {L"Ice", L"Лёд", L"Hielo", L"アイス", L"冰蓝", L"Glace"}, // Ice
  {L"Diagnostics", L"Диагностика", L"Diagnóstico", L"診断", L"诊断", L"Diagnostic"}, // Diagnostics
};
static_assert(sizeof(kText) / sizeof(kText[0]) == static_cast<size_t>(Text::Count));
