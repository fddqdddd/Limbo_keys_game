#ifndef LIMBO_CONFIG_H
#define LIMBO_CONFIG_H

/*
 * config.h — единственный конфиг всей сборки (стиль C).
 * Яркость, скорость, тайминги, цвета, пути, тексты.
 * Для изменения поведения правь только этот файл.
 */

/* ============ Экран и окно ============ */
#define WINDOW_CLASS      L"LimboKeyWindow"
#define WINDOW_TITLE      L"Limbo Key"

/* ============ Ключи и сетка ============ */
#define KEY_COUNT         8
#define KEY_SIZE          72
#define KEY_SPACING       26
#define GRID_COLS         2
#define GRID_ROWS         4
#define KEY_HIT_RADIUS    52
#define KEY_HOVER_SCALE   1.18f     /* увеличение ключа под курсором */

/* ============ Цвета ============ */
/* Прозрачный фон окна (color key). Ключи его не используют. */
#define COLOR_KEY_RGB      RGB(255, 0, 255)
/* Зелёный для мигания верного ключа (фолбэк-рисование) */
#define GREEN_CORRECT_RGB  RGB(0, 230, 118)
/* Палитра ключей: красный, оранжевый, жёлтый, зелёный, голубой,
 * синий, фиолетовый, розовый (порядок = порядок оттенков спрайта) */
#define KEY_COLORS \
  { RGB(230, 57, 70), RGB(255, 146, 0), RGB(255, 213, 0), RGB(0, 200, 83), \
    RGB(0, 188, 212), RGB(33, 110, 243), RGB(140, 50, 220), RGB(255, 64, 160) }

/* Яркость сцен (0..1) */
#define AMBIENT_BRIGHTNESS    0.12f
#define SPIKE_BRIGHTNESS      0.40f
#define COMPUTER_BRIGHTNESS   0.80f
#define BSOD_BG_RGB           RGB(0, 120, 215)

/* ============ Тайминги (мс) ============ */
#define SPAWN_STAGGER_MS      200
#define WAIT_CORRECT_MS       1200
#define BLINK_PERIOD_MS       220
/* Синхронизация с треком (limbo.wav): перемешивание НАЧИНАЕТСЯ на 5-й
 * секунде и длится РОВНО 10 секунд.
 * Два разворота на 180° (блок-свапа): ходы 6-й и 13-й (i==5 и i==12),
 * второй начинается ровно через 3 секунды после первого:
 *   t1 = 5*350 = 1750 мс;  t2 = 1750 + 900 + 6*350 = 4750 мс (Δ = 3000 мс).
 *   Итого: 23 хода * MOVE_MS + 2 * SPECIAL_MOVE_MS + FINAL_MOVE_MS = 10000. */
#define SHUFFLE_START_AT_MS   5000
#define SHUFFLE_TOTAL_MS      10000
#define MOVE_MS               350     /* 23 * 350 = 8050 */
#define SPECIAL_MOVE_MS       900     /* 2 * 900 = 1800 */
#define FINAL_MOVE_MS         150     /* +150 -> итого ровно 10000 мс */

/* Настройка скорости перемешивания: множитель (>1 быстрее, <1 медленнее).
 * При 1.0 перемешивание длится ровно 10 с (синхронно с треком).
 * Без пересборки меняется в settings.ini рядом с exe:
 *   [game]
 *   shuffle_speed = 1.5
 * Диапазон: 0.25 .. 4.0 */
#define SHUFFLE_SPEED_DEFAULT 3.0f
#define SETTINGS_INI_FILE     L"settings.ini"
#define CIRCLE_ROTATE_DEG_PER_SEC  14.0f
/* Постоянного вращения ключей вокруг своей оси в кругу НЕТ:
 * ориентация каждого ключа остаётся той, что вышла из перемешивания. */
#define SCRIMER_HOLD_MS       1000
#define SCRIMER_FLICKER_MS    120
#define SCRIMER_PAUSE_MIN_MS  2000
#define SCRIMER_PAUSE_MAX_MS  4000
#define SCRIMER_COUNT_MIN     1        /* скримеров за раз (может быть несколько) */
#define SCRIMER_COUNT_MAX     3
#define CUTSCENE_SPEED        0.20f    /* доля ширины экрана в секунду */
#define CUTSCENE_BG_SPEED     0.08f    /* фон едет влево (за ним второй такой же) */
#define CUTSCENE_SPIKE_SPEED  0.22f    /* шипы едут влево быстрее фона */
#define BOOM_MS               600      /* длительность взрыва перед BSOD */
#define WIN_EXIT_DELAY_MS     600      /* верный ключ: игра закрывается почти сразу */
#define PC_FALL_SPEED         0.20f    /* скорость падения ПК, доля высоты экрана в сек */
#define SPIKE_TOUCH_FRAC      0.78f    /* нижняя кромка шипов (доля высоты экрана) */
#define BSOD_BLINK_PERIOD_MS  600

