#!/usr/bin/env python3
"""gen_clock_assets.py - 時計・天気画面(docs/design.md §13、Phase 6)のアセットとプレビューの生成。

  python tools/gen_clock_assets.py                 # プレビュー PNG を tools/assets/clock/preview に出す
  python tools/gen_clock_assets.py --check         # ファイルを書かない。検査と ROM の内訳の表示だけ(失敗 = 終了コード 1)
  --preview-dir DIR   既定 tools/assets/clock/preview
  --states-dir DIR    既定 tests/host/data/states(C0.state〜C11.state。設計 §13.10)
  --emit-c8 DIR       12 状態を Python の描画のまま 512x272x8bit の C0.c8〜C11.c8(と clk_<kind>.pal)に書く(H-C7 ⑤ の突き合わせ用)
  --emit-c            C の生成物(clk_assets.h clk_data.c clk_preview.c)を --out-dir(既定 src/app)に書く
  --out-dir DIR       --emit-c の出力先
  --font PATH         既定 C:\\Windows\\Fonts\\NotoSansJP-VF.ttf

色・座標・形はすべてこのファイルの表(設計 §13.2〜§13.4)。乱数・時刻を使わない(再実行で同じ出力)。Pillow と numpy が必要。
プレビューの画素は、パネルに出る色(パレットの RGB565 を RGB888 へ複製)。
株価の gen_stock_assets.py から部品を import するだけで、そちらは変えない。
"""
import argparse
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))     # python -I はスクリプトのフォルダを足さない

import numpy as np
from PIL import Image, ImageChops, ImageDraw, ImageFont

import gen_stock_assets as G
from gen_stock_assets import (Fonts, Painter, W, H, S, contrast, hexrgb, mix565, pal_argb, panel_rgb, rgb565)

LEFT, CENTER = 0, 1

# ---- 天気の種類(設計 §13.4)。(名前, 天気名, 背景) ----
KINDS = (("sun", "晴れ", "#B45309"), ("partly", "晴れ時々くもり", "#1D6FA5"), ("cloud", "くもり", "#4B5B72"),
         ("fog", "霧", "#5F6B73"), ("rain", "雨", "#1E4687"), ("snow", "雪", "#5B7FA6"),
         ("thunder", "雷雨", "#4C3A86"), ("unknown", "不明", "#3F4756"))
UNKNOWN = 7
WHITE = "#FFFFFF"
FG = dict(CLOUD="#E5EAF2", SUN="#FBBF24", DROP="#60A5FA", BOLT="#FACC15")

# ---- ramp とパレットの配置(設計 §13.2 を変更: アイコンは index スプライトにしたので ramp は文字と角の 3 本だけ)----
# 添字: 0 = 背景、1 = パネル(スプライトの透明色)、2〜33 R_BGW、34〜65 R_PNW、66〜97 R_CORN、98〜255 = アイコンの色(天気ごと。最大 158 色)
R_BGW, R_PNW, R_CORN = range(3)
RAMP_NAMES = ("R_BGW", "R_PNW", "R_CORN")
FIXED_N = 98
KEY_IDX = 1                                            # スプライトの透明色(= パネル)


def ramp_idx(k):
    return [0] + [2 + 32 * k + (l - 1) for l in range(1, 33)]


RAMP = [ramp_idx(k) for k in range(3)]

# ---- レイアウト(設計 §13.4 の座標表。動かすのはここだけ)----
PANEL = (40, 168, 400, 92)                             # x, y, w, h
CORNER_R = 16
CORNERS = ((40, 168), (424, 168), (40, 244), (424, 244))   # 左上・右上・左下・右下
CX = 240
TIME_BASE, DATE_BASE = 122, 152
ICON_Y, ICON_W = 175, 78
TEMP_BASE, NAME_BASE = 241, 222
GAP = 12                                               # アイコン・気温・天気名の間
COLON_DIAM, COLON_UP = 15, (31, 62)                    # `:` の円の直径と、ベースラインから中心までの高さ
COLON_ADV = 34
QMARK_PX, QMARK_BASE = 32, 77

# ---- フォント(設計 §13.4)。(名前, px, 太さ, 字)。字の番号 = 文字列の位置 ----
CH124 = "0123456789:-"
CH72 = "0123456789\u2212\u00b0-"
CH20 = "0123456789月日火水木金土()東京都晴れ時々くもり霧雨雪雷不明\u3000"
FONTS = {"F124": (124, 700, CH124), "F72": (72, 700, CH72), "F20": (20, 700, CH20)}
SPEC_ADV = {"F124": {"digit": 73, "-": 46, ":": 34}, "F72": {"digit": 42, "\u2212": 42, "\u00b0": 29, "-": 27},
            "F20": {"digit": 12, "kanji": 20, "(": 8, ")": 8, "\u3000": 20}}
# 注: 設計 §13.4 の表の `(` `)` = 7 は 8 の誤記(測定 7.8。日付 ≒ 204 は 8 で成立)
WDAY = "日月火水木金土"
ICON_FG = (("sun",), ("sun", "cloud"), ("cloud",), ("cloud",), ("cloud", "drop"), ("cloud",), ("cloud", "bolt"), ("cloud",))   # 種類ごとに使うアイコンの色

TITLES = ("C0 晴れ", "C1 晴れ時々くもり", "C2 くもり", "C3 霧", "C4 雨", "C5 雪(マイナス)", "C6 雷雨", "C7 天気だけ不明",
          "C8 最大幅(-99° 晴れ時々くもり)", "C9 時刻未同期", "C10 0°", "C11 99°")
NSTATE = 12
KEYS = ("scr", "clk.clock", "clk.have", "clk.kind", "clk.temp", "clk.mon", "clk.day", "clk.wday")


# ------------------------------------------------------------------ 色
def c565(h):
    return rgb565(hexrgb(h))


def panel_hex(bg):
    return "#%02X%02X%02X" % tuple(int(v * 0.72 + 0.5) for v in hexrgb(bg))   # 黒 28% を重ねる(round(c x 0.72) の half up)


def hex565(p):
    r, g, b = (int(v) for v in panel_rgb(np.array([p]))[0])
    return "#%02X%02X%02X" % (r, g, b)


def make_palette(kind):
    """256 個の RGB565 のうち、全天気で配置が同じ前半 98 個(背景・パネル・ramp 3 本。誤差 0)。後半はアイコンの色(build が入れる)"""
    bg, pn = c565(KINDS[kind][2]), c565(panel_hex(KINDS[kind][2]))
    pal = np.full(256, bg, dtype=np.uint16)
    pal[1] = pn
    for k, (b, f) in enumerate(((bg, c565(WHITE)), (pn, c565(WHITE)), (pn, bg))):
        for l in range(1, 33):
            pal[RAMP[k][l]] = int(mix565(b, f, l))
    return pal


# ------------------------------------------------------------------ グリフ(A8)
def half_up(x):
    """送り幅の丸め。.5 ちょうどは切り捨て(F72 の数字 42.5 → 42。設計 §13.4 の「数字 42」)"""
    return int(np.ceil(x - 0.5))


