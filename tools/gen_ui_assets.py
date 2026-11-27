#!/usr/bin/env python3
"""gen_ui_assets.py - 浴室リモコン製品デモ(docs/design.md v2 / §20 CLUT8)の画面アセット生成。

  python tools/gen_ui_assets.py                 # src/app に ui_*.c / ui_assets.h を、preview と preview_p2b に PNG・誤差表を出力
  python tools/gen_ui_assets.py --check         # ファイルは書かない。検査と容量・色数・P1 との誤差の表示だけ
  --out-dir DIR / --preview-dir DIR / --c8-preview-dir DIR / --font PATH(既定 C:\\Windows\\Fonts\\NotoSansJP-VF.ttf)

座標・色・文言の出どころは docs/ref/bath-remote-mock.html(承認済み v4)と design.md §1。
ファームにフォントは載せない。ホストのフォントで描いた画素(背景、数字 A8)を、全画面で共通の 256 色パレットの
8bit index にして C 配列にする(GLCDC の CLUT8 表示。design §20.3: 固定色 + 重み付き k-means、乱数なし)。
時刻・乱数は使わない(再実行で同じ出力)。Pillow と numpy が必要。
"""
import argparse
import hashlib
import math
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

W, H, S = 480, 272, 4           # 画面と、描画時の拡大率(4倍で描いて縮小)
DEFAULT_FONT = r"C:\Windows\Fonts\NotoSansJP-VF.ttf"
CHARS = "0123456789:.%-"         # 数字フォントの文字(番号 0..13。13 = 負号)
CHARS_F14 = "()月日火水木金土"      # F14 だけの字(番号 14..22。日付「M月D日(曜)」用。ほかのフォントは幅 0 の空の字形)
NGLYPH = len(CHARS) + len(CHARS_F14)   # UI_GLYPHS = 23

# ---- 色(design §1.1)----
C = dict(BG="#f1f5f8", WHITE="#ffffff", LINE="#d3dce3", INK="#374151", SUB="#7b8794",
         BLUE_T="#3b6f93", ORANGE_T="#b4673a", GREEN_T="#3f8a79", GRAY_T="#5b6670", RED_T="#a14a4a",
         BLUE_B="#dcebf7", ORANGE_B="#fbe6d6", GREEN_B="#dcefe9", GRAY_B="#e6e9ec", RED_B="#f7dcdc",
         PM_B="#dfe9f1", GROOVE_F="#dde6ec", BAR_F="#8bbfe0", GROOVE_R="#e8e2dc", BAR_R="#efa979",
         BAR_U="#cfdde6", BAR_U_TODAY="#6fbcae", STBY_BG="#1f262d", STBY_CLK="#6b7785", STBY_SUB="#56616d",
         # 天気・Wi-Fi アイコン(design §22.9)。平らな色なので固定色にする
         SUN="#f5b82e", CLOUD="#aebccb", CLOUD_DK="#8795a4", RAIN="#4f94d4", ICE="#7fb7e6")
NEW_COLORS = ("SUN", "CLOUD", "CLOUD_DK", "RAIN", "ICE")

SCREENS = ["HOME", "FILL", "REHEAT", "USAGE", "STANDBY"]
BGS = [("home", "HOME"), ("fill_run", "FILL"), ("fill_done", "FILL"), ("reheat_run", "REHEAT"),
       ("reheat_done", "REHEAT"), ("usage", "USAGE"), ("stby", "STANDBY")]

# 数字フォント(design §2.3): (ID, px, 太さ)
FONTS = [("F14", 14, 400), ("F19", 19, 400), ("F22", 22, 500), ("F40", 40, 500), ("F54", 54, 500),
         ("F56", 56, 500), ("F62", 62, 500), ("F64", 64, 500), ("F78", 78, 500)]

# ボタン: (id, ログ名, 画面, 見た目 x,y,w,h, 条件, 背景色, 文字色, ラベル, px, アイコン, 角丸, 当たり判定(省略可))
#   条件: ALWAYS / FILL_RUN / FILL_DONE / REHEAT_RUN / REHEAT_DONE
BUTTONS = [
    ("H_DN", "h_dn", "HOME", (298, 136, 72, 38), "ALWAYS", "PM_B", "BLUE_T", "－", 24, None, 10, None),
    ("H_UP", "h_up", "HOME", (382, 136, 72, 38), "ALWAYS", "PM_B", "BLUE_T", "＋", 24, None, 10, None),
    ("H_FILL", "h_fill", "HOME", (16, 192, 106, 64), "ALWAYS", "BLUE_B", "BLUE_T", "湯はり", 15, "tub", 10, None),
    ("H_REHEAT", "h_reheat", "HOME", (130, 192, 106, 64), "ALWAYS", "ORANGE_B", "ORANGE_T", "追い焚き", 15, "flame", 10, None),
    ("H_USAGE", "h_usage", "HOME", (244, 192, 106, 64), "ALWAYS", "GREEN_B", "GREEN_T", "使用量", 15, "drop", 10, None),
    ("H_POWER", "h_power", "HOME", (358, 192, 106, 64), "ALWAYS", "GRAY_B", "GRAY_T", "電源", 15, "power", 10, None),
    ("H_DEMO", "h_demo", "HOME", (228, 5, 84, 24), "ALWAYS", "GRAY_B", "GRAY_T", "デモ", 14, "play", 12, (216, 0, 108, 44)),
    ("F_STOP", "f_stop", "FILL", (16, 208, 224, 48), "FILL_RUN", "RED_B", "RED_T", "停止", 18, None, 10, None),
    ("F_DONE", "f_done", "FILL", (16, 208, 224, 48), "FILL_DONE", "GREEN_B", "GREEN_T", "完了", 18, None, 10, None),
    ("F_HOME", "f_home", "FILL", (248, 208, 216, 48), "ALWAYS", "GRAY_B", "GRAY_T", "ホーム", 18, None, 10, None),
    ("R_STOP", "r_stop", "REHEAT", (16, 216, 224, 42), "REHEAT_RUN", "RED_B", "RED_T", "停止", 17, None, 10, None),
    ("R_DONE", "r_done", "REHEAT", (16, 216, 224, 42), "REHEAT_DONE", "GREEN_B", "GREEN_T", "完了", 17, None, 10, None),
    ("R_HOME", "r_home", "REHEAT", (248, 216, 216, 42), "ALWAYS", "GRAY_B", "GRAY_T", "ホーム", 17, None, 10, None),
    ("U_BACK", "u_back", "USAGE", (380, 234, 84, 32), "ALWAYS", "GRAY_B", "GRAY_T", "戻る", 15, None, 10, None),
    ("S_WAKE", "s_wake", "STANDBY", (0, 0, 480, 272), "ALWAYS", None, None, None, 0, None, 0, None),
    # 更新ボタン(design §22.8)。見た目 28x28、当たり判定 60x52。ラベルは空文字(形だけ作る。中のアイコンは実行時のスプライト)
    ("H_WX", "h_wx", "HOME", (248, 158, 28, 28), "ALWAYS", "BLUE_B", "BLUE_T", "", 14, None, 8, (228, 136, 60, 52)),
]
# どの背景にどのボタンを焼き込むか
BG_BUTTONS = {
    "home": ["H_DN", "H_UP", "H_FILL", "H_REHEAT", "H_USAGE", "H_POWER", "H_DEMO", "H_WX"],
    "fill_run": ["F_STOP", "F_HOME"], "fill_done": ["F_DONE", "F_HOME"],
    "reheat_run": ["R_STOP", "R_HOME"], "reheat_done": ["R_DONE", "R_HOME"],
    "usage": ["U_BACK"], "stby": [],
}

USAGE_H = [42, 34, 50, 31, 47, 54, 44]          # 直近7日の棒の高さ(モックの v/152*54 を丸めた値)
USAGE_LABELS = ["金", "土", "日", "月", "火", "水", "今日"]

ICONS = {   # モックの SVG パス(中心原点)。(パス, 塗りか線か, 線幅)
    "drop": [("M0-13C8-3 11 2 11 6A11 11 0 0 1 -11 6C-11 2 -8-3 0-13Z", "fill", 0)],
    "flame": [("M1-14C2-8 10-4 10 4A10 10 0 0 1 -10 4C-10-1 -6-3 -5-9C-3-6 -2-5 -1-4C1-7 1-11 1-14Z", "fill", 0)],
    "tub": [("M-15-3H15V3A9 9 0 0 1 6 12H-6A9 9 0 0 1 -15 3Z", "fill", 0),
            ("M-9-9Q-5-13 -1-9T7-9", "stroke", 2.5)],
    "power": [("M-7-8A10 10 0 1 0 7-8", "stroke", 3), ("M0-13V-2", "stroke", 3)],
}


def hexrgb(h):
    h = C.get(h, h)
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


