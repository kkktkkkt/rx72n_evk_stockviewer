#!/usr/bin/env python3
"""prepare_slide.py - 任意サイズの画像を 480x272 不透明 RGB PNG へ変換する。

使い方:
  python tools/prepare_slide.py IN.png -o tools/assets/slide_03_480x272.png
  python tools/prepare_slide.py IN.png -o OUT.png --bg "#FFFFFF"
"""
import argparse
import os
import re
import sys

TARGET_W = 480
TARGET_H = 272


def parse_hex_color(value):
    s = value.strip()
    if s.startswith("#"):
        s = s[1:]
    if not re.fullmatch(r"[0-9a-fA-F]{6}", s):
        raise argparse.ArgumentTypeError("背景色は #RRGGBB 形式で指定してください: %r" % (value,))
    return (int(s[0:2], 16), int(s[2:4], 16), int(s[4:6], 16))


def fit_size(src_w, src_h, max_w=TARGET_W, max_h=TARGET_H):
    if src_w <= 0 or src_h <= 0:
        raise ValueError("不正な画像サイズです: %dx%d" % (src_w, src_h))
    if src_w * max_h > src_h * max_w:
        new_w = max_w
        new_h = max(1, min(max_h, round(src_h * max_w / src_w)))
    else:
        new_h = max_h
        new_w = max(1, min(max_w, round(src_w * max_h / src_h)))
    return new_w, new_h


def prepare_slide(input_path, output_path, bg_rgb=(0, 0, 0)):
    try:
        from PIL import Image, ImageOps
    except ImportError:
        sys.stderr.write("エラー: Pillow がありません。pip install pillow\n")
        return 3

    resample_ns = getattr(Image, "Resampling", Image)
    with Image.open(input_path) as src:
        im = ImageOps.exif_transpose(src).convert("RGBA")

    new_w, new_h = fit_size(im.width, im.height)
    resized = im.resize((new_w, new_h), resample=resample_ns.LANCZOS)

    canvas = Image.new("RGBA", (TARGET_W, TARGET_H), (*bg_rgb, 255))
    x = (TARGET_W - new_w) // 2
    y = (TARGET_H - new_h) // 2
    canvas.alpha_composite(resized, dest=(x, y))

    out_im = canvas.convert("RGB")
    out_dir = os.path.dirname(os.path.abspath(output_path))
    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
    out_im.save(output_path, format="PNG")
    print("wrote %s (%dx%d RGB from %dx%d -> %dx%d at (%d,%d))" % (
        output_path, TARGET_W, TARGET_H, im.width, im.height, new_w, new_h, x, y
    ))
    return 0


def main():
    ap = argparse.ArgumentParser(description="任意画像を 480x272 不透明 RGB PNG へ変換")
    ap.add_argument("input", help="入力画像 (PNG/JPG等)")
    ap.add_argument("-o", "--output", required=True, help="出力 PNG パス")
    ap.add_argument("--bg", type=parse_hex_color, default="#000000", help="背景色 #RRGGBB (既定: #000000)")
    args = ap.parse_args()
    return prepare_slide(args.input, args.output, args.bg)


if __name__ == "__main__":
    sys.exit(main())
