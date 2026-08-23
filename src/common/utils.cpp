#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winreg.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <shlobj.h>
#include <mmsystem.h>
#include <string>
#include <vector>
#include <cctype>

#include "config.h"
#include "utils.h"

namespace util {

std::wstring app_dir() {
    wchar_t buf[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, buf, MAX_PATH);
    std::wstring p(buf);
    size_t pos = p.find_last_of(L"\\/");
    if (pos != std::wstring::npos) p = p.substr(0, pos);
    return p;
}

std::wstring appdata_dir() {
    wchar_t buf[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, buf)))
        return std::wstring(buf);
    return L"C:\\Users\\Public";
}

std::wstring desktop_dir() {
    wchar_t buf[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_DESKTOP, NULL, SHGFP_TYPE_CURRENT, buf)))
        return std::wstring(buf);
    return L"C:\\Users\\Public\\Desktop";
}

std::wstring join(const std::wstring &a, const std::wstring &b) {
    if (a.empty()) return b;
    if (a.back() == L'\\' || a.back() == L'/') return a + b;
    return a + L"\\" + b;
}

bool file_exists(const std::wstring &path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return (a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY));
}

bool dir_exists(const std::wstring &path) {
    DWORD a = GetFileAttributesW(path.c_str());
    return (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY));
}

bool make_dirs(const std::wstring &path) {
    if (dir_exists(path)) return true;
    std::wstring cur;
    for (size_t i = 0; i < path.size(); i++) {
        cur += path[i];
        if (path[i] == L'\\' || path[i] == L'/') {
            CreateDirectoryW(cur.c_str(), NULL);
        }
    }
    CreateDirectoryW(path.c_str(), NULL);
    return dir_exists(path);
}

bool write_file(const std::wstring &path, const void *data, size_t size) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    BOOL ok = WriteFile(h, data, (DWORD)size, &wrote, NULL);
    CloseHandle(h);
    return ok && (size_t)wrote == size;
}

bool read_file(const std::wstring &path, std::string &out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(h, NULL);
    out.resize(size);
    DWORD got = 0;
    BOOL ok = ReadFile(h, &out[0], size, &got, NULL);
    CloseHandle(h);
    return ok && got == size;
}

bool copy_file(const std::wstring &from, const std::wstring &to) {
    return CopyFileW(from.c_str(), to.c_str(), FALSE) != 0;
}

