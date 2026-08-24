#!/usr/bin/env python3
"""Генератор ассетов-заглушек. Чистый Python, без внешних зависимостей.

Создаёт:
  resources/icons/key.ico          - золотой ключ 32x32 (32bpp, alpha)
  resources/keys/key_*.bmp         - 8 спрайтов ключей 128x128 (фон чёрный = прозрачный)
  resources/screamers/*.bmp        - 3 скримера 480x360 (24bpp)
  resources/sounds/*.wav           - 3 страшных звука
  resources/music/limbo.wav        - мрачная петля
"""

import math
import random
import struct
import sys
import wave
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))
from tools.paths import ICONS, MUSIC, SCREAMERS, SOUNDS, KEYS, CATSCENE, ensure_dirs  # noqa: E402

random.seed(20260714)

RATE = 22050

# Базовый цвет спрайта ключа — оранжевый. Остальные цвета игра получает
# перекраской (hue-shift) при загрузке.
KEY_BASE = ("orange", (255, 146, 0))


# --------------------------------------------------------------------------
# Битмапы
# --------------------------------------------------------------------------
class Canvas:
    """RGB-канва (top-down). BMP пишется с переворотом строк."""

    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = bytearray(w * h * 3)

    def _i(self, x, y):
        return (y * self.w + x) * 3

    def set(self, x, y, rgb):
        if 0 <= x < self.w and 0 <= y < self.h:
            i = self._i(x, y)
            self.px[i], self.px[i + 1], self.px[i + 2] = rgb[2], rgb[1], rgb[0]

    def fill(self, rgb):
        for i in range(0, self.w * self.h * 3, 3):
            self.px[i], self.px[i + 1], self.px[i + 2] = rgb[2], rgb[1], rgb[0]

    def rect(self, x0, y0, x1, y1, rgb):
        for y in range(y0, y1):
            for x in range(x0, x1):
                self.set(x, y, rgb)

    def ellipse(self, cx, cy, rx, ry, rgb, fill=True, outline=2):
        for y in range(cy - ry, cy + ry + 1):
            for x in range(cx - rx, cx + rx + 1):
                dx = (x - cx) / rx
                dy = (y - cy) / ry
                d = dx * dx + dy * dy
                if fill and d <= 1.0:
                    self.set(x, y, rgb)
                elif not fill and abs(d - 1.0) <= (outline * 1.0 / max(rx, ry)) * 2.2:
                    self.set(x, y, rgb)

    def poly(self, points, rgb):
        """Заливка простого выпуклого многоугольника по строкам."""
        ys = [p[1] for p in points]
        for y in range(min(ys), max(ys) + 1):
            xs = []
            for i in range(len(points)):
                x1, y1 = points[i]
                x2, y2 = points[(i + 1) % len(points)]
                if (y1 <= y < y2) or (y2 <= y < y1):
                    t = (y - y1) / (y2 - y1)
                    xs.append(x1 + t * (x2 - x1))
            if xs:
                xs.sort()
                x0, x1 = int(math.floor(xs[0])), int(math.ceil(xs[-1]))
                for x in range(x0, x1 + 1):
                    self.set(x, y, rgb)

    def noise(self, amount, lo=(0, 0, 0), hi=(255, 255, 255), region=None):
        x0, y0, x1, y1 = region or (0, 0, self.w, self.h)
        for y in range(y0, y1):
            for x in range(x0, x1):
                if random.random() < amount:
                    rgb = tuple(random.randint(lo[i], hi[i]) for i in range(3))
                    self.set(x, y, rgb)

    def save_bmp(self, path):
        if _keep_existing(path):
            return
        w, h = self.w, self.h
        row_size = (w * 3 + 3) & ~3
        data_size = row_size * h
        bfh = struct.pack("<2sIHHI", b"BM", 54 + data_size, 0, 0, 54)
        bih = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, data_size,
                          2835, 2835, 0, 0)
        data = bytearray()
        for y in range(h - 1, -1, -1):
            row = bytes(self.px[y * w * 3:(y + 1) * w * 3])
            data += row + b"\x00" * (row_size - w * 3)
        path.write_bytes(bfh + bih + bytes(data))


