/*
 * computer_repair.exe — «лечение»: удаляет trap из автозагрузки, удаляет
 * %APPDATA%\LimboKeys, снимает политику DisableTaskMgr.
 *
 * Условие: на Рабочем столе лежит файл i_am_an_ideot.txt с правильным
 * текстом (см. IDIOT_TEXT в config.h, сравнение без учёта регистра и пробелов).
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>

#include "config.h"
#include "utils.h"

using std::wstring;

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    wstring file = util::join(util::desktop_dir(), IDIOT_FILE_NAME);

    if (!util::file_exists(file)) {
        MessageBoxW(NULL,
            L"Файл i_am_an_ideot.txt не найден на рабочем столе.\r\n\r\n"
            L"Напиши в нём текст, который просили в чате игры.",
            WINDOW_TITLE, MB_OK | MB_ICONINFORMATION);
        return 1;
    }

    std::string data;
    if (!util::read_file(file, data)) {
        MessageBoxW(NULL, L"Не удалось прочитать i_am_an_ideot.txt.", WINDOW_TITLE,
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    wstring got(data.begin(), data.end());
    got = util::lower_trim(got);
    wstring need = util::lower_trim(IDIOT_TEXT);

    if (got != need) {
        MessageBoxW(NULL,
            L"Текст в i_am_an_ideot.txt не совпадает с нужным.\r\n\r\n"
            L"Перечитай переписку и впиши текст точно, как тебя просили.",
            WINDOW_TITLE, MB_OK | MB_ICONERROR);
        return 1;
    }

    util::reg_delete_value(HKEY_CURRENT_USER, RUN_SUBKEY, RUN_VALUE_NAME);
    util::delete_dir_recursive(util::join(util::appdata_dir(), APP_DIR_NAME));
    util::reg_set_dword(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
                        L"DisableTaskMgr", 0);

    if (util::dry_run_enabled()) {
        MessageBoxW(NULL,
            L"DRY RUN (limbo_noreboot): очистка выполнена, перезагрузка пропущена.",
            WINDOW_TITLE, MB_OK | MB_ICONINFORMATION);
        return 0;
    }

    MessageBoxW(NULL, L"Очистка завершена. Компьютер будет перезагружен.",
                WINDOW_TITLE, MB_OK | MB_ICONINFORMATION);
    util::reboot_now();
    return 0;
}