def rgb565(rgb):
    r, g, b = rgb
    return (((r * 31 + 127) // 255) << 11) | (((g * 63 + 127) // 255) << 5) | ((b * 31 + 127) // 255)


def c565(name):
    return rgb565(hexrgb(name))


# ------------------------------------------------------------------ フォント
class Fonts:
    def __init__(self, path):
        self.path = path
        self.cache = {}
        self.vf = path.lower().endswith("vf.ttf")
        with open(path, "rb") as f:
            self.sha = hashlib.sha256(f.read()).hexdigest()[:12]

    def get(self, px, wght):
        key = (px, wght)
        if key not in self.cache:
            f = ImageFont.truetype(self.path, int(round(px * S)))
            if self.vf:
                f.set_variation_by_axes([wght])
            self.cache[key] = f
        return self.cache[key]

    def width(self, text, px, wght):
        return self.get(px, wght).getlength(text) / S

    def check(self, text, px=24, wght=400):
        """焼き込む全文字が欠けていないか。.notdef(存在しない文字の字形)と同じ画素なら失敗"""
        f = self.get(px, wght)

        def render(ch):
            im = Image.new("L", (px * S * 2, px * S * 2), 0)
            ImageDraw.Draw(im).text((4, 4), ch, font=f, fill=255)
            return im.tobytes()
        nd = render("\uFFFE")
        bad = [ch for ch in sorted(set(text)) if ch != " " and render(ch) == nd]
        if bad:
            raise SystemExit("フォントに無い文字: %s (%s)" % ("".join(bad), self.path))


# ------------------------------------------------------------------ SVG パス(アイコン)
def _arc_points(x1, y1, rx, ry, phi, fa, fs, x2, y2, n=32):
    if rx == 0 or ry == 0:
        return [(x2, y2)]
    phi = math.radians(phi)
    cp, sp = math.cos(phi), math.sin(phi)
    dx, dy = (x1 - x2) / 2, (y1 - y2) / 2
    x1p, y1p = cp * dx + sp * dy, -sp * dx + cp * dy
    rx, ry = abs(rx), abs(ry)
    lam = x1p ** 2 / rx ** 2 + y1p ** 2 / ry ** 2
    if lam > 1:
        s = math.sqrt(lam)
        rx, ry = rx * s, ry * s
    num = rx ** 2 * ry ** 2 - rx ** 2 * y1p ** 2 - ry ** 2 * x1p ** 2
    den = rx ** 2 * y1p ** 2 + ry ** 2 * x1p ** 2
    coef = math.sqrt(max(0.0, num / den))
    if fa == fs:
        coef = -coef
    cxp, cyp = coef * rx * y1p / ry, -coef * ry * x1p / rx
    cx = cp * cxp - sp * cyp + (x1 + x2) / 2
    cy = sp * cxp + cp * cyp + (y1 + y2) / 2
    t1 = math.atan2((y1p - cyp) / ry, (x1p - cxp) / rx)
    t2 = math.atan2((-y1p - cyp) / ry, (-x1p - cxp) / rx)
    d = t2 - t1
    if not fs and d > 0:
        d -= 2 * math.pi
    if fs and d < 0:
        d += 2 * math.pi
    pts = []
    for i in range(1, n + 1):
        t = t1 + d * i / n
        pts.append((cx + rx * math.cos(t) * cp - ry * math.sin(t) * sp,
                    cy + rx * math.cos(t) * sp + ry * math.sin(t) * cp))
    return pts


def flatten_path(d):
    """絶対座標の M L H V C Q T A Z を折れ線(点列のリスト)にする"""
    tok = re.findall(r"[MLHVCQTAZ]|[-+]?(?:\d+\.?\d*|\.\d+)", d)
    i, cmd, x, y, subs, cur = 0, None, 0.0, 0.0, [], None
    lastq = None
    nargs = dict(M=2, L=2, H=1, V=1, C=6, Q=4, T=2, A=7, Z=0)
    while i < len(tok):
        if tok[i] in nargs:
            cmd = tok[i]
            i += 1
            if cmd == "Z":
                if cur:
                    cur.append(cur[0])
                continue
        a = [float(t) for t in tok[i:i + nargs[cmd]]]
        i += nargs[cmd]
        if cmd == "M":
            x, y = a
            cur = [(x, y)]
            subs.append(cur)
            cmd = "L"
        elif cmd == "L":
            x, y = a
            cur.append((x, y))
        elif cmd == "H":
            x = a[0]
            cur.append((x, y))
        elif cmd == "V":
            y = a[0]
            cur.append((x, y))
        elif cmd == "C":
            x0, y0 = x, y
            for k in range(1, 25):
                t = k / 24
                u = 1 - t
                cur.append((u ** 3 * x0 + 3 * u * u * t * a[0] + 3 * u * t * t * a[2] + t ** 3 * a[4],
                            u ** 3 * y0 + 3 * u * u * t * a[1] + 3 * u * t * t * a[3] + t ** 3 * a[5]))
            x, y = a[4], a[5]
        elif cmd in ("Q", "T"):
            if cmd == "T":
                q = (2 * x - lastq[0], 2 * y - lastq[1]) if lastq else (x, y)
                a = [q[0], q[1], a[0], a[1]]
            x0, y0 = x, y
            for k in range(1, 25):
                t = k / 24
                u = 1 - t
                cur.append((u * u * x0 + 2 * u * t * a[0] + t * t * a[2], u * u * y0 + 2 * u * t * a[1] + t * t * a[3]))
            lastq = (a[0], a[1])
            x, y = a[2], a[3]
            if cmd == "T":
                cmd = "T"
            continue
        elif cmd == "A":
            cur.extend(_arc_points(x, y, a[0], a[1], a[2], int(a[3]), int(a[4]), a[5], a[6]))
            x, y = a[5], a[6]
        lastq = None
    return subs


# ------------------------------------------------------------------ 描画の部品(座標は 1倍。S倍して描く)
class Canvas:
    def __init__(self, fonts, bg):
        self.im = Image.new("RGB", (W * S, H * S), hexrgb(bg))
        self.d = ImageDraw.Draw(self.im)
        self.fonts = fonts

    def rect(self, x, y, w, h, color):
        self.d.rectangle([x * S, y * S, (x + w) * S - 1, (y + h) * S - 1], fill=hexrgb(color))

    def rrect(self, x, y, w, h, r, color, line=None):
        if line:
            self.rrect(x - 0.5, y - 0.5, w + 1, h + 1, r + 0.5, line)
            x, y, w, h, r = x + 0.5, y + 0.5, w - 1, h - 1, r - 0.5
        self.d.rounded_rectangle([round(x * S), round(y * S), round((x + w) * S) - 1, round((y + h) * S) - 1],
                                 radius=round(r * S), fill=hexrgb(color))

    def text(self, x, y, s, px, color, anchor="l", wght=400):
        f = self.fonts.get(px, wght)
        self.d.text((x * S, y * S), s, font=f, fill=hexrgb(color), anchor=anchor + "s")

    def icon(self, name, cx, cy, color):
        for path, kind, sw in ICONS[name]:
            for sub in flatten_path(path):
                pts = [((cx + px) * S, (cy + py) * S) for px, py in sub]
                if kind == "fill":
                    self.d.polygon(pts, fill=hexrgb(color))
                else:
                    wpx = round(sw * S)
                    self.d.line(pts, fill=hexrgb(color), width=wpx, joint="curve")
                    for p in (pts[0], pts[-1]):
                        self.d.ellipse([p[0] - wpx / 2, p[1] - wpx / 2, p[0] + wpx / 2, p[1] + wpx / 2], fill=hexrgb(color))

    def triangle(self, x, y, w, h, color):
        self.d.polygon([(x * S, y * S), (x * S, (y + h) * S), ((x + w) * S, (y + h / 2) * S)], fill=hexrgb(color))

    def square(self, x, y, w, color):
        self.d.rectangle([round(x * S), round(y * S), round((x + w) * S) - 1, round((y + w) * S) - 1], fill=hexrgb(color))

    def down(self):
        return self.im.resize((W, H), Image.BOX)


def button(cv, spec, fonts):
    bid, name, scr, (x, y, w, h), cond, bgc, fgc, label, px, icon, r, hit = spec
    if label is None:
        return
    cv.rrect(x, y, w, h, r, bgc)
    if label == "":         # 更新ボタン: 面だけ(中のアイコンは実行時のスプライト)
        return
    if icon == "play":      # ▶ + 文字を1組にして枠の中央に置く(▶ は 10x12、間隔 6)
        tw = fonts.width(label, px, 500)
        x0 = x + (w - (10 + 6 + tw)) / 2
        cv.triangle(x0, y + h / 2 - 6, 10, 12, fgc)
        cv.text(x0 + 16, y + h / 2 + px * 0.36, label, px, fgc, "l", 500)
    elif icon:
        cv.icon(icon, x + w / 2, y + 22, fgc)
        cv.text(x + w / 2, y + h - 9, label, px, fgc, "m", 500)
    else:
        cv.text(x + w / 2, y + h / 2 + px * 0.36, label, px, fgc, "m", 500)


# ------------------------------------------------------------------ アイコン(スプライト。design §22.8)
# 配置(x, y, w, h)。下地の色に不透明で焼き込む(透過キーを使わない)
WIFI_XY, WIFI_WH = (112, 7), (20, 20)         # 下地 = ヘッダーの白
WX_XY, WX_WH = (20, 158), (28, 28)            # 下地 = 画面の背景 BG
REF_XY, REF_WH = (252, 162), (20, 20)         # 下地 = 更新ボタンの面 BLUE_B
KINDS = ["sun", "partly", "cloud", "fog", "rain", "snow", "thunder", "unknown"]   # wx_core.h の wx_kind_t と同じ順
WIFI_STATES = ["off", "conn", "on", "err"]
SPRITE_W = 20.0       # スプライトの重み(背景 1 枚 = 1/7 に対し、小さい絵は縁の色が割り当たるよう大きく取る。設計 §22.9 の 50 から 20 に変更: 掃引(10 / 20 / 30 / 50)で、20 は スプライト最大 3 段のまま、静的な画面の不一致が 0.56% -> 0.48%(§20.3.3 の目標 0.5% 以内)になる最小の重み)


class Ic:
    """アイコンを 4 倍で描いて縮小する小さなキャンバス(円・線・多角形・弧だけ)"""

    def __init__(self, w, h, bg):
        self.w, self.h = w, h
        self.im = Image.new("RGB", (w * S, h * S), hexrgb(bg))
        self.d = ImageDraw.Draw(self.im)

    def circle(self, cx, cy, r, color):
        self.d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S - 1, (cy + r) * S - 1], fill=hexrgb(color))

    def line(self, x1, y1, x2, y2, wd, color):
        self.d.line([x1 * S, y1 * S, x2 * S, y2 * S], fill=hexrgb(color), width=round(wd * S))
        for (x, y) in ((x1, y1), (x2, y2)):
            self.circle(x, y, wd / 2, color)

    def poly(self, pts, color):
        self.d.polygon([(x * S, y * S) for x, y in pts], fill=hexrgb(color))

    def rrect(self, x, y, w, h, r, color):
        self.d.rounded_rectangle([x * S, y * S, (x + w) * S - 1, (y + h) * S - 1], radius=r * S, fill=hexrgb(color))

    def cloud(self, cx, cy, k, color):
        self.circle(cx - 5.5 * k, cy + 1.5 * k, 4.5 * k, color)
        self.circle(cx + 0.5 * k, cy - 2.5 * k, 6.5 * k, color)
        self.circle(cx + 6 * k, cy + 1.5 * k, 4.5 * k, color)
        self.rrect(cx - 5.5 * k, cy + 1.0 * k, 11.5 * k, 5 * k, 2.5 * k, color)

    def sun(self, cx, cy, r, ray0, ray1, wd, color):
        self.circle(cx, cy, r, color)
        for i in range(8):
            a = math.pi / 4 * i
            self.line(cx + ray0 * math.cos(a), cy + ray0 * math.sin(a), cx + ray1 * math.cos(a), cy + ray1 * math.sin(a), wd, color)

    def done(self):
        return self.im.resize((self.w, self.h), Image.BOX)


def weather_icon(kind, fonts, bg="BG"):
    c = Ic(28, 28, bg)
    if kind == "sun":
        c.sun(14, 14, 6, 9.2, 12.3, 2.0, "SUN")
    elif kind == "partly":
        c.sun(10, 10, 4.6, 7.6, 10.2, 1.7, "SUN")
        c.cloud(16.5, 17, 1.05, "CLOUD")
    elif kind == "cloud":
        c.cloud(10, 11.5, 0.85, "CLOUD")
        c.cloud(15.5, 16.5, 1.15, "CLOUD_DK")
    elif kind == "fog":
        c.cloud(14, 9, 1.0, "CLOUD")
        for i, (x0, x1) in enumerate(((4, 24), (7, 21), (4, 24))):
            c.line(x0, 18.5 + i * 3.2, x1, 18.5 + i * 3.2, 2.0, "CLOUD")
    elif kind == "rain":
        c.cloud(14, 9.5, 1.15, "CLOUD_DK")
        for x in (8.5, 14, 19.5):
            c.line(x + 1.5, 18, x - 1.2, 24, 2.0, "RAIN")
    elif kind == "snow":
        c.cloud(14, 9.5, 1.15, "CLOUD_DK")
        for x in (8.5, 14, 19.5):
            c.circle(x, 21, 1.9, "ICE")
    elif kind == "thunder":
        c.cloud(14, 9, 1.15, "CLOUD_DK")
        c.poly([(15.5, 15), (9.5, 22.5), (13.5, 22.5), (11.5, 27.5), (19, 19.5), (14.8, 19.5), (17.5, 15)], "SUN")
    else:   # unknown: 灰色の輪 + ?
        c.circle(14, 14, 10.5, "LINE")
        c.circle(14, 14, 8.6, bg)
        c.d.text((14 * S, 14.5 * S), "?", font=fonts.get(18, 500), fill=hexrgb("SUB"), anchor="mm")
    return c.done()


def wifi_icon(state, bg="WHITE"):
    """off / conn / on / err。20x20。扇形の弧 3 本 + 点"""
    c = Ic(20, 20, bg)
    col = {"off": "SUB", "conn": "BLUE_T", "on": "BLUE_T", "err": "RED_T"}[state]
    cx, cy = 10, 16.5
    arcs = [(4.2, col), (8.2, col), (12.2, "LINE" if state == "conn" else col)]
    for r, cl in arcs:
        c.d.arc([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], 227, 313, fill=hexrgb(cl), width=round(2.0 * S))
    c.circle(cx, cy, 1.9, col)
    if state == "off":
        c.line(3, 3, 17, 17.5, 1.8, "SUB")
    if state == "err":
        c.line(16.4, 10.2, 16.4, 14.2, 2.0, "RED_T")
        c.circle(16.4, 17.2, 1.2, "RED_T")
    return c.done()


def refresh_icon(phase):
    """20x20。円弧(約 270 度)+ 矢じり。phase = 0..3(90 度ずつ回す)。ボタンの面の色 BLUE_B の上に焼く"""
    c = Ic(20, 20, "BLUE_B")
    cx = cy = 10
    r = 6.2
    c.d.arc([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], 40, 320, fill=hexrgb("BLUE_T"), width=round(2.2 * S))
    a = math.radians(320)
    ex, ey = cx + r * math.cos(a), cy + r * math.sin(a)
    tx, ty = -math.sin(a), math.cos(a)
    nx, ny = math.cos(a), math.sin(a)
    c.poly([(ex + tx * 4.2, ey + ty * 4.2), (ex + nx * 3.6, ey + ny * 3.6), (ex - nx * 3.6, ey - ny * 3.6)], "BLUE_T")
    return c.done().rotate(-90 * phase, resample=Image.BICUBIC, fillcolor=hexrgb("BLUE_B"))


def sprites(fonts):
    """全スプライト(PIL の RGB 画像)。名前 = wx_<kind> / wifi_<state> / ref_<phase>"""
    sp = {}
    for k in KINDS:
        sp["wx_" + k] = weather_icon(k, fonts)
    for st in WIFI_STATES:
        sp["wifi_" + st] = wifi_icon(st)
    for p in range(4):
        sp["ref_%d" % p] = refresh_icon(p)
    return sp