# --------------------------------------------------------------------------
# ICO (32bpp, alpha)
# --------------------------------------------------------------------------
class ARGB:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = bytearray(w * h * 4)

    def _i(self, x, y):
        return (y * self.w + x) * 4

    def set(self, x, y, bgra):
        if 0 <= x < self.w and 0 <= y < self.h:
            i = self._i(x, y)
            self.px[i:i + 4] = bytes(bgra)

    def circle(self, cx, cy, r, bgra, fill=True, hole_r=0):
        for y in range(cy - r, cy + r + 1):
            for x in range(cx - r, cx + r + 1):
                d = (x - cx) ** 2 + (y - cy) ** 2
                if fill:
                    if d <= r * r and d > hole_r * hole_r:
                        self.set(x, y, bgra)
                else:
                    if abs(math.sqrt(d) - r) < 1.0:
                        self.set(x, y, bgra)

    def save_ico(self, path):
        if _keep_existing(path):
            return
        w, h = self.w, self.h
        and_row = (w + 31) // 32 * 4
        and_size = and_row * h
        xor = b""
        for y in range(h - 1, -1, -1):
            xor += bytes(self.px[y * w * 4:(y + 1) * w * 4])
        and_mask = b"\x00" * and_size
        bih = struct.pack("<IiiHHIIiiII", 40, w, h * 2, 1, 32, 0,
                          len(xor), 0, 0, 0, 0)
        data = bih + xor + and_mask
        icon_dir = struct.pack("<HHH", 0, 1, 1)
        entry = struct.pack("<BBBBHHII", w & 0xFF, h & 0xFF, 0, 0, 1, 32,
                            len(data), 6 + 16)
        path.write_bytes(icon_dir + entry + data)


def make_key_icon():
    img = ARGB(32, 32)
    gold = (55, 175, 212, 255)      # BGRA
    gold_dark = (40, 140, 175, 255)
    gold_hi = (120, 220, 250, 255)
    # кольцо
    img.circle(9, 16, 6, gold)
    img.circle(9, 16, 3, (0, 0, 0, 0))       # дырка
    img.circle(9, 16, 6, gold_hi, fill=False, hole_r=0)
    # тело
    for y in range(10, 23):
        for x in range(13, 27):
            if y in (10, 11):
                c = gold_hi
            elif y in (20, 21, 22):
                c = gold_dark
            else:
                c = gold
            img.set(x, y, c)
    img.set(13, 10, gold_hi)
    # зубцы
    for (x0, x1, y0, y1) in [(24, 27, 13, 16), (24, 27, 18, 21)]:
        for y in range(y0, y1):
            for x in range(x0, x1):
                img.set(x, y, gold_dark)
    ICONS.mkdir(parents=True, exist_ok=True)
    img.save_ico(ICONS / "key.ico")


def make_trap_icon():
    """Иконка trap.exe: тёмная рожица с горящими красными глазами и оскалом."""
    img = ARGB(32, 32)
    bg = (14, 12, 12, 255)          # BGRA
    rim = (60, 60, 220, 255)
    face = (70, 62, 58, 255)
    red = (40, 40, 235, 255)        # ярко-красный
    red_hi = (120, 160, 255, 255)
    white = (235, 235, 240, 255)
    dark = (8, 8, 8, 255)
    # круглый фон с красной каймой
    img.circle(16, 16, 15, bg)
    img.circle(16, 16, 15, rim, fill=False)
    # лицо
    img.circle(16, 17, 11, face)
    # глаза: красные с белым блик-зрачком
    img.circle(11, 13, 3, red)
    img.circle(21, 13, 3, red)
    img.set(11, 13, white)
    img.set(21, 13, white)
    img.set(11, 13 - 1, red_hi)
    img.set(21, 13 - 1, red_hi)
    # рот-оскал: тёмная полоса + белые зубья
    for x in range(9, 24):
        for y in range(21, 25):
            img.set(x, y, dark)
    for i, x in enumerate(range(10, 23)):
        if i % 2 == 0:
            img.set(x, 22, white)
            img.set(x, 23, white)
    ICONS.mkdir(parents=True, exist_ok=True)
    img.save_ico(ICONS / "trap.ico")