void delete_dir_recursive(const std::wstring &path) {
    if (!dir_exists(path)) return;
    std::wstring search = join(path, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(search.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring name = fd.cFileName;
            if (name == L"." || name == L"..") continue;
            std::wstring full = join(path, name);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                delete_dir_recursive(full);
            } else {
                SetFileAttributesW(full.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(full.c_str());
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(path.c_str());
}

bool extract_resource(HMODULE h, int resid, const std::wstring &out_path) {
    HRSRC hr = FindResourceW(h, MAKEINTRESOURCEW(resid), MAKEINTRESOURCEW(10));
    if (!hr) return false;
    HGLOBAL hg = LoadResource(h, hr);
    if (!hg) return false;
    void *data = LockResource(hg);
    DWORD size = SizeofResource(h, hr);
    if (!data || size == 0) return false;
    return write_file(out_path, data, size);
}

bool reg_set_dword(HKEY hive, const wchar_t *subkey, const wchar_t *name, DWORD value) {
    HKEY key;
    if (RegCreateKeyExW(hive, subkey, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(key, name, 0, REG_DWORD, (const BYTE *)&value, sizeof(value));
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

bool reg_set_string(HKEY hive, const wchar_t *subkey, const wchar_t *name, const std::wstring &value) {
    HKEY key;
    if (RegCreateKeyExW(hive, subkey, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) != ERROR_SUCCESS)
        return false;
    LONG r = RegSetValueExW(key, name, 0, REG_SZ,
                            (const BYTE *)value.c_str(), (DWORD)((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

std::wstring reg_get_string(HKEY hive, const wchar_t *subkey, const wchar_t *name) {
    HKEY key;
    std::wstring out;
    if (RegOpenKeyExW(hive, subkey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return out;
    wchar_t buf[1024] = {0};
    DWORD size = sizeof(buf);
    if (RegQueryValueExW(key, name, NULL, NULL, (BYTE *)buf, &size) == ERROR_SUCCESS)
        out = buf;
    RegCloseKey(key);
    return out;
}

bool reg_delete_value(HKEY hive, const wchar_t *subkey, const wchar_t *name) {
    HKEY key;
    if (RegOpenKeyExW(hive, subkey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return false;
    LONG r = RegDeleteValueW(key, name);
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

void kill_processes_named(const std::vector<std::wstring> &names) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return;
    PROCESSENTRY32W pe;
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (pe.th32ProcessID == GetCurrentProcessId()) continue;
            for (size_t i = 0; i < names.size(); i++) {
                if (lstrcmpiW(pe.szExeFile, names[i].c_str()) == 0) {
                    HANDLE hp = OpenProcess(PROCESS_TERMINATE, FALSE, pe.th32ProcessID);
                    if (hp) { TerminateProcess(hp, 1); CloseHandle(hp); }
                }
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
}

bool enable_shutdown_privilege() {
    HANDLE token;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        return false;
    TOKEN_PRIVILEGES tp;
    LUID luid;
    LookupPrivilegeValueW(NULL, L"SeShutdownPrivilege", &luid);
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    BOOL ok = AdjustTokenPrivileges(token, FALSE, &tp, 0, NULL, NULL);
    CloseHandle(token);
    return ok != 0;
}

bool reboot_now() {
    enable_shutdown_privilege();
    return ExitWindowsEx(EWX_REBOOT | EWX_FORCE,
                         SHTDN_REASON_MAJOR_APPLICATION | SHTDN_REASON_FLAG_PLANNED) != 0;
}

HBITMAP load_bmp(const std::wstring &path) {
    return (HBITMAP)LoadImageW(NULL, path.c_str(), IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
}

void play_wav_async(const std::wstring &path) {
    PlaySoundW(path.c_str(), NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

void play_music(const std::wstring &path) {
    /* без SND_LOOP: трек проигрывается ровно ОДИН раз и сам затихает,
     * даже если игрок так и не выбрал ключ */
    PlaySoundW(path.c_str(), NULL, SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
}

void stop_music() {
    PlaySoundW(NULL, NULL, 0);
}

std::wstring lower_trim(const std::wstring &s) {
    size_t a = 0, b = s.size();
    while (a < b && iswspace(s[a])) a++;
    while (b > a && iswspace(s[b - 1])) b--;
    std::wstring out;
    for (size_t i = a; i < b; i++) out += (wchar_t)towlower(s[i]);
    return out;
}

bool dry_run_enabled() {
    wchar_t tmp[MAX_PATH] = {0};
    GetTempPathW(MAX_PATH, tmp);
    return file_exists(join(tmp, DRYRUN_MARKER));
}

} /* namespace util */

namespace win {

static void register_class(HINSTANCE hinst, const wchar_t *cls, WNDPROC proc) {
    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = proc;
    wc.hInstance = hinst;
    wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    /* своя иконка (resources/icons/key.ico вшита как IDR_ICON) */
    wc.hIcon = (HICON)LoadImageW(hinst, MAKEINTRESOURCEW(IDR_ICON), IMAGE_ICON,
                                 0, 0, LR_DEFAULTSIZE);
    wc.hIconSm = (HICON)LoadImageW(hinst, MAKEINTRESOURCEW(IDR_ICON), IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON),
                                   GetSystemMetrics(SM_CYSMICON), 0);
    wc.hbrBackground = NULL;
    wc.lpszClassName = cls;
    RegisterClassExW(&wc);
}

bool init_fullscreen(HINSTANCE hinst, const wchar_t *cls, const wchar_t *title,
                     WNDPROC proc, HWND *out_hwnd, int *out_w, int *out_h) {
    register_class(hinst, cls, proc);

    /* Полноэкранность по монитору, где сейчас курсор (поддержка мультимонитора). */
    RECT rc;
    POINT pt;
    GetCursorPos(&pt);
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {0};
    mi.cbSize = sizeof(mi);
    if (mon && GetMonitorInfoW(mon, &mi))
        rc = mi.rcMonitor;
    else {
        rc.left = 0; rc.top = 0;
        rc.right = GetSystemMetrics(SM_CXSCREEN);
        rc.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;

    HWND hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                                cls, title, WS_POPUP, rc.left, rc.top, w, h,
                                NULL, NULL, hinst, NULL);
    if (!hwnd) return false;
    set_layered(hwnd, true);
    SetWindowPos(hwnd, HWND_TOPMOST, rc.left, rc.top, w, h, SWP_SHOWWINDOW);
    *out_hwnd = hwnd;
    *out_w = w;
    *out_h = h;
    return true;
}

void set_layered(HWND hwnd, bool transparent) {
    if (transparent)
        SetLayeredWindowAttributes(hwnd, COLOR_KEY_RGB, 0, LWA_COLORKEY);
    else
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
}

void set_alpha(HWND hwnd, int alpha) {
    SetLayeredWindowAttributes(hwnd, 0, (BYTE)alpha, LWA_ALPHA);
}

bool create_back_buffer(HDC *mdc, HBITMAP *dib, HBITMAP *old, int w, int h) {
    HDC dc = CreateCompatibleDC(NULL);
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp) { DeleteDC(dc); return false; }
    HBITMAP oldbmp = (HBITMAP)SelectObject(dc, bmp);
    SetGraphicsMode(dc, GM_ADVANCED);
    *mdc = dc;
    *dib = bmp;
    *old = oldbmp;
    return true;
}

} /* namespace win */
