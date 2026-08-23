#ifndef LIMBO_UTILS_H
#define LIMBO_UTILS_H

#include <windows.h>
#include <string>
#include <vector>

namespace util {

std::wstring app_dir();
std::wstring appdata_dir();
std::wstring desktop_dir();
std::wstring join(const std::wstring &a, const std::wstring &b);
bool file_exists(const std::wstring &path);
bool dir_exists(const std::wstring &path);
bool make_dirs(const std::wstring &path);
bool write_file(const std::wstring &path, const void *data, size_t size);
bool read_file(const std::wstring &path, std::string &out);
bool copy_file(const std::wstring &from, const std::wstring &to);
void delete_dir_recursive(const std::wstring &path);

bool extract_resource(HMODULE h, int resid, const std::wstring &out_path);

bool reg_set_dword(HKEY hive, const wchar_t *subkey, const wchar_t *name, DWORD value);
bool reg_set_string(HKEY hive, const wchar_t *subkey, const wchar_t *name, const std::wstring &value);
std::wstring reg_get_string(HKEY hive, const wchar_t *subkey, const wchar_t *name);
bool reg_delete_value(HKEY hive, const wchar_t *subkey, const wchar_t *name);

void kill_processes_named(const std::vector<std::wstring> &names);

bool enable_shutdown_privilege();
bool reboot_now();

HBITMAP load_bmp(const std::wstring &path);

void play_wav_async(const std::wstring &path);
void play_music(const std::wstring &path);
void stop_music();

std::wstring lower_trim(const std::wstring &s);

bool dry_run_enabled();

} /* namespace util */

namespace win {

bool init_fullscreen(HINSTANCE hinst, const wchar_t *cls, const wchar_t *title,
                     WNDPROC proc, HWND *out_hwnd, int *out_w, int *out_h);
void set_layered(HWND hwnd, bool transparent);
void set_alpha(HWND hwnd, int alpha);
bool create_back_buffer(HDC *mdc, HBITMAP *dib, HBITMAP *old, int w, int h);

} /* namespace win */

#endif /* LIMBO_UTILS_H */