def make_repair_icon():
    """Иконка computer_repair.exe: монитор с зелёным крестом ремонта."""
    img = ARGB(32, 32)
    frame = (205, 200, 195, 255)    # серебристый корпус
    frame_dk = (140, 135, 130, 255)
    screen = (35, 30, 22, 255)      # тёмный экран
    green = (80, 200, 90, 255)      # зелёный крест
    green_hi = (150, 245, 160, 255)
    # корпус монитора
    for y in range(3, 24):
        for x in range(2, 30):
            c = frame if y < 5 or y >= 22 or x < 4 or x >= 28 else screen
            img.set(x, y, c)
    # ножка и подставка
    for y in range(24, 27):
        for x in range(14, 18):
            img.set(x, y, frame_dk)
    for x in range(9, 23):
        img.set(x, 27, frame)
        img.set(x, 28, frame_dk)
        img.set(x, 29, frame)
    # зелёный крест
    for y in range(7, 19):
        for x in range(14, 18):
            img.set(x, y, green)
    for y in range(11, 15):
        for x in range(10, 22):
            img.set(x, y, green)
    # блики
    img.set(15, 8, green_hi)
    img.set(15, 9, green_hi)
    img.set(11, 12, green_hi)
    ICONS.mkdir(parents=True, exist_ok=True)
    img.save_ico(ICONS / "repair.ico")


# --------------------------------------------------------------------------
# Скримеры
# --------------------------------------------------------------------------
def screamer_face():
    c = Canvas(480, 360)
    c.fill((18, 4, 4))
    # виньетка
    for y in range(360):
        for x in range(480):
            dx = (x - 240) / 240.0
            dy = (y - 180) / 180.0
            d = math.sqrt(dx * dx + dy * dy)
            if d > 0.8:
                f = min(1.0, (d - 0.8) / 0.5)
                i = c._i(x, y)
                c.px[i] = int(c.px[i] * (1 - f * 0.85))
                c.px[i + 1] = int(c.px[i + 1] * (1 - f * 0.85))
                c.px[i + 2] = int(c.px[i + 2] * (1 - f * 0.85))
    # лицо
    c.ellipse(240, 180, 150, 170, (205, 212, 218))
    # глазницы
    c.ellipse(160, 145, 62, 72, (12, 12, 16))
    c.ellipse(320, 145, 62, 72, (12, 12, 16))
    # зрачки-точки
    for cx, cy in ((160, 170), (320, 170)):
        c.ellipse(cx, cy, 10, 10, (255, 255, 255))
    # нос
    c.poly([(240, 190), (228, 215), (252, 215)], (40, 40, 48))
    # рот с зубами
    c.ellipse(240, 270, 105, 62, (8, 8, 10))
    c.rect(160, 205, 320, 225, (205, 212, 218))
    for i in range(7):
        x0 = 165 + i * 22
        c.poly([(x0, 208), (x0 + 11, 205), (x0 + 22, 208), (x0 + 11, 228)],
               (240, 242, 244))
        c.poly([(x0, 322), (x0 + 11, 326), (x0 + 22, 322), (x0 + 11, 300)],
               (240, 242, 244))
    # тёмные круги под глазами
    c.ellipse(160, 225, 55, 20, (70, 75, 82))
    c.ellipse(320, 225, 55, 20, (70, 75, 82))
    # пятна и шум
    for _ in range(400):
        x, y = random.randrange(480), random.randrange(360)
        v = random.randrange(70)
        c.set(x, y, (v, v, v))
    c.save_bmp(SCREAMERS / "face.bmp")