def bake_font(fonts, name):
    """字ごとに (幅, 高さ, bx, by, adv, A8 の bytes)。幅 0 = 字形なし。数字は送り幅をそろえ、字は送り幅の中で左右中央"""
    px, wg, chars = FONTS[name]
    f4 = fonts.get(px, wg)
    nat = {c: f4.getlength(c) / S for c in chars}
    adv_digit = max(half_up(nat[c]) for c in "0123456789")
    out = []
    for c in chars:
        digit = c.isdigit()
        adv = adv_digit if (digit or (name == "F72" and c == "\u2212")) else half_up(nat[c])
        if name == "F124" and c == ":":
            adv = COLON_ADV
        shift = (adv - nat[c]) / 2 if (digit or (name == "F72" and c == "\u2212")) else 0.0
        if c == "\u3000":
            out.append((0, 0, 0, 0, adv, b""))
            continue
        x0, y0, x1, y1 = -4, -int(px * 1.3), int(adv) + 8, int(px * 0.4)
        im = Image.new("L", ((x1 - x0) * S, (y1 - y0) * S), 0)
        d = ImageDraw.Draw(im)
        if name == "F124" and c == ":":                  # フォントの字形でなく円 2 個(設計 §13.4)
            r = COLON_DIAM / 2.0
            for up in COLON_UP:
                cx, cy = adv / 2.0, -up
                d.ellipse([(cx - r - x0) * S, (cy - r - y0) * S, (cx + r - x0) * S - 1, (cy + r - y0) * S - 1], fill=255)
        else:
            d.text(((-x0 + shift) * S, -y0 * S), c, font=f4, fill=255, anchor="ls")
        a = im.resize((x1 - x0, y1 - y0), Image.BOX)
        bb = a.getbbox()
        if bb is None:
            raise SystemExit("空の字形: %r (%s)" % (c, name))
        a = a.crop(bb)
        out.append((a.width, a.height, x0 + bb[0], -(y0 + bb[1]), adv, a.tobytes()))
    return out


# ------------------------------------------------------------------ アイコン(設計 §13.4 の形)
# 形は符号付き距離(SDF)で書く。8 倍の副格子で評価し、縁を 1/8 画素の幅で解析的にぼかして平均する(縁の濃度は 8 bit 以上の精度)。
K = 8
BLEND = "linear"                                       # 縁の混色: "linear" = 線形光、"srgb" = sRGB の値のまま
CLOUD_FILLET = 5.0                                     # 雲の円どうしの継ぎ目(谷)を丸める半径(px)
BOLT_GROW = 0.5                                        # 稲妻の角を丸める膨らみ(px)


def sub_grid():
    c = (np.arange(ICON_W * K) + 0.5) / K
    return np.meshgrid(c, c)


def cov_of(sd):
    c = np.clip(0.5 - sd * K, 0.0, 1.0)
    return c.reshape(ICON_W, K, ICON_W, K).mean(axis=(1, 3))


def sd_circle(X, Y, cx, cy, r):
    return np.hypot(X - cx, Y - cy) - r


def sd_capsule(X, Y, x1, y1, x2, y2, r):
    px, py, bx, by = X - x1, Y - y1, x2 - x1, y2 - y1
    h = np.clip((px * bx + py * by) / (bx * bx + by * by), 0, 1)
    return np.hypot(px - bx * h, py - by * h) - r


def sd_rrect(X, Y, x0, y0, x1, y1, r):
    qx, qy = np.abs(X - (x0 + x1) / 2) - ((x1 - x0) / 2 - r), np.abs(Y - (y0 + y1) / 2) - ((y1 - y0) / 2 - r)
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r


def smin(a, b, k):
    h = np.maximum(k - np.abs(a - b), 0) / k
    return np.minimum(a, b) - h * h * k / 4


def sd_poly(X, Y, pts):
    d = np.full(X.shape, 1e9)
    sg = np.ones(X.shape)
    for i in range(len(pts)):
        j = i - 1
        ex, ey = pts[j][0] - pts[i][0], pts[j][1] - pts[i][1]
        wx, wy = X - pts[i][0], Y - pts[i][1]
        t = np.clip((wx * ex + wy * ey) / (ex * ex + ey * ey), 0, 1)
        d = np.minimum(d, (wx - ex * t) ** 2 + (wy - ey * t) ** 2)
        c1, c2, c3 = Y >= pts[i][1], Y < pts[j][1], ex * wy > ey * wx
        sg = np.where((c1 & c2 & c3) | (~c1 & ~c2 & ~c3), -sg, sg)
    return sg * np.sqrt(d)


def sd_cloud(X, Y, sc=1.0, ox=0.0, oy=0.0):
    """雲。外接 x 8〜70、y 6〜50(sc = 1)。円と角丸四角は、継ぎ目を CLOUD_FILLET で丸めてつなぐ"""
    x, y = (X - ox) / sc, (Y - oy) / sc
    d = sd_circle(x, y, 27, 30, 12)
    d = smin(d, sd_circle(x, y, 43, 22, 16), CLOUD_FILLET)
    d = smin(d, sd_circle(x, y, 58, 32, 11), CLOUD_FILLET)
    d = smin(d, sd_rrect(x, y, 8, 30, 70, 50, 10), CLOUD_FILLET)
    return d * sc


def sd_sun(X, Y, cx, cy, r, r0, r1, wd):
    d = sd_circle(X, Y, cx, cy, r)
    for i in range(8):
        t = np.pi / 4 * i
        d = np.minimum(d, sd_capsule(X, Y, cx + r0 * np.cos(t), cy + r0 * np.sin(t), cx + r1 * np.cos(t), cy + r1 * np.sin(t), wd / 2.0))
    return d


def cov_qmark(fonts):
    f = ImageFont.truetype(fonts.path, QMARK_PX * K)
    f.set_variation_by_axes([700])
    im = Image.new("L", (ICON_W * K, ICON_W * K), 0)
    # 設計は 34px・ベースライン 76 だが雲の下端(50)に重なる → 32px・77
    ImageDraw.Draw(im).text((39 * K, QMARK_BASE * K), "?", font=f, fill=255, anchor="ms")
    return np.asarray(im, dtype=np.float64).reshape(ICON_W, K, ICON_W, K).mean(axis=(1, 3)) / 255.0


def icon_layers(kind, fonts):
    """種類 -> 層の一覧 [(濃度 78x78 の float、前景色)]。層どうしは重ならない(重なる所は下の層を切り抜く)"""
    X, Y = sub_grid()
    if kind == 0:
        return [(cov_of(sd_sun(X, Y, 39, 39, 15, 21, 32, 5)), FG["SUN"])]
    if kind == 1:
        cs = sd_cloud(X, Y, 0.85, 12, 20)
        sun = np.maximum(sd_sun(X, Y, 26, 26, 11, 16, 23, 4), 3.0 - cs)      # CLOUD_S を 3px 膨らませた形を切り抜く
        return [(cov_of(sun), FG["SUN"]), (cov_of(cs), FG["CLOUD"])]
    if kind == 2:
        return [(cov_of(sd_cloud(X, Y, 1.0, 0, 12)), FG["CLOUD"])]
    top = (cov_of(sd_cloud(X, Y)), FG["CLOUD"])
    if kind == 3:
        d = np.minimum.reduce([sd_capsule(X, Y, x0, y, x1, y, 2.0) for (y, x0, x1) in ((57, 10, 68), (64, 18, 60), (71, 10, 54))])
        return [top, (cov_of(d), FG["CLOUD"])]
    if kind == 4:
        d = np.minimum.reduce([sd_capsule(X, Y, x, 56, x - 4, 70, 2.5) for x in (25, 40, 55)])
        return [top, (cov_of(d), FG["DROP"])]
    if kind == 5:
        d = np.minimum.reduce([sd_circle(X, Y, x, y, 4) for (x, y) in ((25, 60), (40, 66), (55, 60))])
        return [top, (cov_of(d), WHITE)]
    if kind == 6:
        pts = ((46, 52), (32, 66), (41, 66), (35, 77), (54, 60), (45, 60), (51, 52))
        return [top, (cov_of(sd_poly(X, Y, pts) - BOLT_GROW), FG["BOLT"])]
    return [top, (cov_qmark(fonts), WHITE)]


