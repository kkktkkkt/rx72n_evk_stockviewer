#!/usr/bin/env python3
"""gen_stock_assets.py - 株価ビュー(docs/design.md §3.11)の画面アセットとプレビューの生成。

  python tools/gen_stock_assets.py                 # プレビュー PNG を tools/assets/stock/preview に出す
  python tools/gen_stock_assets.py --emit-c        # 加えて C の生成物(ui_assets.h ui_layout.c ui_gfx_data.c ui_strips.c ui_preview.c)を src/app に書く
  python tools/gen_stock_assets.py --check         # ファイルを書かない。検査と ROM の内訳・誤差の表示だけ(失敗 = 終了コード 1)
  --preview-dir DIR   既定 tools/assets/stock/preview
  --states-dir DIR    既定 tests/host/data/states(S0.state〜S7.state。設計 §3.12)
  --emit-c8 DIR       8 状態を Python の描画のまま 512x272x8bit の S0.c8〜S7.c8(と stock.pal)に書く(H-8 ⑥ の突き合わせ用)
  --out-dir DIR       --emit-c の出力先(既定 src/app)
  --font PATH         既定 C:\\Windows\\Fonts\\NotoSansJP-VF.ttf

色・文言・座標はすべてこのファイルの表(設計 §3.2〜§3.7)。乱数・時刻を使わない(再実行で同じ出力)。Pillow と numpy が必要。
プレビューの描画は設計 §3.13 の規則(C の ui_render と同じ)。画素の色はパネルに出る色(RGB565 の切り捨てを RGB888 へ複製)。
"""
import argparse
import hashlib
import os
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

W, H, S = 480, 272, 4                   # 画面と、描画時の拡大率(4 倍で描いて縮小)
DEFAULT_FONT = "C:\\Windows\\Fonts\\NotoSansJP-VF.ttf"
HDR_H, ROW_H, FOOT_Y, FOOT_H = 28, 74, 250, 22
ROW_Y0 = (28, 102, 176)
UP, DOWN, FLAT = 0, 1, 2                # 色の添字 = dir(設計 §3.2)
CNAME = ("UP", "DOWN", "FLAT")

# ---- 色(設計 §3.2。配色はここだけ)----
C = dict(
    SCREEN_BG="#0B1220", HDR_BG="#16213A", SEP="#1F2D4D",
    UP_BAR="#D23B3B", DOWN_BAR="#1F9D57", FLAT_BAR="#556080",
    UP_BG="#191522", DOWN_BG="#0C1C24", FLAT_BG="#101727",
    NAME="#E8EEFC", PRICE="#F4F7FF", SUB="#8DA0C4",
    UP_T="#FF6B6B", DOWN_T="#41D17A", FLAT_T="#C9D3EA", DOT_T="#5FD38D", STALE="#7D8AA6",
    HDR_TXT="#E8EEFC", HDR_SUB="#9FB0D0", WARN="#F0B840", ERR="#FF6B6B",
    WIFI_ON="#5FD38D", WIFI_CONN="#F0B840", WIFI_STALE="#F0B840", WIFI_OFF="#FF6B6B")
BAR = ("UP_BAR", "DOWN_BAR", "FLAT_BAR")
BG = ("UP_BG", "DOWN_BG", "FLAT_BG")
ACC_FG = ("UP_T", "DOWN_T", "FLAT_T")      # R_ACC の前景(下地の色で決まる)

# ---- 文言(設計 §3.4、§3.5)----
ROW_NAME = ("ルネサス エレクトロニクス", "日経平均", "S&P 500")
ROW_CODE = ("6723 円", "N225 円", "SPX USD")
NAME_X, CODE_X, TEXT_BASE = 16, 188, 20          # 行の銘柄名・コードの x と、Y0 からのベースライン
HDR_TITLE = ("マーケット", 12, 19, 14, 500)      # 文字, x, ベースライン, px, 太さ
HDR_UPD = ("更新", 408, 19, 13, 400)
FOOT_L = ("10分ごとに自動更新(15分遅れ)", "起動中… 接続しています", "更新中…", "データが古くなっています",
          "オフライン: 接続できません。再試行します", "一部の銘柄を取得できませんでした")
FOOT_L_COL = ("HDR_SUB", "HDR_SUB", "HDR_SUB", "WARN", "ERR", "WARN")
FOOT_R = "タッチで今すぐ更新"
FOOT_L_X, FOOT_R_X, FOOT_BASE_ABS, FOOT_PX, FOOT_RIGHT_LIMIT = 12, 468, 265, 12, 360
WIFI_XY, WIFI_WH = (380, 4), (20, 20)
WIFI_STATES = ("ON", "CONN", "STALE", "OFF")      # アイコンの番号(設計 §3.9)

# ---- フォント・グリフ(設計 §3.6)。番号 0..18。C の文字列では ▲ = \x01、▼ = \x02、− = \x03 ----
GLYPH_CHARS = "0123456789,.+\u2212%\u25b2\u25bc:-"
NG = len(GLYPH_CHARS)                              # UI_GLYPHS = 19
GMAP = {"\x01": 15, "\x02": 16, "\x03": 13}
FONTS = (("F36", 36, 500, tuple(range(10)) + (10, 11, 18)),
         ("F30", 30, 700, tuple(range(10)) + (11, 12, 13, 14, 18)),
         ("F15", 15, 500, tuple(range(10)) + (10, 11, 15, 16, 18)),
         ("F18", 18, 500, tuple(range(10)) + (17, 18)),
         ("F13", 13, 400, tuple(range(10)) + (17, 18)))
FI = {f[0]: i for i, f in enumerate(FONTS)}
ARROW_GAP = 3                                      # ▲ ▼ の送り幅 = 字の幅 + 間隔 3px(設計 §3.6)
LEFT, CENTER, RIGHT = 0, 1, 2

# ---- スロット(設計 §3.7)。(名前, x, y(ベースライン。行はY0を足す), フォント, 揃え, 最大文字数)----
SLOTS = {"PRICE": (16, 62, "F36", LEFT, 10), "PCT": (468, 40, "F30", RIGHT, 8), "AMT": (468, 62, "F15", RIGHT, 12),
         "CLOCK": (240, 20, "F18", CENTER, 5), "UPD": (468, 19, "F13", RIGHT, 5)}

# ---- shape(A8。設計 §3.7)。(名前, 幅, 高さ, x, 上端のY0からの値, 文字)----
DOT_XY, STATUS_XY = (246, 11), (258, 8)
STATUS = (("TRADING", "取引中"), ("CLOSED", "終値"), ("FAIL", "取得失敗"))
STATUS_PX, STATUS_BASE = 12, 11                    # 12px の字を高さ 13 の A8 にする。ベースラインは上から 11 行目(= Y0 + 19。字の下端が銘柄名と同じ Y0 + 20 に揃う。12 行目だと下がはみ出す)

# ---- ramp(設計 §3.7)。種類 -> (前景の取り方) ----
RAMP_KINDS = ("PRICE", "STALE", "ACC", "DOTG", "SUB")

STATE_TITLE = ("S0 起動中", "S1 通常(上昇・下落・変化なし)", "S2 全上昇", "S3 全下落", "S4 古い", "S5 オフライン", "S6 一部失敗", "S7 最大幅")

M565 = np.uint32(0x07E0F81F)
DIST_W = np.array([1.0, 1.0, 0.6])                 # 距離の重み(R, G, B)。成分は RGB565 の段
KMEANS_MAX_IT = 60
FIXED_MIN_PX = 200
EXPECT_ROM = 415924                                # 設計 §3.10(アセット合計)
LIM_STEP = 3                                       # 最大誤差(RGB565 の段。設計 §3.8)


def hexrgb(h):
    h = C.get(h, h)
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


