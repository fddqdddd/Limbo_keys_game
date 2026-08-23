#!/usr/bin/env python3
"""Безопасный тест-раннер (dev). Ставит %TEMP%\\limbo_noreboot, из-за которого:
  - guard (DisableTaskMgr) не включается;
  - неверный ключ НЕ устанавливает trap.exe и НЕ перезагружает ПК;
  - computer_repair.exe чистит, но не перезагружается.

Примеры:
  python tools/tester.py game            # запустить миниигру (закрыть вручную)
  python tools/tester.py trap 3          # показать trap.exe 3 секунды, потом убить
  python tools/tester.py repair          # создать ideot-файл, прогнать repair, удалить файл
"""

import os
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tools.paths import BUILD, REPAIR_EXE, TRAP_EXE  # noqa: E402

GAME_EXE_PATH = BUILD / "game" / "limbo key.exe"
TRAP_EXE_PATH = BUILD / "trap" / TRAP_EXE
REPAIR_EXE_PATH = BUILD / "repair" / REPAIR_EXE

MARKER = Path(os.environ.get("TEMP", os.environ.get("TMP", "."))) / "limbo_noreboot"

IDIOT_TEXT = ("I'm a complete loser, forgive me for not guessing the keys. "
              "I'll improve. You and your author are great. And I'm a complete bitch!!!")


def set_marker():
    MARKER.write_text("dry run", encoding="utf-8")
    print(f"[*] dry-run marker: {MARKER}")


def clear_marker():
    if MARKER.exists():
        MARKER.unlink()
        print("[*] dry-run marker removed")


def proc_alive(p):
    return p.poll() is None


def kill_tree(name):
    subprocess.run(["taskkill", "/IM", name, "/F"], capture_output=True)


def desktop_path():
    d = Path.home() / "Desktop"
    if d.exists():
        return d
    d = Path.home() / "OneDrive" / "Desktop"
    return d if d.exists() else Path.home()


def test_trap(seconds):
    set_marker()
    # в продакшене computer_repair.exe лежит рядом с trap.exe;
    # для dev-теста кладём его рядом в build/trap/
    import shutil
    shutil.copy2(REPAIR_EXE_PATH, TRAP_EXE_PATH.parent / REPAIR_EXE)
    print(f"[*] запуск trap.exe на {seconds} сек (dry-run, сторож выключен).")
    try:
        p = subprocess.Popen([str(TRAP_EXE_PATH)])
        time.sleep(seconds)
        if proc_alive(p):
            kill_tree(TRAP_EXE)
            print(f"[*] trap.exe убит по таймауту")
        else:
            print(f"[!] trap.exe завершился сам, код {p.returncode}")
    finally:
        (TRAP_EXE_PATH.parent / REPAIR_EXE).unlink(missing_ok=True)
        copy = desktop_path() / "computer_repair.exe"
        if copy.exists():
            copy.unlink()
            print(f"[*] удалена копия {copy}")
        clear_marker()


def test_game():
    set_marker()
    print("[*] запуск игры (dry-run). Верный ключ -> выход; неверный -> BSOD -> любой клавиша -> сообщение -> выход.")
    print("[*] закрой игру, чтобы завершить тест.")
    try:
        subprocess.run([str(GAME_EXE_PATH)])
    except KeyboardInterrupt:
        pass
    finally:
        clear_marker()


def test_trap(seconds):
    set_marker()
    print(f"[*] запуск trap.exe на {seconds} сек (dry-run, сторож выключен).")
    try:
        p = subprocess.Popen([str(TRAP_EXE_PATH)])
        time.sleep(seconds)
        if proc_alive(p):
            kill_tree(TRAP_EXE)
            print(f"[*] trap.exe убит по таймауту")
        else:
            print(f"[!] trap.exe завершился сам, код {p.returncode}")
    finally:
        clear_marker()


def test_repair():
    set_marker()
    desktop = desktop_path()
    f = desktop / "i_am_an_ideot.txt"
    f.write_text(IDIOT_TEXT, encoding="utf-8")
    print(f"[*] создан {f}")
    try:
        print("[*] запуск computer_repair.exe (dry-run: без перезагрузки)")
        r = subprocess.run([str(REPAIR_EXE_PATH)])
        print(f"[*] код возврата: {r.returncode}")
    finally:
        if f.exists():
            f.unlink()
            print("[*] ideot-файл удалён")
        clear_marker()


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "game"
    if cmd == "game":
        test_game()
    elif cmd == "trap":
        test_trap(int(sys.argv[2]) if len(sys.argv) > 2 else 3)
    elif cmd == "repair":
        test_repair()
    else:
        print(__doc__)
        sys.exit(1)
