# package.py — сборка релиза для игроков: release\AlienPlayableMod-<версия>\ и такой же .zip рядом.
#   python dev\package.py [версия]        (по умолчанию 0.9-beta)
# Кладёт только то, что нужно игроку: установщик, инжектор, мод, дерево поведения мода (и оригинал игры — для
# «Удалить из игры», если .bak нет), README, runUI.bat. Без исходников, логов, настроек и папки dev.
# Перед упаковкой пересобирает всё (MinGW из C:\msys64\mingw32\bin) и проверяет, что мод не тянет лишних DLL.
import os, shutil, subprocess, sys, zipfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
VERSION = sys.argv[1] if len(sys.argv) > 1 else "0.9-beta"
MINGW = r"C:\msys64\mingw32\bin"
FILES = [
    "AlienModSetup.exe",
    "AlienInjectorUI.exe",
    "AlienModUI.dll",
    "runUI.bat",
    "README.md",
    r"data\BINARY_BEHAVIOR\_DIRECTORY_CONTENTS.BML",
    r"data\BINARY_BEHAVIOR\_DIRECTORY_CONTENTS.original.BML",
]
SYSTEM_DLLS = {"kernel32.dll", "user32.dll", "gdi32.dll", "imm32.dll", "winmm.dll", "dwmapi.dll", "msvcrt.dll",
               "shell32.dll", "ole32.dll", "comctl32.dll", "advapi32.dll"}

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

run(["cmake", "--build", "build-mingw32"])
for exe in ("AlienModSetup.exe", "AlienInjectorUI.exe", "AlienModUI.dll"):
    extra = [d for d in imports(os.path.join(ROOT, exe)) if d not in SYSTEM_DLLS]
    if extra:
        sys.exit("%s зависит не только от системных DLL: %s" % (exe, ", ".join(extra)))

name = "AlienPlayableMod-" + VERSION
dst = os.path.join(ROOT, "release", name)
if os.path.isdir(dst):
    shutil.rmtree(dst)
for f in FILES:
    src = os.path.join(ROOT, f)
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