def screamer_skull():
    c = Canvas(480, 360)
    c.fill((6, 6, 8))
    # красное свечение
    for y in range(200, 360):
        for x in range(140, 340):
            d = math.hypot(x - 240, y - 280) / 90.0
            if d < 1.0:
                f = (1 - d) * 0.5
                i = c._i(x, y)
                c.px[i] = int(c.px[i] + 90 * f)
    # череп
    c.ellipse(240, 165, 105, 118, (225, 228, 232))
    c.rect(240 - 60, 255, 240 + 60, 285, (225, 228, 232))
    c.rect(240 - 34, 282, 240 + 34, 305, (225, 228, 232))
    # глазницы
    c.ellipse(198, 155, 34, 40, (10, 10, 12))
    c.ellipse(282, 155, 34, 40, (10, 10, 12))
    # нос
    c.poly([(240, 195), (226, 225), (240, 238), (254, 225)], (10, 10, 12))
    # рот - ряд зубов
    c.rect(196, 268, 284, 285, (20, 20, 24))
    for i in range(5):
        x0 = 200 + i * 17
        for y in range(268, 285):
            for x in range(x0, x0 + 12):
                if (x - x0) % 3 != 0 or (y - 268) % 4 != 1:
                    c.set(x, y, (235, 238, 242))
    # трещины
    for (a, b) in [((205, 60), (185, 100)), ((275, 55), (300, 110)),
                   ((150, 180), (185, 205))]:
        x, y = a
        while abs(x - b[0]) > 2 or abs(y - b[1]) > 2:
            c.set(x, y, (40, 42, 46))
            x += 1 if b[0] > x else -1
            y += 1 if b[1] > y else -1
    for _ in range(500):
        x, y = random.randrange(480), random.randrange(360)
        v = random.randrange(30)
        c.set(x, y, (v, v, v))
    c.save_bmp(SCREAMERS / "skull.bmp")


def screamer_distort():
    c = Canvas(480, 360)
    c.fill((10, 12, 14))
    c.noise(0.35, (30, 34, 40), (90, 96, 104))
    # лицо, размытое полосами
    base = (150, 156, 162)
    for y in range(60, 300):
        shift = int(14 * math.sin(y / 9.0) + random.randrange(-4, 5))
        for x in range(60, 420):
            xx = x + shift
            if 0 <= xx < 480:
                c.set(xx, y, base)
    # глаза - красные
    for cx, cy in ((175, 150), (305, 150)):
        c.ellipse(cx, cy, 28, 22, (255, 30, 30))
        c.ellipse(cx, cy, 10, 10, (255, 120, 90))
    # рот
    for y in range(245, 285):
        for x in range(170, 310):
            c.set(x, y, (20, 20, 24))
    for i in range(10):
        x0 = 172 + i * 14
        c.poly([(x0, 248), (x0 + 7, 243), (x0 + 14, 248), (x0 + 7, 268)],
               (240, 244, 248))
    # помехи
    for y in range(0, 360, 6):
        if random.random() < 0.35:
            c.rect(0, y, 480, y + 2, (20, 200, 30) if random.random() < 0.3
                   else (60, 60, 70))
    c.save_bmp(SCREAMERS / "distort.bmp")


# --------------------------------------------------------------------------
# Спрайт ключа resources/keys/key.png (RGBA, прозрачный фон).
# Единственный оранжевый ключ: игра сама перекрашивает его в остальные цвета.
# --------------------------------------------------------------------------
def _shade(rgb, k):
    return tuple(max(0, min(255, int(c * k))) for c in rgb)


def save_png(path, w, h, rgba):
    """Минимальный PNG-райтер (RGBA8, без фильтров), чистый Python."""
    if _keep_existing(path):
        return
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xffffffff))
    raw = b"".join(b"\x00" + bytes(rgba[y * w * 4:(y + 1) * w * 4])
                   for y in range(h))
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
                     chunk(b"IDAT", zlib.compress(raw, 9)) +
                     chunk(b"IEND", b""))


