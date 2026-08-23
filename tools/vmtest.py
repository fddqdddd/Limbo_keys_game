#!/usr/bin/env python3
"""Автотест для VM (dev). Прогоняет игру «до конца» без участия человека:

  1. LIMBO_AUTO=wrong   -> игра сама кликает НЕверный ключ;
     ожидаем результат LOSE, отсутствие установки в %APPDATA%\\LimboKeys.
  2. LIMBO_AUTO=correct -> игра сама кликает верный ключ; ожидаем результат WIN.

Игра кладёт итог в %TEMP%\\limbo_auto_result.txt (WIN/LOSE).
Используется dry-run маркер (limbo_noreboot), поэтому никакого вреда нет:
сторож, автозагрузка, перезагрузка отключены.

Пример:
  python tools/vmtest.py
"""

import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tools.tester import clear_marker, set_marker  # noqa: E402

GAME_EXE_PATH = ROOT / "build" / "game" / "limbo key.exe"
APP_DATA_DIR = Path(os.environ.get("APPDATA", "")) / "LimboKeys"
TEMP = Path(os.environ.get("TEMP", os.environ.get("TMP", ".")))
RESULT_FILE = TEMP / "limbo_auto_result.txt"
RUN_TIMEOUT = 180  # сек на один прогон (26 ходов + спин + катсцена)


def run_once(mode, expected):
    set_marker()
    if RESULT_FILE.exists():
        RESULT_FILE.unlink()
    if APP_DATA_DIR.exists():
        shutil.rmtree(APP_DATA_DIR, ignore_errors=True)

    env = dict(os.environ)
    env["LIMBO_AUTO"] = mode
    print(f"\n[*] LIMBO_AUTO={mode}, ожидаем результат {expected!r} ...")
    t0 = time.time()
    try:
        r = subprocess.run([str(GAME_EXE_PATH)], env=env, timeout=RUN_TIMEOUT)
    except subprocess.TimeoutExpired:
        print(f"[!] TIMEOUT ({RUN_TIMEOUT} с) — игра не завершилась.")
        r = None
    elapsed = time.time() - t0
    result = RESULT_FILE.read_text(errors="replace").strip() if RESULT_FILE.exists() else None

    ok = True
    if r is None:
        ok = False
    elif r.returncode != 0:
        print(f"[!] код возврата {r.returncode}")
        ok = False
    if result != expected:
        print(f"[!] результат = {result!r} (ожидалось {expected!r})")
        ok = False
    if APP_DATA_DIR.exists():
        print(f"[!] установка в %APPDATA%\\LimboKeys произошла! (dry-run нарушен)")
        ok = False

    status = "PASS" if ok else "FAIL"
    print(f"[*] {elapsed:5.1f} с | результат={result!r} | код={r.returncode if r else '-'} | {status}")
    return ok


def main():
    if not GAME_EXE_PATH.exists():
        print(f"[!] не найден {GAME_EXE_PATH} — соберите сборку (python installer.py)")
        return 1
    print(f"[*] игра: {GAME_EXE_PATH}")
    print(f"[*] результат будет в {RESULT_FILE}")
    results = [
        run_once("wrong", "LOSE"),
        run_once("correct", "WIN"),
    ]
    clear_marker()
    ok = all(results)
    print("\n[=] ИТОГО:", "PASS" if ok else "FAIL")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