def rgb565(rgb):
    r, g, b = rgb
    return (((r * 31 + 127) // 255) << 11) | (((g * 63 + 127) // 255) << 5) | ((b * 31 + 127) // 255)


def c565(name):
    return rgb565(hexrgb(name))


def img565(im):
    p = np.asarray(im.convert("RGB"), dtype=np.uint32)
    return ((((p[..., 0] * 31 + 127) // 255) << 11) | (((p[..., 1] * 63 + 127) // 255) << 5)
            | ((p[..., 2] * 31 + 127) // 255)).astype(np.uint16)


def panel_rgb(p565):
    """RGB565 -> パネルに出る色(上位ビットを下位へ複製した RGB888。tools/c8png.py の to_panel と同じ)。(.., 3) uint8"""
    p = np.asarray(p565, dtype=np.uint32)
    return np.stack([((p >> 11) << 3) | ((p >> 13) & 7), (((p >> 5) & 63) << 2) | ((p >> 9) & 3), ((p & 31) << 3) | ((p >> 2) & 7)],
                    axis=-1).astype(np.uint8)


def pal_argb(p):
    r, g, b = (int(x) for x in panel_rgb(np.array([p]))[0])
    return 0xFF000000 | (r << 16) | (g << 8) | b


def mix565(bg, fg, a5):
    """gfx.c の mix565 と同じ計算(uint32 の巻き戻りも同じ)。a5 = 0..32。配列可"""
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


def step_err(ref565, got565):
    """画素ごとの誤差 = 3 チャンネルの段の差の最大(RGB565 の 1 段)"""
    ref, got = np.asarray(ref565).astype(np.int32), np.asarray(got565).astype(np.int32)
    return np.maximum(np.maximum(abs((ref >> 11) - (got >> 11)), abs(((ref >> 5) & 63) - ((got >> 5) & 63))), abs((ref & 31) - (got & 31)))


# ------------------------------------------------------------------ フォント
class Fonts:
    def __init__(self, path):
        self.path, self.cache = path, {}
        with open(path, "rb") as f:
            self.sha = hashlib.sha256(f.read()).hexdigest()

    def get(self, px, wght, scale=S):
        key = (px, wght, scale)
        if key not in self.cache:
            f = ImageFont.truetype(self.path, int(round(px * scale)))
            f.set_variation_by_axes([wght])
            self.cache[key] = f
        return self.cache[key]

    def width(self, text, px, wght):
        return self.get(px, wght).getlength(text) / S

    def missing(self, text, px=24, wght=400):
        """フォントに無い文字(.notdef と同じ画素になる字)の一覧"""
        f = self.get(px, wght, 1)

        def render(ch):
            im = Image.new("L", (px * 2, px * 2), 0)
            ImageDraw.Draw(im).text((4, 4), ch, font=f, fill=255)
            return im.tobytes()
        nd = render("\uFFFE")
        return [ch for ch in sorted(set(text)) if ch != " " and render(ch) == nd]


class Cv:
    """4 倍で描いて BOX で縮小する小さなキャンバス(座標は 1 倍)"""

    def __init__(self, w, h, bg, fonts):
        self.w, self.h, self.fonts = w, h, fonts
        self.im = Image.new("RGB", (w * S, h * S), hexrgb(bg))
        self.d = ImageDraw.Draw(self.im)

    def rect(self, x, y, w, h, col):
        self.d.rectangle([x * S, y * S, (x + w) * S - 1, (y + h) * S - 1], fill=hexrgb(col))

    def text(self, x, y, s, px, wght, col, anchor="l"):
        self.d.text((x * S, y * S), s, font=self.fonts.get(px, wght), fill=hexrgb(col), anchor=anchor + "s")

    def circle(self, cx, cy, r, col):
        self.d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S - 1, (cy + r) * S - 1], fill=hexrgb(col))

    def line(self, x1, y1, x2, y2, wd, col):
        self.d.line([x1 * S, y1 * S, x2 * S, y2 * S], fill=hexrgb(col), width=round(wd * S))
        for (x, y) in ((x1, y1), (x2, y2)):
            self.circle(x, y, wd / 2, col)

    def arc(self, cx, cy, r, a0, a1, wd, col):
        self.d.arc([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], a0, a1, fill=hexrgb(col), width=round(wd * S))

    def done(self):
        return img565(self.im.resize((self.w, self.h), Image.BOX))


# ------------------------------------------------------------------ 帯・ヘッダー・フッター・アイコン
def make_strip(fonts, r, c):
    cv = Cv(W, ROW_H, BG[c], fonts)
    cv.rect(0, 0, 6, ROW_H, BAR[c])
    cv.rect(0, ROW_H - 1, W, 1, "SEP")                # 区切り線(最下行 1px。バーの列も含む。設計 §3.1)
    cv.text(NAME_X, TEXT_BASE, ROW_NAME[r], 13, 500, "NAME")
    cv.text(CODE_X, TEXT_BASE, ROW_CODE[r], 12, 400, "SUB")
    return cv.done()


def make_hdr(fonts):
    cv = Cv(W, HDR_H, "HDR_BG", fonts)
    t, x, y, px, wg = HDR_TITLE
    cv.text(x, y, t, px, wg, "HDR_TXT")
    t, x, y, px, wg = HDR_UPD
    cv.text(x, y, t, px, wg, "HDR_SUB")
    return cv.done()


def make_foot(fonts, f):
    cv = Cv(W, FOOT_H, "HDR_BG", fonts)
    base = FOOT_BASE_ABS - FOOT_Y
    cv.text(FOOT_L_X, base, FOOT_L[f], FOOT_PX, 400, FOOT_L_COL[f])
    cv.text(FOOT_R_X, base, FOOT_R, FOOT_PX, 400, "HDR_SUB", "r")
    return cv.done()


def make_wifi(state, fonts):
    """ヘッダーの地に焼き込んだ 20x20。扇形の弧 3 本 + 点(CONN は弧 2 本)。OFF は取り消し線"""
    cv = Cv(20, 20, "HDR_BG", fonts)
    col = {"ON": "WIFI_ON", "CONN": "WIFI_CONN", "STALE": "WIFI_STALE", "OFF": "WIFI_OFF"}[state]
    cx, cy = 10, 17
    for r in ((5.2, 9.2) if state == "CONN" else (5.2, 9.2, 13.2)):
        cv.arc(cx, cy, r, 227, 313, 2.0, col)
    cv.circle(cx, cy, 1.6, col)
    if state == "OFF":
        cv.line(3, 3, 17, 17.5, 1.8, "WIFI_OFF")
    return cv.done()


# ------------------------------------------------------------------ A8: グリフ・shape
def half_up(x):
    return int(np.floor(x + 0.5))


def build_font(fonts, px, wght, used):
    """19 グリフを (幅, 高さ, bx, by, adv, A8 の bytes) にする。使わない字は空(幅 0)。数字の送り幅は最大値にそろえ、字は送り幅の中で左右中央"""
    f4 = fonts.get(px, wght)
    nat = {ch: f4.getlength(ch) / S for ch in GLYPH_CHARS}
    adv_digit = max(half_up(nat[ch]) for ch in "0123456789")
    out = []
    for k, ch in enumerate(GLYPH_CHARS):
        if k not in used:
            out.append((0, 0, 0, 0, 0, b""))
            continue
        adv = adv_digit if ch.isdigit() else half_up(nat[ch]) + (ARROW_GAP if ch in "\u25b2\u25bc" else 0)
        shift = (adv - nat[ch]) / 2 if ch.isdigit() else 0.0
        l, t, r, b = f4.getbbox(ch, anchor="ls")
        x0, y0 = int(np.floor(l / S + shift)) - 2, int(np.floor(t / S)) - 2
        x1, y1 = int(np.ceil(r / S + shift)) + 2, int(np.ceil(b / S)) + 2
        im = Image.new("L", ((x1 - x0) * S, (y1 - y0) * S), 0)
        ImageDraw.Draw(im).text(((-x0 + shift) * S, -y0 * S), ch, font=f4, fill=255, anchor="ls")
        a = im.resize((x1 - x0, y1 - y0), Image.BOX)
        bb = a.getbbox()
        if bb is None:
            raise SystemExit("空の字形: %r (%s)" % (ch, px))
        a = a.crop(bb)
        out.append((a.width, a.height, x0 + bb[0], -(y0 + bb[1]), adv, a.tobytes()))
    return adv_digit, out


def make_dot():
    im = Image.new("L", (8 * S, 8 * S), 0)
    ImageDraw.Draw(im).ellipse([0, 0, 8 * S - 1, 8 * S - 1], fill=255)
    return np.asarray(im.resize((8, 8), Image.BOX), dtype=np.uint8)


def make_status(fonts, text):
    """幅 = 12px x 文字数、高さ 13 の A8。ベースラインは上から STATUS_BASE 行目。字が枠からはみ出したら (a8, False)"""
    w, pad = STATUS_PX * len(text), 4
    im = Image.new("L", ((w + 2 * pad) * S, (13 + 2 * pad) * S), 0)
    ImageDraw.Draw(im).text((pad * S, (pad + STATUS_BASE) * S), text, font=fonts.get(STATUS_PX, 400), fill=255, anchor="ls")
    a = np.asarray(im.resize((w + 2 * pad, 13 + 2 * pad), Image.BOX), dtype=np.uint8)
    inside = a[pad:pad + 13, pad:pad + w]
    return inside.copy(), int(a.sum()) == int(inside.sum())


# ------------------------------------------------------------------ パレット(設計 §3.8)
def level_hist(arr):
    return np.bincount(((np.asarray(arr, dtype=np.int32) + 4) >> 3).ravel(), minlength=33).astype(np.float64)


def make_palette(wt, fixed):
    """固定色 + 重み付き k-means で 256 色。乱数なし。戻り値 = 256 個の RGB565(固定色が先頭)"""
    uni = np.nonzero(wt)[0]
    U, Wt = comps(uni), wt[uni]
    nfix = len(fixed)
    cent = comps(np.array(fixed, dtype=np.int32))
    mind = d2mat(U, cent).min(axis=1)
    while len(cent) < 256:
        i = int(np.argmax(Wt * mind))
        if Wt[i] * mind[i] <= 0:
            raise SystemExit("パレットを作れない: 色の集合が 256 色に足りない")
        cent = np.concatenate([cent, U[i:i + 1]], axis=0)
        mind = np.minimum(mind, d2mat(U, U[i:i + 1])[:, 0])
    hi = np.array([31.0, 63.0, 31.0])
    for _ in range(KMEANS_MAX_IT):
        asg = d2mat(U, cent).argmin(axis=1)
        new = cent.copy()
        for k in range(nfix, 256):
            m = asg == k
            if m.any():
                new[k] = np.clip(np.round((U[m] * Wt[m][:, None]).sum(axis=0) / Wt[m].sum()), 0, hi)
        if (new == cent).all():
            break
        cent = new
    pal = (cent[:, 0].astype(np.int32) << 11) | (cent[:, 1].astype(np.int32) << 5) | cent[:, 2].astype(np.int32)
    return np.array(list(pal[:nfix]) + sorted(int(v) for v in pal[nfix:]), dtype=np.uint16)


def make_lut(pal565):
    """RGB565(65536 通り)-> 最も近いパレットの添字(同点は小さい添字)"""
    Cc, P = comps(np.arange(65536)), comps(pal565)
    out = np.zeros(65536, dtype=np.uint8)
    for s in range(0, 65536, 4096):
        out[s:s + 4096] = d2mat(Cc[s:s + 4096], P).argmin(axis=1)
    return out


# ------------------------------------------------------------------ 全アセット
def build(fonts):
    a = {}
    a["strip565"] = [[make_strip(fonts, r, c) for c in range(3)] for r in range(3)]
    a["hdr565"] = make_hdr(fonts)
    a["foot565"] = [make_foot(fonts, f) for f in range(len(FOOT_L))]
    a["wifi565"] = [make_wifi(s, fonts) for s in WIFI_STATES]
    # グリフ
    pool, glyphs, adv = bytearray(), [], []
    for fid, px, wg, used in FONTS:
        ad, g = build_font(fonts, px, wg, used)
        adv.append(ad)
        lst = []
        for (w, h, bx, by, ax, data) in g:
            lst.append((len(pool), w, h, bx, by, ax))
            pool += data
        glyphs.append(lst)
    a["glyphs"], a["pool"], a["adv"] = glyphs, bytes(pool), adv
    # shape
    a["dot"] = make_dot()
    a["status"], a["status_ok"] = [], []
    for nm, t in STATUS:
        s, ok = make_status(fonts, t)
        a["status"].append(s)
        a["status_ok"].append(ok)
    # 色の集合と重み(設計 §3.8)
    wt = np.zeros(65536)
    fixed = set()
    imgs = [x for row in a["strip565"] for x in row] + [a["hdr565"]] + a["foot565"]
    for im in imgs:
        cnt = np.bincount(im.ravel(), minlength=65536)
        wt += cnt
        fixed |= set(np.nonzero(cnt >= FIXED_MIN_PX)[0].tolist())
    for im in a["wifi565"]:
        cnt = np.bincount(im.ravel(), minlength=65536)
        wt += cnt * 16.0                                   # 小さい絵は縁の色が割り当たるよう重く
        fixed |= set(np.nonzero(cnt >= 40)[0].tolist())
    for nm in C:                                          # §3.2 の色は(画面に出ない SCREEN_BG を除き)すべて厳密に残す
        if nm != "SCREEN_BG":
            fixed.add(c565(nm))
    a["ramp_src"] = ramp_sources()
    ghist = [sum((level_hist(np.frombuffer(a["pool"][o:o + w * h], dtype=np.uint8)) for (o, w, h, *_ ) in a["glyphs"][i]), np.zeros(33))
             for i in range(len(FONTS))]
    use = {"PRICE": ghist[FI["F36"]] * 0.6, "STALE": ghist[FI["F36"]] * 0.3 + ghist[FI["F30"]] * 0.3,
           "ACC": ghist[FI["F30"]] * 0.6 + ghist[FI["F15"]] * 0.6, "DOTG": level_hist(a["dot"]) * 3 + 20,
           "SUB": sum((level_hist(s) for s in a["status"]), np.zeros(33)), "HCLK": ghist[FI["F18"]] * 0.5, "HUPD": ghist[FI["F13"]] * 0.5}
    for key, (bgc, fgc) in a["ramp_src"].items():
        u = use[key[0]]
        for l in range(1, 33):
            wt[int(mix565(c565(bgc), c565(fgc), l))] += u[l] + 1.0
    a["wt"] = wt
    a["fixed"] = sorted(fixed)
    pal = make_palette(wt, a["fixed"])
    lut = make_lut(pal)
    a["pal565"], a["lut"] = pal, lut
    a["pal"] = [pal_argb(p) for p in pal]
    a["strip"] = [[lut[x] for x in row] for row in a["strip565"]]
    a["hdr"], a["foot"] = lut[a["hdr565"]], [lut[x] for x in a["foot565"]]
    a["wifi"] = [lut[x] for x in a["wifi565"]]
    a["ramp"] = {key: [int(lut[int(mix565(c565(bgc), c565(fgc), l))]) for l in range(33)] for key, (bgc, fgc) in a["ramp_src"].items()}
    a["ramp_ideal"] = {key: [int(mix565(c565(bgc), c565(fgc), l)) for l in range(33)] for key, (bgc, fgc) in a["ramp_src"].items()}
    return a


def ramp_sources():
    """ramp の (種類, 下地の色 0..2 / 0..1) -> (下地の色名, 前景の色名)。設計 §3.7"""
    d = {}
    for t in range(3):
        d[("PRICE", t)] = (BG[t], "PRICE")
        d[("STALE", t)] = (BG[t], "STALE")
        d[("ACC", t)] = (BG[t], ACC_FG[t])
        d[("DOTG", t)] = (BG[t], "DOT_T")
        d[("SUB", t)] = (BG[t], "SUB")
    d[("HCLK", 0)] = ("HDR_BG", "HDR_TXT")
    d[("HUPD", 0)] = ("HDR_BG", "HDR_SUB")
    return d


def digest(a, renders):
    h = hashlib.sha256()
    h.update(a["pal565"].tobytes())
    for row in a["strip"]:
        for x in row:
            h.update(x.tobytes())
    h.update(a["hdr"].tobytes())
    for x in a["foot"] + a["wifi"]:
        h.update(x.tobytes())
    h.update(a["pool"])
    h.update(repr(a["glyphs"]).encode())
    h.update(a["dot"].tobytes())
    for s in a["status"]:
        h.update(s.tobytes())
    h.update(repr(sorted(a["ramp"].items())).encode())
    for r in renders:
        h.update(r.tobytes())
    return h.hexdigest()


# ------------------------------------------------------------------ 状態と文字列(設計 §3.12、§4.2)
def parse_state(path):
    v = dict(net=0, busy=0, have_any=0, stale_all=0, partial=0, clock=65535, upd=65535)
    rows = [dict(have=0, stale=0, fail=0, mkt=0, price=0, diff=0, bp=0) for _ in range(3)]
    with open(path, encoding="utf-8") as f:
        for ln, line in enumerate(f, 1):
            line = line.split("#", 1)[0].strip()
            if not line:
                continue
            k, _, n = line.partition("=")
            k, n = k.strip(), int(n)
            if k[:1] == "r" and k[1:2] in "012" and k[2:3] == ".":
                if k[3:] not in rows[0]:
                    raise SystemExit("%s:%d: 未知のキー %s" % (path, ln, k))
                rows[int(k[1])][k[3:]] = n
            elif k in v:
                v[k] = n
            else:
                raise SystemExit("%s:%d: 未知のキー %s" % (path, ln, k))
    v["row"] = rows
    return v


def commas(n):
    return "{:,}".format(n)


def row_dir(r):
    return (UP if r["diff"] > 0 else DOWN if r["diff"] < 0 else FLAT) if r["have"] else FLAT


def fmt_price(p, yen):
    return commas((p + 50) // 100) if yen else "%s.%02d" % (commas(p // 100), p % 100)


def fmt_pct(bp, d):
    if d == FLAT:
        return "0.00%"
    return ("+" if d == UP else "\x03") + "%d.%02d%%" % (abs(bp) // 100, abs(bp) % 100)


def fmt_amt(diff, d, yen):
    a = abs(diff)
    if d == FLAT:
        return "0" if yen else "0.00"
    num = commas((a + 50) // 100) if yen else "%s.%02d" % (commas(a // 100), a % 100)
    return ("\x01" if d == UP else "\x02") + num


def foot_index(v):
    if v["net"] in (0, 3):
        return 4
    if not v["have_any"]:
        return 1
    if v["busy"]:
        return 2
    if v["stale_all"]:
        return 3
    if v["partial"]:
        return 5
    return 0


def icon_index(v):
    if v["net"] in (0, 3):
        return 3
    if v["net"] == 1:
        return 1
    if v["stale_all"]:
        return 2
    return 0


def hhmm(m):
    return "--:--" if m == 65535 else "%02d:%02d" % (m // 60, m % 60)


# ------------------------------------------------------------------ 描画(設計 §3.13。C の ui_render と同じ規則)
class Painter:
    def __init__(self, a):
        self.a = a
        self.fb = np.zeros((H, W), dtype=np.uint8)
        self.pool = np.frombuffer(a["pool"], dtype=np.uint8)

    def blit(self, src, x, y):
        h, w = src.shape
        x0, y0, x1, y1 = max(x, 0), max(y, 0), min(x + w, W), min(y + h, H)
        if x1 > x0 and y1 > y0:
            self.fb[y0:y1, x0:x1] = src[y0 - y:y1 - y, x0 - x:x1 - x]

    def a8(self, a8, x, y, ramp):
        h, w = a8.shape
        x0, y0, x1, y1 = max(x, 0), max(y, 0), min(x + w, W), min(y + h, H)
        if x1 <= x0 or y1 <= y0:
            return
        s = a8[y0 - y:y1 - y, x0 - x:x1 - x].astype(np.int32)
        lv = (s + 4) >> 3
        sub = self.fb[y0:y1, x0:x1]
        self.fb[y0:y1, x0:x1] = np.where(lv != 0, np.asarray(ramp, dtype=np.uint8)[lv], sub)

    def text(self, font, s, x, y, align, ramp):
        gl = self.a["glyphs"][FI[font]]
        ks = [GMAP[ch] if ch in GMAP else GLYPH_CHARS.index(ch) for ch in s if (ch in GMAP or ch in GLYPH_CHARS)]
        ks = [k for k in ks if gl[k][5] > 0]              # グリフ表に無い字は飛ばす(幅 0)
        wd = sum(gl[k][5] for k in ks)
        x -= wd if align == RIGHT else wd // 2 if align == CENTER else 0
        for k in ks:
            off, gw, gh, bx, by, ad = gl[k]
            if gw:
                self.a8(self.pool[off:off + gw * gh].reshape(gh, gw), x + bx, y - by, ramp)
            x += ad


def render(a, v):
    p = Painter(a)
    R = a["ramp"]
    p.blit(a["hdr"], 0, 0)                                                         # 1
    dirs = [row_dir(r) for r in v["row"]]
    for i in range(3):                                                              # 2
        p.blit(a["strip"][i][dirs[i]], 0, ROW_Y0[i])
    p.blit(a["foot"][foot_index(v)], 0, FOOT_Y)                                     # 3
    p.blit(a["wifi"][icon_index(v)], *WIFI_XY)                                      # 4
    x, y, f, al, _ = SLOTS["CLOCK"]                                                 # 5
    p.text(f, hhmm(v["clock"]), x, y, al, R[("HCLK", 0)])
    x, y, f, al, _ = SLOTS["UPD"]
    p.text(f, hhmm(v["upd"]), x, y, al, R[("HUPD", 0)])
    for i, r in enumerate(v["row"]):
        Y0, t, yen = ROW_Y0[i], dirs[i], i == 0
        dx, dy = DOT_XY
        sx, sy = STATUS_XY
        st = None                                                                   # 6: (shape 番号, 丸の ramp)
        if r["mkt"] == 1:
            st = (0, "DOTG")
        elif r["mkt"] == 2:
            st = (1, "STALE")
        elif r["fail"]:
            st = (2, "STALE")
        if st is not None:
            p.a8(a["dot"], dx, Y0 + dy, R[(st[1], t)])
            p.a8(a["status"][st[0]], sx, Y0 + sy, R[("SUB", t)])
        px_, py_, pf, pal_, _ = SLOTS["PRICE"]                                      # 7
        qx, qy, qf, qal, _ = SLOTS["PCT"]
        ax, ay, af, aal, _ = SLOTS["AMT"]
        if not r["have"]:
            p.text(pf, "---", px_, Y0 + py_, pal_, R[("STALE", t)])
            p.text(qf, "---", qx, Y0 + qy, qal, R[("STALE", t)])
        else:
            stale = r["stale"]
            p.text(pf, fmt_price(r["price"], yen), px_, Y0 + py_, pal_, R[("STALE" if stale else "PRICE", t)])
            p.text(qf, fmt_pct(r["bp"], t), qx, Y0 + qy, qal, R[("STALE" if stale else "ACC", t)])
            p.text(af, fmt_amt(r["diff"], t, yen), ax, Y0 + ay, aal, R[("STALE" if stale else "ACC", t)])
    return p.fb


# ------------------------------------------------------------------ 検査(設計 §3.11 --check)
def text_rect(a, font, s, x, y, align):
    """文字列の矩形 (x0, y0, x1, y1)。横 = 送り幅の合計、縦 = 字の外形(ベースライン y)"""
    gl = a["glyphs"][FI[font]]
    ks = [GMAP[ch] if ch in GMAP else GLYPH_CHARS.index(ch) for ch in s]
    wd = sum(gl[k][5] for k in ks)
    x0 = x - wd if align == RIGHT else x - wd // 2 if align == CENTER else x
    top = min(y - gl[k][4] for k in ks)
    bot = max(y - gl[k][4] + gl[k][2] for k in ks)
    return (x0, top, x0 + wd, bot)


def ink_rect(fonts, s, x, y, px, wght, anchor="l"):
    l, t, r, b = fonts.get(px, wght).getbbox(s, anchor=anchor + "s")
    return (x + int(np.floor(l / S)), y + int(np.floor(t / S)), x + int(np.ceil(r / S)), y + int(np.ceil(b / S)))


def overlap(p, q):
    return p[0] < q[2] and q[0] < p[2] and p[1] < q[3] and q[1] < p[3]


def check_all(a, fonts, states):
    errs, warns = [], []
    pal, lut = a["pal565"], a["lut"]
    # ① パレット
    if len(a["pal"]) != 256:
        errs.append("パレットが 256 エントリーでない")
    for i, p in enumerate(pal):
        if a["pal"][i] != pal_argb(p) or (a["pal"][i] >> 24) != 0xFF:
            errs.append("ui_pal[%d] が 0xFF000000 | RGB565 を 8bit に広げた値でない" % i)
    for c in a["fixed"]:
        if int(pal[lut[c]]) != c:
            errs.append("固定色 %04X が厳密でない" % c)
    imgs = [("帯 %s/%s" % (r, CNAME[c]), a["strip565"][r][c], a["strip"][r][c]) for r in range(3) for c in range(3)]
    imgs += [("ヘッダー", a["hdr565"], a["hdr"])] + [("フッター F%d" % f, a["foot565"][f], a["foot"][f]) for f in range(len(FOOT_L))]
    imgs += [("Wi-Fi %s" % WIFI_STATES[s], a["wifi565"][s], a["wifi"][s]) for s in range(4)]
    stats, mx_all = [], 0
    for nm, ref, idx in imgs:
        e = step_err(ref, pal[idx])
        stats.append((nm, e.size, int((e > 0).sum()), int((e > 1).sum()), int((e > 2).sum()), int(e.max())))
        mx_all = max(mx_all, int(e.max()))
    if mx_all > LIM_STEP:
        errs.append("画像の最大誤差 %d 段 > %d" % (mx_all, LIM_STEP))
    rmx, rbad = 0, 0
    for key, ideal in a["ramp_ideal"].items():
        e = step_err(np.array(ideal), pal[np.array(a["ramp"][key])])
        rmx = max(rmx, int(e.max()))
        rbad += int((e > 2).sum())
    if rmx > LIM_STEP:
        errs.append("ramp の最大誤差 %d 段 > %d" % (rmx, LIM_STEP))
    used = set()
    for _, _, idx in imgs:
        used |= set(np.unique(idx).tolist())
    for r in a["ramp"].values():
        used |= set(r[1:])
    unused = 256 - len(used)
    if unused:
        warns.append("使われないパレットのエントリー %d 個" % unused)
    # ② グリフ
    miss = fonts.missing(GLYPH_CHARS + "".join(ROW_NAME + ROW_CODE + FOOT_L + (FOOT_R, HDR_TITLE[0], HDR_UPD[0]) + tuple(t for _, t in STATUS)), 24, 400)
    if miss:
        errs.append("フォントに無い文字: %s" % "".join(miss))
    for i, (fid, *_r) in enumerate(FONTS):
        for k in FONTS[i][3]:
            if a["glyphs"][i][k][1] == 0:
                errs.append("%s のグリフ %r が空" % (fid, GLYPH_CHARS[k]))
    for i, ok in enumerate(a["status_ok"]):
        if not ok:
            errs.append("状態の文字 %s が shape の枠(幅 %d x 高さ 13)からはみ出す" % (STATUS[i][0], 12 * len(STATUS[i][1])))
    # ③ 最悪の幅の矩形(1 行の中。Y0 = 0)
    W36, W30, W15 = "F36", "F30", "F15"
    px_, py_, pf, pal_, _ = SLOTS["PRICE"]
    qx, qy, qf, qal, _ = SLOTS["PCT"]
    ax, ay, af, aal, _ = SLOTS["AMT"]
    rects = {
        "名前(最大)": ink_rect(fonts, ROW_NAME[0], NAME_X, TEXT_BASE, 13, 500),
        "コード(最大)": ink_rect(fonts, ROW_CODE[2], CODE_X, TEXT_BASE, 12, 400),
        "丸": (DOT_XY[0], DOT_XY[1], DOT_XY[0] + 8, DOT_XY[1] + 8),
        "状態の文字(最大)": (STATUS_XY[0], STATUS_XY[1], STATUS_XY[0] + 12 * 4, STATUS_XY[1] + 13),
        "現在値(最悪)": text_rect(a, pf, "100,000.00", px_, py_, pal_),
        "騰落率(最悪)": text_rect(a, qf, "\x03999.99%", qx, qy, qal),
        "前日比(最悪)": text_rect(a, af, "\x01999,999.99", ax, ay, aal),
    }
    names = list(rects)
    for i in range(len(names)):
        r = rects[names[i]]
        if r[0] < 0 or r[2] > W or r[1] < 0 or r[3] > ROW_H:
            errs.append("行の外にはみ出す: %s %s" % (names[i], r))
        for j in range(i + 1, len(names)):
            if overlap(r, rects[names[j]]):
                errs.append("重なる: %s %s と %s %s" % (names[i], r, names[j], rects[names[j]]))
    a["rects"] = rects
    # ヘッダー
    hr = {"マーケット": ink_rect(fonts, HDR_TITLE[0], HDR_TITLE[1], HDR_TITLE[2], HDR_TITLE[3], HDR_TITLE[4]),
          "時計(最悪)": text_rect(a, "F18", "88:88", *SLOTS["CLOCK"][:2], SLOTS["CLOCK"][3]),
          "アイコン": (WIFI_XY[0], WIFI_XY[1], WIFI_XY[0] + 20, WIFI_XY[1] + 20),
          "「更新」": ink_rect(fonts, HDR_UPD[0], HDR_UPD[1], HDR_UPD[2], HDR_UPD[3], HDR_UPD[4]),
          "更新時刻(最悪)": text_rect(a, "F13", "88:88", *SLOTS["UPD"][:2], SLOTS["UPD"][3])}
    hn = list(hr)
    for i in range(len(hn)):
        r = hr[hn[i]]
        if r[0] < 0 or r[2] > W or r[1] < 0 or r[3] > HDR_H:
            errs.append("ヘッダーの外にはみ出す: %s %s" % (hn[i], r))
        for j in range(i + 1, len(hn)):
            if overlap(r, hr[hn[j]]):
                errs.append("ヘッダーで重なる: %s %s と %s %s" % (hn[i], r, hn[j], hr[hn[j]]))
    a["hdr_rects"] = hr
    # ④ 帯の大きさ、フッターの左の文言が x 360 に届かない
    for r in range(3):
        for c in range(3):
            if a["strip"][r][c].shape != (ROW_H, W):
                errs.append("帯 %d/%d の大きさが 480x74 でない" % (r, c))
    if a["hdr"].shape != (HDR_H, W) or any(f.shape != (FOOT_H, W) for f in a["foot"]):
        errs.append("ヘッダー・フッターの大きさが違う")
    if sum(ROW_Y0[0:1]) != HDR_H or ROW_Y0[2] + ROW_H != FOOT_Y or FOOT_Y + FOOT_H != H:
        errs.append("領域の分割が 272 にならない")
    a["foot_w"] = []
    for f in range(len(FOOT_L)):
        r = ink_rect(fonts, FOOT_L[f], FOOT_L_X, FOOT_BASE_ABS - FOOT_Y, FOOT_PX, 400)
        a["foot_w"].append(r)
        if r[2] >= FOOT_RIGHT_LIMIT:
            errs.append("フッター F%d の左の文言が x %d に届く(右端 %d)" % (f, FOOT_RIGHT_LIMIT, r[2]))
        if r[1] < 0 or r[3] > FOOT_H:
            errs.append("フッター F%d の左の文言が高さ %d を超える %s" % (f, FOOT_H, r))
    fr = ink_rect(fonts, FOOT_R, FOOT_R_X, FOOT_BASE_ABS - FOOT_Y, FOOT_PX, 400, "r")
    a["foot_r"] = fr
    if fr[0] < FOOT_RIGHT_LIMIT - 1 or fr[1] < 0 or fr[3] > FOOT_H:
        errs.append("フッター右の文言が想定の範囲外 %s" % (fr,))
    # コントラスト(設計 §3.2)
    a["contrast"] = []
    for nm, fg, bgs, lim in CONTRAST:
        vals = [contrast(fg, b) for b in bgs]
        a["contrast"].append((nm, vals, lim))
        if min(vals) < lim:
            errs.append("コントラスト不足: %s %s < %.1f" % (nm, " / ".join("%.2f" % v for v in vals), lim))
    # 区切り線: 各帯の最下行がすべて SEP、その上の行は SEP でない。内容は Y0 + 70 まで(線と重ならない)
    for r in range(3):
        for c in range(3):
            st = a["strip565"][r][c]
            if not (st[ROW_H - 1] == c565("SEP")).all() or (st[ROW_H - 2] == c565("SEP")).any():
                errs.append("帯 %d/%s の区切り線(最下行 1px)が不正" % (r, CNAME[c]))
    for k, v in rects.items():
        if v[3] > ROW_H - 3:
            errs.append("行の内容が区切り線に近すぎる: %s y 終端 %d > %d" % (k, v[3], ROW_H - 3))
    # 暗い配色の色表(§3.2)と生成物の色が一致: 下地 3・バー 3・区切り線・ヘッダーの地の添字がパレットの固定色
    for c in range(3):
        for r in range(3):
            st = a["strip"][r][c]
            for nm, px in ((BG[c], st[10, 300]), (BAR[c], st[10, 2]), ("SEP", st[ROW_H - 1, 300]), ("SEP", st[ROW_H - 1, 2])):
                if int(pal[px]) != c565(nm):
                    errs.append("帯 %d/%s の %s の画素がパレットの固定色でない" % (r, CNAME[c], nm))
    if int(pal[a["hdr"][2, 2]]) != c565("HDR_BG") or int(pal[a["foot"][0][2, 2]]) != c565("HDR_BG"):
        errs.append("ヘッダー・フッターの地がパレットの固定色でない")
    rom = sum(r[1] for r in rom_table(a))
    if rom != EXPECT_ROM:
        errs.append("アセット合計 %d が設計の %d と違う" % (rom, EXPECT_ROM))
    # 状態ファイルの 8 枚が描ける(画素がはみ出さない)
    if len(states) != 8:
        errs.append("状態ファイルが 8 枚でない")
    return errs, warns, stats, (mx_all, rmx, rbad, len(used))


def lum(name):
    r, g, b = (((v / 255.0) / 12.92) if v / 255.0 <= 0.03928 else (((v / 255.0) + 0.055) / 1.055) ** 2.4 for v in hexrgb(name))
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def contrast(fg, bg):
    a, b = lum(fg), lum(bg)
    return (max(a, b) + 0.05) / (min(a, b) + 0.05)


# 設計 §3.2 のコントラスト表(文字は 4.5 以上、図形は 3 以上)。(名前, 前景, 地の一覧, 下限)
CONTRAST = (("NAME", "NAME", BG, 4.5), ("PRICE", "PRICE", BG, 4.5), ("SUB", "SUB", BG, 4.5), ("STALE", "STALE", BG, 4.5),
            ("UP_T", "UP_T", ("UP_BG",), 4.5), ("DOWN_T", "DOWN_T", ("DOWN_BG",), 4.5), ("FLAT_T", "FLAT_T", ("FLAT_BG",), 4.5),
            ("DOT_T(丸)", "DOT_T", BG, 3.0), ("HDR_TXT", "HDR_TXT", ("HDR_BG",), 4.5), ("HDR_SUB", "HDR_SUB", ("HDR_BG",), 4.5),
            ("WARN", "WARN", ("HDR_BG",), 4.5), ("ERR", "ERR", ("HDR_BG",), 4.5),
            ("Wi-Fi 緑", "WIFI_ON", ("HDR_BG",), 3.0), ("Wi-Fi 黄", "WIFI_STALE", ("HDR_BG",), 3.0), ("Wi-Fi 赤", "WIFI_OFF", ("HDR_BG",), 3.0))


def rom_table(a):
    pool = len(a["pool"])
    shp = 64 + sum(s.size for s in a["status"])
    rows = [("帯 9 枚", 9 * ROW_H * W), ("ヘッダー帯", HDR_H * W), ("フッター帯 6 枚", len(FOOT_L) * FOOT_H * W), ("グリフ A8", pool),
            ("グリフ表", len(FONTS) * NG * 12), ("shape", shp), ("Wi-Fi sprite 4 枚", 4 * 400), ("パレット", 1024),
            ("ramp %d 本" % len(a["ramp"]), len(a["ramp"]) * 33)]
    return rows


# ------------------------------------------------------------------ プレビュー(PNG)
def pil_rgb(fb, pal565):
    return Image.fromarray(panel_rgb(pal565[fb]))


def write_previews(a, fonts, renders, pdir):
    os.makedirs(pdir, exist_ok=True)
    pal = a["pal565"]
    for n, fb in enumerate(renders):
        pil_rgb(fb, pal).save(os.path.join(pdir, "state_S%d.png" % n))
    # sheet: 2 列 x 4 行。各面の上に題
    cap, gap = 24, 16
    tw, th = W, H + cap
    sheet = Image.new("RGB", (2 * tw + 3 * gap, 4 * th + 5 * gap), (52, 56, 64))
    dr = ImageDraw.Draw(sheet)
    ft = ImageFont.truetype(fonts.path, 15)
    ft.set_variation_by_axes([500])
    for n, fb in enumerate(renders):
        x, y = gap + (n % 2) * (tw + gap), gap + (n // 2) * (th + gap)
        dr.text((x, y + 4), STATE_TITLE[n], font=ft, fill=(236, 239, 244))
        sheet.paste(pil_rgb(fb, pal), (x, y + cap))
    sheet.save(os.path.join(pdir, "sheet.png"))
    # strips: 行 3 x 色 3
    g = 6
    st = Image.new("RGB", (3 * W + 4 * g, 3 * ROW_H + 4 * g), (52, 56, 64))
    for r in range(3):
        for c in range(3):
            st.paste(Image.fromarray(panel_rgb(pal[a["strip"][r][c]])), (g + c * (W + g), g + r * (ROW_H + g)))
    st.save(os.path.join(pdir, "strips.png"))
    # ヘッダー・フッター・アイコン(S2 の取得中 F2 などは 8 状態に出ないので、ここで確認する)
    parts = [a["hdr"]] + a["foot"]
    ft2 = Image.new("RGB", (W + 2 * g, HDR_H + len(a["foot"]) * FOOT_H + (len(parts) + 1) * g + 40 + g), (52, 56, 64))
    y = g
    for im in parts:
        ft2.paste(Image.fromarray(panel_rgb(pal[im])), (g, y))
        y += (HDR_H if im is a["hdr"] else FOOT_H) + g
    for i, wf in enumerate(a["wifi"]):
        ft2.paste(Image.fromarray(panel_rgb(pal[wf])).resize((40, 40), Image.NEAREST), (g + i * 44, y))
    ft2.save(os.path.join(pdir, "header_footer.png"))
    # palette: 16 x 16。固定色は左上に白い点
    chip = 28
    im = np.full((16 * chip, 16 * chip, 3), 255, dtype=np.uint8)
    fixed = set(a["fixed"])
    for i in range(256):
        r, c = divmod(i, 16)
        im[r * chip + 1:(r + 1) * chip - 1, c * chip + 1:(c + 1) * chip - 1] = panel_rgb(np.array([[pal[i]]]))[0, 0]
        if int(pal[i]) in fixed:
            im[r * chip + 3:r * chip + 7, c * chip + 3:c * chip + 7] = (255, 0, 255)
    Image.fromarray(im).save(os.path.join(pdir, "palette.png"))


def emit_c8(a, renders, d):
    os.makedirs(d, exist_ok=True)
    for n, fb in enumerate(renders):
        out = np.zeros((H, 512), dtype=np.uint8)
        out[:, :W] = fb
        with open(os.path.join(d, "S%d.c8" % n), "wb") as f:
            f.write(out.tobytes())
    with open(os.path.join(d, "stock.pal"), "wb") as f:       # ui_pal(256 x LE 0xAARRGGBB)。tools/c8png.py png に渡せる
        f.write(np.array(a["pal"], dtype="<u4").tobytes())


# ------------------------------------------------------------------ C の生成物(設計 §3.11 の出力)
def carr(vals, per, fmt="%d"):
    return "\n".join("    " + ",".join(fmt % v for v in vals[i:i + per]) + "," for i in range(0, len(vals), per))


def c_files(a, fonts, states):
    hdr = "/* Generated by tools/gen_stock_assets.py. 手で編集しない。font=%s sha256=%s */\n" % (os.path.basename(fonts.path), fonts.sha)
    out = {}
    slot_names = ["PRICE%d" % i for i in range(3)] + ["PCT%d" % i for i in range(3)] + ["AMT%d" % i for i in range(3)] + ["CLOCK", "UPD"]
    slot_tab = []
    for k in ("PRICE", "PCT", "AMT"):
        x, y, f, al, mx = SLOTS[k]
        slot_tab += [(x, ROW_Y0[r] + y, f, al, mx) for r in range(3)]
    slot_tab += [SLOTS["CLOCK"], SLOTS["UPD"]]
    shape_defs = [("DOT", a["dot"], DOT_XY)] + [(STATUS[i][0], a["status"][i], STATUS_XY) for i in range(3)]
    # --- ui_assets.h ---
    t = hdr + "#ifndef UI_ASSETS_H\n#define UI_ASSETS_H\n\n#include <stdint.h>\n\n"
    t += "#include \"wifi_sw.h\"      /* APP_STK_PREVIEW は手書きの wifi_sw.h が定義する(-DAPP_STK_PREVIEW=1 でも切り替え可) */\n\n"
    t += "#define UI_W            (480)\n#define UI_H            (272)\n#define UI_STRIDE       (512)   /* 面の行の長さ(バイト)。gfx.h の GFX_STRIDE と同じ */\n#define UI_PAL_N        (256u)\n"
    t += "#define UI_ROW_N        (3)\n#define UI_ROW_H        (%d)\n#define UI_HDR_H        (%d)\n#define UI_FOOT_Y       (%d)\n#define UI_FOOT_H       (%d)\n#define UI_FOOT_N       (%du)\n" % (
        ROW_H, HDR_H, FOOT_Y, FOOT_H, len(FOOT_L))
    t += "#define UI_GLYPHS       (%du)   /* 0-9 , . + − %% ▲ ▼ : - (設計 §3.6) */\n#define UI_PREVIEW_N    (8u)\n\n" % NG
    t += "enum { UI_C_UP = 0, UI_C_DOWN, UI_C_FLAT, UI_C_N };       /* 色の添字 = dir(帯・ramp) */\n"
    t += "enum { %s, UI_FONT_COUNT };\n" % ", ".join("UI_FONT_%s%s" % (f[0][1:], " = 0" if i == 0 else "") for i, f in enumerate(FONTS))
    t += "enum { UI_ALIGN_LEFT = 0, UI_ALIGN_CENTER, UI_ALIGN_RIGHT };\n"
    t += "enum { %s, UI_SL_COUNT };\n" % ", ".join("UI_SL_%s%s" % (n, " = 0" if i == 0 else "") for i, n in enumerate(slot_names))
    t += "enum { %s, UI_SH_COUNT };\n" % ", ".join("UI_SH_%s%s" % (n, " = 0" if i == 0 else "") for i, (n, _, _) in enumerate(shape_defs))
    t += "enum { %s, UI_R_KINDS };\n" % ", ".join("UI_R_%s%s" % (n, " = 0" if i == 0 else "") for i, n in enumerate(RAMP_KINDS))
    t += "enum { UI_RH_CLOCK = 0, UI_RH_UPD, UI_RH_N };\n"
    t += "#define UI_WIFI_X (%d)\n#define UI_WIFI_Y (%d)\n#define UI_WIFI_W (%d)\n#define UI_WIFI_H (%d)\n#define UI_WIFI_N (4u)   /* ON CONN STALE OFF */\n\n" % (*WIFI_XY, *WIFI_WH)
    t += "typedef struct { int16_t x, y; uint8_t font, align, maxch; } ui_slot_t;      /* y = ベースライン(画面の y) */\n"
    t += "typedef struct { uint32_t off; uint8_t w, h; int8_t bx; int8_t by; uint8_t adv; } ui_glyph_t;   /* by = ベースラインから字の上端までの高さ */\n"
    t += "typedef struct { const uint8_t *a8; uint8_t w, h; int16_t x, y; } ui_shape_t;    /* x = 画面の x、y = 行の上端からの距離 */\n\n"
    t += "extern const int16_t ui_row_y0[UI_ROW_N];\nextern const ui_slot_t ui_slots[UI_SL_COUNT];\n"
    t += "extern const ui_glyph_t ui_glyphs[UI_FONT_COUNT][UI_GLYPHS];\nextern const uint8_t ui_glyph_pool[%d];\n" % len(a["pool"])
    t += "extern const ui_shape_t ui_shapes[UI_SH_COUNT];\n"
    for n, d, _ in shape_defs:
        t += "extern const uint8_t ui_shp_%s[%d];\n" % (n.lower(), d.size)
    t += "extern const uint8_t ui_ramp[UI_R_KINDS][UI_C_N][33];\nextern const uint8_t ui_ramp_hdr[UI_RH_N][33];\n"
    t += "extern const uint32_t ui_pal[UI_PAL_N];     /* 0xFFRRGGBB。R_GLCDC_Open が 1 回だけ書く */\n"
    t += "extern const uint8_t ui_spr_wifi[UI_WIFI_N][UI_WIFI_W * UI_WIFI_H];\n"
    t += "extern const uint8_t * const ui_strip[UI_ROW_N][UI_C_N];   /* 各 480 x 74(行を詰めた 480 バイト幅) */\n"
    t += "extern const uint8_t ui_hdr[%d];\nextern const uint8_t ui_foot[UI_FOOT_N][%d];\n" % (HDR_H * W, FOOT_H * W)
    t += "\n#if APP_STK_PREVIEW\n#include \"stk_core.h\"\nextern const stk_view_t ui_preview_view[UI_PREVIEW_N];\n#endif\n\n#endif /* UI_ASSETS_H */\n"
    out["ui_assets.h"] = t
    # --- ui_layout.c ---
    t = hdr + "#include \"ui_assets.h\"\n\n"
    t += "const int16_t ui_row_y0[UI_ROW_N] = { %s };\n\n" % ", ".join(str(y) for y in ROW_Y0)
    t += "const ui_slot_t ui_slots[UI_SL_COUNT] = {\n"
    for nm, (x, y, f, al, mx) in zip(slot_names, slot_tab):
        t += "    { %d, %d, UI_FONT_%s, UI_ALIGN_%s, %d }, /* %s */\n" % (x, y, f[1:], ("LEFT", "CENTER", "RIGHT")[al], mx, nm)
    t += "};\n\nconst ui_glyph_t ui_glyphs[UI_FONT_COUNT][UI_GLYPHS] = {\n"
    for i, (fid, *_r) in enumerate(FONTS):
        t += "    { /* %s */\n" % fid
        for (off, gw, gh, bx, by, ad) in a["glyphs"][i]:
            assert -128 <= bx < 128 and -128 <= by < 128 and gw < 256 and gh < 256 and ad < 256
            t += "        { %du, %d, %d, %d, %d, %d },\n" % (off, gw, gh, bx, by, ad)
        t += "    },\n"
    t += "};\n\nconst ui_shape_t ui_shapes[UI_SH_COUNT] = {\n"
    for n, d, (x, y) in shape_defs:
        t += "    { ui_shp_%s, %d, %d, %d, %d }, /* %s */\n" % (n.lower(), d.shape[1], d.shape[0], x, y, n)
    t += "};\n\nconst uint8_t ui_ramp[UI_R_KINDS][UI_C_N][33] = {\n"
    for k in RAMP_KINDS:
        t += "    { /* %s */\n" % k
        for c in range(3):
            t += "        { %s },\n" % ",".join(str(v) for v in a["ramp"][(k, c)])
        t += "    },\n"
    t += "};\n\nconst uint8_t ui_ramp_hdr[UI_RH_N][33] = {\n"
    for k in ("HCLK", "HUPD"):
        t += "    { %s },\n" % ",".join(str(v) for v in a["ramp"][(k, 0)])
    t += "};\n\n/* パレット(0xFFRRGGBB。RGB565 の各色を 8bit に広げた値) */\n"
    t += "const uint32_t ui_pal[UI_PAL_N] __attribute__((aligned(4))) = {\n%s\n};\n" % carr(a["pal"], 8, "0x%08Xu")
    out["ui_layout.c"] = t
    # --- ui_gfx_data.c ---
    t = hdr + "#include \"ui_assets.h\"\n\n"
    t += "const uint8_t ui_glyph_pool[%d] = {\n%s\n};\n\n" % (len(a["pool"]), carr(list(a["pool"]), 24, "0x%02X"))
    for n, d, _ in shape_defs:
        t += "const uint8_t ui_shp_%s[%d] = {\n%s\n};\n" % (n.lower(), d.size, carr(d.ravel().tolist(), 24, "0x%02X"))
    t += "\nconst uint8_t ui_spr_wifi[UI_WIFI_N][UI_WIFI_W * UI_WIFI_H] __attribute__((aligned(4))) = {\n"
    for i, st in enumerate(WIFI_STATES):
        t += "    { /* %s */\n%s\n    },\n" % (st, carr(a["wifi"][i].ravel().tolist(), 20))
    t += "};\n"
    out["ui_gfx_data.c"] = t
    # --- ui_strips.c ---
    t = hdr + "#include \"ui_assets.h\"\n\n"
    for r in range(3):
        for c in range(3):
            t += "static const uint8_t strip_%d_%d[%d] __attribute__((aligned(4))) = {\n%s\n};\n" % (r, c, ROW_H * W, carr(a["strip"][r][c].ravel().tolist(), 48))
    t += "\nconst uint8_t * const ui_strip[UI_ROW_N][UI_C_N] = {\n"
    for r in range(3):
        t += "    { %s },\n" % ", ".join("strip_%d_%d" % (r, c) for c in range(3))
    t += "};\n\nconst uint8_t ui_hdr[%d] __attribute__((aligned(4))) = {\n%s\n};\n" % (HDR_H * W, carr(a["hdr"].ravel().tolist(), 48))
    t += "\nconst uint8_t ui_foot[UI_FOOT_N][%d] __attribute__((aligned(4))) = {\n" % (FOOT_H * W)
    for f in range(len(FOOT_L)):
        t += "    { /* F%d */\n%s\n    },\n" % (f, carr(a["foot"][f].ravel().tolist(), 48))
    t += "};\n"
    out["ui_strips.c"] = t
    # --- ui_preview.c ---
    t = hdr + "#include \"ui_assets.h\"\n\ntypedef int ui_preview_unit_t;      /* APP_STK_PREVIEW = 0 でも空の翻訳単位にしない */\n\n#if APP_STK_PREVIEW\n"
    t += "/* 設計 §3.12 の 8 状態(tests/host/data/states/S0〜S7.state から生成) */\nconst stk_view_t ui_preview_view[UI_PREVIEW_N] = {\n"
    for n, v in enumerate(states):
        t += "    { /* S%d */\n        .net = %d, .busy = %d, .have_any = %d, .stale_all = %d, .partial = %d,\n        .clock_min = %du, .upd_min = %du,\n        .row = {\n" % (
            n, v["net"], v["busy"], v["have_any"], v["stale_all"], v["partial"], v["clock"], v["upd"])
        for r in v["row"]:
            t += "            { .have = %d, .stale = %d, .fail = %d, .mkt = %d, .price_x100 = %d, .diff_x100 = %d, .pct_bp = %d },\n" % (
                r["have"], r["stale"], r["fail"], r["mkt"], r["price"], r["diff"], r["bp"])
        t += "        },\n    },\n"
    t += "};\n#endif\n"
    out["ui_preview.c"] = t
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
    ap.add_argument("--emit-c", action="store_true", help="C の生成物(ui_assets.h ui_layout.c ui_gfx_data.c ui_strips.c ui_preview.c)を --out-dir に書く")
    ap.add_argument("--preview-dir", default=os.path.join(here, "assets", "stock", "preview"))
    ap.add_argument("--states-dir", default=os.path.join(here, "..", "tests", "host", "data", "states"))
    ap.add_argument("--emit-c8", default=None, metavar="DIR")
    ap.add_argument("--font", default=DEFAULT_FONT)
    ap.add_argument("--check", action="store_true", help="ファイルを書かず、検査と表示だけ")
    args = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    if not os.path.exists(args.font):
        print("フォントが見つからない: %s (--font で指定)" % args.font, file=sys.stderr)
        return 2
    states = []
    for n in range(8):
        p = os.path.join(args.states_dir, "S%d.state" % n)
        if not os.path.exists(p):
            print("状態ファイルが無い: %s" % p, file=sys.stderr)
            return 2
        states.append(parse_state(p))
    fonts = Fonts(args.font)
    a = build(fonts)
    renders = [render(a, v) for v in states]
    dg = digest(a, renders)
    a2 = build(fonts)                                     # 再実行で同じか(乱数・時刻なし)
    dg2 = digest(a2, [render(a2, v) for v in states])
    errs, warns, stats, (mx_all, rmx, rbad, nused) = check_all(a, fonts, states)
    if dg != dg2:
        errs.append("2 回の生成で出力が違う(%s / %s)" % (dg[:12], dg2[:12]))
    print("フォント: %s sha256=%s" % (os.path.basename(args.font), fonts.sha))
    print("パレット: 固定色 %d + k-means %d = 256、使われる添字 %d、色の集合(RGB565 の異なる値)%d 色" % (
        len(a["fixed"]), 256 - len(a["fixed"]), nused, int((a["wt"] > 0).sum())))
    print("誤差(RGB565 の段。基準 <= %d): 画像(帯 9・ヘッダー・フッター 6・Wi-Fi 4 = 20 枚)最大 %d 段、ramp(%d 本 x 33 段)最大 %d 段(2 段超 %d 画素)" % (
        LIM_STEP, mx_all, len(a["ramp"]), rmx, rbad))
    tot = sum(s[1] for s in stats)
    print("  画素の不一致 %d / %d(%.3f%%)、1 段超 %d、2 段超 %d" % (sum(s[2] for s in stats), tot, 100.0 * sum(s[2] for s in stats) / tot,
                                                      sum(s[3] for s in stats), sum(s[4] for s in stats)))
    worst = sorted(stats, key=lambda s: (-s[5], -s[2]))[:3]
    for s in worst:
        print("    %-14s 不一致 %5d 1段超 %4d 2段超 %3d 最大 %d段" % (s[0], s[2], s[3], s[4], s[5]))
    print("数字の送り幅(F36 F30 F15 F18 F13): %s" % " ".join(str(x) for x in a["adv"]))
    gl = a["glyphs"]
    for fid, i in FI.items():
        print("  %s: %s" % (fid, " ".join("%s=%d" % (GLYPH_CHARS[k].replace("\u2212", "−"), gl[i][k][5]) for k in FONTS[i][3][10:])))
    print("行の中の矩形(Y0 = 0):")
    for k, v in a["rects"].items():
        print("  %-14s x %3d..%3d  y %3d..%3d" % ((k,) + (v[0], v[2], v[1], v[3])))
    print("コントラスト(WCAG。文字 >= 4.5、図形 >= 3):")
    for nm, vals, lim in a["contrast"]:
        print("  %-10s %s (>= %.1f)" % (nm, " / ".join("%.2f" % v for v in vals), lim))
    print("ヘッダー: " + "  ".join("%s x %d..%d" % (k, v[0], v[2]) for k, v in a["hdr_rects"].items()))
    print("フッター左の右端: " + " ".join("F%d=%d" % (i, r[2]) for i, r in enumerate(a["foot_w"])) + "(< %d)、右の文言 x %d..%d" % (
        FOOT_RIGHT_LIMIT, a["foot_r"][0], a["foot_r"][2]))
    rows = rom_table(a)
    print("ROM(アセット): " + " / ".join("%s %d" % r for r in rows) + " = %d バイト(設計 §3.10 は 415,924)" % sum(r[1] for r in rows))
    print("出力の sha256(パレット・全帯・グリフ・shape・ramp・8 状態の描画): %s(2 回の生成で %s)" % (dg, "一致" if dg == dg2 else "不一致"))
    for w in warns:
        print("警告:", w)
    if errs:
        for e in errs:
            print("NG:", e, file=sys.stderr)
        return 1
    print("検査 OK")
    if args.check:
        return 0
    write_previews(a, fonts, renders, args.preview_dir)
    print("プレビュー: %s" % os.path.normpath(args.preview_dir))
    if args.emit_c8:
        emit_c8(a, renders, args.emit_c8)
        print(".c8: %s" % os.path.normpath(args.emit_c8))
    if args.emit_c:
        write_c(a, fonts, states, args.out_dir)
        print("C: %s(ui_assets.h ui_layout.c ui_gfx_data.c ui_strips.c ui_preview.c)" % os.path.normpath(args.out_dir))
    return 0


if __name__ == "__main__":
    sys.exit(main())