def header(cv, left, right_parts, slots, fonts):
    """ヘッダー帯。right_parts = (接頭辞, 接尾辞) なら「接頭辞 ■■接尾辞」(■■ は数字2桁のスロット)。str なら焼き込みのみ"""
    cv.rect(0, 0, 480, 34, "WHITE")
    cv.rect(0, 34, 480, 1, "LINE")
    cv.text(16, 23, left, 14, "SUB")
    if isinstance(right_parts, str):
        cv.text(464, 23, right_parts, 14, "SUB", "r")
        return None
    pre, suf, key = right_parts
    adv = slots["_adv14"]
    sx = round(464 - fonts.width(suf, 14, 400)) - 2 * adv
    cv.text(464, 23, suf, 14, "SUB", "r")
    cv.text(sx - fonts.width(" ", 14, 400), 23, pre, 14, "SUB", "r")
    slots[key] = sx
    return sx


def card(cv, x, y, w, h, r=12):
    cv.rrect(x, y, w, h, r, "WHITE", line="LINE")


# ------------------------------------------------------------------ 背景7枚
def draw_bg(name, fonts, slots):
    stby = name == "stby"
    cv = Canvas(fonts, "STBY_BG" if stby else "BG")
    if name == "home":
        header(cv, "", ("浴槽", "℃ ・ 保温中", "HOME_BATH"), slots, fonts)       # 日付は動的(HOME_DATE)
        cv.text(26, 148, "湯はり完了 ・ 水位 標準", 15, "SUB")
        card(cv, 288, 48, 176, 134)
        cv.text(376, 72, "設定温度", 13, "SUB", "m")
        cv.text(406, 124, "℃", 20, "BLUE_T", "l", 500)
        cv.text(108, 181, "℃", 16, "INK", "l", 500)          # 気温の右(数字は HOME_TEMP)
        cv.text(134, 177, "東京", 13, "SUB")
        cv.text(166, 177, "更新", 13, "SUB")                   # 更新時刻は HOME_UPD
    elif name.startswith("fill"):
        done = name == "fill_done"
        header(cv, "湯はり", ("浴槽", "℃", "FILL_BATH"), slots, fonts)
        cv.text(24, 78, "湯はり完了" if done else "湯はり中", 26, "INK", "l", 500)
        cv.rrect(24, 94, 432, 38, 10, "GROOVE_F")
        cv.text(24, 164, "設定水位", 13, "SUB")
        cv.text(24, 188, "標準 200L", 19, "INK")
        cv.text(180, 164, "残り時間", 13, "SUB")
        if done:
            cv.text(180, 188, "－", 19, "INK")
        else:
            cv.text(180, 188, "約", 19, "INK")
            sx = round(180 + fonts.width("約", 19, 400))
            cv.text(sx + slots["_adv19"], 188, "分", 19, "INK")
            slots["FILL_REMAIN"] = sx
        cv.text(336, 164, "今の使用量", 13, "SUB")
        cv.text(336, 188, "0 L/分" if done else "12 L/分", 19, "INK")
    elif name.startswith("reheat"):
        done = name == "reheat_done"
        header(cv, "追い焚き", ("設定", "℃", "REHEAT_SETHDR"), slots, fonts)
        cv.text(24, 78, "追い焚き完了" if done else "追い焚き中", 26, "INK", "l", 500)
        cv.text(172, 142, "℃", 22, "ORANGE_T", "l", 500)
        cv.text(300, 112, "設定温度", 14, "SUB")
        cv.text(352, 150, "℃", 18, "INK")
        cv.rrect(24, 166, 432, 14, 7, "GROOVE_R")
        if done:
            cv.text(24, 204, "設定温度になりました", 14, "SUB")
        else:
            cv.text(24, 204, "あと約", 14, "SUB")
            x1 = round(24 + fonts.width("あと約", 14, 400))
            right = x1 + 2 * slots["_adv14"]
            cv.text(right, 204, "分", 14, "SUB")
            slots["REHEAT_REMAIN"] = right
    elif name == "usage":
        header(cv, "使用量", "今日の累計", slots, fonts)
        cv.text(24, 68, "今の使用量", 14, "SUB")
        cv.text(176, 122, "L/分", 18, "SUB")
        card(cv, 250, 46, 102, 76)
        cv.text(301, 68, "今日", 13, "SUB", "m")
        cv.text(301, 102, "124 L", 24, "INK", "m", 500)
        card(cv, 362, 46, 102, 76)
        cv.text(413, 68, "今月", 13, "SUB", "m")
        cv.text(413, 102, "3,120 L", 24, "INK", "m", 500)
        cv.text(24, 148, "直近7日", 13, "SUB")
        for i, lab in enumerate(USAGE_LABELS):
            cv.text(44 + 64 * i, 224, lab, 12, "SUB", "m")
    else:
        cv.text(240, 250, "タップで復帰", 13, "STBY_SUB", "m")
    for bid in BG_BUTTONS[name]:
        button(cv, next(b for b in BUTTONS if b[0] == bid), fonts)
    return cv.down()


def bg_text_all():
    s = "浴槽℃ ・ 保温中東京更新湯はり完了 ・ 水位 標準設定温度湯はり中標準 200L残り時間約分－今の使用量0 L/分12 L/分"
    s += "追い焚き完了中設定あと設定温度になりました使用量今日の累計L/分今日今月124 L3,120 L直近7日タップで復帰"
    s += "".join(USAGE_LABELS) + "".join(b[7] for b in BUTTONS if b[7])
    return s


# ------------------------------------------------------------------ 数字フォント(A8)
def build_font(fonts, px, wght, f14=False):
    """CHARS(+ F14 だけ CHARS_F14)の各文字を (幅, 高さ, bx, by, adv, A8 の bytes) にする。数字の送り幅は最大値にそろえる。
    F14 以外の CHARS_F14 は幅 0・高さ 0 の空の字形(UI_GLYPHS をそろえるため)"""
    f4 = fonts.get(px, wght)
    chars = CHARS + (CHARS_F14 if f14 else "")
    nat = {ch: f4.getlength(ch) / S for ch in chars}
    adv_digit = max(int(round(nat[ch])) for ch in "0123456789")
    out = []
    for ch in chars:
        adv = adv_digit if ch.isdigit() else int(round(nat[ch]))
        shift = (adv - nat[ch]) / 2 if ch.isdigit() else 0.0     # 数字は送り幅の中で左右中央に
        l, t, r, b = f4.getbbox(ch, anchor="ls")
        x0, y0 = int(math.floor((l / S + shift))) - 2, int(math.floor(t / S)) - 2
        x1, y1 = int(math.ceil((r / S + shift))) + 2, int(math.ceil(b / S)) + 2
        im = Image.new("L", ((x1 - x0) * S, (y1 - y0) * S), 0)
        ImageDraw.Draw(im).text(((-x0 + shift) * S, -y0 * S), ch, font=f4, fill=255, anchor="ls")
        a = im.resize((x1 - x0, y1 - y0), Image.BOX)
        bb = a.getbbox()
        if bb is None:
            raise SystemExit("空の字形: %r" % ch)
        a = a.crop(bb)
        out.append((a.width, a.height, x0 + bb[0], -(y0 + bb[1]), adv, a.tobytes()))
    if not f14:
        out += [(0, 0, 0, 0, 0, b"")] * len(CHARS_F14)
    return adv_digit, out


def rrect_mask(w, h, r, scale=S):
    im = Image.new("L", (w * scale, h * scale), 0)
    ImageDraw.Draw(im).rounded_rectangle([0, 0, w * scale - 1, h * scale - 1], radius=r * scale, fill=255)
    return im.resize((w, h), Image.BOX)


def bar_images(w, h, r, groove, bar):
    """満タンの RGB565 画像(溝色の上に進みを描いたもの)と、右端の角丸だけの A8(幅 r)"""
    m = rrect_mask(w, h, r)
    g, b = hexrgb(groove), hexrgb(bar)
    full = Image.new("RGB", (w, h), g)
    full.paste(Image.new("RGB", (w, h), b), (0, 0), m)
    cap = m.crop((w - r, 0, w, h))
    return full, cap


def tag_image(fonts):
    """デモ中タグ 84x24(ヘッダー WHITE の上に不透明で描く)"""
    cv = Canvas(fonts, "WHITE")
    # 84x24 のタグだけを (228,5) に描いて切り出す
    cv.rrect(228, 5, 84, 24, 12, "RED_B")
    tw = fonts.width("デモ中", 14, 500)
    x0 = 228 + (84 - (9 + 6 + tw)) / 2
    cv.square(x0, 17 - 4.5, 9, "RED_T")
    cv.text(x0 + 15, 17 + 14 * 0.36, "デモ中", 14, "RED_T", "l", 500)
    return cv.down().crop((228, 5, 312, 29))


def build(fonts):
    """全アセットを作って dict で返す"""
    fonts.check(bg_text_all() + "－＋℃・", 24, 400)
    fonts.check("デモ中デモ", 14, 500)
    fonts.check(CHARS_F14, 14, 400)
    slots = {}
    glyphs, pool, adv = {}, bytearray(), {}
    for fid, px, wg in FONTS:
        a, g = build_font(fonts, px, wg, fid == "F14")
        adv[fid] = a
        slots["_adv" + fid[1:]] = a
        lst = []
        for (w, h, bx, by, ad, data) in g:
            lst.append((len(pool), w, h, bx, by, ad))
            pool += data
        glyphs[fid] = lst
    bgs = {}
    for name, scr in BGS:
        bgs[name] = draw_bg(name, fonts, slots)
    shapes, shape_of = [], {}
    for b in BUTTONS:
        if b[7] is None:
            shape_of[b[0]] = -1
            continue
        key = (b[3][2], b[3][3], b[10])
        if key not in shape_of:
            shape_of[key] = len(shapes)
            m = rrect_mask(*key)
            shapes.append((key[0], key[1], m.tobytes()))
        shape_of[b[0]] = shape_of[key]
    bars = [("FILL", 24, 94, 432, 38, 10, 20, "GROOVE_F", "BAR_F"), ("REHEAT", 24, 166, 432, 14, 7, 14, "GROOVE_R", "BAR_R")]
    barimg = [bar_images(w, h, r, gr, bc) for (_, x, y, w, h, r, mw, gr, bc) in bars]
    spr = sprites(fonts)
    return dict(slots=slots, glyphs=glyphs, pool=bytes(pool), adv=adv, bgs=bgs, shapes=shapes, shape_of=shape_of,
                bars=bars, barimg=barimg, tag=tag_image(fonts), spr=spr, spr565={k: img565(v) for k, v in spr.items()})


def slot_table(a):
    """スロット表(design §2.2)。(ID, 画面, フォント, 配置, x, y, 色, 最大文字数)"""
    s = a["slots"]
    return [
        ("HOME_CLOCK", "HOME", "F78", "LEFT", 22, 116, "INK", 5),
        ("HOME_SET", "HOME", "F56", "CENTER", 366, 124, "BLUE_T", 2),
        ("HOME_BATH", "HOME", "F14", "LEFT", s["HOME_BATH"], 23, "SUB", 2),
        ("FILL_BATH", "FILL", "F14", "LEFT", s["FILL_BATH"], 23, "SUB", 2),
        ("FILL_PCT", "FILL", "F22", "CENTER", 240, 121, "INK", 4),
        ("FILL_REMAIN", "FILL", "F19", "LEFT", s["FILL_REMAIN"], 188, "INK", 1),
        ("REHEAT_SETHDR", "REHEAT", "F14", "LEFT", s["REHEAT_SETHDR"], 23, "SUB", 2),
        ("REHEAT_BATH", "REHEAT", "F62", "LEFT", 24, 142, "ORANGE_T", 4),
        ("REHEAT_SET", "REHEAT", "F40", "LEFT", 300, 150, "INK", 2),
        ("REHEAT_REMAIN", "REHEAT", "F14", "RIGHT", s["REHEAT_REMAIN"], 204, "SUB", 2),
        ("USAGE_FLOW", "USAGE", "F54", "LEFT", 24, 122, "GREEN_T", 4),
        ("STBY_CLOCK", "STANDBY", "F64", "CENTER", 240, 150, "STBY_CLK", 5),
        # ホームの天気の行(design §22.8)。日付(ヘッダー左)、気温(右詰め。右端 x = 106)、更新時刻
        ("HOME_DATE", "HOME", "F14", "LEFT", 16, 23, "SUB", 10),
        ("HOME_TEMP", "HOME", "F22", "RIGHT", 106, 181, "INK", 5),
        ("HOME_UPD", "HOME", "F14", "LEFT", 196, 177, "SUB", 5),
    ]


# 数字スロットの下地の色(design §20.4.1)。先頭 = ramp0、2つ目があれば ramp1(下地が進みの色の時)
SLOT_UNDER = {
    "HOME_CLOCK": ("BG",), "HOME_SET": ("WHITE",), "HOME_BATH": ("WHITE",), "FILL_BATH": ("WHITE",),
    "FILL_PCT": ("GROOVE_F", "BAR_F"), "FILL_REMAIN": ("BG",), "REHEAT_SETHDR": ("WHITE",), "REHEAT_BATH": ("BG",),
    "REHEAT_SET": ("BG",), "REHEAT_REMAIN": ("BG",), "USAGE_FLOW": ("BG",), "STBY_CLOCK": ("STBY_BG",),
    "HOME_DATE": ("WHITE",), "HOME_TEMP": ("BG",), "HOME_UPD": ("BG",),
}
SLOT_COL2 = {"HOME_TEMP": "SUB"}       # 同じ下地で文字色が 2 つ(新しい = INK、30 分超の古い = SUB)のスロット。2 つ目の表(ramp[1])に入れる


