#!/usr/bin/env python3
"""wx_mock.py - ホーム画面の「Wi-Fi・更新ボタン・気温・天気」の配置モックと、パレットへの影響の実測(docs/design.md §22)。

  python tools/wx_mock.py [--out-dir tools/assets/bath/preview_wx] [--font PATH]

  出力: mock_home_true.png(元の色)/ mock_home_c8.png(共通パレット 256 色に写した見え方)/ states.png(状態の見本)/
        icons_x4.png(アイコン 4 倍)/ palette_report.md(誤差の実測)
gen_ui_assets.py を import して同じ背景・フォント・パレット生成を使う(src/ は書かない)。
アイコンの描き方(§22.6.4)は、コーディング担当が gen_ui_assets.py へ移す元の案。乱数・時刻は使わない。
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_ui_assets as g          # noqa: E402

S = g.S
# ---- 新しい色(§22.7)。ほかは gen_ui_assets の C をそのまま使う ----
NEW = dict(SUN="#f5b82e", CLOUD="#aebccb", CLOUD_DK="#8795a4", RAIN="#4f94d4", ICE="#7fb7e6")
g.C.update(NEW)

# ---- 配置(§22.6.1)。x, y, w, h ----
WIFI_XY, WIFI_WH = (112, 7), (20, 20)
WX_ICON_XY, WX_ICON_WH = (20, 158), (28, 28)
TEMP_RIGHT_X, TEMP_BASE_Y = 106, 181               # 気温の数字(F22、右詰め)
UNIT_X = 108                                       # 焼き込みの「℃」
TOKYO_X, UPD_X, UPD_TIME_X, LABEL_BASE_Y = 134, 166, 196, 177
REF_BTN = (248, 158, 28, 28)                       # 更新ボタンの見た目(角丸 r=8)
REF_ICON_XY, REF_ICON_WH = (252, 162), (20, 20)
REF_HIT = (228, 136, 60, 52)                       # 当たり判定
DATE_X, DATE_BASE_Y = 16, 23

# スプライト(1枚の表示頻度に対する重み。背景 1 枚 = 1/7 に対し、アイコンは小さいので大きめに取る。§22.7 の実測で決めた値)
SPRITE_W = float(os.environ.get("WX_SPRITE_W", "50"))

WX_BUTTON = ("H_WX", "h_wx", "HOME", REF_BTN, "ALWAYS", "BLUE_B", "BLUE_T", "", 14, None, 8, REF_HIT)


def rgb(name):
    return g.hexrgb(name)


# ------------------------------------------------------------------ アイコンの描画(4倍で描いて縮小。背景色の上に不透明で焼く)
class Ic:
    def __init__(self, w, h, bg):
        self.w, self.h = w, h
        self.im = Image.new("RGB", (w * S, h * S), rgb(bg))
        self.d = ImageDraw.Draw(self.im)

    def circle(self, cx, cy, r, color):
        self.d.ellipse([(cx - r) * S, (cy - r) * S, (cx + r) * S - 1, (cy + r) * S - 1], fill=rgb(color))

    def line(self, x1, y1, x2, y2, wd, color):
        self.d.line([x1 * S, y1 * S, x2 * S, y2 * S], fill=rgb(color), width=round(wd * S))
        for (x, y) in ((x1, y1), (x2, y2)):
            self.circle(x, y, wd / 2, color)

    def poly(self, pts, color):
        self.d.polygon([(x * S, y * S) for x, y in pts], fill=rgb(color))

    def rrect(self, x, y, w, h, r, color):
        self.d.rounded_rectangle([x * S, y * S, (x + w) * S - 1, (y + h) * S - 1], radius=r * S, fill=rgb(color))

    def cloud(self, cx, cy, k, color):
        """雲: 3つの円 + 下の角丸。cx, cy = 雲の中心、k = 大きさの倍率"""
        self.circle(cx - 5.5 * k, cy + 1.5 * k, 4.5 * k, color)
        self.circle(cx + 0.5 * k, cy - 2.5 * k, 6.5 * k, color)
        self.circle(cx + 6 * k, cy + 1.5 * k, 4.5 * k, color)
        self.rrect(cx - 5.5 * k, cy + 1.0 * k, 11.5 * k, 5 * k, 2.5 * k, color)

    def sun(self, cx, cy, r, ray0, ray1, wd, color):
        import math
        self.circle(cx, cy, r, color)
        for i in range(8):
            a = math.pi / 4 * i
            self.line(cx + ray0 * math.cos(a), cy + ray0 * math.sin(a), cx + ray1 * math.cos(a), cy + ray1 * math.sin(a), wd, color)

    def done(self):
        return self.im.resize((self.w, self.h), Image.BOX)


def weather_icon(kind, bg="BG"):
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
    else:   # unknown: 灰色の円 + ?
        c.circle(14, 14, 10.5, "LINE")
        c.circle(14, 14, 8.6, bg)
        f = g.Fonts(g.DEFAULT_FONT).get(18, 500)
        c.d.text((14 * S, 14.5 * S), "?", font=f, fill=rgb("SUB"), anchor="mm")
    return c.done()


def wifi_icon(state, bg="WHITE"):
    """state: off / conn / on / err。20x20。扇形の3本の弧 + 点"""
    c = Ic(20, 20, bg)
    col = {"off": "SUB", "conn": "BLUE_T", "on": "BLUE_T", "err": "RED_T"}[state]
    dim = "LINE"
    cx, cy = 10, 16.5
    arcs = [(4.2, col), (8.2, col), (12.2, col)]
    if state == "conn":
        arcs = [(4.2, col), (8.2, col), (12.2, dim)]
    for r, cl in arcs:
        c.d.arc([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], 227, 313, fill=rgb(cl), width=round(2.0 * S))
    c.circle(cx, cy, 1.9, col)
    if state == "off":
        c.line(3, 3, 17, 17.5, 1.8, "SUB")
    if state == "err":
        c.line(16.4, 10.2, 16.4, 14.2, 2.0, "RED_T")
        c.circle(16.4, 17.2, 1.2, "RED_T")
    return c.done()


def refresh_icon(phase):
    """20x20。円弧(約 270 度)+ 矢じり。phase = 0..3(90 度ずつ回す)。ボタンの面の色 BLUE_B の上に焼く"""
    import math
    c = Ic(20, 20, "BLUE_B")
    cx = cy = 10
    r = 6.2
    c.d.arc([(cx - r) * S, (cy - r) * S, (cx + r) * S, (cy + r) * S], 40, 320, fill=rgb("BLUE_T"), width=round(2.2 * S))
    a = math.radians(320)
    ex, ey = cx + r * math.cos(a), cy + r * math.sin(a)
    tx, ty = -math.sin(a), math.cos(a)          # 進行方向(接線)
    nx, ny = math.cos(a), math.sin(a)
    c.poly([(ex + tx * 4.2, ey + ty * 4.2), (ex + nx * 3.6, ey + ny * 3.6), (ex - nx * 3.6, ey - ny * 3.6)], "BLUE_T")
    im = c.done()
    return im.rotate(-90 * phase, resample=Image.BICUBIC, fillcolor=rgb("BLUE_B"))


KINDS = ["sun", "partly", "cloud", "fog", "rain", "snow", "thunder", "unknown"]


# ------------------------------------------------------------------ 新しいホーム背景(焼き込み)と動的部分の重ね
def draw_home_wx(fonts, slots):
    """承認済みの HOME 背景から、日付(動的になる)を消し、東京・更新・℃・更新ボタンの面を足したもの"""
    cv = g.Canvas(fonts, "BG")
    g.header(cv, "", ("浴槽", "℃ ・ 保温中", "HOME_BATH"), slots, fonts)
    cv.text(26, 148, "湯はり完了 ・ 水位 標準", 15, "SUB")
    g.card(cv, 288, 48, 176, 134)
    cv.text(376, 72, "設定温度", 13, "SUB", "m")
    cv.text(406, 124, "℃", 20, "BLUE_T", "l", 500)
    cv.text(UNIT_X, TEMP_BASE_Y, "℃", 16, "INK", "l", 500)
    cv.text(TOKYO_X, LABEL_BASE_Y, "東京", 13, "SUB")
    cv.text(UPD_X, LABEL_BASE_Y, "更新", 13, "SUB")
    for bid in g.BG_BUTTONS["home"]:
        g.button(cv, next(b for b in g.BUTTONS if b[0] == bid), fonts)
    x, y, w, h = REF_BTN
    cv.rrect(x, y, w, h, 8, "BLUE_B")
    return cv


def overlay(cv, fonts, wx):
    """動的部分(実機では ui_render が描く)を、ホームの cv に重ねる(モック用。字形は実機と違い、フォントで直接描く)"""
    cv.text(DATE_X, DATE_BASE_Y, wx["date"], 14, "SUB")
    temp_col = "SUB" if wx["stale"] else "INK"
    cv.text(TEMP_RIGHT_X, TEMP_BASE_Y, wx["temp"], 22, temp_col, "r", 500)
    cv.text(UPD_TIME_X, LABEL_BASE_Y, wx["upd"], 14, "SUB")
    cv.text(22, 116, wx["clock"], 78, "INK", "l", 500)
    cv.text(366, 124, "42", 56, "BLUE_T", "m", 500)                       # 設定温度(既存の動的部分)
    cv.text(wx["_bath_x"], 23, "40", 14, "SUB")                           # 浴槽温度(ヘッダー右。既存)
    return cv


def compose(fonts, a, wx, true_color=True):
    cv = draw_home_wx(fonts, dict(a["slots"]))
    wx = dict(wx, _bath_x=a["slots"]["HOME_BATH"])
    overlay(cv, fonts, wx)
    im = cv.down()
    im.paste(weather_icon(wx["kind"]), WX_ICON_XY)
    im.paste(wifi_icon(wx["wifi"]), WIFI_XY)
    im.paste(refresh_icon(wx.get("spin", 0)), REF_ICON_XY)
    return im


# ------------------------------------------------------------------ パレットへの影響
def sprite_sets():
    sp = {}
    for k in KINDS:
        sp["wx_" + k] = weather_icon(k)
    for st in ("off", "conn", "on", "err"):
        sp["wifi_" + st] = wifi_icon(st)
    for p in range(4):
        sp["ref_%d" % p] = refresh_icon(p)
    return sp


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=os.path.join(os.path.dirname(os.path.abspath(__file__)), "assets", "bath", "preview_wx"))
    ap.add_argument("--font", default=g.DEFAULT_FONT)
    args = ap.parse_args()
    if any(b[0] == "H_WX" for b in g.BUTTONS):
        print("このモックは設計時の試作。アイコン・パレット・ホーム背景は tools/gen_ui_assets.py に統合済み(H_WX も BUTTONS にある)なので、何もしない。")
        print("プレビューは python tools/gen_ui_assets.py(preview_wx/)と python tools/wx_compare_p2b.py")
        return
    os.makedirs(args.out_dir, exist_ok=True)
    fonts = g.Fonts(args.font)

    # 更新ボタンを製品のボタン表へ足して、アセットを作る(押下の形・当たり判定の検査も同じ表を使う)
    g.BUTTONS.append(WX_BUTTON)
    g.BG_BUTTONS["home"].append("H_WX")
    a0 = g.build(fonts)                                    # 日付入りの旧ホーム
    errs = g.check_all(a0)
    sp = sprite_sets()

    # ---- (A) 現行(P2b)のパレットの誤差(基準)。H_WX を足す前の状態に戻して測る ----
    g.BUTTONS.pop()
    g.BG_BUTTONS["home"].pop()
    base = g.build(fonts)
    c8_base = g.build_c8(base)
    rows_b, tot_b = g.bg_errors(base, c8_base)
    fixed_b = len(c8_base["fixed"])
    g.BUTTONS.append(WX_BUTTON)
    g.BG_BUTTONS["home"].append("H_WX")

    # ---- (B) 新しいホーム背景 + アイコンの色を universe と固定色に加える ----
    a = g.build(fonts)
    cv = draw_home_wx(fonts, dict(a["slots"]))
    a["bgs"]["home"] = cv.down()
    sp565 = {k: g.img565(v) for k, v in sp.items()}
    orig_universe = g.build_universe

    def universe_wx(aa, bg565, bar565, tag565):
        wt, fixed = orig_universe(aa, bg565, bar565, tag565)
        fx = set(fixed)
        n = len(sp565)
        for k, p in sp565.items():
            fam = 8 if k.startswith("wx_") else (4 if k.startswith("wifi_") else 4)
            wt += np.bincount(p.ravel(), minlength=65536) * (1.0 / fam) * SPRITE_W / 7.0
            cnt = np.bincount(p.ravel(), minlength=65536)
            fx |= set(np.nonzero(cnt >= 40)[0].tolist()) if k.startswith("wifi_") or k.startswith("ref_") else set()
        for nm in NEW:                                       # 新しい平らな色は固定色(厳密に残す)
            wt[g.c565(nm)] += 600.0
            fx.add(g.c565(nm))
        return wt, sorted(fx)

    g.build_universe = universe_wx
    c8 = g.build_c8(a)
    g.build_universe = orig_universe
    rows, tot = g.bg_errors(a, c8)
    lut, pal565 = c8["lut"], c8["pal565"]

    lines = ["# ホーム画面の Wi-Fi・天気 追加によるパレットへの影響(tools/wx_mock.py の実測)", ""]
    lines.append("| 項目 | 現行(P2b) | 追加後 |")
    lines.append("|---|---|---|")
    lines.append("| universe の色数 | %d | %d |" % (c8_base["universe"], c8["universe"]))
    lines.append("| 固定色 | %d | %d |" % (fixed_b, len(c8["fixed"])))
    nb, mb = sum(r[1] for r in rows_b), tot_b
    n2, m2 = sum(r[1] for r in rows), tot
    lines.append("| 背景7枚の不一致画素 | %d(%.3f%%)| %d(%.3f%%)|" % (mb[0], 100.0 * mb[0] / nb, m2[0], 100.0 * m2[0] / n2))
    lines.append("| 背景7枚の 1 段超 / 最大 | %d / %d 段 | %d / %d 段 |" % (mb[1], mb[3], m2[1], m2[3]))
    lines.append("")
    lines.append("## 背景ごと(追加後)")
    lines.append("")
    lines.append("| 背景 | 不一致 | 割合 | 1段超 | 最大 |")
    lines.append("|---|---|---|---|---|")
    for (n, px, mis, o1, o2, mx) in rows:
        lines.append("| %s | %d | %.3f%% | %d | %d |" % (n, mis, 100.0 * mis / px, o1, mx))
    lines.append("")
    lines.append("## アイコン(スプライト)ごとの誤差(追加後。元の RGB565 と、パレット最近傍の差)")
    lines.append("")
    lines.append("| スプライト | 大きさ | 不一致 | 割合 | 1段超 | 最大 | 使う色(添字)数 |")
    lines.append("|---|---|---|---|---|---|---|")
    used = set()
    worst = 0
    for k, p in sp565.items():
        idx = lut[p]
        px, mis, o1, o2, mx = g.err_stats(p, pal565[idx])
        used |= set(np.unique(idx).tolist())
        worst = max(worst, mx)
        lines.append("| %s | %dx%d | %d | %.1f%% | %d | %d | %d |" % (k, p.shape[1], p.shape[0], mis, 100.0 * mis / px, o1, mx, len(np.unique(idx))))
    lines.append("")
    lines.append("- 全スプライトの最大誤差 = %d 段。スプライトが使うパレットの添字 = %d 個(うち固定色は含む)。" % (worst, len(used)))
    lines.append("- 使われているパレットの添字(背景・バー・タグ・ramp・スプライト)= %d / 256(現行は %d)。" % (len(g.used_entries(a, c8) | used), len(g.used_entries(base, c8_base))))
    lines.append("- 検査(check_all): %s" % ("違反なし" if not errs else "; ".join(errs)))
    c8chk = g.check_c8(a, c8)
    lines.append("- 検査(check_c8。合格基準 不一致 <= 0.3%%、1枚 <= 1.0%%、最大 <= 2 段): %s" % ("合格" if not c8chk else "; ".join(c8chk)))
    with open(os.path.join(args.out_dir, "palette_report.md"), "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print("\n".join(lines))

    # ---- (C) 画像 ----
    states = [
        dict(date="11月25日(水)", clock="13:24", temp="12.6", upd="13:24", kind="sun", wifi="on", stale=False),
        dict(date="11月25日(水)", clock="--:--", temp="--.-", upd="--:--", kind="unknown", wifi="off", stale=False),
        dict(date="11月25日(水)", clock="13:24", temp="12.6", upd="12:40", kind="rain", wifi="err", stale=True),
        dict(date="1月5日(月)", clock="07:05", temp="-1.2", upd="07:05", kind="snow", wifi="conn", stale=False, spin=1),
    ]

    def panel(wx, c8mode):
        im = compose(fonts, a, wx)
        if c8mode:
            p = g.img565(im)
            im = Image.fromarray(g.panel_rgb(pal565[lut[p]]).astype(np.uint8))
        return im

    for mode, name in ((False, "mock_home_true.png"), (True, "mock_home_c8.png")):
        panel(states[0], mode).resize((960, 544), Image.NEAREST).save(os.path.join(args.out_dir, name))
    sheet = Image.new("RGB", (480 * 2 + 12, 272 * 2 + 12), (60, 60, 60))
    for i, wx in enumerate(states):
        sheet.paste(panel(wx, True), ((i % 2) * 492, (i // 2) * 284))
    sheet.save(os.path.join(args.out_dir, "states.png"))
    # アイコンの 4 倍
    tiles = [weather_icon(k) for k in KINDS] + [wifi_icon(s) for s in ("off", "conn", "on", "err")] + [refresh_icon(p) for p in range(4)]
    w4 = Image.new("RGB", (len(tiles) * 4 * 30 + 8, 4 * 30 + 8), (200, 200, 200))
    for i, t in enumerate(tiles):
        w4.paste(t.resize((t.width * 4, t.height * 4), Image.NEAREST), (8 + i * 120, 8))
    w4.save(os.path.join(args.out_dir, "icons_x4.png"))
    print("出力:", args.out_dir)


if __name__ == "__main__":
    main()
