#!/usr/bin/env python3
"""Полная сборка: ассеты -> repair.exe -> trap.exe -> limbo key.exe -> installer.exe -> dist/.

Порядок важен: game.rc вшивает в себя собранные trap.exe и computer_repair.exe
(как RCDATA-ресурсы 102/103).
"""

import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
sys.path.insert(0, str(ROOT))

from tools.paths import (  # noqa: E402
    ROOT, COMMON, GAME, TRAP, REPAIR, INSTALLER, RES,
    BUILD, DIST, GAME_EXE, TRAP_EXE, REPAIR_EXE, INSTALLER_EXE,
    WIN32_LIBS, ensure_dirs, sync_resources,
)
import tools.generate_assets as assets  # noqa: E402

CC = "g++"
WINDRES = "windres"

COMMON_OBJS = [BUILD / "common" / "utils.o", BUILD / "common" / "guard.o"]
INCLUDE_FLAGS = ["-I", COMMON]
EXE_FLAGS = ["-O2", "-mwindows", "-static", "-s"]


def run(args, cwd=None):
    print(">>", " ".join(str(a) for a in args))
    r = subprocess.run([str(a) for a in args], cwd=str(cwd or ROOT),
                       capture_output=True, text=True)
    if r.stdout:
        print(r.stdout, end="")
    if r.stderr:
        print(r.stderr, end="", file=sys.stderr)
    if r.returncode != 0:
        print(f"FAILED: {' '.join(str(a) for a in args)}", file=sys.stderr)
        sys.exit(1)
    return r


def exe_locked(path):
    try:
        with open(path, "ab"):
            return False
    except PermissionError:
        return True


def build():
    ensure_dirs(BUILD / "common", BUILD / "repair", BUILD / "trap",
                BUILD / "game", DIST)

    print("== 1/6 ассеты-заглушки ==")
    assets.main()

    print("== 2/6 общие объекты (utils, guard) ==")
    run([CC, "-c", COMMON / "utils.cpp", "-o", BUILD / "common" / "utils.o",
         "-O2", *INCLUDE_FLAGS])
    run([CC, "-c", COMMON / "guard.cpp", "-o", BUILD / "common" / "guard.o",
         "-O2", *INCLUDE_FLAGS])

    print("== 3/6 computer_repair.exe ==")
    run([WINDRES, REPAIR / "repair.rc", "-O", "coff", "-o",
         BUILD / "repair" / "repair.res"], cwd=REPAIR)
    run([CC, *COMMON_OBJS, REPAIR / "main.cpp", BUILD / "repair" / "repair.res",
         "-o", BUILD / "repair" / REPAIR_EXE, *EXE_FLAGS, *INCLUDE_FLAGS, *WIN32_LIBS])

    print("== 4/6 trap.exe ==")
    run([WINDRES, TRAP / "trap.rc", "-O", "coff", "-o", BUILD / "trap" / "trap.res"],
        cwd=TRAP)
    run([CC, *COMMON_OBJS, TRAP / "main.cpp", BUILD / "trap" / "trap.res",
         "-o", BUILD / "trap" / TRAP_EXE, *EXE_FLAGS, *INCLUDE_FLAGS, *WIN32_LIBS])

    print("== 5/6 limbo key.exe ==")
    run([WINDRES, GAME / "game.rc", "-O", "coff", "-o", BUILD / "game" / "game.res"],
        cwd=GAME)
    run([CC, *COMMON_OBJS, GAME / "main.cpp", BUILD / "game" / "game.res",
         "-o", BUILD / "game" / GAME_EXE, *EXE_FLAGS, *INCLUDE_FLAGS, *WIN32_LIBS])

    print("== 6/6 installer.exe ==")
    inst = BUILD / INSTALLER_EXE
    if inst.exists() and exe_locked(inst):
        print("[i] installer.exe сейчас запущен — пересборка пропущена (уже актуален)")
    else:
        run([CC, INSTALLER / "main.cpp", "-o", inst, "-O2", "-static", "-s"])

    print("== пакет dist/ ==")
    shutil.copy2(BUILD / "game" / GAME_EXE, DIST / GAME_EXE)
    if (DIST / "resources").exists():
        shutil.rmtree(DIST / "resources")
    shutil.copytree(RES, DIST / "resources")

    # ресурсы рядом с dev-сборками, чтобы тесты из build/ видели ассеты
    sync_resources(BUILD / "game", ("screamers", "sounds", "music", "keys",
                                    "cat-scene"))
    sync_resources(BUILD / "trap", ("screamers", "sounds"))

    print()
    print("Готово. dist/:")
    for p in sorted(DIST.rglob("*")):
        if p.is_file():
            print(f"  {p.relative_to(DIST)} ({p.stat().st_size} bytes)")


if __name__ == "__main__":
    build()
