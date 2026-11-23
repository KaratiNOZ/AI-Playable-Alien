# package.py — сборка релиза для игроков: release\AlienPlayableMod-<версия>\ и такой же .zip рядом.
#   python dev\package.py [версия]        (по умолчанию 0.9-beta)
# Кладёт только то, что нужно игроку: установщик, инжектор, мод, дерево поведения мода (и оригинал игры — для
# «Удалить из игры», если .bak нет), README, runUI.bat. Без исходников, логов, настроек и папки dev.
# Перед упаковкой пересобирает всё (MinGW из C:\msys64\mingw32\bin) и проверяет, что мод не тянет лишних DLL.
import argparse
import os, shutil, subprocess, sys, zipfile
from pathlib import Path

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
parser = argparse.ArgumentParser()
parser.add_argument("version", nargs="?", default="0.9-beta")
parser.add_argument("--replace", action="store_true")
parser.add_argument("--level-packages", type=Path, help="Root containing staged <level>/COMMANDS.PAK files")
options = parser.parse_args()
VERSION = options.version
MINGW = r"C:\msys64\mingw32\bin"
FILES = [
    "AlienModSetup.exe",
    "AlienModSetupWorker.exe",
    "AlienInjectorUI.exe",
    "AlienModUI.dll",
    "runUI.bat",
    "README.md",
    r"data\BINARY_BEHAVIOR\_DIRECTORY_CONTENTS.BML",
    r"data\BINARY_BEHAVIOR\_DIRECTORY_CONTENTS.original.BML",
    r"data\setup-version.txt",
]
SYSTEM_DLLS = {"kernel32.dll", "user32.dll", "gdi32.dll", "imm32.dll", "winmm.dll", "dwmapi.dll", "msvcrt.dll",
               "shell32.dll", "shlwapi.dll", "ole32.dll", "comctl32.dll", "advapi32.dll"}

LEVEL_PACKAGES = {}
if options.level_packages:
    for package in sorted(options.level_packages.rglob("COMMANDS.PAK")):
        level = package.parent.relative_to(options.level_packages)
        if len(level.parts) == 1 or (len(level.parts) == 2 and level.parts[0].upper() == "DLC"):
            relative = os.path.join("data", "ENV", "PRODUCTION", *level.parts, "WORLD", package.name)
            LEVEL_PACKAGES[relative] = package
    if not LEVEL_PACKAGES:
        parser.error("--level-packages does not contain staged level packages")
    FILES.extend(LEVEL_PACKAGES)

def run(cmd):
    env = dict(os.environ, PATH=MINGW + os.pathsep + os.environ.get("PATH", ""))
    r = subprocess.run(cmd, cwd=ROOT, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if r.returncode:
        print(r.stdout[-3000:], r.stderr[-3000:])
        sys.exit("ошибка: " + " ".join(cmd))
    return r.stdout

def imports(path):
    out = run([os.path.join(MINGW, "objdump.exe"), "-p", path])
    return [l.split(":", 1)[1].strip().lower() for l in out.splitlines() if "DLL Name:" in l]

BUILD = os.path.join(ROOT, "build-package")
BIN = os.path.join(BUILD, "bin")
run(["cmake", "-S", ROOT, "-B", BUILD, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
     "-DCMAKE_C_COMPILER=" + os.path.join(MINGW, "gcc.exe"),
     "-DCMAKE_CXX_COMPILER=" + os.path.join(MINGW, "g++.exe"),
     "-DALIENMOD_BINARY_DIR=" + BIN, "-DALIENMOD_DEV_TOOLS=OFF"])
run(["cmake", "--build", BUILD])
for exe in ("AlienModSetupWorker.exe", "AlienInjectorUI.exe", "AlienModUI.dll"):
    extra = [d for d in imports(os.path.join(BIN, exe)) if d not in SYSTEM_DLLS]
    if extra:
        sys.exit("%s зависит не только от системных DLL: %s" % (exe, ", ".join(extra)))

name = "AlienPlayableMod-" + VERSION
dst = os.path.join(ROOT, "release", name)
if os.path.isdir(dst):
    if not options.replace:
        sys.exit("папка релиза уже существует: " + dst)
    existing = {os.path.relpath(os.path.join(base, f), dst) for base, _, files in os.walk(dst) for f in files}
    if existing - set(FILES):
        sys.exit("в папке релиза есть посторонние файлы — замена остановлена")
for f in FILES:
    if f == r"data\setup-version.txt":
        with open(os.path.join(dst, "data", "setup-version.txt"), "w", encoding="utf-8") as version_file:
            version_file.write(VERSION)
        continue
    src = LEVEL_PACKAGES.get(f, os.path.join(BIN if f.endswith((".exe", ".dll")) else ROOT, f))
    if not os.path.isfile(src):
        sys.exit("нет файла " + f)
    os.makedirs(os.path.dirname(os.path.join(dst, f)), exist_ok=True)
    shutil.copy2(src, os.path.join(dst, f))
zpath = dst + ".zip"
with zipfile.ZipFile(zpath, "w", zipfile.ZIP_DEFLATED) as z:
    for base, _, files in os.walk(dst):
        for f in files:
            full = os.path.join(base, f)
            z.write(full, os.path.join(name, os.path.relpath(full, dst)))
print("готово:", zpath, "(%d КБ)" % (os.path.getsize(zpath) // 1024))