class RGBA:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = bytearray(w * h * 4)   # фон = прозрачный

    def set(self, x, y, rgba):
        if 0 <= x < self.w and 0 <= y < self.h:
            i = (y * self.w + x) * 4
            self.px[i:i + 4] = bytes(rgba)

    def poly(self, points, rgb, alpha=255):
        ys = [p[1] for p in points]
        for y in range(min(ys), max(ys) + 1):
            xs = []
            for i in range(len(points)):
                x1, y1 = points[i]
                x2, y2 = points[(i + 1) % len(points)]
                if (y1 <= y < y2) or (y2 <= y < y1):
                    t = (y - y1) / (y2 - y1)
                    xs.append(x1 + t * (x2 - x1))
            if xs:
                xs.sort()
                for x in range(int(math.floor(xs[0])), int(math.ceil(xs[-1])) + 1):
                    self.set(x, y, (rgb[0], rgb[1], rgb[2], alpha))

    def ring(self, cx, cy, r_out, r_in, rgb):
        for y in range(cy - r_out - 1, cy + r_out + 2):
            for x in range(cx - r_out - 1, cx + r_out + 2):
                d = math.hypot(x - cx, y - cy)
                if r_in <= d <= r_out:
                    self.set(x, y, (rgb[0], rgb[1], rgb[2], 255))


def make_key_sprite():
    """Оранжевый ключ 128x128 с прозрачным фоном."""
    img = RGBA(128, 128)
    base = KEY_BASE[1]
    hi = _shade(base, 1.35)
    lo = _shade(base, 0.60)
    outline = _shade(base, 0.30)

    # кольцо: тёмный контур чуть шире, затем основной цвет
    img.ring(88, 64, 24, 8, outline)
    img.ring(88, 64, 22, 10, base)
    for a in range(-40, 140):   # тень на нижней дуге
        t = math.radians(a)
        x, y = int(88 + 16 * math.cos(t)), int(64 + 16 * math.sin(t))
        if y > 70:
            img.set(x, y, (lo[0], lo[1], lo[2], 255))
            img.set(x, y + 1, (lo[0], lo[1], lo[2], 255))

    def body(x0, y0, x1, y1, ch, color):
        pts = [(x0 + ch, y0), (x1 - ch, y0), (x1, y0 + ch), (x1, y1 - ch),
               (x1 - ch, y1), (x0 + ch, y1), (x0, y1 - ch), (x0, y0 + ch)]
        img.poly(pts, outline)
        pts2 = [(x0 + ch, y0 + 1), (x1 - ch, y0 + 1), (x1 - 1, y0 + ch),
                (x1 - 1, y1 - ch), (x1 - ch, y1 - 1), (x0 + ch, y1 - 1),
                (x0 + 1, y1 - ch), (x0 + 1, y0 + ch)]
        img.poly(pts2, color)

    body(20, 50, 72, 78, 7, base)          # тело
    for x in range(21, 71):
        img.set(x, 52, (hi[0], hi[1], hi[2], 255))   # блик
        img.set(x, 53, (hi[0], hi[1], hi[2], 255))
        img.set(x, 75, (lo[0], lo[1], lo[2], 255))   # тень
        img.set(x, 76, (lo[0], lo[1], lo[2], 255))

    body(6, 36, 26, 54, 4, base)           # зубцы
    body(6, 74, 26, 92, 4, base)
    for x in range(7, 25):
        img.set(x, 38, (hi[0], hi[1], hi[2], 255))
        img.set(x, 76, (hi[0], hi[1], hi[2], 255))

    ensure_dirs(KEYS)
    save_png(KEYS / "key.png", 128, 128, img.px)


