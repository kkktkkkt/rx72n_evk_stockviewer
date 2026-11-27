#!/usr/bin/env python3
"""gen_clock_assets.py - 時計・天気画面(docs/design.md §13、Phase 6)のアセットとプレビューの生成。

  python tools/gen_clock_assets.py                 # プレビュー PNG を tools/assets/clock/preview に出す
  python tools/gen_clock_assets.py --check         # ファイルを書かない。検査と ROM の内訳の表示だけ(失敗 = 終了コード 1)
  --preview-dir DIR   既定 tools/assets/clock/preview
  --states-dir DIR    既定 tests/host/data/states(C0.state〜C11.state。設計 §13.10)
  --emit-c8 DIR       12 状態を Python の描画のまま 512x272x8bit の C0.c8〜C11.c8(と clk_<kind>.pal)に書く(H-C7 ⑤ の突き合わせ用)
  --font PATH         既定 C:\\Windows\\Fonts\\NotoSansJP-VF.ttf

色・座標・形はすべてこのファイルの表(設計 §13.2〜§13.4)。乱数・時刻を使わない(再実行で同じ出力)。Pillow と numpy が必要。
プレビューの画素は、パネルに出る色(パレットの RGB565 を RGB888 へ複製)。--emit-c(C の生成物)は、プレビュー承認(V-C0)の後。
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

# ---- ramp(設計 §13.2)。k = 0..6。ramp の添字 = 2 + 32 k + (l - 1)、l = 0 は 0(描かない)----
R_BGW, R_PNW, R_CLOUD, R_SUN, R_DROP, R_BOLT, R_CORN = range(7)
RAMP_NAMES = ("R_BGW", "R_PNW", "R_CLOUD", "R_SUN", "R_DROP", "R_BOLT", "R_CORN")
USED_N = 2 + 32 * 7                                    # 226


def ramp_idx(k):
    return [0] + [2 + 32 * k + (l - 1) for l in range(1, 33)]


RAMP = [ramp_idx(k) for k in range(7)]

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
    """256 個の RGB565。添字の配置は全天気で同じ(設計 §13.2)。誤差 0(計算で作る)"""
    bg, pn = c565(KINDS[kind][2]), c565(panel_hex(KINDS[kind][2]))
    pal = np.full(256, bg, dtype=np.uint16)
    pal[1] = pn
    srcs = ((bg, c565(WHITE)), (pn, c565(WHITE)), (pn, c565(FG["CLOUD"])), (pn, c565(FG["SUN"])),
            (pn, c565(FG["DROP"])), (pn, c565(FG["BOLT"])), (pn, bg))
    for k, (b, f) in enumerate(srcs):
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


# ------------------------------------------------------------------ アイコン部品(設計 §13.4。箱 78x78、4 倍で描く)
def canvas():
    im = Image.new("L", (ICON_W * S, ICON_W * S), 0)
    return im, ImageDraw.Draw(im)


def circ(d, cx, cy, r):
    d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S - 1, (cy + r) * S - 1], fill=255)


def thick(d, x1, y1, x2, y2, wd):
    d.line([x1 * S, y1 * S, x2 * S, y2 * S], fill=255, width=int(round(wd * S)))
    circ(d, x1, y1, wd / 2.0)
    circ(d, x2, y2, wd / 2.0)


def rays(d, cx, cy, r0, r1, wd):
    for i in range(8):
        t = np.pi / 4 * i
        thick(d, cx + r0 * np.cos(t), cy + r0 * np.sin(t), cx + r1 * np.cos(t), cy + r1 * np.sin(t), wd)


def cloud(d, sc=1.0, ox=0.0, oy=0.0, grow=0.0):
    """雲。外接 x 8〜70、y 6〜50(sc = 1)。grow = 周りを膨らませる px(切り抜き用)"""
    def P(x, y):
        return x * sc + ox, y * sc + oy
    for (x, y, r) in ((27, 30, 12), (43, 22, 16), (58, 32, 11)):
        cx, cy = P(x, y)
        circ(d, cx, cy, r * sc + grow)
    x0, y0 = P(8, 30)
    x1, y1 = P(70, 50)
    d.rounded_rectangle([(x0 - grow) * S, (y0 - grow) * S, (x1 + grow) * S - 1, (y1 + grow) * S - 1], radius=(10 * sc + grow) * S, fill=255)


def part_sun():
    im, d = canvas()
    circ(d, 39, 39, 15)
    rays(d, 39, 39, 21, 32, 5)
    return im


def part_cloud():
    im, d = canvas()
    cloud(d)
    return im


def part_cloud_s():
    im, d = canvas()
    cloud(d, 0.85, 12, 20)
    return im


def part_sun_s():
    im, d = canvas()
    circ(d, 26, 26, 11)
    rays(d, 26, 26, 16, 23, 4)
    cut, dc = canvas()
    cloud(dc, 0.85, 12, 20, grow=3)                    # CLOUD_S を 3px 膨らませた形を切り抜く
    return ImageChops.subtract(im, cut)


def part_fog():
    im, d = canvas()
    for (y, x0, x1) in ((57, 10, 68), (64, 18, 60), (71, 10, 54)):
        thick(d, x0, y, x1, y, 4)
    return im


def part_drops():
    im, d = canvas()
    for x in (25, 40, 55):
        thick(d, x, 56, x - 4, 70, 5)
    return im


def part_dots():
    im, d = canvas()
    for (x, y) in ((25, 60), (40, 66), (55, 60)):
        circ(d, x, y, 4)
    return im


def part_bolt():
    im, d = canvas()
    pts = ((46, 52), (32, 66), (41, 66), (35, 77), (54, 60), (45, 60), (51, 52))
    d.polygon([(x * S, y * S) for x, y in pts], fill=255)
    return im


def part_qmark(fonts):
    im, d = canvas()
    d.text((39 * S, QMARK_BASE * S), "?", font=fonts.get(QMARK_PX, 700), fill=255, anchor="ms")   # 設計は 34px・ベースライン 76 だが雲の下端(50)に 5 画素重なる → 32px・77(雲と 2px あく)
    return im


def to_part(im4):
    """4 倍の画像 -> (A8、外接矩形に切り詰め、箱の中の位置 ox, oy)"""
    a = im4.resize((ICON_W, ICON_W), Image.BOX)
    bb = a.getbbox()
    return np.asarray(a.crop(bb), dtype=np.uint8).copy(), bb[0], bb[1]


# 種類 -> 層(下から。(部品, 置く dx, dy, ramp))
LAYERS = {0: (("SUN", 0, 0, R_SUN),),
          1: (("SUN_S", 0, 0, R_SUN), ("CLOUD_S", 0, 0, R_CLOUD)),
          2: (("CLOUD", 0, 12, R_CLOUD),),
          3: (("CLOUD", 0, 0, R_CLOUD), ("FOG_LINES", 0, 0, R_CLOUD)),
          4: (("CLOUD", 0, 0, R_CLOUD), ("DROPS", 0, 0, R_DROP)),
          5: (("CLOUD", 0, 0, R_CLOUD), ("DOTS", 0, 0, R_PNW)),
          6: (("CLOUD", 0, 0, R_CLOUD), ("BOLT", 0, 0, R_BOLT)),
          7: (("CLOUD", 0, 0, R_CLOUD), ("QMARK", 0, 0, R_PNW))}


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
    parts = {"SUN": part_sun(), "CLOUD": part_cloud(), "SUN_S": part_sun_s(), "CLOUD_S": part_cloud_s(), "FOG_LINES": part_fog(),
             "DROPS": part_drops(), "DOTS": part_dots(), "BOLT": part_bolt(), "QMARK": part_qmark(fonts)}
    a["parts"] = {k: to_part(v) for k, v in parts.items()}
    for k, (arr, ox, oy) in a["parts"].items():
        pool += arr.tobytes()
    a["pool"] = bytes(pool)
    a["corner"] = make_corners()
    a["pal565"] = [make_palette(k) for k in range(8)]
    a["pal"] = [[pal_argb(int(p)) for p in pal] for pal in a["pal565"]]
    return a


def digest(a, renders):
    h = hashlib.sha256()
    h.update(a["pool"])
    h.update(repr(sorted(a["glyphs"].items())).encode())
    h.update(repr(sorted((k, v[1], v[2]) for k, v in a["parts"].items())).encode())
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
    for (part, dx, dy, rk) in LAYERS[kind]:                                            # 6 アイコン
        arr, ox, oy = a["parts"][part]
        p.a8(arr, x0 + dx + ox, ICON_Y + dy + oy, RAMP[rk])
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
    ic = None
    for (part, dx, dy, rk) in LAYERS[kind]:
        arr, ox, oy = a["parts"][part]
        q = (x0 + dx + ox, ICON_Y + dy + oy, x0 + dx + ox + arr.shape[1], ICON_Y + dy + oy + arr.shape[0])
        ic = q if ic is None else (min(ic[0], q[0]), min(ic[1], q[1]), max(ic[2], q[2]), max(ic[3], q[3]))
    return {"時刻": ink(a, "F124", hhmm_seq(v["clk.clock"]), CX, TIME_BASE, CENTER),
            "日付・地名": ink(a, "F20", date_seq(v), CX, DATE_BASE, CENTER),
            "アイコン": ic,
            "気温": ink(a, "F72", temp_seq(v), x0 + ICON_W + GAP, TEMP_BASE, LEFT),
            "天気名": ink(a, "F20", name_seq(kind), x0 + ICON_W + GAP + tw + GAP, NAME_BASE, LEFT)}, G_, x0


def check_all(a, fonts, states, renders):
    errs, warns, info = [], [], {}
    # ① パレット: 226 色・誤差 0・§13.2 の配置・全天気で ramp の添字が同じ
    nmax = 0
    for k in range(8):
        pal = a["pal565"][k]
        bg, pn = c565(KINDS[k][2]), c565(panel_hex(KINDS[k][2]))
        if int(pal[0]) != bg or int(pal[1]) != pn:
            errs.append("パレット %s: 添字 0・1 が背景・パネルの RGB565 でない" % KINDS[k][0])
        for rk, (b, f) in enumerate(((bg, c565(WHITE)), (pn, c565(WHITE)), (pn, c565(FG["CLOUD"])), (pn, c565(FG["SUN"])),
                                      (pn, c565(FG["DROP"])), (pn, c565(FG["BOLT"])), (pn, bg))):
            for l in range(1, 33):
                if int(pal[RAMP[rk][l]]) != int(mix565(b, f, l)):          # 誤差 0(各段が mix565 と一致)
                    errs.append("パレット %s の %s の段 %d が mix565 と違う" % (KINDS[k][0], RAMP_NAMES[rk], l))
            if int(pal[RAMP[rk][32]]) != (f if rk != R_CORN else bg):
                errs.append("パレット %s の %s の l = 32 が前景そのものでない" % (KINDS[k][0], RAMP_NAMES[rk]))
        for i in range(USED_N, 256):
            if int(pal[i]) != bg:
                errs.append("パレット %s の添字 %d(未使用)が背景と同じ色でない" % (KINDS[k][0], i))
        for i in range(256):
            if a["pal"][k][i] != pal_argb(int(pal[i])) or (a["pal"][k][i] >> 24) != 0xFF:
                errs.append("clk_pal[%d][%d] が 0xFF000000 | RGB565 を 8bit に広げた値でない" % (k, i))
        nmax = max(nmax, len(set(int(x) for x in pal[:USED_N])))
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
    # ③ アイコンの層が重ならない・箱に収まる
    for kind, layers in LAYERS.items():
        full = []
        for (part, dx, dy, rk) in layers:
            arr, ox, oy = a["parts"][part]
            m = np.zeros((ICON_W, ICON_W), dtype=bool)
            y0, x0_ = oy + dy, ox + dx
            if x0_ < 0 or y0 < 0 or x0_ + arr.shape[1] > ICON_W or y0 + arr.shape[0] > ICON_W:
                errs.append("アイコン %s の部品 %s が 78x78 の箱からはみ出す" % (KINDS[kind][0], part))
                continue
            m[y0:y0 + arr.shape[0], x0_:x0_ + arr.shape[1]] = arr > 0
            full.append((part, m))
        for i in range(len(full)):
            for j in range(i + 1, len(full)):
                n = int((full[i][1] & full[j][1]).sum())
                if n:
                    errs.append("アイコン %s: 層 %s と %s が %d 画素重なる" % (KINDS[kind][0], full[i][0], full[j][0], n))
    # 角: 外の端 = 濃い、内の端 = 0
    cs = a["corner"]
    if not (cs[0][0, 0] >= 250 and cs[0][15, 15] == 0 and cs[1][0, 15] >= 250 and cs[2][15, 0] >= 250 and cs[3][15, 15] >= 250 and cs[3][0, 0] == 0):
        errs.append("パネルの角の濃度が想定と違う")
    # ④ 12 状態: 配置・はみ出し・重なり、使う添字
    worst_g, rect_rows = 0, []
    for n, v in enumerate(states):
        fb = renders[n]
        if int(fb.max()) >= USED_N:
            errs.append("C%d: 添字 %d(>= %d)を使っている" % (n, int(fb.max()), USED_N))
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
        row = dict(kind=nm, wbg=contrast(wh, bg), wpn=contrast(wh, pn),
                   cloud=contrast(hex565(int(pal[RAMP[R_CLOUD][32]])), pn), sun=contrast(hex565(int(pal[RAMP[R_SUN][32]])), pn),
                   drop=contrast(hex565(int(pal[RAMP[R_DROP][32]])), pn), bolt=contrast(hex565(int(pal[RAMP[R_BOLT][32]])), pn),
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
    shapes = sum(arr.size for arr, _, _ in a["parts"].values())
    return [("字 A8(F124・F72・F20)", pool - shapes), ("字の表(%d 個 x 12)" % (gt // 12), gt), ("アイコンの部品 %d 枚(外接矩形)" % len(a["parts"]), shapes),
            ("角 4 枚", 4 * 256), ("パレット 8 枚", 8 * 1024), ("ramp 7 本 x 33", 7 * 33)]


# ------------------------------------------------------------------ プレビュー(PNG)
def pil_rgb(fb, pal565):
    return Image.fromarray(panel_rgb(pal565[fb]))


def title_font(fonts, px=15, wg=500):
    f = ImageFont.truetype(fonts.path, px)
    f.set_variation_by_axes([wg])
    return f


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
        p = CP(a)
        p.fill(0, 0, ICON_W, ICON_W, 1)
        for (part, dx, dy, rk) in LAYERS[k]:
            arr, ox, oy = a["parts"][part]
            p.a8(arr, dx + ox, dy + oy, RAMP[rk])
        im = pil_rgb(p.fb[:ICON_W, :ICON_W], a["pal565"][k]).resize((sz, sz), Image.NEAREST)
        x, y = g2 + (k % 4) * (sz + g2), g2 + (k // 4) * (sz + g2 + 24)
        di.text((x, y), "%d %s %s" % (k, KINDS[k][0], KINDS[k][1]), font=ft, fill=(236, 239, 244))
        ic.paste(im, (x, y + 24))
    ic.save(os.path.join(pdir, "icons.png"))
    # palette_<kind>: 16 x 16。グループの先頭に白い点、未使用(226〜)は斜線の代わりに小さな × で印
    chip = 28
    starts = {0, 1} | {2 + 32 * k for k in range(7)}
    for k in range(8):
        im = np.full((16 * chip, 16 * chip, 3), 255, dtype=np.uint8)
        pal = a["pal565"][k]
        for i in range(256):
            r, c = divmod(i, 16)
            im[r * chip + 1:(r + 1) * chip - 1, c * chip + 1:(c + 1) * chip - 1] = panel_rgb(np.array([[pal[i]]]))[0, 0]
            if i in starts:
                im[r * chip + 3:r * chip + 7, c * chip + 3:c * chip + 7] = (255, 0, 255)
            if i >= USED_N:
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


# ------------------------------------------------------------------ main
def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
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
    print("パレット 8 枚: 使う添字 %d / 256(0〜%d)、各色は mix565 の計算で作る(誤差 0 段。k-means・丸めなし)、未使用 %d 個は背景色" % (
        USED_N, USED_N - 1, 256 - USED_N))
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
    print("ROM(アセット): " + " / ".join("%s %d" % r for r in rows) + " = %d バイト(設計 §13.12 の見積は約 117,000)" % sum(r[1] for r in rows))
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
    return 0


if __name__ == "__main__":
    sys.exit(main())
