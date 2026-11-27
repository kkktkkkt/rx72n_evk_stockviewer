#!/usr/bin/env python3
"""wx_compare_p2b.py - 天気・Wi-Fi 追加(design §22)の背景 7 枚を、P2b(コミット e6eb603)の背景と並べて比べる(目視用)。

  python tools/wx_compare_p2b.py [--out-dir tools/assets/bath/preview_wx]

  出力: compare_bg_<名前>.png(P2b | 新 | 差分。差分 = 一致 白 / 1段 黄 / 2段 赤 / 3段以上 黒)、compare_report.md(背景ごとの画素の差)
  P2b の背景 = git show e6eb603:src/app/ui_bg_*.c(8bit の添字)と ui_layout.c の ui_pal。新 = 作業ツリーの src/app。どちらもパネルに出る色(RGB565 に切り捨て)で比べる。
  home の日付(動的になった)と天気の行(新規)の矩形は、変わるのが当然なので「矩形の外」の統計を別に出す(パレットを作り直したことによる差)。"""
import argparse
import os
import re
import subprocess
import sys

import numpy as np
from PIL import Image

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
OLD = "e6eb603"
NAMES = ["home", "fill_run", "fill_done", "reheat_run", "reheat_done", "usage", "stby"]
W, H = 480, 272
CHANGED = [(8, 6, 96, 26), (16, 154, 268, 36)]        # home: 日付、天気の行(アイコン・気温・東京・更新・更新ボタン)


def old_text(path):
    return subprocess.check_output(["git", "-C", ROOT, "show", "%s:%s" % (OLD, path)]).decode("utf-8")


def new_text(path):
    return open(os.path.join(ROOT, path), encoding="utf-8").read()


def nums(text, pat):
    return [int(x, 0) for x in re.findall(pat, text[text.index("= {") + 3:text.rindex("}")])]


def panel(pal, idx):
    a = np.asarray(pal, dtype=np.uint32)[idx.reshape(H, W)]
    r5, g6, b5 = (a >> 19) & 31, (a >> 10) & 63, (a >> 3) & 31
    return ((r5 << 11) | (g6 << 5) | b5).astype(np.uint16)


def rgb(p):
    p = p.astype(np.uint32)
    return np.stack([((p >> 11) << 3) | ((p >> 13) & 7), (((p >> 5) & 63) << 2) | ((p >> 9) & 3), ((p & 31) << 3) | ((p >> 2) & 7)], axis=-1).astype(np.uint8)


def steps(a, b):
    a, b = a.astype(np.int32), b.astype(np.int32)
    return np.maximum(np.maximum(abs((a >> 11) - (b >> 11)), abs(((a >> 5) & 63) - ((b >> 5) & 63))), abs((a & 31) - (b & 31)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out-dir", default=os.path.join(ROOT, "tools", "assets", "bath", "preview_wx"))
    args = ap.parse_args()
    sys.stdout.reconfigure(encoding="utf-8")
    os.makedirs(args.out_dir, exist_ok=True)

    def pal(t):
        i = t.index("ui_pal[UI_PAL_N]")
        return np.array(re.findall(r"0x[0-9A-Fa-f]{8}", t[i:t.index("}", i)]), dtype=object).astype(str)
    po = np.array([int(x, 16) for x in pal(old_text("src/app/ui_layout.c"))], dtype=np.uint32)
    pn = np.array([int(x, 16) for x in pal(new_text("src/app/ui_layout.c"))], dtype=np.uint32)
    lines = ["# 背景 7 枚: P2b(%s)と、天気・Wi-Fi 追加後の比較(tools/wx_compare_p2b.py が出力)" % OLD, "",
             "| 背景 | 画素の差(全体) | 1段超 | 最大 | 矩形の外の差 | 矩形の外の 1段超 | 矩形の外の最大 |", "|---|---|---|---|---|---|---|"]
    for n in NAMES:
        path = "src/app/ui_bg_%s.c" % n
        a = panel(po, np.array(nums(old_text(path), r"\d+"), dtype=np.uint8))
        b = panel(pn, np.array(nums(new_text(path), r"\d+"), dtype=np.uint8))
        e = steps(a, b)
        keep = np.ones((H, W), dtype=bool)
        if n == "home":
            for (x, y, w, h) in CHANGED:
                keep[y:y + h, x:x + w] = False
        eo = e[keep]
        lines.append("| %s | %d | %d | %d段 | %d | %d | %d段 |" % (n, int((e > 0).sum()), int((e > 1).sum()), int(e.max()), int((eo > 0).sum()), int((eo > 1).sum()), int(eo.max())))
        d = np.full((H, W, 3), 255, dtype=np.uint8)
        d[e == 1] = (255, 220, 0)
        d[e == 2] = (230, 0, 0)
        d[e >= 3] = (0, 0, 0)
        sheet = np.full((H, 3 * W + 16, 3), 160, dtype=np.uint8)
        for i, im in enumerate((rgb(a), rgb(b), d)):
            sheet[:, i * (W + 8):i * (W + 8) + W] = im
        Image.fromarray(sheet).save(os.path.join(args.out_dir, "compare_bg_%s.png" % n))
    lines += ["", "- home の矩形(日付 (8,6,96,26)・天気の行 (16,154,268,36))は、新しい要素(日付が焼き込みから動的に、天気の行が新規)なので変わるのが当然。その外の差は、共通パレットを作り直した影響(P2b の 256 色から、アイコンの色 31 固定色へ)。"]
    open(os.path.join(args.out_dir, "compare_report.md"), "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


if __name__ == "__main__":
    sys.exit(main())