# --------------------------------------------------------------------------
# Катсцена resources/cat-scene/: background.png (фон) и pc.png (компьютер).
# RGBA с прозрачностью; свои файлы просто кладутся рядом с теми же именами.
# --------------------------------------------------------------------------
def make_cat_background():
    """Тёмный фон 960x540: градиент + полосы + шум (игра растягивает на экран)."""
    w, h = 960, 540
    img = RGBA(w, h)
    top = (14, 14, 20)
    bottom = (4, 4, 6)
    for y in range(h):
        t = y / (h - 1)
        r = int(top[0] + (bottom[0] - top[0]) * t)
        g = int(top[1] + (bottom[1] - top[1]) * t)
        b = int(top[2] + (bottom[2] - top[2]) * t)
        for x in range(w):
            img.set(x, y, (r, g, b, 255))
    # едва заметные вертикальные полосы
    for x in range(0, w, 48):
        for y in range(h):
            i = (y * w + x) * 4
            for k in range(3):
                img.px[i + k] = min(255, img.px[i + k] + 6)
    # шум
    for _ in range(9000):
        x, y = random.randrange(w), random.randrange(h)
        v = random.randrange(24)
        i = (y * w + x) * 4
        img.px[i] = min(255, img.px[i] + v)
        img.px[i + 1] = min(255, img.px[i + 1] + v)
        img.px[i + 2] = min(255, img.px[i + 2] + v)
    ensure_dirs(CATSCENE)
    save_png(CATSCENE / "background.png", w, h, img.px)


def make_cat_pc():
    """Компьютер 256x320 (RGBA): монитор, экран, подставка, основание."""
    w, h = 256, 320
    img = RGBA(w, h)
    body = (150, 150, 155, 255)
    body_hi = (185, 185, 190, 255)
    body_lo = (95, 95, 100, 255)
    screen = (18, 22, 26, 255)

    def rect(x0, y0, x1, y1, rgba):
        for y in range(y0, y1):
            for x in range(x0, x1):
                img.set(x, y, rgba)

    # монитор
    rect(58, 20, 198, 140, body)
    rect(66, 28, 190, 132, screen)
    # блик на экране
    for y in range(34, 70):
        for x in range(72 + (y - 34), 120 + (y - 34)):
            if x < 186:
                img.set(x, y, (36, 44, 52, 255))
    # рамка-тень снизу монитора
    rect(58, 128, 198, 140, body_lo)
    rect(58, 20, 198, 26, body_hi)
    # подставка
    rect(116, 140, 140, 176, body)
    rect(116, 168, 140, 176, body_lo)
    # пьедестал (трапеция)
    img.poly([(96, 176), (160, 176), (172, 232), (84, 232)], body)
    img.poly([(96, 176), (100, 176), (112, 232), (84, 232)], body_lo)
    rect(84, 232, 172, 244, body_lo)   # низ пьедестала
    rect(84, 228, 172, 236, body)      # основание
    rect(84, 228, 172, 230, body_hi)
    # индикатор питания
    for y in range(206, 218):
        for x in range(98, 110):
            if (x - 104) ** 2 + (y - 212) ** 2 <= 36:
                img.set(x, y, (0, 255, 120, 255))
    ensure_dirs(CATSCENE)
    save_png(CATSCENE / "pc.png", w, h, img.px)


def make_cat_spikes():
    """Шипы в стиле Geometry Dash: 960x270 RGBA (прозрачный верх),
    чёрные треугольники со светлой обводкой, ровный ряд для бесшовного скролла."""
    w, h = 960, 270
    img = RGBA(w, h)
    n = 12
    step = w // n
    tip_y = 22                       # вершины почти у верха канвы
    fill = (8, 8, 10, 255)           # почти чёрный, как в GD
    outline = (232, 232, 236, 255)   # светлая обводка

    def tri(pts):
        img.poly([(int(x), int(y)) for x, y in pts], outline)
        # внутренний треугольник: стянуть к центроиду -> видна обводка
        cx = sum(p[0] for p in pts) / 3.0
        cy = sum(p[1] for p in pts) / 3.0
        inner = [(cx + (p[0] - cx) * 0.84, cy + (p[1] - cy) * 0.84)
                 for p in pts]
        img.poly([(int(x), int(y)) for x, y in inner], fill)

    for i in range(n):
        x0 = i * step
        tri([(x0 + 3, h - 2), (x0 + step / 2, tip_y), (x0 + step - 3, h - 2)])
    ensure_dirs(CATSCENE)
    save_png(CATSCENE / "spikes.png", w, h, img.px)


