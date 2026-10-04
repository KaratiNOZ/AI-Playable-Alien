@echo off
setlocal
cd /d "%~dp0.."
rem Build AlienModUI.dll + AlienInjectorUI.exe + AlienModSetup.exe (x86) into the mod folder (one level up).

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" goto msvc
if exist "C:\msys64\mingw32\bin\g++.exe" goto mingw32
goto nocompiler

:msvc
echo == Visual Studio (Win32)
rem build folder copied from another path (CMakeCache remembers the old one) -> wipe it and configure again
cmake -S . -B build-msvc -A Win32 || (rmdir /s /q build-msvc & cmake -S . -B build-msvc -A Win32) || goto fail
cmake --build build-msvc --config Release || goto fail
goto ok

:mingw32
echo == MSYS2 MINGW32 (i686)
set "PATH=C:\msys64\mingw32\bin;C:\msys64\ucrt64\bin;%PATH%"
set "CFG=-G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=C:/msys64/mingw32/bin/gcc.exe -DCMAKE_CXX_COMPILER=C:/msys64/mingw32/bin/g++.exe"
rem build folder copied from another path (CMakeCache remembers the old one) -> wipe it and configure again
cmake -S . -B build-mingw32 %CFG% || (rmdir /s /q build-mingw32 & cmake -S . -B build-mingw32 %CFG%) || goto fail
cmake --build build-mingw32 || goto fail
goto ok

:nocompiler
echo No 32-bit compiler found. Install one of:
echo   1) Visual Studio / Build Tools with "Desktop development with C++"
echo   2) MSYS2: pacman -S --needed mingw-w64-i686-gcc   (cmake and ninja from ucrt64 or mingw32)
goto fail

:ok
echo.
echo Done: AlienModUI.dll, AlienInjectorUI.exe, AlienModSetup.exe
pause
exit /b 0

:fail
echo BUILD FAILED.
pause
exit /b 1