def to_lin(c, blend):
    c = np.asarray(c, dtype=np.float64) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4) if blend == "linear" else c


def from_lin(v, blend):
    if blend != "linear":
        return v * 255.0
    return np.where(v <= 0.0031308, v * 12.92, 1.055 * np.power(np.maximum(v, 0), 1 / 2.4) - 0.055) * 255.0


def v565(rgb8):
    r, g, b = (np.asarray(rgb8[..., i], dtype=np.int32) for i in range(3))
    return ((((r * 31 + 127) // 255) << 11) | (((g * 63 + 127) // 255) << 5) | ((b * 31 + 127) // 255)).astype(np.uint16)


def ideal_icon(layers, panel565, blend=None):
    """理想の画像: 高精度の濃度で、パネルの色(画面に出る色)とアイコンの色を混ぜ、RGB565 に丸める。戻り値 = (RGB565, 濃度の合計)"""
    blend = blend or BLEND
    tot = sum(c for c, _ in layers)
    s = np.minimum(tot, 1.0)
    w = np.where(tot > 0, s / np.maximum(tot, 1e-12), 0)
    pn = np.array(hexrgb(hex565(int(panel565))), dtype=np.float64)
    acc = to_lin(pn, blend)[None, None, :] * (1 - s)[..., None]
    for c, fg in layers:
        acc = acc + (c * w)[..., None] * to_lin(np.array(hexrgb(fg), dtype=np.float64), blend)[None, None, :]
    return v565(np.clip(np.rint(from_lin(acc, blend)), 0, 255)), tot


def legacy_shown(layers, panel565):
    """旧方式(4 倍の副格子 = 17 段の濃度 -> A8 -> 33 段の ramp = mix565)を同じ形に当てた画像(比較の指標用)"""
    out = np.full((ICON_W, ICON_W), panel565, dtype=np.uint16)
    for c, fg in layers:
        lv = (np.rint(np.round(c * 16) / 16 * 255).astype(np.int32) + 4) >> 3
        out = np.where(lv > 0, mix565(panel565, c565(fg), lv), out).astype(np.uint16)
    return out


def fit_icon_colors(pal, ideal, nfree):
    """アイコンの色(RGB565)。固定の前半 98 個に無い色を最大 nfree 色まで。足りなければ k-means(乱数なし)。戻り値 = 追加する色の昇順"""
    fixed = [int(x) for x in pal[:FIXED_N]]
    fs = set(fixed)
    cols, cnt = np.unique(ideal, return_counts=True)
    cand = [(int(c), int(n)) for c, n in zip(cols, cnt) if int(c) not in fs]
    if len(cand) <= nfree:
        return sorted(c for c, _ in cand)
    wt = np.zeros(65536)
    for c, n in cand:
        wt[c] = n
    return [int(x) for x in G.make_palette(wt, fixed)[FIXED_N:]]


def to_sprite(ideal, pal, used_n):
    """理想の色 -> 最も近いパレットの添字(同点は小さい添字)。透明色 = KEY_IDX(パネル)。戻り値 = (78x78 の添字、切り詰めた配列、ox、oy)"""
    cols = np.unique(ideal)
    nearest = G.d2mat(G.comps(cols), G.comps(pal[:used_n])).argmin(axis=1)
    lut = dict((int(c), int(i)) for c, i in zip(cols, nearest))
    idx = np.vectorize(lambda c: lut[int(c)], otypes=[np.uint8])(ideal)
    ys, xs = np.nonzero(idx != KEY_IDX)
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    return idx, idx[y0:y1, x0:x1].copy(), int(x0), int(y0)


def make_corners():
    """角 4 枚(16x16)。半径 16 の円の外側の濃度(外側 = 255)"""
    im = Image.new("L", (32 * S, 32 * S), 0)
    ImageDraw.Draw(im).ellipse([0, 0, 32 * S - 1, 32 * S - 1], fill=255)
    a = np.asarray(im.resize((32, 32), Image.BOX), dtype=np.uint8)
    inv = 255 - a
    return [inv[:16, :16].copy(), inv[:16, 16:].copy(), inv[16:, :16].copy(), inv[16:, 16:].copy()]


# ------------------------------------------------------------------ 全アセット
def build(fonts):
    a = {}
    pool, tabs = bytearray(), {}
    for name in FONTS:
        lst = []
        for (w, h, bx, by, ad, data) in bake_font(fonts, name):
            lst.append((len(pool), w, h, bx, by, ad))
            pool += data
        tabs[name] = lst
    a["glyphs"] = tabs
    a["pool"] = bytes(pool)
    a["corner"] = make_corners()
    a["pal565"], a["used_n"], a["sprite"], a["icon"] = [], [], [], []
    for k in range(8):
        pal = make_palette(k)
        layers = icon_layers(k, fonts)
        ideal, tot = ideal_icon(layers, int(pal[1]))
        extra = fit_icon_colors(pal, ideal, 256 - FIXED_N)
        pal[FIXED_N:FIXED_N + len(extra)] = extra
        used_n = FIXED_N + len(extra)
        idx, arr, ox, oy = to_sprite(ideal, pal, used_n)
        a["pal565"].append(pal)
        a["used_n"].append(used_n)
        a["sprite"].append((arr, ox, oy))
        a["icon"].append(dict(layers=layers, ideal=ideal, tot=tot, idx=idx))
    a["pal"] = [[pal_argb(int(p)) for p in pal] for pal in a["pal565"]]
    return a


def digest(a, renders):
    h = hashlib.sha256()
    h.update(a["pool"])
    h.update(repr(sorted(a["glyphs"].items())).encode())
    for arr, ox, oy in a["sprite"]:
        h.update(arr.tobytes())
        h.update(repr((ox, oy, arr.shape)).encode())
    for c in a["corner"]:
        h.update(c.tobytes())
    for p in a["pal565"]:
        h.update(p.tobytes())
    for r in renders:
        h.update(r.tobytes())
    return h.hexdigest()


# ------------------------------------------------------------------ 状態(設計 §13.10)
def parse_state(path):
    v = dict((k, 0) for k in KEYS)
    v["clk.clock"] = 65535
    with open(path, encoding="utf-8") as f:
        for ln, line in enumerate(f, 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            k, _, n = line.partition("=")
            k = k.strip()
            if k not in v:
                raise SystemExit("%s:%d: 未知のキー %s" % (path, ln, k))
            v[k] = int(n)
    if v["scr"] != 1:
        raise SystemExit("%s: scr=1 でない" % path)
    return v


def kind_of(v):
    return v["clk.kind"] if v["clk.have"] else UNKNOWN          # have = 0 のときは unknown(設計 §13.8)


def hhmm_seq(m):
    return [11, 11, 10, 11, 11] if m > 1439 else [m // 60 // 10, m // 60 % 10, 10, m % 60 // 10, m % 60 % 10]


def date_seq(v):
    idx = CH20.index
    tail = [idx("東"), idx("京"), idx("都")]
    if v["clk.mon"] == 0:
        return tail
    d = lambda n: [idx(c) for c in str(n)]
    return d(v["clk.mon"]) + [idx("月")] + d(v["clk.day"]) + [idx("日"), idx("("), idx(WDAY[v["clk.wday"]]), idx(")"), idx("\u3000")] + tail


def temp_seq(v):
    if not v["clk.have"]:
        return [12, 12, 11]                                       # --°
    t = v["clk.temp"]
    return ([10] if t < 0 else []) + [int(c) for c in str(abs(t))] + [11]


def name_seq(kind):
    return [CH20.index(c) for c in KINDS[kind][1]]


# ------------------------------------------------------------------ 描画(設計 §13.4 の描画の順。C の ui_render_clock と同じ規則)
class CP(Painter):
    def fill(self, x, y, w, h, idx):
        self.fb[y:y + h, x:x + w] = idx

    def key_blit(self, src, x, y, key=KEY_IDX):
        """index スプライトを、透明色 key を飛ばして重ねる(C の gfx では新しい関数。画面内に収まる前提)"""
        h, w = src.shape
        sub = self.fb[y:y + h, x:x + w]
        self.fb[y:y + h, x:x + w] = np.where(src != key, src, sub)

    def seq(self, font, ks, x, y, align, ramp):
        """字番号の並びを描く。(送り幅の合計 = 戻り値)"""
        gl = self.a["glyphs"][font]
        wd = sum(gl[k][5] for k in ks)
        x -= wd // 2 if align == CENTER else 0
        for k in ks:
            off, gw, gh, bx, by, ad = gl[k]
            if gw:
                self.a8(np.frombuffer(self.a["pool"], dtype=np.uint8)[off:off + gw * gh].reshape(gh, gw), x + bx, y - by, ramp)
            x += ad
        return wd


def widths(a, v):
    kind = kind_of(v)
    gl20, gl72 = a["glyphs"]["F20"], a["glyphs"]["F72"]
    tw = sum(gl72[k][5] for k in temp_seq(v))
    nw = sum(gl20[k][5] for k in name_seq(kind))
    G_ = ICON_W + GAP + tw + GAP + nw
    return tw, nw, G_, CX - G_ // 2


def render(a, v):
    p = CP(a)
    kind = kind_of(v)
    tw, nw, G_, x0 = widths(a, v)
    p.fill(0, 0, W, H, 0)                                                              # 1 背景
    p.fill(*PANEL, 1)                                                                  # 2 パネル
    for i, (cx, cy) in enumerate(CORNERS):                                             # 3 角
        p.a8(a["corner"][i], cx, cy, RAMP[R_CORN])
    p.seq("F124", hhmm_seq(v["clk.clock"]), CX, TIME_BASE, CENTER, RAMP[R_BGW])        # 4 時刻
    p.seq("F20", date_seq(v), CX, DATE_BASE, CENTER, RAMP[R_BGW])                      # 5 日付・地名
    arr, ox, oy = a["sprite"][kind]                                                    # 6 アイコン(index スプライト。透明色 = パネル)
    p.key_blit(arr, x0 + ox, ICON_Y + oy)
    p.seq("F72", temp_seq(v), x0 + ICON_W + GAP, TEMP_BASE, LEFT, RAMP[R_PNW])         # 7 気温
    p.seq("F20", name_seq(kind), x0 + ICON_W + GAP + tw + GAP, NAME_BASE, LEFT, RAMP[R_PNW])   # 8 天気名
    return p.fb


# ------------------------------------------------------------------ 検査
def ink(a, font, ks, x, y, align):
    """文字列の外形 (x0, y0, x1, y1)。横 = 字の外形、縦 = ベースライン y 基準"""
    gl = a["glyphs"][font]
    wd = sum(gl[k][5] for k in ks)
    x = x - wd // 2 if align == CENTER else x
    r = None
    for k in ks:
        off, gw, gh, bx, by, ad = gl[k]
        if gw:
            q = (x + bx, y - by, x + bx + gw, y - by + gh)
            r = q if r is None else (min(r[0], q[0]), min(r[1], q[1]), max(r[2], q[2]), max(r[3], q[3]))
        x += ad
    return r


def overlap(p, q):
    return p[0] < q[2] and q[0] < p[2] and p[1] < q[3] and q[1] < p[3]


def inside(r, box):
    return r[0] >= box[0] and r[1] >= box[1] and r[2] <= box[0] + box[2] and r[3] <= box[1] + box[3]


def state_rects(a, v):
    kind = kind_of(v)
    tw, nw, G_, x0 = widths(a, v)
    arr, ox, oy = a["sprite"][kind]
    ic = (x0 + ox, ICON_Y + oy, x0 + ox + arr.shape[1], ICON_Y + oy + arr.shape[0])
    return {"時刻": ink(a, "F124", hhmm_seq(v["clk.clock"]), CX, TIME_BASE, CENTER),
            "日付・地名": ink(a, "F20", date_seq(v), CX, DATE_BASE, CENTER),
            "アイコン": ic,
            "気温": ink(a, "F72", temp_seq(v), x0 + ICON_W + GAP, TEMP_BASE, LEFT),
            "天気名": ink(a, "F20", name_seq(kind), x0 + ICON_W + GAP + tw + GAP, NAME_BASE, LEFT)}, G_, x0


def check_all(a, fonts, states, renders):
    errs, warns, info = [], [], {}
    # ① パレット: 前半 98 個は配置が全天気で同じ・誤差 0、後半はアイコンの色
    nmax = 0
    for k in range(8):
        pal = a["pal565"][k]
        bg, pn = c565(KINDS[k][2]), c565(panel_hex(KINDS[k][2]))
        if int(pal[0]) != bg or int(pal[1]) != pn:
            errs.append("パレット %s: 添字 0・1 が背景・パネルの RGB565 でない" % KINDS[k][0])
        for rk, (b, f) in enumerate(((bg, c565(WHITE)), (pn, c565(WHITE)), (pn, bg))):
            for l in range(1, 33):
                if int(pal[RAMP[rk][l]]) != int(mix565(b, f, l)):          # 誤差 0(各段が mix565 と一致)
                    errs.append("パレット %s の %s の段 %d が mix565 と違う" % (KINDS[k][0], RAMP_NAMES[rk], l))
            if int(pal[RAMP[rk][32]]) != (f if rk != R_CORN else bg):
                errs.append("パレット %s の %s の l = 32 が前景そのものでない" % (KINDS[k][0], RAMP_NAMES[rk]))
        if not FIXED_N <= a["used_n"][k] <= 256:
            errs.append("パレット %s: 使う添字の数 %d が範囲外" % (KINDS[k][0], a["used_n"][k]))
        for i in range(a["used_n"][k], 256):
            if int(pal[i]) != bg:
                errs.append("パレット %s の添字 %d(未使用)が背景と同じ色でない" % (KINDS[k][0], i))
        for i in range(256):
            if a["pal"][k][i] != pal_argb(int(pal[i])) or (a["pal"][k][i] >> 24) != 0xFF:
                errs.append("clk_pal[%d][%d] が 0xFF000000 | RGB565 を 8bit に広げた値でない" % (k, i))
        nmax = max(nmax, a["used_n"][k])
    # ② フォントに字がある・送り幅が設計どおり
    allch = "".join(CH124 + CH72 + CH20)
    miss = fonts.missing(allch, 24, 700)
    if miss:
        errs.append("フォントに無い文字: %s" % "".join(miss))
    for name, (px, wg, chars) in FONTS.items():
        for i, c in enumerate(chars):
            if a["glyphs"][name][i][1] == 0 and c != "\u3000":
                errs.append("%s のグリフ %r が空" % (name, c))
    adv = {n: {c: a["glyphs"][n][i][5] for i, c in enumerate(FONTS[n][2])} for n in FONTS}
    info["adv"] = adv
    for n, spec in SPEC_ADV.items():
        for key, val in spec.items():
            cs = "0123456789" if key == "digit" else "月日火水木金土東京都晴れ時々くもり霧雨雪雷不明" if key == "kanji" else key
            for c in cs:
                if adv[n][c] != val:
                    warns.append("送り幅 %s %r = %d(設計 §13.4 の表は %d)" % (n, c, adv[n][c], val))
                    break
    # F20 の字の番号が設計の表(§13.4)どおり
    for c, k in (("月", 10), ("日", 11), ("火", 12), ("水", 13), ("木", 14), ("金", 15), ("土", 16), ("(", 17), (")", 18), ("東", 19),
                 ("京", 20), ("都", 21), ("晴", 22), ("れ", 23), ("時", 24), ("々", 25), ("く", 26), ("も", 27), ("り", 28),
                 ("霧", 29), ("雨", 30), ("雪", 31), ("雷", 32), ("不", 33), ("明", 34), ("\u3000", 35)):
        if CH20.index(c) != k:
            errs.append("F20 の字番号 %r = %d(設計は %d)" % (c, CH20.index(c), k))
    # ③ アイコンの層が重ならない・箱に収まる・スプライトの添字の範囲・縁の質(理想との差)
    em = []
    for k in range(8):
        ic = a["icon"][k]
        ls = ic["layers"]
        for i in range(len(ls)):
            for j in range(i + 1, len(ls)):
                n = int((np.minimum(ls[i][0], ls[j][0]) > 0.02).sum())
                if n:
                    errs.append("アイコン %s: 層 %d と %d が %d 画素で重なる(濃度 0.02 超)" % (KINDS[k][0], i, j, n))
        pal = a["pal565"][k]
        if int(ic["idx"].max()) >= a["used_n"][k]:
            errs.append("アイコン %s: スプライトの添字が使う範囲(%d)を超える" % (KINDS[k][0], a["used_n"][k]))
        edge = (ic["tot"] > 0.004) & (ic["tot"] < 0.996)
        after = pal[ic["idx"]]
        before = legacy_shown(ls, int(pal[1]))
        ea, eb = G.step_err(ic["ideal"][edge], after[edge]), G.step_err(ic["ideal"][edge], before[edge])
        ebs = G.step_err(ideal_icon(ls, int(pal[1]), "srgb")[0][edge], before[edge])      # 旧方式は sRGB の値で混ぜるので、その理想との差も
        em.append(dict(kind=KINDS[k][0], n=int(edge.sum()), a_max=int(ea.max()), a_mean=float(ea.mean()), a_col=len(np.unique(after[edge])),
                       b_max=int(eb.max()), b_mean=float(eb.mean()), b_col=len(np.unique(before[edge])), bs_max=int(ebs.max()), bs_mean=float(ebs.mean()),
                       ideal_col=len(np.unique(ic["ideal"][edge]))))
    info["edge"] = em
    # 角: 外の端 = 濃い、内の端 = 0
    cs = a["corner"]
    if not (cs[0][0, 0] >= 250 and cs[0][15, 15] == 0 and cs[1][0, 15] >= 250 and cs[2][15, 0] >= 250 and cs[3][15, 15] >= 250 and cs[3][0, 0] == 0):
        errs.append("パネルの角の濃度が想定と違う")
    # ④ 12 状態: 配置・はみ出し・重なり、使う添字
    worst_g, rect_rows = 0, []
    for n, v in enumerate(states):
        fb = renders[n]
        if int(fb.max()) >= a["used_n"][kind_of(v)]:
            errs.append("C%d: 添字 %d(>= 使う数 %d)を使っている" % (n, int(fb.max()), a["used_n"][kind_of(v)]))
        rc, G_, x0 = state_rects(a, v)
        rect_rows.append((n, rc, G_, x0))
        worst_g = max(worst_g, G_)
        if rc["時刻"] is None or not inside(rc["時刻"], (0, 0, W, H)):
            errs.append("C%d: 時刻が画面からはみ出す %s" % (n, rc["時刻"]))
        if not inside(rc["日付・地名"], (0, 0, W, PANEL[1])):
            errs.append("C%d: 日付・地名が画面かパネルの上の領域からはみ出す %s" % (n, rc["日付・地名"]))
        for k in ("アイコン", "気温", "天気名"):
            if not inside(rc[k], PANEL):
                errs.append("C%d: %s がパネルからはみ出す %s" % (n, k, rc[k]))
        if x0 < PANEL[0] or x0 + G_ > PANEL[0] + PANEL[2]:
            errs.append("C%d: G = %d(x0 = %d)がパネル x %d〜%d に収まらない" % (n, G_, x0, PANEL[0], PANEL[0] + PANEL[2]))
        names = list(rc)
        for i in range(len(names)):
            for j in range(i + 1, len(names)):
                if overlap(rc[names[i]], rc[names[j]]):
                    errs.append("C%d: %s と %s が重なる %s %s" % (n, names[i], names[j], rc[names[i]], rc[names[j]]))
        if int(((fb == 0).sum() + (fb != 0).sum())) != W * H:
            errs.append("C%d: 画素数が合わない" % n)
    if len(states) != NSTATE:
        errs.append("状態ファイルが %d 枚でない" % NSTATE)
    if len(states) == NSTATE and worst_g != 397:
        warns.append("最大の G = %d(設計 §13.4 の最悪は 397)" % worst_g)
    info["rects"], info["G"] = rect_rows, worst_g
    # ⑤ コントラスト(RGB565 後の色。WCAG 相対輝度比)
    con = []
    for k, (nm, jp, bgh) in enumerate(KINDS):
        pal = a["pal565"][k]
        bg, pn = hex565(int(pal[0])), hex565(int(pal[1]))
        wh = hex565(int(pal[RAMP[R_BGW][32]]))
        fgc = lambda f: hex565(c565(f))
        row = dict(kind=nm, wbg=contrast(wh, bg), wpn=contrast(wh, pn),
                   cloud=contrast(fgc(FG["CLOUD"]), pn), sun=contrast(fgc(FG["SUN"]), pn),
                   drop=contrast(fgc(FG["DROP"]), pn), bolt=contrast(fgc(FG["BOLT"]), pn),
                   bg=bg, panel=pn)
        con.append(row)
        if row["wbg"] < 3.0:
            errs.append("コントラスト不足 %s: 時刻・日付(白/背景) %.2f < 3.0" % (nm, row["wbg"]))
        elif row["wbg"] < 4.5:
            warns.append("日付(20px・太さ 700)の白/背景 %s = %.2f(4.5 未満。太字の大きい文字として 3.0 は満たす。設計 §13.4 の許容)" % (nm, row["wbg"]))
        if row["wpn"] < 4.5:
            errs.append("コントラスト不足 %s: 気温・天気名(白/パネル) %.2f < 4.5" % (nm, row["wpn"]))
        for key in ICON_FG[k]:
            if row[key] < 3.0:
                errs.append("コントラスト不足 %s: アイコン %s %.2f < 3.0" % (nm, key, row[key]))
    info["contrast"] = con
    # ⑥ 設計の表の RGB565 後の背景・パネル
    exp = (("#B55508", "#843C08"), ("#216DA5", "#185173"), ("#4A5973", "#394152"), ("#636973", "#424D52"),
           ("#214584", "#183063"), ("#5A7DA5", "#42597B"), ("#4A3884", "#392863"), ("#424952", "#293442"))
    for k, (eb, ep) in enumerate(exp):
        if con[k]["bg"] != eb or con[k]["panel"] != ep:
            errs.append("RGB565 後の色 %s: %s / %s(設計 §13.4 は %s / %s)" % (KINDS[k][0], con[k]["bg"], con[k]["panel"], eb, ep))
    info["fog_cloud_dist"] = int(np.linalg.norm(np.array(hexrgb(KINDS[3][2])) - np.array(hexrgb(KINDS[2][2]))))
    return errs, warns, info, nmax


def rom_table(a):
    pool = len(a["pool"])
    gt = sum(len(v) for v in a["glyphs"].values()) * 12
    spr = sum(arr.size for arr, _, _ in a["sprite"])
    return [("字 A8(F124・F72・F20)", pool), ("字の表(%d 個 x 12)" % (gt // 12), gt), ("アイコンの index スプライト 8 枚(外接矩形)", spr),
            ("角 4 枚", 4 * 256), ("パレット 8 枚", 8 * 1024), ("ramp 3 本 x 33", 3 * 33)]


# ------------------------------------------------------------------ プレビュー(PNG)
def pil_rgb(fb, pal565):
    return Image.fromarray(panel_rgb(pal565[fb]))


def title_font(fonts, px=15, wg=500):
    f = ImageFont.truetype(fonts.path, px)
    f.set_variation_by_axes([wg])
    return f


def icon_rgb(a, k):
    """スプライトをパネルの上に置いた 78x78 の RGB(パネルに出る色)"""
    p = CP(a)
    p.fill(0, 0, ICON_W, ICON_W, 1)
    arr, ox, oy = a["sprite"][k]
    p.key_blit(arr, ox, oy)
    return panel_rgb(a["pal565"][k][p.fb[:ICON_W, :ICON_W]])


LEGACY = os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets", "clock", "legacy_icons_v1.png")


def legacy_rgb(k):
    """旧版(コミット c47b080 の icons.png。2 倍の最近傍)から 1 倍の 78x78 を取り出す"""
    im = np.asarray(Image.open(LEGACY).convert("RGB"))
    x, y = 14 + (k % 4) * 170, 14 + (k // 4) * 194 + 24
    return im[y:y + 156:2, x:x + 156:2].copy()


def write_zoom(a, ft, pdir):
    """icons_zoom.png: 旧版と新版を 4 倍の最近傍で並べる(縁の質の比較)"""
    if not os.path.exists(LEGACY):
        print("警告: %s が無いので icons_zoom.png を作らない" % LEGACY)
        return
    z, g2, cap = 4 * ICON_W, 12, 22
    im = Image.new("RGB", (2 * (2 * z + 2 * g2) + g2, 4 * (z + cap + g2) + g2), (52, 56, 64))
    d = ImageDraw.Draw(im)
    for k in range(8):
        x, y = g2 + (k % 2) * (2 * z + 3 * g2), g2 + (k // 2) * (z + cap + g2)
        d.text((x, y + 2), "%d %s  前(左)" % (k, KINDS[k][0]), font=ft, fill=(236, 239, 244))
        d.text((x + z + g2, y + 2), "後(右)", font=ft, fill=(236, 239, 244))
        im.paste(Image.fromarray(legacy_rgb(k)).resize((z, z), Image.NEAREST), (x, y + cap))
        im.paste(Image.fromarray(icon_rgb(a, k)).resize((z, z), Image.NEAREST), (x + z + g2, y + cap))
    im.save(os.path.join(pdir, "icons_zoom.png"))


def write_previews(a, fonts, states, renders, pdir):
    os.makedirs(pdir, exist_ok=True)
    kinds = [kind_of(v) for v in states]
    for n, fb in enumerate(renders):
        pil_rgb(fb, a["pal565"][kinds[n]]).save(os.path.join(pdir, "state_C%d.png" % n))
    cap, gap = 24, 16
    tw, th = W, H + cap
    sheet = Image.new("RGB", (3 * tw + 4 * gap, 4 * th + 5 * gap), (52, 56, 64))
    dr = ImageDraw.Draw(sheet)
    ft = title_font(fonts)
    for n, fb in enumerate(renders):
        x, y = gap + (n % 3) * (tw + gap), gap + (n // 3) * (th + gap)
        dr.text((x, y + 4), TITLES[n], font=ft, fill=(236, 239, 244))
        sheet.paste(pil_rgb(fb, a["pal565"][kinds[n]]), (x, y + cap))
    sheet.save(os.path.join(pdir, "sheet.png"))
    # icons: 8 種を 2 倍に拡大。各天気のパネルの色の上
    sz, g2 = ICON_W * 2, 14
    ic = Image.new("RGB", (4 * (sz + g2) + g2, 2 * (sz + g2 + 24) + g2), (52, 56, 64))
    di = ImageDraw.Draw(ic)
    for k in range(8):
        im = Image.fromarray(icon_rgb(a, k)).resize((sz, sz), Image.NEAREST)
        x, y = g2 + (k % 4) * (sz + g2), g2 + (k // 4) * (sz + g2 + 24)
        di.text((x, y), "%d %s %s" % (k, KINDS[k][0], KINDS[k][1]), font=ft, fill=(236, 239, 244))
        ic.paste(im, (x, y + 24))
    ic.save(os.path.join(pdir, "icons.png"))
    write_zoom(a, ft, pdir)
    # palette_<kind>: 16 x 16。グループの先頭に白い点、未使用(226〜)は斜線の代わりに小さな × で印
    chip = 28
    starts = {0, 1} | {2 + 32 * k for k in range(3)}
    for k in range(8):
        im = np.full((16 * chip, 16 * chip, 3), 255, dtype=np.uint8)
        pal = a["pal565"][k]
        for i in range(256):
            r, c = divmod(i, 16)
            im[r * chip + 1:(r + 1) * chip - 1, c * chip + 1:(c + 1) * chip - 1] = panel_rgb(np.array([[pal[i]]]))[0, 0]
            if i in starts:
                im[r * chip + 3:r * chip + 7, c * chip + 3:c * chip + 7] = (255, 0, 255)
            if i == FIXED_N:
                im[r * chip + 3:r * chip + 7, c * chip + 3:c * chip + 7] = (0, 200, 255)
            if i >= a["used_n"][k]:
                im[r * chip + 12:r * chip + 16, c * chip + 12:c * chip + 16] = (128, 128, 128)
        Image.fromarray(im).save(os.path.join(pdir, "palette_%s.png" % KINDS[k][0]))


def emit_c8(a, states, renders, d):
    os.makedirs(d, exist_ok=True)
    for n, fb in enumerate(renders):
        out = np.zeros((H, 512), dtype=np.uint8)
        out[:, :W] = fb
        with open(os.path.join(d, "C%d.c8" % n), "wb") as f:
            f.write(out.tobytes())
    for k in range(8):
        with open(os.path.join(d, "clk_%s.pal" % KINDS[k][0]), "wb") as f:
            f.write(np.array(a["pal"][k], dtype="<u4").tobytes())


# ------------------------------------------------------------------ C の生成物(設計 §13.8 の「生成物の名前」。株価の ui_* と同じ書式)
def carr(vals, per, fmt="%d"):
    return G.carr(vals, per, fmt)


def c_files(a, fonts, states):
    """clk_assets.h / clk_data.c / clk_preview.c の本文。名前はすべて clk_ / CLK_ で始める"""
    hdr = "/* Generated by tools/gen_clock_assets.py. 手で編集しない。font=%s sha256=%s */\n" % (os.path.basename(fonts.path), fonts.sha)
    out = {}
    names = [k[0] for k in KINDS]
    glyph_arr = {"F124": ("clk_g124", "CLK_G124_N"), "F72": ("clk_g72", "CLK_G72_N"), "F20": ("clk_g20", "CLK_G20_N")}
    idx20 = CH20.index
    # --- clk_assets.h ---
    t = hdr + "#ifndef CLK_ASSETS_H\n#define CLK_ASSETS_H\n\n#include <stdint.h>\n\n"
    t += "#include \"wifi_sw.h\"      /* APP_CLK_PREVIEW は手書きの wifi_sw.h が定義する */\n"
    t += "#include \"ui_assets.h\"    /* ui_glyph_t の型だけを借りる(株価の名前は足さない・変えない) */\n"
    t += "#if APP_CLK_PREVIEW\n#include \"clk_core.h\"\n#endif\n\n"
    t += "#define CLK_KINDS       (8)\n#define CLK_KEY         (%d)    /* スプライトの透明色 = パネルの添字 */\n#define CLK_FIXED_N     (%d)   /* パレットの前半(全天気で同じ配置) */\n" % (KEY_IDX, FIXED_N)
    t += "#define CLK_G124_N      (%d)\n#define CLK_G72_N       (%d)\n#define CLK_G20_N       (%d)\n#define CLK_PREVIEW_N   (%du)\n\n" % (len(CH124), len(CH72), len(CH20), NSTATE)
    t += "/* 座標(設計 §13.4 の表。動かすのは gen_clock_assets.py の表) */\n"
    t += "#define CLK_PANEL_X     (%d)\n#define CLK_PANEL_Y     (%d)\n#define CLK_PANEL_W     (%d)\n#define CLK_PANEL_H     (%d)\n" % PANEL
    t += "#define CLK_CX          (%d)\n#define CLK_TIME_BASE   (%d)\n#define CLK_DATE_BASE   (%d)\n#define CLK_ICON_Y      (%d)\n#define CLK_ICON_W      (%d)\n" % (
        CX, TIME_BASE, DATE_BASE, ICON_Y, ICON_W)
    t += "#define CLK_TEMP_BASE   (%d)\n#define CLK_NAME_BASE   (%d)\n#define CLK_GAP         (%d)\n\n" % (TEMP_BASE, NAME_BASE, GAP)
    t += "/* ramp の番号(clk_ramp[k])。添字は全天気で同じ */\n#define CLK_R_BGW       (%d)\n#define CLK_R_PNW       (%d)\n#define CLK_R_CORN      (%d)\n\n" % (R_BGW, R_PNW, R_CORN)
    t += "/* 字の番号。数字 0〜9 の字番号は数字そのもの(F124・F72・F20 とも先頭の 10 字) */\n"
    t += "#define CLK_G124_COLON  (%d)\n#define CLK_G124_DASH   (%d)\n" % (CH124.index(":"), CH124.index("-"))
    t += "#define CLK_G72_MINUS   (%d)\n#define CLK_G72_DEG     (%d)\n#define CLK_G72_DASH    (%d)\n" % (CH72.index("−"), CH72.index("°"), CH72.index("-"))
    t += "#define CLK_G20_MON     (%d)\n#define CLK_G20_DAY     (%d)\n#define CLK_G20_PAREN_L (%d)\n#define CLK_G20_PAREN_R (%d)\n" % (
        idx20("月"), idx20("日"), idx20("("), idx20(")"))
    t += "#define CLK_G20_SPACE   (%d)    /* 全角空白(字形なし。送り幅だけ) */\n#define CLK_G20_TOKYO0  (%d)\n#define CLK_G20_TOKYO1  (%d)\n#define CLK_G20_TOKYO2  (%d)\n\n" % (
        idx20("　"), idx20("東"), idx20("京"), idx20("都"))
    t += "#define CLK_WDAY_G20    { %s }    /* 日月火水木金土(wday 0〜6)の F20 の字番号 */\n\n" % ", ".join(str(idx20(c)) for c in WDAY)
    t += "typedef struct { const uint8_t *px; uint8_t w, h, ox, oy; } clk_icon_t;      /* index スプライト(外接矩形)と、78 x 78 の箱の中の位置 */\n"
    t += "typedef struct { const uint8_t *ids; uint8_t n; } clk_seq_t;                  /* 字番号の並び */\n\n"
    t += "extern const uint8_t clk_glyph_pool[%d];\n" % len(a["pool"])
    t += "extern const ui_glyph_t clk_g124[CLK_G124_N];\nextern const ui_glyph_t clk_g72[CLK_G72_N];\nextern const ui_glyph_t clk_g20[CLK_G20_N];\n"
    t += "extern const uint8_t clk_corner[4][256];\nextern const int16_t clk_corner_x[4];\nextern const int16_t clk_corner_y[4];\n"
    t += "extern const uint8_t clk_ramp[3][33];\nextern const uint32_t clk_pal[CLK_KINDS][256];     /* 0xFFRRGGBB。pal_id = 1 + kind */\n"
    t += "extern const clk_icon_t clk_icon[CLK_KINDS];\nextern const clk_seq_t clk_name_seq[CLK_KINDS];      /* 天気名の F20 の字番号 */\n"
    t += "\n#if APP_CLK_PREVIEW\nextern const clk_view_t clk_preview_view[CLK_PREVIEW_N];\n#endif\n\n#endif /* CLK_ASSETS_H */\n"
    out["clk_assets.h"] = t
    # --- clk_data.c ---
    t = hdr + "#include \"clk_assets.h\"\n\n"
    t += "const uint8_t clk_glyph_pool[%d] = {\n%s\n};\n\n" % (len(a["pool"]), carr(list(a["pool"]), 24, "0x%02X"))
    for fid, (arr, macro) in glyph_arr.items():
        t += "const ui_glyph_t %s[%s] = {\n" % (arr, macro)
        for (off, gw, gh, bx, by, ad) in a["glyphs"][fid]:
            assert -128 <= bx < 128 and -128 <= by < 128 and gw < 256 and gh < 256 and ad < 256
            t += "    { %du, %d, %d, %d, %d, %d },\n" % (off, gw, gh, bx, by, ad)
        t += "};\n\n"
    t += "const uint8_t clk_corner[4][256] __attribute__((aligned(4))) = {\n"
    for i, c in enumerate(a["corner"]):
        t += "    { /* %d */\n%s\n    },\n" % (i, carr(c.ravel().tolist(), 32))
    t += "};\n\nconst int16_t clk_corner_x[4] = { %s };\nconst int16_t clk_corner_y[4] = { %s };\n\n" % (
        ", ".join(str(c[0]) for c in CORNERS), ", ".join(str(c[1]) for c in CORNERS))
    t += "const uint8_t clk_ramp[3][33] = {\n"
    for k in range(3):
        t += "    { %s }, /* %s */\n" % (",".join(str(v) for v in RAMP[k]), RAMP_NAMES[k])
    t += "};\n\n/* パレット 8 枚(0xFFRRGGBB。RGB565 の各色を 8bit に広げた値。前半 98 個は全天気で同じ配置、後半はアイコンの縁の色) */\n"
    t += "const uint32_t clk_pal[CLK_KINDS][256] __attribute__((aligned(4))) = {\n"
    for k in range(8):
        t += "    { /* %d %s */\n%s\n    },\n" % (k, names[k], carr(a["pal"][k], 8, "0x%08Xu"))
    t += "};\n\n"
    for k in range(8):
        arr, ox, oy = a["sprite"][k]
        t += "static const uint8_t clk_spr_%s[%d] __attribute__((aligned(4))) = {\n%s\n};\n" % (names[k], arr.size, carr(arr.ravel().tolist(), 32))
    t += "\nconst clk_icon_t clk_icon[CLK_KINDS] = {\n"
    for k in range(8):
        arr, ox, oy = a["sprite"][k]
        assert arr.shape[0] < 256 and arr.shape[1] < 256 and ox < 256 and oy < 256
        t += "    { clk_spr_%s, %d, %d, %d, %d }, /* %s */\n" % (names[k], arr.shape[1], arr.shape[0], ox, oy, names[k])
    t += "};\n\n"
    for k in range(8):
        t += "static const uint8_t clk_nm_%s[] = { %s };\n" % (names[k], ", ".join(str(i) for i in name_seq(k)))
    t += "\nconst clk_seq_t clk_name_seq[CLK_KINDS] = {\n"
    for k in range(8):
        t += "    { clk_nm_%s, %d }, /* %s */\n" % (names[k], len(name_seq(k)), KINDS[k][1])
    t += "};\n"
    out["clk_data.c"] = t
    # --- clk_preview.c ---
    t = hdr + "#include \"clk_assets.h\"\n\ntypedef int clk_preview_unit_t;      /* APP_CLK_PREVIEW = 0 でも空の翻訳単位にしない */\n\n#if APP_CLK_PREVIEW\n"
    t += "/* 設計 §13.10 の 12 状態(tests/host/data/states/C0〜C11.state から生成) */\nconst clk_view_t clk_preview_view[CLK_PREVIEW_N] = {\n"
    for n, v in enumerate(states):
        have = v["clk.have"]
        t += "    { /* C%d */ .clock_min = %du, .kind = %d, .have = %d, .temp = %d, .mon = %d, .day = %d, .wday = %d },\n" % (
            n, v["clk.clock"], kind_of(v), have, v["clk.temp"] if have else 0, v["clk.mon"], v["clk.day"], v["clk.wday"])
    t += "};\n#endif\n"
    out["clk_preview.c"] = t
    return out


def write_c(a, fonts, states, d):
    os.makedirs(d, exist_ok=True)
    for name, text in c_files(a, fonts, states).items():
        with open(os.path.join(d, name), "w", newline="\n", encoding="utf-8") as f:
            f.write(text)


# ------------------------------------------------------------------ main
def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out-dir", default=os.path.join(here, "..", "src", "app"), help="--emit-c の出力先(既定 src/app)")
    ap.add_argument("--emit-c", action="store_true", help="C の生成物(clk_assets.h clk_data.c clk_preview.c)を --out-dir に書く")
    ap.add_argument("--preview-dir", default=os.path.join(here, "assets", "clock", "preview"))
    ap.add_argument("--states-dir", default=os.path.join(here, "..", "tests", "host", "data", "states"))
    ap.add_argument("--emit-c8", default=None, metavar="DIR")
    ap.add_argument("--font", default=G.DEFAULT_FONT)
    ap.add_argument("--check", action="store_true", help="ファイルを書かず、検査と表示だけ")
    args = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    if not os.path.exists(args.font):
        print("フォントが見つからない: %s (--font で指定)" % args.font, file=sys.stderr)
        return 2
    states = []
    for n in range(NSTATE):
        p = os.path.join(args.states_dir, "C%d.state" % n)
        if not os.path.exists(p):
            print("状態ファイルが無い: %s" % p, file=sys.stderr)
            return 2
        states.append(parse_state(p))
    fonts = Fonts(args.font)
    a = build(fonts)
    renders = [render(a, v) for v in states]
    dg = digest(a, renders)
    a2 = build(fonts)                                      # 再実行で同じか(乱数・時刻なし)
    dg2 = digest(a2, [render(a2, v) for v in states])
    errs, warns, info, nmax = check_all(a, fonts, states, renders)
    if dg != dg2:
        errs.append("2 回の生成で出力が違う(%s / %s)" % (dg[:12], dg2[:12]))
    print("フォント: %s sha256=%s" % (os.path.basename(args.font), fonts.sha))
    print("パレット 8 枚: 前半 %d 個(背景・パネル・ramp 3 本)は全天気で同じ配置・誤差 0。後半はアイコンの色。使う添字 %s / 256(旧設計は 226)" % (
        FIXED_N, " ".join("%s=%d" % (KINDS[k][0], a["used_n"][k]) for k in range(8))))
    print("アイコンの縁(濃度が 0.004〜0.996 の画素)の質。理想 = %dx%d の副格子・混色 %s・RGB565 丸め。誤差 = RGB565 の段(3 チャンネルの最大差)。前 = 旧方式(17 段の濃度 → 33 段 ramp)を同じ形に当てた値" % (K, K, BLEND))
    for e in info["edge"]:
        print("  %-8s 縁 %4d 画素 / 理想の色数 %3d  前: 最大 %d 平均 %.3f(sRGB 理想比 %d / %.3f) 色数 %3d  後: 最大 %d 平均 %.3f 色数 %3d" % (
            e["kind"], e["n"], e["ideal_col"], e["b_max"], e["b_mean"], e["bs_max"], e["bs_mean"], e["b_col"], e["a_max"], e["a_mean"], e["a_col"]))
    tn = sum(e["n"] for e in info["edge"])
    print("  全体     縁 %4d 画素          前: 最大 %d 平均 %.3f 色数 計 %d  後: 最大 %d 平均 %.3f 色数 計 %d" % (
        tn, max(e["b_max"] for e in info["edge"]), sum(e["b_mean"] * e["n"] for e in info["edge"]) / tn, sum(e["b_col"] for e in info["edge"]),
        max(e["a_max"] for e in info["edge"]), sum(e["a_mean"] * e["n"] for e in info["edge"]) / tn, sum(e["a_col"] for e in info["edge"])))
    for n, d in info["adv"].items():
        print("送り幅 %s: " % n + " ".join("%s=%d" % (c.replace("−", "−"), w) for c, w in d.items() if not c.isdigit()) + " 数字=%d" % d["0"])
    print("コントラスト(WCAG。RGB565 後の色。時刻・日付は白/背景 >= 3.0、気温・天気名は白/パネル >= 4.5、アイコン >= 3.0):")
    for r in info["contrast"]:
        print("  %-8s 背景 %s 白/背景 %5.2f  パネル %s 白/パネル %5.2f  雲 %5.2f 太陽 %5.2f 雨粒 %5.2f 稲妻 %5.2f" % (
            r["kind"], r["bg"], r["wbg"], r["panel"], r["wpn"], r["cloud"], r["sun"], r["drop"], r["bolt"]))
    print("くもりと霧の背景色の距離(RGB)= %d" % info["fog_cloud_dist"])
    print("12 状態の配置(x0 = アイコンの左、G = アイコン + 気温 + 天気名 の全幅):")
    for n, rc, G_, x0 in info["rects"]:
        print("  C%-2d x0=%3d G=%3d  時刻 x %3d..%3d y %3d..%3d  日付 x %3d..%3d y %3d..%3d  気温 x %3d..%3d  天気名 x %3d..%3d" % (
            n, x0, G_, rc["時刻"][0], rc["時刻"][2], rc["時刻"][1], rc["時刻"][3], rc["日付・地名"][0], rc["日付・地名"][2],
            rc["日付・地名"][1], rc["日付・地名"][3], rc["気温"][0], rc["気温"][2], rc["天気名"][0], rc["天気名"][2]))
    rows = rom_table(a)
    print("ROM(アセット): " + " / ".join("%s %d" % r for r in rows) + " = %d バイト(旧版 117,202。設計 §13.12 の見積は約 117,000)" % sum(r[1] for r in rows))
    print("出力の sha256(字・部品・角・パレット 8 枚・12 状態の描画): %s(2 回の生成で %s)" % (dg, "一致" if dg == dg2 else "不一致"))
    for w in warns:
        print("警告:", w)
    if errs:
        for e in errs:
            print("NG:", e, file=sys.stderr)
        return 1
    print("検査 OK")
    if args.check:
        return 0
    write_previews(a, fonts, states, renders, args.preview_dir)
    print("プレビュー: %s" % os.path.normpath(args.preview_dir))
    if args.emit_c8:
        emit_c8(a, states, renders, args.emit_c8)
        print(".c8: %s" % os.path.normpath(args.emit_c8))
    if args.emit_c:
        write_c(a, fonts, states, args.out_dir)
        print("C: %s(clk_assets.h clk_data.c clk_preview.c)" % os.path.normpath(args.out_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