# --------------------------------------------------------------------------
# Звуки
# --------------------------------------------------------------------------
def _keep_existing(path):
    """True, если файл уже есть и его нельзя перезаписывать."""
    if path.exists():
        print(f"  [skip] {path} уже существует — не перезаписываю")
        return True
    return False


def write_wav(path, samples):
    if _keep_existing(path):
        return
    data = bytearray()
    for s in samples:
        v = max(-1.0, min(1.0, s))
        data += struct.pack("<h", int(v * 32767))
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(bytes(data))


def gen_music():
    dur, fade = 12.0, 1.2
    n = int(RATE * dur)
    freqs = [55.0, 110.0, 130.81, 164.81, 196.0]
    out = []
    for i in range(n):
        t = i / RATE
        env = min(1.0, t / fade) * min(1.0, (dur - t) / fade)
        lfo = 0.72 + 0.28 * math.sin(2 * math.pi * 0.06 * t)
        v = 0.0
        for f in freqs:
            v += math.sin(2 * math.pi * f * t)
        v /= len(freqs)
        pad = math.sin(2 * math.pi * 55.5 * t) * 0.12
        out.append((v * 0.38 + pad) * env * lfo)
    write_wav(MUSIC / "limbo.wav", out)


def gen_scary():
    n = int(RATE * 2.2)
    s = []
    for i in range(n):
        t = i / RATE
        rumble = math.sin(2 * math.pi * 36 * t) * 0.5 + math.sin(2 * math.pi * 51 * t + 1.0) * 0.3
        screech = math.sin(2 * math.pi * (420 + 1300 * t / 2.2) * t) * math.sin(math.pi * t / 2.2) * 0.55
        noise = (random.random() * 2 - 1) * 0.12
        s.append(rumble + screech + noise)
    write_wav(SOUNDS / "scary1.wav", s)

    n = int(RATE * 1.6)
    s = []
    for i in range(n):
        t = i / RATE
        env = math.exp(-t * 7.0)
        v = 0.0
        for f in (233.0, 311.0, 466.0, 587.0, 932.0):
            v += math.sin(2 * math.pi * f * t)
        v /= 5.0
        noise = (random.random() * 2 - 1) * 0.35 * env
        s.append((v * 0.7 + noise) * env)
    write_wav(SOUNDS / "scary2.wav", s)

    n = int(RATE * 3.0)
    s = []
    breath = 0.0
    for i in range(n):
        t = i / RATE
        if i % 2000 == 0:
            breath = random.uniform(0.25, 0.7)
        noise = (random.random() * 2 - 1) * breath
        drone = math.sin(2 * math.pi * 48 * t) * 0.3
        s.append(noise + drone)
    write_wav(SOUNDS / "scary3.wav", s)


# --------------------------------------------------------------------------
def main():
    ensure_dirs(ICONS, MUSIC, SCREAMERS, SOUNDS, KEYS, CATSCENE)
    make_key_icon()
    make_trap_icon()
    make_repair_icon()
    make_key_sprite()
    screamer_face()
    screamer_skull()
    screamer_distort()
    gen_music()
    gen_scary()
    make_cat_background()
    make_cat_pc()
    make_cat_spikes()
    print("Ассеты-заглушки сгенерированы.")
    print(f"  icons:     {ICONS / 'key.ico'}, {ICONS / 'trap.ico'}, "
          f"{ICONS / 'repair.ico'}")
    print(f"  keys:      {KEYS / 'key.png'}")
    print(f"  screamers: {len(list(SCREAMERS.glob('*.bmp')))} шт")
    print(f"  sounds:    {len(list(SOUNDS.glob('*.wav')))} шт")
    print(f"  music:     {len(list(MUSIC.glob('*.wav')))} шт")
    print(f"  cat-scene: {CATSCENE / 'background.png'}, {CATSCENE / 'pc.png'}, "
          f"{CATSCENE / 'spikes.png'}")


if __name__ == "__main__":
    main()
