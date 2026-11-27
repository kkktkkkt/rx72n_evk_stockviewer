#!/usr/bin/env python3
"""ppm2png.py IN_DIR OUT_DIR - test_ui_render が出した PPM(ファームと同じ C コードで描いた画面)を PNG にする(目視用)。"""
import os
import sys

from PIL import Image


def main():
    src, dst = sys.argv[1], sys.argv[2]
    os.makedirs(dst, exist_ok=True)
    names = sorted(f for f in os.listdir(src) if f.endswith(".ppm"))
    for f in names:
        Image.open(os.path.join(src, f)).save(os.path.join(dst, "fw_" + f[:-4] + ".png"))
    print("%d 枚を %s に出力" % (len(names), dst))


if __name__ == "__main__":
    sys.exit(main())
