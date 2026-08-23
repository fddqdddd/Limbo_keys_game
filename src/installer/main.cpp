/*
 * installer.exe — консольная обёртка: находит Python и запускает installer.py.
 * Вывод — UTF-8 (консоль переключается на CP65001).
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>

static bool file_exists(const wchar_t *p) {
    DWORD a = GetFileAttributesW(p);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

static std::wstring exe_dir() {
    wchar_t buf[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring p(buf);
    size_t pos = p.find_last_of(L"\\/");
    if (pos != std::wstring::npos) p = p.substr(0, pos);
    return p;
}

static std::vector<std::wstring> find_python() {
    std::vector<std::wstring> out;
    wchar_t buf[MAX_PATH] = {0};
    if (GetEnvironmentVariableW(L"PYTHON", buf, MAX_PATH) > 0)
        out.push_back(buf);
    if (SearchPathW(NULL, L"python.exe", NULL, MAX_PATH, buf, NULL) > 0)
        out.push_back(buf);
    if (SearchPathW(NULL, L"python3.exe", NULL, MAX_PATH, buf, NULL) > 0)
        out.push_back(buf);

    const wchar_t *dirs[] = {
        L"C:\\msys64\\ucrt64\\bin", L"C:\\msys64\\mingw64\\bin",
        L"C:\\Python313", L"C:\\Python312", L"C:\\Python311", L"C:\\Python310",
        L"C:\\Python39", L"C:\\Python38", L"C:\\Python37", L"C:\\Python36",
        L"C:\\Program Files\\Python313", L"C:\\Program Files\\Python312",
        L"C:\\Program Files\\Python311", L"C:\\Program Files\\Python310",
        L"C:\\Program Files\\Python39", L"C:\\Program Files\\Python38",
    };
    for (int i = 0; i < (int)(sizeof(dirs) / sizeof(dirs[0])); i++) {
        std::wstring p = std::wstring(dirs[i]) + L"\\python.exe";
        if (file_exists(p.c_str())) out.push_back(p);
    }
    return out;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    printf("== Limbo Keys installer ==\n");

    std::vector<std::wstring> pythons = find_python();
    if (pythons.empty()) {
        printf("[X] Python not found. Install Python 3 (add to PATH)\n"
               "    or run manually: python installer.py\n");
        return 1;
    }

    std::wstring script = exe_dir() + L"\\installer.py";
    if (!file_exists(script.c_str())) {
        /* build\installer.exe -> ищем installer.py в корне проекта */
        std::wstring alt = exe_dir() + L"\\..\\installer.py";
        if (file_exists(alt.c_str())) {
            script = alt;
        } else {
            printf("[X] missing installer.py next to installer.exe\n");
            return 1;
        }
    }

    for (size_t i = 0; i < pythons.size(); i++) {
        std::wstring cmd = L"\"" + pythons[i] + L"\" \"" + script + L"\"";
        std::vector<wchar_t> buf(cmd.begin(), cmd.end());
        buf.push_back(0);

        STARTUPINFOW si = {0};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi = {0};
        if (!CreateProcessW(NULL, buf.data(), NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
            printf("[i] cannot run python (%lu), trying next...\n", GetLastError());
            continue;
        }
        WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        printf("[i] installer.py exited with code %lu\n", code);
        return (int)code;
    }

    printf("[X] no usable python.\n");
    return 1;
}
