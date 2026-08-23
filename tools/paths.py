"""Единый источник путей для сборочных скриптов (Python, dev-time).

Рантайм-пути на стороне C++ решаются через Win32 API (GetModuleFileName,
SHGetFolderPath); этот модуль нужен только для инструментов разработки:
installer.py, generate_assets.py.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SRC = ROOT / "src"
COMMON = SRC / "common"
GAME = SRC / "game"
TRAP = SRC / "trap"
REPAIR = SRC / "repair"
INSTALLER = SRC / "installer"

TOOLS = ROOT / "tools"

RES = ROOT / "resources"
ICONS = RES / "icons"
MUSIC = RES / "music"
SOUNDS = RES / "sounds"
SCREAMERS = RES / "screamers"
KEYS = RES / "keys"
CATSCENE = RES / "cat-scene"

BUILD = ROOT / "build"
DIST = ROOT / "dist"
BUILD_GAME = BUILD / "game"
BUILD_TRAP = BUILD / "trap"

GAME_EXE = "limbo key.exe"
TRAP_EXE = "trap.exe"
REPAIR_EXE = "computer_repair.exe"
INSTALLER_EXE = "installer.exe"

# Общие флаги линковки для Win32 exe
WIN32_LIBS = ["-lgdi32", "-luser32", "-lwinmm", "-lshell32", "-ladvapi32",
              "-lmsimg32", "-lgdiplus"]


def ensure_dirs(*paths):
    for p in paths:
        p.mkdir(parents=True, exist_ok=True)


def sync_resources(dst_root, subdirs=("screamers", "sounds")):
    """Копирует ресурсы рядом с exe (для dev-запуска из build/)."""
    import shutil
    for name in subdirs:
        src = RES / name
        if src.exists():
            shutil.copytree(src, dst_root / "resources" / name,
                            dirs_exist_ok=True)
