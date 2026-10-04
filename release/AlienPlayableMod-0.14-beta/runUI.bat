@echo off
rem Waits for AI.exe, injects AlienModUI.dll (next to this file) and shows the log. Any time: main menu or mid-level.
rem Run it again while the game is running to swap in a rebuilt AlienModUI.dll without restarting the game.
if not exist "%~dp0AlienInjectorUI.exe" (
  echo AlienInjectorUI.exe not found - build it first: dev\build.bat
  pause
  exit /b 1
)
if not exist "%~dp0AlienModUI.dll" (
  echo AlienModUI.dll not found - build it first: dev\build.bat
  pause
  exit /b 1
)
"%~dp0AlienInjectorUI.exe" %*
pause