/* ============ Вспышка после клика и пасхалка ============ */
#define FLASH_HOLD_MS         600     /* весь экран цветом выбранного ключа */
#define FLASH_FADE_MS         450     /* плавное исчезновение вспышки */
#define IDLE_COWARD_MS        10000   /* без клика в кругу -> PICK THE KEY COWARD */
#define CIRCLE_SPIN_MS        2600    /* продолжительность «хаотичного» спина в круг */
#define CIRCLE_SPIN_STAGGER_MS 90     /* задержка спина между ключами */

/* ============ Пути и имена ============ */
#define APP_DIR_NAME       L"LimboKeys"
#define RUN_SUBKEY         L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_VALUE_NAME     L"LimboKey"
#define TRAP_EXE_NAME      L"trap.exe"
#define REPAIR_EXE_NAME    L"computer_repair.exe"
#define IDIOT_FILE_NAME    L"i_am_an_ideot.txt"
#define SCREAMERS_SUBDIR   L"resources\\screamers"
#define SOUNDS_SUBDIR      L"resources\\sounds"
#define MUSIC_SUBDIR       L"resources\\music"
#define KEYS_SUBDIR        L"resources\\keys"
#define CATSCENE_SUBDIR    L"resources\\cat-scene"
#define CATSCENE_BG_JPG_FILE L"background.jpg"  /* авторский фон (приоритет) */
#define CATSCENE_BG_FILE   L"background.png"
#define CATSCENE_PC_FILE   L"pc.png"
#define CATSCENE_SPIKES_FILE L"spikes.png"
#define PC_DEST_HEIGHT_FRAC 0.24f    /* высота ПК в катсцене, доля высоты экрана */
#define SPIKE_DEST_HEIGHT_FRAC 0.28f /* высота шипов, доля высоты экрана */
#define CHEAT_HINT_MS        1800    /* сколько мигает верный ключ после Ctrl+Shift+Alt+K */
#define CHEAT_PULSE_AMPLITUDE 0.18f  /* пульс: амплитуда увеличения (доля размера) */
#define CHEAT_PULSE_PERIOD_MS 600    /* пульс: период увеличения/уменьшения */

/* Единственный спрайт ключа (оранжевый); оттенки остальных цветов
 * получаются перекраской (hue-shift) при загрузке. */
#define KEY_SPRITE_FILE    L"key.png"
#define KEY_SPRITE_DEST    76      /* видимый размер ключа на экране */

/* ============ Тексты ============ */
#define IDIOT_TEXT \
  L"I'm a complete loser, forgive me for not guessing the keys. I'll improve. You and your author are great. And I'm a complete bitch!!!"

#define BSOD_LINE1 L"Your PC ran into a problem and needs to restart."
#define BSOD_LINE2 L"We're just collecting some error info, and then we'll restart for you."
#define BSOD_STOP  L"Stop code: CRITICAL_PROCESS_DIED"
#define BSOD_HINT  L"Press any key to continue..."

#define COWARD_LINE1     L"PICK THE KEY COWARD"
#define COWARD_LINE2     L"DON'T LET IT WIN"

/* ============ Dev-автотест (только для VM) ============ */
/* env LIMBO_AUTO=wrong|correct -> игра сама кликает нужный ключ в кругу,
 * пишет результат в %TEMP%\limbo_auto_result.txt (WIN/LOSE) и выходит.
 * Работает только вместе с dry-run маркером. */
#define AUTO_RESULT_FILE L"limbo_auto_result.txt"

/* ============ Ресурсы (совпадает с src/game/game.rc) ============ */
#define IDR_ICON     101
#define IDR_TRAP     102
#define IDR_REPAIR   103

/* ============ Dev-флаг ============ */
/* Если в %TEMP% лежит файл limbo_noreboot — неправильный ключ НЕ устанавливает
 * trap.exe и не перезагружает ПК (безопасный тест BSOD). */
#define DRYRUN_MARKER L"limbo_noreboot"

#endif /* LIMBO_CONFIG_H */