def slot_pairs(sid, col):
    """(下地, 文字色) の組。ramp[0]、ramp[1] の順。FILL_PCT = 下地が 2 色、HOME_TEMP = 文字色が 2 色"""
    un = SLOT_UNDER[sid]
    if sid in SLOT_COL2:
        return [(un[0], col), (un[0], SLOT_COL2[sid])]
    return [(u, col) for u in un]
SLOT_BGS = {"HOME": ["home"], "FILL": ["fill_run", "fill_done"], "REHEAT": ["reheat_run", "reheat_done"], "USAGE": ["usage"],
            "STANDBY": ["stby"]}


def hit_rect(b):
    if b[11]:
        return b[11]
    x, y, w, h = b[3]
    if b[0] == "S_WAKE":
        return (0, 0, 480, 272)
    x0, y0, x1, y1 = max(0, x - 3), max(0, y - 3), min(W, x + w + 3), min(H, y + h + 3)
    return (x0, y0, x1 - x0, y1 - y0)


def check_all(a):
    """design §10.1 の --check。違反があれば説明つきで失敗"""
    errs = []
    for name, im in a["bgs"].items():
        if im.size != (W, H):
            errs.append("背景 %s が 480x272 でない" % name)
    adv = a["adv"]
    gl = a["glyphs"]
    for (sid, scr, fid, al, x, y, col, mx) in slot_table(a):
        g = gl[fid]
        wmax = mx * adv[fid]
        x0 = x if al == "LEFT" else (x - wmax // 2 if al == "CENTER" else x - wmax)
        x1 = x0 + wmax
        top = y - max(t[4] for t in g)
        bot = y + max(t[2] - t[4] for t in g)
        if x0 < 0 or x1 > W or top < 0 or bot > H:
            errs.append("スロット %s が画面外 (%d..%d, %d..%d)" % (sid, x0, x1, top, bot))
    bt = BUTTONS
    for i in range(len(bt)):
        for j in range(i + 1, len(bt)):
            p, q = bt[i], bt[j]
            if p[2] != q[2] or (p[4] != "ALWAYS" and q[4] != "ALWAYS" and p[4] != q[4]):
                continue
            a1, b1 = hit_rect(p), hit_rect(q)
            if a1[0] < b1[0] + b1[2] and b1[0] < a1[0] + a1[2] and a1[1] < b1[1] + b1[3] and b1[1] < a1[1] + a1[3]:
                errs.append("当たり判定が重なる: %s と %s" % (p[1], q[1]))
    for b in bt:
        x, y, w, h = hit_rect(b)
        if x < 0 or y < 0 or x + w > W or y + h > H:
            errs.append("当たり判定が画面外: %s" % b[1])
    # スプライトの置き場所(design §22.8)
    for nm, (x, y), (w, h) in (("wifi", WIFI_XY, WIFI_WH), ("wx", WX_XY, WX_WH), ("ref", REF_XY, REF_WH)):
        if x < 0 or y < 0 or x + w > W or y + h > H:
            errs.append("スプライト %s が画面外" % nm)
    if WIFI_XY[1] + WIFI_WH[1] > 34:
        errs.append("Wi-Fi アイコンがヘッダー(高さ 34)からはみ出す")
    hx, hy, hw, hh = next(b for b in bt if b[0] == "H_WX")[3]
    if not (hx <= REF_XY[0] and REF_XY[0] + REF_WH[0] <= hx + hw and hy <= REF_XY[1] and REF_XY[1] + REF_WH[1] <= hy + hh):
        errs.append("更新アイコンが更新ボタンの見た目の外")
    f14 = gl["F14"]
    wd = 4 * f14[0][5] + f14[16][5] + f14[17][5] + f14[14][5] + max(f14[i][5] for i in range(16, 23)) + f14[15][5]
    if 16 + wd + 4 > WIFI_XY[0]:
        errs.append("日付(最大 %d px)が Wi-Fi アイコン(x=%d)に重なる" % (wd, WIFI_XY[0]))
    f22 = gl["F22"]
    tw = sum(f22[i][5] for i in (13, 1, 0, 11, 5))                                 # "-10.5"
    if 106 - tw < WX_XY[0] + WX_WH[0]:
        errs.append("気温(最大 %d px)が天気アイコンに重なる" % tw)
    return errs


# ------------------------------------------------------------------ CLUT8(design §20.3, §20.4)
M565 = np.uint32(0x07E0F81F)
FIXED_MIN_PX = 200                       # 1つの画像の中でこれ以上ある色 = 平らな面の色 = 固定色
KMEANS_MAX_IT = 60
DIST_W = np.array([1.0, 1.0, 0.6])       # 距離の重み(R, G, B)。成分は RGB565 の段(R・B 0..31、G 0..63)
DIM_L, FLASH_L, REMAP_TH = 6, 19, 128    # 押下の暗転 / 白光の l(P1 の 52/256、154/256)と、形の A8 のしきい値
# 合格基準(H-D1。設計 §20.3.2 の採用行に余裕を持たせた値)
LIM_TOTAL_PCT, LIM_BG_PCT, LIM_MAX_STEP = 0.3, 1.0, 2
LIM_SPR_STEP = 3                         # スプライトの最大誤差(design §22.9。§20.3 の目標)


def img565(im):
    p = np.asarray(im.convert("RGB"), dtype=np.uint32)
    return ((((p[..., 0] * 31 + 127) // 255) << 11) | (((p[..., 1] * 63 + 127) // 255) << 5)
            | ((p[..., 2] * 31 + 127) // 255)).astype(np.uint16)


def mix565(bg, fg, a5):
    """gfx.c の mix565 と同じ計算(uint32 の巻き戻りも同じ)。a5 = 0..32(fg の割合 a5/32)。配列可"""
    bg = np.asarray(bg, dtype=np.uint32)
    fg = np.asarray(fg, dtype=np.uint32)
    a5 = np.asarray(a5, dtype=np.uint32)
    with np.errstate(over="ignore"):
        b = (bg | (bg << np.uint32(16))) & M565
        f = (fg | (fg << np.uint32(16))) & M565
        m = ((((f - b) * a5) >> np.uint32(5)) + b) & M565
        return ((m | (m >> np.uint32(16))) & np.uint32(0xFFFF)).astype(np.uint16)


def comps(p):
    p = np.asarray(p, dtype=np.int32)
    return np.stack([p >> 11, (p >> 5) & 63, p & 31], axis=-1).astype(np.float64)


def d2mat(A, B):
    d = (A[:, None, :] - B[None, :, :]) * DIST_W
    return (d * d).sum(axis=2)


def pal_to_argb(p):
    r5, g6, b5 = int(p) >> 11, (int(p) >> 5) & 63, int(p) & 31
    return 0xFF000000 | (((r5 << 3) | (r5 >> 2)) << 16) | (((g6 << 2) | (g6 >> 4)) << 8) | ((b5 << 3) | (b5 >> 2))


def level_hist(arr):
    """A8 の配列 -> レベル l = (a + 4) >> 3 (0..32) ごとの画素数"""
    return np.bincount(((np.asarray(arr, dtype=np.int32) + 4) >> 3).ravel(), minlength=33)


def build_universe(a, bg565, bar565, tag565):
    """色の集合(universe)と重み(設計 §20.3.1 段1)を解析的に作る。戻り値 = (重みの配列[65536], 固定色の集合)"""
    wt = np.zeros(65536)
    for n in bg565:
        wt += np.bincount(bg565[n].ravel(), minlength=65536) / 7.0                              # (a)
    st = slot_table(a)
    for (sid, scr, fid, al, x, y, col, mx) in st:                                               # (b)
        lv = np.zeros(33)
        for (off, gw, gh, bx, by, ad) in a["glyphs"][fid]:
            lv += level_hist(np.frombuffer(a["pool"][off:off + gw * gh], dtype=np.uint8))
        for (under, tcol) in slot_pairs(sid, col):
            for l in range(1, 33):
                wt[int(mix565(c565(under), c565(tcol), l))] += lv[l] * mx / 13.0 / 7.0
    for i, (nm, x, y, bw, bh, r, mw, gr, bc) in enumerate(a["bars"]):                           # (c)
        wt += np.bincount(bar565[i].ravel(), minlength=65536) * 0.5 / 7.0
        cap = level_hist(np.frombuffer(a["barimg"][i][1].tobytes(), dtype=np.uint8))
        for l in range(1, 33):
            wt[int(mix565(c565(gr), c565(bc), l))] += cap[l] * 0.5 / 7.0
    wt += np.bincount(tag565.ravel(), minlength=65536) * 0.1 / 7.0                              # (d)
    for k in ("BAR_U", "BAR_U_TODAY"):                                                          # (e)
        wt[c565(k)] += 2000.0 / 7.0
    nbtn = sum(1 for b in BUTTONS if b[7] is not None)
    for b in BUTTONS:                                                                           # (f)
        if b[7] is None:
            continue
        x, y, w, h = b[3]
        sw, sh, sd = a["shapes"][a["shape_of"][b[0]]]
        inside = np.frombuffer(sd, dtype=np.uint8).reshape(sh, sw) >= REMAP_TH
        for n, lst in BG_BUTTONS.items():
            if b[0] not in lst:
                continue
            c = bg565[n][y:y + h, x:x + w][inside]
            for l, fgc in ((DIM_L, 0x0000), (FLASH_L, 0xFFFF)):
                wt += np.bincount(mix565(c, fgc, l).ravel(), minlength=65536) * 0.3 / nbtn / 7.0
    fixed = set()
    for arr in list(bg565.values()) + list(bar565) + [tag565]:
        cnt = np.bincount(arr.ravel(), minlength=65536)
        fixed |= set(np.nonzero(cnt >= FIXED_MIN_PX)[0].tolist())
    fixed |= {c565("BAR_U"), c565("BAR_U_TODAY")}
    for k, p in a["spr565"].items():                                                            # (g) スプライト(design §22.9)
        cnt = np.bincount(p.ravel(), minlength=65536)
        wt += cnt * (1.0 / 8 if k.startswith("wx_") else 1.0 / 4) * SPRITE_W / 7.0
        if not k.startswith("wx_"):                                                             # Wi-Fi・更新アイコンの平らな色(40 画素以上)
            fixed |= set(np.nonzero(cnt >= 40)[0].tolist())
    for nm in NEW_COLORS:                                                                       # 新しい平らな色は固定色(厳密に残す)
        wt[c565(nm)] += 600.0
        fixed.add(c565(nm))
    return wt, sorted(fixed)


def make_palette(wt, fixed):
    """固定色 + 重み付き k-means で 256 色。乱数なし(設計 §20.3.1 段3)。戻り値 = 256 個の RGB565"""
    uni = np.nonzero(wt)[0]
    U = comps(uni)
    Wt = wt[uni]
    nfix = len(fixed)
    cent = comps(np.array(fixed, dtype=np.int32)) if fixed else np.zeros((0, 3))
    mind = d2mat(U, cent).min(axis=1) if nfix else np.full(len(uni), 1e18)
    while len(cent) < 256:                        # k-means++ の決定版: 重み x 距離が最大の色を次の中心に
        i = int(np.argmax(Wt * mind))
        if Wt[i] * mind[i] <= 0:
            raise SystemExit("パレットを作れない: universe の色が 256 未満")
        cent = np.concatenate([cent, U[i:i + 1]], axis=0)
        mind = np.minimum(mind, d2mat(U, U[i:i + 1])[:, 0])
    hi = np.array([31.0, 63.0, 31.0])
    for _ in range(KMEANS_MAX_IT):                # Lloyd 法。固定色は動かさない
        asg = d2mat(U, cent).argmin(axis=1)
        new = cent.copy()
        for k in range(nfix, 256):
            m = asg == k
            if m.any():
                new[k] = np.clip(np.round((U[m] * Wt[m][:, None]).sum(axis=0) / Wt[m].sum()), 0, hi)
        if (new == cent).all():
            break
        cent = new
    pal = ((cent[:, 0].astype(np.int32) << 11) | (cent[:, 1].astype(np.int32) << 5) | cent[:, 2].astype(np.int32))
    free = sorted(int(v) for v in pal[nfix:])
    return np.array(list(pal[:nfix]) + free, dtype=np.uint16)


def make_lut(pal565):
    """RGB565(65536 通り)-> 最も近いパレットの添字(同点は小さい添字)"""
    C = comps(np.arange(65536))
    P = comps(pal565)
    out = np.zeros(65536, dtype=np.uint8)
    for s in range(0, 65536, 4096):
        out[s:s + 4096] = d2mat(C[s:s + 4096], P).argmin(axis=1)
    return out


def err_stats(ref565, got565):
    """画素ごとの誤差 = 3チャンネルの段の差の最大(RGB565 の1段)。戻り値 = (画素数, 不一致, 1段超, 2段超, 最大)"""
    ref = ref565.astype(np.int32)
    got = got565.astype(np.int32)
    e = np.maximum(np.maximum(abs((ref >> 11) - (got >> 11)), abs(((ref >> 5) & 63) - ((got >> 5) & 63))),
                   abs((ref & 31) - (got & 31)))
    return int(e.size), int((e > 0).sum()), int((e > 1).sum()), int((e > 2).sum()), int(e.max())


def build_c8(a):
    """パレット・8bit 画像・ramp・remap を作る(設計 §20.3 / §20.4)"""
    bg565 = {n: img565(a["bgs"][n]) for n, _ in BGS}
    bar565 = [img565(f) for f, c in a["barimg"]]
    tag565 = img565(a["tag"])
    wt, fixed = build_universe(a, bg565, bar565, tag565)
    pal565 = make_palette(wt, fixed)
    lut = make_lut(pal565)
    idx = lambda c: int(lut[int(c)])
    c8 = dict(bg565=bg565, pal565=pal565, lut=lut, fixed=fixed, universe=int((wt > 0).sum()),
              bg_idx={n: lut[v] for n, v in bg565.items()}, bar_idx=[lut[v] for v in bar565], tag_idx=lut[tag565],
              pal=[pal_to_argb(p) for p in pal565])
    ramp, alt = [], []
    for (sid, scr, fid, al, x, y, col, mx) in slot_table(a):
        un = SLOT_UNDER[sid]
        rs = [[idx(mix565(c565(u), c565(tc), l)) for l in range(33)] for (u, tc) in slot_pairs(sid, col)]
        ramp.append(rs + ([[0] * 33] if len(rs) == 1 else []))
        alt.append(idx(c565(un[1])) if len(un) > 1 else -1)
    c8["slot_ramp"], c8["slot_alt"] = ramp, alt
    c8["bar_ramp"] = [[idx(mix565(c565(gr), c565(bc), l)) for l in range(33)] for (nm, x, y, bw, bh, r, mw, gr, bc) in a["bars"]]
    pv = pal565.astype(np.uint16)
    c8["remap"] = [[int(lut[int(mix565(p, fgc, l))]) for p in pv] for l, fgc in ((DIM_L, 0x0000), (FLASH_L, 0xFFFF))]
    c8["i_bar_u"], c8["i_bar_u_today"] = idx(c565("BAR_U")), idx(c565("BAR_U_TODAY"))
    c8["spr_idx"] = {k: lut[v] for k, v in a["spr565"].items()}
    a["bgs565"] = bg565                                    # Painter(P1 側)が使う
    return c8


def bg_errors(a, c8):
    """背景7枚の P1 との差(設計 §20.3.2)"""
    rows = []
    for n, _ in BGS:
        rows.append((n,) + err_stats(c8["bg565"][n], c8["pal565"][c8["bg_idx"][n]]))
    tot = tuple(sum(r[i] for r in rows) for i in (2, 3, 4))
    return rows, tot + (max(r[5] for r in rows),)


def check_c8(a, c8):
    """設計 §20.3.1 段7 / H-D1 の自己検査。違反の一覧を返す"""
    errs = []
    pal565, lut = c8["pal565"], c8["lut"]
    if len(c8["pal"]) != 256:
        errs.append("パレットが 256 エントリーでない")
    for c in c8["fixed"]:
        if int(pal565[lut[c]]) != c:
            errs.append("固定色 %04X が厳密でない" % c)
    for k in ("BAR_U", "BAR_U_TODAY"):
        if int(pal565[lut[c565(k)]]) != c565(k):
            errs.append("%s が厳密でない" % k)
    for nm in NEW_COLORS:
        if int(pal565[lut[c565(nm)]]) != c565(nm):
            errs.append("%s が厳密でない" % nm)
    for k, v in c8["spr_idx"].items():
        e = err_stats(a["spr565"][k], pal565[v])
        if e[4] > LIM_SPR_STEP:
            errs.append("スプライト %s の最大誤差 %d 段 > %d" % (k, e[4], LIM_SPR_STEP))
    if len(used_entries(a, c8)) > 256:
        errs.append("パレットの使用 %d > 256" % len(used_entries(a, c8)))
    rows, (n, m1, m2, mx) = bg_errors(a, c8)
    total_px = sum(r[1] for r in rows)
    if mx > LIM_MAX_STEP:
        errs.append("背景の最大誤差 %d 段 > %d" % (mx, LIM_MAX_STEP))
    if 100.0 * n / total_px > LIM_TOTAL_PCT:
        errs.append("背景の不一致 %.3f%% > %.1f%%" % (100.0 * n / total_px, LIM_TOTAL_PCT))
    for r in rows:
        if 100.0 * r[2] / r[1] > LIM_BG_PCT:
            errs.append("背景 %s の不一致 %.3f%% > %.1f%%" % (r[0], 100.0 * r[2] / r[1], LIM_BG_PCT))
    return errs


def used_entries(a, c8):
    u = set()
    for v in c8["bg_idx"].values():
        u |= set(np.unique(v).tolist())
    for v in c8["bar_idx"] + [c8["tag_idx"]] + list(c8["spr_idx"].values()):
        u |= set(np.unique(v).tolist())
    for rs in c8["slot_ramp"]:
        for r in rs:
            u |= set(r[1:])
    for r in c8["bar_ramp"]:
        u |= set(r[1:])
    return u


def sizes(a, c8):
    bg = 130560 * len(a["bgs"])
    pool = len(a["pool"])
    shp = sum(w * h for (w, h, _) in a["shapes"])
    bars = sum(f.width * f.height + c.width * c.height for f, c in a["barimg"])
    tag = 84 * 24
    tbl = 1024 + len(c8["slot_ramp"]) * 66 + len(c8["slot_alt"]) * 2 + len(c8["bar_ramp"]) * 33 + 512
    spr = sum(v.size for v in c8["spr_idx"].values())
    return bg, pool, shp, bars, tag, tbl, spr


# ------------------------------------------------------------------ C 出力
def fmt_arr(vals, per, fmt):
    lines = []
    for i in range(0, len(vals), per):
        lines.append("    " + ",".join(fmt % v for v in vals[i:i + per]) + ",")
    return "\n".join(lines)


def write_c(a, c8, out_dir, fonts):
    os.makedirs(out_dir, exist_ok=True)
    hdr = "/* Generated by tools/gen_ui_assets.py. 手で編集しない。font=%s sha256=%s */\n" % (os.path.basename(fonts.path), fonts.sha)

    def w(name, text):
        with open(os.path.join(out_dir, name), "w", newline="\n", encoding="utf-8") as f:
            f.write(text)

    # --- 背景(8bit。行を詰めた 480 バイト幅) ---
    for name, scr in BGS:
        w("ui_bg_%s.c" % name, hdr + "#include \"ui_assets.h\"\n\nconst uint8_t ui_bgi_%s[UI_BG_PIXELS] __attribute__((aligned(4))) = {\n%s\n};\n"
          % (name, fmt_arr(c8["bg_idx"][name].ravel().tolist(), 32, "%d")))

    # --- 数字・形・バー・タグ ---
    t = hdr + "#include \"ui_assets.h\"\n\n"
    t += "const uint8_t ui_glyph_pool[%d] = {\n%s\n};\n\n" % (len(a["pool"]), fmt_arr(list(a["pool"]), 24, "0x%02X"))
    t += "const ui_glyph_t ui_glyphs[UI_FONT_COUNT][UI_GLYPHS] = {\n"
    for fid, px, wg in FONTS:
        t += "    { /* %s */\n" % fid
        for (off, gw, gh, bx, by, ad) in a["glyphs"][fid]:
            t += "        { %du, %d, %d, %d, %d, %d },\n" % (off, gw, gh, bx, by, ad)
        t += "    },\n"
    t += "};\n\n"
    for i, (sw, sh, data) in enumerate(a["shapes"]):
        t += "static const uint8_t shape_%d[%d] = {\n%s\n};\n" % (i, len(data), fmt_arr(list(data), 24, "0x%02X"))
    t += "const ui_shape_t ui_shapes[UI_SHAPE_COUNT] = {\n"
    for i, (sw, sh, data) in enumerate(a["shapes"]):
        t += "    { shape_%d, %d, %d },\n" % (i, sw, sh)
    t += "};\n\n"
    for i, (full, cap) in enumerate(a["barimg"]):
        t += "static const uint8_t bar_full_%d[%d] __attribute__((aligned(4))) = {\n%s\n};\n" % (i, full.width * full.height, fmt_arr(c8["bar_idx"][i].ravel().tolist(), 32, "%d"))
        t += "static const uint8_t bar_cap_%d[%d] = {\n%s\n};\n" % (i, cap.width * cap.height, fmt_arr(list(cap.tobytes()), 24, "0x%02X"))
    t += "const ui_bar_t ui_bars[UI_BAR_COUNT] = {\n"
    for i, (nm, x, y, bw, bh, r, mw, gr, bc) in enumerate(a["bars"]):
        t += "    { bar_full_%d, bar_cap_%d, %d, %d, %d, %d, %d, %d },\n" % (i, i, x, y, bw, bh, r, mw)
    t += "};\n\n"
    t += "const uint8_t ui_tag_pix[UI_TAG_W * UI_TAG_H] __attribute__((aligned(4))) = {\n%s\n};\n\n" % fmt_arr(c8["tag_idx"].ravel().tolist(), 32, "%d")
    for sym, pre, n, wh in (("wx", "wx_", len(KINDS), WX_WH), ("wifi", "wifi_", len(WIFI_STATES), WIFI_WH), ("ref", "ref_", 4, REF_WH)):
        t += "/* %s */\nconst uint8_t ui_spr_%s[UI_SPR_%s_N][UI_%s_W * UI_%s_H] __attribute__((aligned(4))) = {\n" % (
            ", ".join(k for k in c8["spr_idx"] if k.startswith(pre)), sym, sym.upper(), sym.upper(), sym.upper())
        for k in c8["spr_idx"]:
            if k.startswith(pre):
                t += "    {\n%s\n    },\n" % fmt_arr(c8["spr_idx"][k].ravel().tolist(), wh[0], "%d")
        t += "};\n"
    w("ui_gfx_data.c", t)

    # --- 配置表・パレット・表 ---
    t = hdr + "#include \"ui_assets.h\"\n\n"
    t += "const ui_btn_def_t ui_btn_defs[UI_BTN_COUNT] = {\n    { {0,0,0,0}, {0,0,0,0}, 0, UI_COND_ALWAYS, -1, \"none\" },\n"
    for b in BUTTONS:
        v, h = b[3], hit_rect(b)
        t += "    { {%d,%d,%d,%d}, {%d,%d,%d,%d}, UI_SCR_%s, UI_COND_%s, %d, \"%s\" },\n" % (*v, *h, b[2], b[4], a["shape_of"][b[0]], b[1])
    t += "};\n\n"
    t += "const ui_slot_t ui_slots[UI_SL_COUNT] = {\n"
    for (sid, scr, fid, al, x, y, col, mx) in slot_table(a):
        t += "    { %d, %d, UI_FONT_%s, UI_ALIGN_%s, %d }, /* %s */\n" % (x, y, fid[1:], al, mx, sid)
    t += "};\n\n"
    t += "/* パレット(0xFFRRGGBB。RGB565 の各色を 8bit に広げた値)。R_GLCDC_Open が1回だけ書く */\n"
    t += "const uint32_t ui_pal[UI_PAL_N] __attribute__((aligned(4))) = {\n%s\n};\n\n" % fmt_arr(c8["pal"], 8, "0x%08Xu")
    t += "const uint8_t * const ui_bgi[UI_BG_COUNT] = { %s };\n" % ", ".join("ui_bgi_" + n for n, _ in BGS)
    t += "const char * const ui_scr_name[UI_SCR_COUNT] = { %s };\n" % ", ".join('"%s"' % s.lower() for s in SCREENS)
    t += "const uint8_t ui_usage_h[UI_USAGE_N] = { %s };\n\n" % ", ".join(str(x) for x in USAGE_H)
    t += "/* 数字の段階の表: [スロット][下地 0 / 1][レベル l = (a + 4) >> 3]。下地 1 は ui_slot_alt >= 0 のスロットだけ */\n"
    t += "const uint8_t ui_slot_ramp[UI_SL_COUNT][2][33] = {\n"
    for (sid, *_), rs in zip(slot_table(a), c8["slot_ramp"]):
        t += "    { /* %s */\n" % sid
        for r in rs:
            t += "        { %s },\n" % ",".join(str(v) for v in r)
        t += "    },\n"
    t += "};\n"
    t += "const int16_t ui_slot_alt[UI_SL_COUNT] = { %s };\n" % ", ".join(str(v) for v in c8["slot_alt"])
    t += "/* バー右端の角丸の段階の表(進み色 -> 溝色) */\n"
    t += "const uint8_t ui_bar_ramp[UI_BAR_COUNT][33] = {\n%s\n};\n" % "\n".join("    { %s }," % ",".join(str(v) for v in r) for r in c8["bar_ramp"])
    t += "/* 押下の暗転(0)/ 白光(1)の添字の置き換え表 */\n"
    t += "const uint8_t ui_remap[2][UI_PAL_N] = {\n%s\n};\n" % "\n".join("    {\n%s\n    }," % fmt_arr(r, 32, "%d") for r in c8["remap"])
    w("ui_layout.c", t)

    # --- ヘッダー ---
    t = hdr + "#ifndef UI_ASSETS_H\n#define UI_ASSETS_H\n\n#include <stdint.h>\n\n"
    t += "#define UI_W            (480)\n#define UI_H            (272)\n#define UI_BG_PIXELS    (130560u)   /* 480*272(8bit、行を詰めた 480 バイト幅) */\n#define UI_PAL_N        (256u)\n\n"
    t += "enum { %s, UI_SCR_COUNT };\n" % ", ".join("UI_SCR_%s%s" % (s, " = 0" if i == 0 else "") for i, s in enumerate(SCREENS))
    t += "enum { UI_BTN_NONE = 0, %s, UI_BTN_COUNT };\n" % ", ".join("UI_BTN_%s" % b[0] for b in BUTTONS)
    t += "enum { UI_COND_ALWAYS = 0, UI_COND_FILL_RUN, UI_COND_FILL_DONE, UI_COND_REHEAT_RUN, UI_COND_REHEAT_DONE };\n"
    t += "enum { %s, UI_BG_COUNT };\n" % ", ".join("UI_BG_%s%s" % (n.upper(), " = 0" if i == 0 else "") for i, (n, _) in enumerate(BGS))
    t += "enum { %s, UI_FONT_COUNT };\n" % ", ".join("UI_FONT_%s%s" % (f[0][1:], " = 0" if i == 0 else "") for i, f in enumerate(FONTS))
    t += "enum { %s, UI_SL_COUNT };\n" % ", ".join("UI_SL_%s%s" % (s[0], " = 0" if i == 0 else "") for i, s in enumerate(slot_table(a)))
    t += "enum { UI_ALIGN_LEFT = 0, UI_ALIGN_CENTER, UI_ALIGN_RIGHT };\n"
    t += "enum { UI_BAR_FILL = 0, UI_BAR_REHEAT, UI_BAR_COUNT };\n"
    t += "#define UI_SHAPE_COUNT  (%du)\n#define UI_GLYPHS       (%du)   /* 0-9 : . %% - ( ) 月 日 火 水 木 金 土(後ろの 9 字は F14 だけ。ほかは幅 0) */\n" % (len(a["shapes"]), NGLYPH)
    t += "#define UI_G_MINUS      (13u)\n#define UI_G_LPAR       (14u)\n#define UI_G_RPAR       (15u)\n#define UI_G_MON        (16u)   /* 月日火水木金土 = 16..22 */\n\n"
    t += "/* パレットの添字(使用量の棒の2色。固定色なので厳密) */\n#define UI_I_BAR_U        (%du)\n#define UI_I_BAR_U_TODAY  (%du)\n" % (c8["i_bar_u"], c8["i_bar_u_today"])
    t += "\n/* 使用量の棒グラフ(design §1.5, §2.5) */\n#define UI_USAGE_N      (7u)\n#define UI_USAGE_X0     (24)\n#define UI_USAGE_PITCH  (64)\n#define UI_USAGE_W      (40)\n#define UI_USAGE_BASE   (206)\n"
    t += "extern const uint8_t ui_usage_h[UI_USAGE_N];\n\n"
    t += "/* アイコン(8bit、下地に焼き込み済み。(x,y) に不透明で描く。design §22.8)。wx = 天気 8 種(sun partly cloud fog rain snow thunder unknown)、wifi = off conn on err、ref = 更新の回転 4 段階 */\n"
    t += "#define UI_WIFI_X (%d)\n#define UI_WIFI_Y (%d)\n#define UI_WIFI_W (%d)\n#define UI_WIFI_H (%d)\n" % (*WIFI_XY, *WIFI_WH)
    t += "#define UI_WX_X   (%d)\n#define UI_WX_Y   (%d)\n#define UI_WX_W   (%d)\n#define UI_WX_H   (%d)\n" % (*WX_XY, *WX_WH)
    t += "#define UI_REF_X  (%d)\n#define UI_REF_Y  (%d)\n#define UI_REF_W  (%d)\n#define UI_REF_H  (%d)\n" % (*REF_XY, *REF_WH)
    t += "#define UI_SPR_WX_N (%du)\n#define UI_SPR_WIFI_N (%du)\n#define UI_SPR_REF_N (4u)\n" % (len(KINDS), len(WIFI_STATES))
    t += "extern const uint8_t ui_spr_wx[UI_SPR_WX_N][UI_WX_W * UI_WX_H];\nextern const uint8_t ui_spr_wifi[UI_SPR_WIFI_N][UI_WIFI_W * UI_WIFI_H];\n"
    t += "extern const uint8_t ui_spr_ref[UI_SPR_REF_N][UI_REF_W * UI_REF_H];\n\n"
    t += "/* デモ中タグ(84x24、8bit、(228,5) に不透明で描く) */\n#define UI_TAG_X (228)\n#define UI_TAG_Y (5)\n#define UI_TAG_W (84)\n#define UI_TAG_H (24)\nextern const uint8_t ui_tag_pix[UI_TAG_W * UI_TAG_H];\n\n"
    t += "typedef struct { int16_t x, y, w, h; } ui_rect_t;\n"
    t += "typedef struct { ui_rect_t vis; ui_rect_t hit; uint8_t scr; uint8_t cond; int8_t shape; const char *name; } ui_btn_def_t;\n"
    t += "typedef struct { int16_t x, y; uint8_t font; uint8_t align; uint8_t maxch; } ui_slot_t;\n"
    t += "typedef struct { uint32_t off; uint8_t w, h; int8_t bx; uint8_t by; uint8_t adv; } ui_glyph_t;\n"
    t += "typedef struct { const uint8_t *a8; uint16_t w, h; } ui_shape_t;\n"
    t += "typedef struct { const uint8_t *full; const uint8_t *cap; int16_t x, y; uint16_t w, h, r, min_w; } ui_bar_t;\n\n"
    t += "extern const ui_btn_def_t ui_btn_defs[UI_BTN_COUNT];\nextern const ui_slot_t ui_slots[UI_SL_COUNT];\n"
    t += "extern const ui_glyph_t ui_glyphs[UI_FONT_COUNT][UI_GLYPHS];\nextern const uint8_t ui_glyph_pool[%d];\n" % len(a["pool"])
    t += "extern const ui_shape_t ui_shapes[UI_SHAPE_COUNT];\nextern const ui_bar_t ui_bars[UI_BAR_COUNT];\n"
    t += "extern const uint32_t ui_pal[UI_PAL_N];\n"
    t += "extern const uint8_t ui_slot_ramp[UI_SL_COUNT][2][33];\nextern const int16_t ui_slot_alt[UI_SL_COUNT];\n"
    t += "extern const uint8_t ui_bar_ramp[UI_BAR_COUNT][33];\nextern const uint8_t ui_remap[2][UI_PAL_N];\n"
    t += "extern const uint8_t * const ui_bgi[UI_BG_COUNT];\nextern const char * const ui_scr_name[UI_SCR_COUNT];\n"
    for n, _ in BGS:
        t += "extern const uint8_t ui_bgi_%s[UI_BG_PIXELS];\n" % n
    t += "\n#endif /* UI_ASSETS_H */\n"
    w("ui_assets.h", t)


# ------------------------------------------------------------------ プレビュー(PNG)と誤差表
def panel_rgb(p565):
    """RGB565 -> パネルに出る色(下位ビットに上位ビットを複製した RGB888)。(H, W, 3) uint8"""
    p = np.asarray(p565, dtype=np.uint32)
    return np.stack([((p >> 11) << 3) | ((p >> 13) & 7), (((p >> 5) & 63) << 2) | ((p >> 9) & 3), ((p & 31) << 3) | ((p >> 2) & 7)],
                    axis=-1).astype(np.uint8)


def diff_rgb(ref565, got565):
    """差分の画像: 一致 = 白、1段 = 黄、2段 = 赤、3段以上 = 黒"""
    ref, got = ref565.astype(np.int32), got565.astype(np.int32)
    e = np.maximum(np.maximum(abs((ref >> 11) - (got >> 11)), abs(((ref >> 5) & 63) - ((got >> 5) & 63))), abs((ref & 31) - (got & 31)))
    out = np.empty(e.shape + (3,), dtype=np.uint8)
    out[...] = (255, 255, 255)
    out[e == 1] = (255, 220, 0)
    out[e == 2] = (230, 0, 0)
    out[e >= 3] = (0, 0, 0)
    return out


def triplet(ref565, got565, scale=1, gap=8):
    ims = [panel_rgb(ref565), panel_rgb(got565), diff_rgb(ref565, got565)]
    h, w = ims[0].shape[:2]
    sheet = np.full((h, 3 * w + 2 * gap, 3), 160, dtype=np.uint8)
    for i, im in enumerate(ims):
        sheet[:, i * (w + gap):i * (w + gap) + w] = im
    img = Image.fromarray(sheet)
    return img.resize((img.width * scale, img.height * scale), Image.NEAREST) if scale != 1 else img


def _clip(x, y, w, h):
    x0, y0, x1, y1 = max(x, 0), max(y, 0), min(x + w, W), min(y + h, H)
    return (x0, y0, x1, y1) if (x1 > x0 and y1 > y0) else None


def glyph_index(ch):
    """文字 -> 数字フォントの番号。ui_render.c の glyph_index と同じ(\x01..\x07 = 月 日 火 水 木 金 土)"""
    if ch in CHARS:
        return CHARS.index(ch)
    if ch == "(":
        return 14
    if ch == ")":
        return 15
    if "\x01" <= ch <= "\x07":
        return 15 + ord(ch)
    return None


class Painter:
    """ui_render.c と同じ描き方(P1 = RGB565、P2b = 8bit index)の Python 版。プレビューと誤差表だけに使う"""

    def __init__(self, a, c8, mode):
        self.a, self.c8, self.mode = a, c8, mode
        self.fb = np.zeros((H, W), dtype=np.uint16 if mode == "p1" else np.uint8)
        self.st = slot_table(a)
        self.sid = {s[0]: i for i, s in enumerate(self.st)}
        self.pool = np.frombuffer(a["pool"], dtype=np.uint8)
        self.bar_full = [img565(f) if mode == "p1" else c8["bar_idx"][i] for i, (f, c) in enumerate(a["barimg"])]
        self.bar_cap = [np.frombuffer(c.tobytes(), dtype=np.uint8).reshape(c.height, c.width) for f, c in a["barimg"]]
        self.shapes = [np.frombuffer(d, dtype=np.uint8).reshape(h, w) for (w, h, d) in a["shapes"]]
        self.tag = img565(a["tag"]) if mode == "p1" else c8["tag_idx"]
        self.spr = a["spr565"] if mode == "p1" else c8["spr_idx"]

    def blit(self, src, x, y):
        h, w = src.shape
        r = _clip(x, y, w, h)
        if r:
            x0, y0, x1, y1 = r
            self.fb[y0:y1, x0:x1] = src[y0 - y:y1 - y, x0 - x:x1 - x]

    def fill(self, x, y, w, h, c):
        r = _clip(x, y, w, h)
        if r:
            self.fb[r[1]:r[3], r[0]:r[2]] = c

    def a8(self, a8, x, y, color=None, scale=256, ramp0=None, ramp1=None, alt=-1):
        h, w = a8.shape
        r = _clip(x, y, w, h)
        if not r:
            return
        x0, y0, x1, y1 = r
        s = a8[y0 - y:y1 - y, x0 - x:x1 - x].astype(np.uint32)
        sub = self.fb[y0:y1, x0:x1]
        if self.mode == "p1":
            l = ((((s * scale) >> 8) + 4) >> 3)
            self.fb[y0:y1, x0:x1] = np.where(s != 0, mix565(sub, color, l), sub)
        else:
            l = (s + 4) >> 3
            r0 = np.asarray(ramp0, dtype=np.uint8)[l]
            r1 = np.asarray(ramp1, dtype=np.uint8)[l] if alt >= 0 else r0
            self.fb[y0:y1, x0:x1] = np.where(l != 0, np.where((alt >= 0) & (sub == alt), r1, r0), sub)

    def remap(self, a8, x, y, k):
        h, w = a8.shape
        r = _clip(x, y, w, h)
        if not r:
            return
        x0, y0, x1, y1 = r
        s = a8[y0 - y:y1 - y, x0 - x:x1 - x]
        sub = self.fb[y0:y1, x0:x1]
        self.fb[y0:y1, x0:x1] = np.where(s >= REMAP_TH, np.asarray(self.c8["remap"][k], dtype=np.uint8)[sub], sub)

    def text(self, sid, s, xo, sel=0):
        i = self.sid[sid]
        (_, scr, fid, al, x, y, col, mx) = self.st[i]
        gl = self.a["glyphs"][fid]
        if sel and sid in SLOT_COL2:
            col = SLOT_COL2[sid]
        ks = [glyph_index(ch) for ch in s if glyph_index(ch) is not None]
        x += xo
        if al != "LEFT":
            wd = sum(gl[k][5] for k in ks)
            x -= wd // 2 if al == "CENTER" else wd
        for k in ks:
            off, gw, gh, bx, by, ad = gl[k]
            g = self.pool[off:off + gw * gh].reshape(gh, gw)
            if self.mode == "p1":
                self.a8(g, x + bx, y - by, color=c565(col))
            else:
                alt = self.c8["slot_alt"][i]
                self.a8(g, x + bx, y - by, ramp0=self.c8["slot_ramp"][i][0 if alt >= 0 else (1 if sel else 0)], ramp1=self.c8["slot_ramp"][i][1], alt=alt)
            x += ad

    def bar(self, i, w, xo):
        (nm, x, y, bw, bh, r, mw, gr, bc) = self.a["bars"][i]
        w = max(mw, min(w, bw))
        self.blit(self.bar_full[i][:, :w - r], x + xo, y)
        if self.mode == "p1":
            self.a8(self.bar_cap[i], x + xo + w - r, y, color=c565(bc))
        else:
            self.a8(self.bar_cap[i], x + xo + w - r, y, ramp0=self.c8["bar_ramp"][i])

    def usage(self, hs, xo):
        for i, h in enumerate(hs):
            if h <= 0:
                continue
            x, y = 24 + 64 * i + xo, 206 - h
            c = "BAR_U_TODAY" if i == 6 else "BAR_U"
            c = c565(c) if self.mode == "p1" else self.c8["i_bar_u_today" if i == 6 else "i_bar_u"]
            if h >= 3:
                self.fill(x + 2, y, 36, 1, c)
                self.fill(x + 1, y + 1, 38, 1, c)
                self.fill(x, y + 2, 40, h - 2, c)
            else:
                self.fill(x, y, 40, h, c)

    def feedback(self, bid, kind, xo):
        b = next(b for b in BUTTONS if b[0] == bid)
        sh = self.shapes[self.a["shape_of"][bid]]
        x, y = b[3][0] + xo, b[3][1]
        if self.mode == "p1":
            self.a8(sh, x, y, color=0x0000 if kind == "press" else 0xFFFF, scale=52 if kind == "press" else 154)
        else:
            self.remap(sh, x, y, 0 if kind == "press" else 1)

    def draw(self, sc, xo=0):
        self.blit(self.a["bgs565"][sc["bg"]] if self.mode == "p1" else self.c8["bg_idx"][sc["bg"]], xo, 0)
        for op in sc.get("ops", []):
            if op[0] == "text":
                self.text(op[1], op[2], xo, op[3] if len(op) > 3 else 0)
            elif op[0] == "spr":
                self.blit(self.spr[op[1]], {"wx": WX_XY, "wifi": WIFI_XY, "ref": REF_XY}[op[1].split("_")[0]][0] + xo,
                          {"wx": WX_XY, "wifi": WIFI_XY, "ref": REF_XY}[op[1].split("_")[0]][1])
            elif op[0] == "bar":
                self.bar(op[1], op[2], xo)
            elif op[0] == "usage":
                self.usage(op[1], xo)
        if sc.get("fb"):
            self.feedback(sc["fb"][0], sc["fb"][1], xo)
        if sc.get("demo"):
            self.blit(self.tag, 228 + xo, 5)


DATE_S = "11\x0125\x02(\x04)"        # 11月25日(水)
WIFI_ON = ("spr", "wifi_on")
SCENES = {
    # 同期済み・晴れ・ONLINE
    "home": dict(bg="home", ops=[("text", "HOME_CLOCK", "13:24"), ("text", "HOME_SET", "42"), ("text", "HOME_BATH", "40"),
                                 ("text", "HOME_DATE", DATE_S), ("text", "HOME_TEMP", "12.6"), ("text", "HOME_UPD", "13:24"),
                                 ("spr", "wx_sun"), WIFI_ON, ("spr", "ref_0")]),
    # 未取得・OFFLINE(--:-- --.-)
    "home_none": dict(bg="home", ops=[("text", "HOME_CLOCK", "--:--"), ("text", "HOME_SET", "42"), ("text", "HOME_BATH", "40"),
                                      ("text", "HOME_TEMP", "--.-"), ("text", "HOME_UPD", "--:--"),
                                      ("spr", "wx_unknown"), ("spr", "wifi_off"), ("spr", "ref_0")]),
    # エラー・30 分超の古い値(気温が灰色)・雨
    "home_err_stale": dict(bg="home", ops=[("text", "HOME_CLOCK", "13:24"), ("text", "HOME_SET", "42"), ("text", "HOME_BATH", "40"),
                                           ("text", "HOME_DATE", DATE_S), ("text", "HOME_TEMP", "12.6", 1), ("text", "HOME_UPD", "12:40"),
                                           ("spr", "wx_rain"), ("spr", "wifi_err"), ("spr", "ref_0")]),
    # 接続中・雪・負の気温・更新アイコンの回転(90 度)
    "home_conn_snow": dict(bg="home", ops=[("text", "HOME_CLOCK", "07:05"), ("text", "HOME_SET", "42"), ("text", "HOME_BATH", "40"),
                                           ("text", "HOME_DATE", "1\x015\x02(\x01)"), ("text", "HOME_TEMP", "-1.2"), ("text", "HOME_UPD", "07:05"),
                                           ("spr", "wx_snow"), ("spr", "wifi_conn"), ("spr", "ref_1")]),
    "home_demo": dict(bg="home", demo=1, ops=[("text", "HOME_CLOCK", "08:07"), ("text", "HOME_SET", "38"), ("text", "HOME_BATH", "38"),
                                              ("text", "HOME_DATE", "11\x0121\x02(\x07)"), ("text", "HOME_TEMP", "-10.5"), ("text", "HOME_UPD", "08:07"),
                                              ("spr", "wx_thunder"), WIFI_ON, ("spr", "ref_3")]),
    "fill_run": dict(bg="fill_run", ops=[("text", "FILL_BATH", "40"), ("bar", 0, 240), ("text", "FILL_PCT", "55%"), ("text", "FILL_REMAIN", "9"), WIFI_ON]),
    "fill_w20": dict(bg="fill_run", ops=[("text", "FILL_BATH", "40"), ("bar", 0, 20), ("text", "FILL_PCT", "3%"), ("text", "FILL_REMAIN", "8")]),
    "fill_w400": dict(bg="fill_run", ops=[("text", "FILL_BATH", "40"), ("bar", 0, 400), ("text", "FILL_PCT", "92%"), ("text", "FILL_REMAIN", "1")]),
    "fill_done": dict(bg="fill_done", ops=[("text", "FILL_BATH", "40"), ("bar", 0, 432), ("text", "FILL_PCT", "100%"), ("spr", "wifi_err")]),
    "reheat_run": dict(bg="reheat_run", ops=[("text", "REHEAT_SETHDR", "42"), ("text", "REHEAT_BATH", "39.1"), ("text", "REHEAT_SET", "42"),
                                            ("bar", 1, 300), ("text", "REHEAT_REMAIN", "5"), ("spr", "wifi_conn")]),
    "reheat_w20": dict(bg="reheat_run", ops=[("text", "REHEAT_SETHDR", "48"), ("text", "REHEAT_BATH", "35.0"), ("text", "REHEAT_SET", "48"),
                                            ("bar", 1, 20), ("text", "REHEAT_REMAIN", "18")]),
    "reheat_done": dict(bg="reheat_done", ops=[("text", "REHEAT_SETHDR", "42"), ("text", "REHEAT_BATH", "42.0"), ("text", "REHEAT_SET", "42"), ("bar", 1, 432)]),
    "usage": dict(bg="usage", ops=[("text", "USAGE_FLOW", "10.3"), ("usage", USAGE_H), ("spr", "wifi_off")]),
    "usage_growing": dict(bg="usage", ops=[("text", "USAGE_FLOW", "6.7"), ("usage", [h // 2 for h in USAGE_H[:4]] + [1, 2, 0])]),
    "standby": dict(bg="stby", ops=[("text", "STBY_CLOCK", "21:45")]),
}
# 押下・白光(14 ボタン。S_WAKE を除く)を載せる画面
FB_SCENE = {"H_WX": "home_conn_snow", "H_DN": "home", "H_UP": "home", "H_FILL": "home", "H_REHEAT": "home", "H_USAGE": "home", "H_POWER": "home", "H_DEMO": "home_demo",
            "F_STOP": "fill_run", "F_DONE": "fill_done", "F_HOME": "fill_run", "R_STOP": "reheat_run", "R_DONE": "reheat_done",
            "R_HOME": "reheat_run", "U_BACK": "usage"}


def render_scene(a, c8, mode, sc, slide=None):
    p = Painter(a, c8, mode)
    if slide:                                    # (古い画面, 新しい画面, dx, 前へ進むか)
        old, new, dx, fwd = slide
        p.draw(old, -dx if fwd else dx)
        p.draw(new, 480 - dx if fwd else dx - 480)
    else:
        p.draw(sc)
    return p.fb


def scene_pair(a, c8, sc, slide=None):
    """(P1 の RGB565, P2b を RGB565 に戻した値)"""
    p1 = render_scene(a, c8, "p1", sc, slide)
    p2 = c8["pal565"][render_scene(a, c8, "p2", sc, slide)]
    return p1, p2


def all_scene_stats(a, c8):
    """静的・押下・白光・スライドの誤差表(設計 §20.3.3 と同じ形式)。行 = (名前, 画素数, 不一致, 1段超, 2段超, 最大)"""
    rows = []
    for n, sc in SCENES.items():
        rows.append(("静的 " + n,) + err_stats(*scene_pair(a, c8, sc)))
    for bid, scn in FB_SCENE.items():
        for kind in ("press", "flash"):
            sc = dict(SCENES[scn], fb=(bid, kind))
            rows.append(("%s %s" % ("押下" if kind == "press" else "白光", bid),) + err_stats(*scene_pair(a, c8, sc)))
    for (o, n, dx, fwd) in (("home", "fill_run", 240, 1), ("fill_run", "home", 240, 0), ("reheat_run", "usage", 120, 1), ("usage", "standby", 479, 1)):
        sc = None
        rows.append(("スライド %s>%s dx%d" % (o, n, dx),) + err_stats(*scene_pair(a, c8, sc, (SCENES[o], SCENES[n], dx, fwd))))
    return rows


def group_stats(rows):
    g = {}
    for r in rows:
        k = r[0].split(" ")[0]
        s = g.setdefault(k, [0, 0, 0, 0, 0])
        for i in range(4):
            s[i] += r[1 + i]
        s[4] = max(s[4], r[5])
    return g


def write_c8_preview(a, c8, cdir):
    os.makedirs(cdir, exist_ok=True)
    pal565 = c8["pal565"]
    # 1. 背景7枚 P1 | P2b | 差分
    for n, _ in BGS:
        triplet(c8["bg565"][n], pal565[c8["bg_idx"][n]]).save(os.path.join(cdir, "bg_%s.png" % n))
    # 2. 動く画面(数字・バー・棒・タグ)と押下・白光
    for n, sc in SCENES.items():
        triplet(*scene_pair(a, c8, sc)).save(os.path.join(cdir, "scr_%s.png" % n))
    sl = (SCENES["home"], SCENES["fill_run"], 240, 1)
    triplet(*scene_pair(a, c8, None, sl)).save(os.path.join(cdir, "scr_slide_home_fill_dx240.png"))
    for kind in ("press", "flash"):
        rows = []
        for bid, scn in FB_SCENE.items():
            b = next(b for b in BUTTONS if b[0] == bid)
            x, y, w, h = b[3]
            r1, r2 = scene_pair(a, c8, dict(SCENES[scn], fb=(bid, kind)))
            box = (max(0, x - 6), max(0, y - 6), min(W, x + w + 6), min(H, y + h + 6))
            rows.append(triplet(r1[box[1]:box[3], box[0]:box[2]], r2[box[1]:box[3], box[0]:box[2]], 2))
        sheet = Image.new("RGB", (max(r.width for r in rows), sum(r.height + 6 for r in rows)), (255, 255, 255))
        yy = 0
        for r in rows:
            sheet.paste(r, (0, yy))
            yy += r.height + 6
        sheet.save(os.path.join(cdir, "buttons_%s.png" % kind))
    # 3. 数字スロットの拡大(P1 | P2b | 差分)
    slot_scene = {"HOME_CLOCK": "home", "HOME_SET": "home", "HOME_BATH": "home", "FILL_BATH": "fill_run", "FILL_PCT": "fill_run",
                  "FILL_REMAIN": "fill_run", "REHEAT_SETHDR": "reheat_run", "REHEAT_BATH": "reheat_run", "REHEAT_SET": "reheat_run",
                  "REHEAT_REMAIN": "reheat_run", "USAGE_FLOW": "usage", "STBY_CLOCK": "standby", "HOME_DATE": "home", "HOME_TEMP": "home_conn_snow",
                  "HOME_UPD": "home"}
    rows = []
    for (sid, scr, fid, al, x, y, col, mx) in slot_table(a):
        gl = a["glyphs"][fid]
        wmax = mx * a["adv"][fid]
        x0 = x if al == "LEFT" else (x - wmax // 2 if al == "CENTER" else x - wmax)
        top, bot = y - max(t[4] for t in gl), y + max(t[2] - t[4] for t in gl)
        r1, r2 = scene_pair(a, c8, SCENES[slot_scene[sid]])
        box = (max(0, x0 - 3), max(0, top - 3), min(W, x0 + wmax + 3), min(H, bot + 3))
        rows.append(triplet(r1[box[1]:box[3], box[0]:box[2]], r2[box[1]:box[3], box[0]:box[2]], 2 if fid in ("F14", "F19", "F22") else 1))
    sheet = Image.new("RGB", (max(r.width for r in rows), sum(r.height + 6 for r in rows)), (255, 255, 255))
    yy = 0
    for r in rows:
        sheet.paste(r, (0, yy))
        yy += r.height + 6
    sheet.save(os.path.join(cdir, "digits.png"))
    # 4. パレット見本(16 x 16 のチップ。固定色は左上に白い点)
    chip = 28
    im = np.full((16 * chip, 16 * chip, 3), 255, dtype=np.uint8)
    fixed = set(c8["fixed"])
    for i in range(256):
        r, c = divmod(i, 16)
        im[r * chip + 1:(r + 1) * chip - 1, c * chip + 1:(c + 1) * chip - 1] = panel_rgb(np.array([[pal565[i]]]))[0, 0]
        if int(pal565[i]) in fixed:
            im[r * chip + 3:r * chip + 7, c * chip + 3:c * chip + 7] = (255, 0, 255)
    Image.fromarray(im).save(os.path.join(cdir, "palette.png"))
    # 4b. 天気・Wi-Fi(design §22.8)。ホーム 4 状態(P2b のパレットに写した見え方)、ホームの 2 倍(元の色 | パレット)、アイコンの 4 倍
    names = ["home", "home_none", "home_err_stale", "home_conn_snow"]
    sheet = Image.new("RGB", (W * 2 + 12, H * 2 + 12), (60, 60, 60))
    for i, n in enumerate(names):
        sheet.paste(Image.fromarray(panel_rgb(scene_pair(a, c8, SCENES[n])[1])), ((i % 2) * (W + 12), (i // 2) * (H + 12)))
    sheet.save(os.path.join(cdir, "states.png"))
    r1, r2 = scene_pair(a, c8, SCENES["home"])
    for nm, im in (("mock_home_true.png", r1), ("mock_home_c8.png", r2)):
        Image.fromarray(panel_rgb(im)).resize((W * 2, H * 2), Image.NEAREST).save(os.path.join(cdir, nm))
    tiles = [(k, v) for k, v in c8["spr_idx"].items()]
    sh = Image.new("RGB", (len(tiles) * 4 * 30 + 8, 4 * 30 + 8), (200, 200, 200))
    for i, (k, v) in enumerate(tiles):
        sh.paste(Image.fromarray(panel_rgb(pal565[v])).resize((v.shape[1] * 4, v.shape[0] * 4), Image.NEAREST), (8 + i * 120, 8))
    sh.save(os.path.join(cdir, "icons_x4.png"))
    # 5. 誤差表
    rows, (n, m1, m2, mx) = bg_errors(a, c8)
    srows = all_scene_stats(a, c8)
    lines = ["# 共通パレット(CLUT8、256 色)と元の色(RGB565)の誤差(gen_ui_assets.py が出力。画素の誤差 = 3チャンネルの段の差の最大。段 = RGB565 の1段)", "",
             "## 背景7枚(固定色 %d、universe %d 色、パレット 256)" % (len(c8["fixed"]), c8["universe"]), "",
             "| 背景 | 画素数 | 不一致 | 割合 | 1段超 | 2段超 | 最大 |", "|---|---|---|---|---|---|---|"]
    for r in rows:
        lines.append("| %s | %d | %d | %.3f%% | %d | %d | %d段 |" % (r[0], r[1], r[2], 100.0 * r[2] / r[1], r[3], r[4], r[5]))
    tp = sum(r[1] for r in rows)
    lines.append("| **合計** | %d | %d | %.3f%% | %d | %d | %d段 |" % (tp, n, 100.0 * n / tp, m1, m2, mx))
    lines += ["", "## 画面(Python の試作描画。ホストの C 描画との比較は tests/host の H-D4)", "",
              "| 種類 | 画素数 | 不一致 | 割合 | 1段超 | 2段超 | 最大 |", "|---|---|---|---|---|---|---|"]
    for k, s in group_stats(srows).items():
        lines.append("| %s | %d | %d | %.3f%% | %d | %d | %d段 |" % (k, s[0], s[1], 100.0 * s[1] / s[0], s[2], s[3], s[4]))
    lines += ["", "### 内訳", "", "| 画面 | 画素数 | 不一致 | 1段超 | 2段超 | 最大 |", "|---|---|---|---|---|---|"]
    for r in srows:
        lines.append("| %s | %d | %d | %d | %d | %d段 |" % (r[0], r[1], r[2], r[3], r[4], r[5]))
    lines += ["", "## スプライト 16 枚(元の RGB565 と、パレットの最近傍の差。目標 最大 <= %d 段)" % LIM_SPR_STEP, "",
              "| スプライト | 大きさ | 不一致 | 割合 | 1段超 | 最大 | 使う添字の数 |", "|---|---|---|---|---|---|---|"]
    for k, v in c8["spr_idx"].items():
        px, mis, o1, o2, mxe = err_stats(a["spr565"][k], pal565[v])
        lines.append("| %s | %dx%d | %d | %.1f%% | %d | %d段 | %d |" % (k, v.shape[1], v.shape[0], mis, 100.0 * mis / px, o1, mxe, len(np.unique(v))))
    lines += ["", "## パレット", "",
              "- universe %d 色 / 固定色 %d(新しい色 5 = %s を含む)/ 使われている添字 %d / 256" % (
                  c8["universe"], len(c8["fixed"]), " ".join(NEW_COLORS), len(used_entries(a, c8)))]
    with open(os.path.join(cdir, "error_table.md"), "w", newline="\n", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")


def write_preview(a, c8, pdir):
    """旧来のプレビュー(bg_*.png = パネルに出る色。digits_sample.png、tag_demo.png)"""
    os.makedirs(pdir, exist_ok=True)
    tiles = []
    for name, scr in BGS:
        im = Image.fromarray(panel_rgb(c8["pal565"][c8["bg_idx"][name]]))
        im.save(os.path.join(pdir, "bg_%s.png" % name))
        tiles.append(im)
    sheet = Image.new("RGB", (W * 2 + 30, (H + 10) * 4 + 10), (200, 200, 200))
    for i, im in enumerate(tiles):
        sheet.paste(im, (10 + (i % 2) * (W + 10), 10 + (i // 2) * (H + 10)))
    sheet.save(os.path.join(pdir, "all_backgrounds.png"))
    rows = []
    for fid, px, wg in FONTS:
        row = Image.new("L", (13 * (px + 10), px + 12), 0)
        x = 4
        for (off, gw, gh, bx, by, ad) in a["glyphs"][fid]:
            g = Image.frombytes("L", (gw, gh), a["pool"][off:off + gw * gh])
            row.paste(g, (x + bx, 4 + (px) - by))
            x += ad + 6
        rows.append(row)
    sh = Image.new("L", (max(r.width for r in rows), sum(r.height for r in rows)), 0)
    y = 0
    for r in rows:
        sh.paste(r, (0, y))
        y += r.height
    sh.save(os.path.join(pdir, "digits_sample.png"))
    Image.fromarray(panel_rgb(c8["pal565"][c8["tag_idx"]])).resize((84 * 4, 24 * 4), Image.NEAREST).save(os.path.join(pdir, "tag_demo.png"))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    here = os.path.dirname(os.path.abspath(__file__))
    ap.add_argument("--out-dir", default=os.path.join(here, "..", "src", "app"))
    ap.add_argument("--preview-dir", default=os.path.join(here, "assets", "bath", "preview"))
    ap.add_argument("--c8-preview-dir", default=None, help="元の色 | パレット(256 色)| 差分の PNG と誤差表(既定: --preview-dir の隣の preview_wx。preview_p2b は P2b 時点の記録として残す)")
    ap.add_argument("--font", default=DEFAULT_FONT)
    ap.add_argument("--check", action="store_true", help="ファイルを書かず、検査と容量・色数・誤差を表示")
    args = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    if not os.path.exists(args.font):
        print("フォントが見つからない: %s (--font で指定)" % args.font, file=sys.stderr)
        return 2
    fonts = Fonts(args.font)
    a = build(fonts)
    errs = check_all(a)
    c8 = build_c8(a)
    errs += check_c8(a, c8)
    bg, pool, shp, bars, tag, tbl, spr = sizes(a, c8)
    total = bg + pool + shp + bars + tag + tbl + spr
    print("フォント: %s sha256=%s" % (os.path.basename(args.font), fonts.sha))
    print("ROM(データ): 背景 %d / 数字 %d / 形 %d / バー %d / タグ %d / パレットと表 %d / アイコン %d = %d バイト" % (bg, pool, shp, bars, tag, tbl, spr, total))
    allc = set()
    for name, scr in BGS:
        cols = set(c8["bg565"][name].ravel().tolist())
        allc |= cols
        print("  色数(RGB565) %-12s %5d" % (name, len(cols)))
    print("  色数 全背景の合計 %d / universe %d 色 / 固定色 %d / パレット 256(使われている添字 %d)" % (
        len(allc), c8["universe"], len(c8["fixed"]), len(used_entries(a, c8))))
    rows, (n, m1, m2, mx) = bg_errors(a, c8)
    tp = sum(r[1] for r in rows)
    for k, v in c8["spr_idx"].items():
        e = err_stats(a["spr565"][k], c8["pal565"][v])
        print("  スプライト誤差 %-12s 不一致 %3d / %d 1段超 %2d 最大 %d段" % (k, e[1], e[0], e[2], e[4]))
    for r in rows:
        print("  背景誤差 %-12s 不一致 %5d (%.3f%%) 1段超 %3d 最大 %d段" % (r[0], r[2], 100.0 * r[2] / r[1], r[4 - 1], r[5]))
    print("  背景誤差 合計: 不一致 %d / %d (%.3f%%)  1段超 %d  2段超 %d  最大 %d段" % (n, tp, 100.0 * n / tp, m1, m2, mx))
    if errs:
        for e in errs:
            print("NG:", e, file=sys.stderr)
        return 1
    print("検査 OK(全スロットが画面内、当たり判定が重ならない、背景 480x272、パレット・固定色・誤差が基準内)")
    if args.check:
        return 0
    write_c(a, c8, args.out_dir, fonts)
    write_preview(a, c8, args.preview_dir)
    cdir = args.c8_preview_dir or (os.path.normpath(args.preview_dir) + "_wx")
    write_c8_preview(a, c8, cdir)
    print("出力: %s / %s / %s" % (os.path.normpath(args.out_dir), os.path.normpath(args.preview_dir), os.path.normpath(cdir)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
