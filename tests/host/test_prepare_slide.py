#!/usr/bin/env python3
"""test_prepare_slide.py - tools/prepare_slide.py のテスト(設計 §12.2, §14 H9)。
実行: python3 -I tests/host/test_prepare_slide.py [-v]
  - 251x305 RGBA -> 480x272 不透明 RGB PNG(アスペクト比維持 224x272・中央配置・黒帯・透明合成)
  - --bg オプションによる背景色変更
  - 横長画像の上下黒帯配置
  - コミット済み slide_03_src.png -> slide_03_480x272.png との一致、および img2c.py 出力と img_03.c の一致
  - tools/img2c.py が非 480x272 (251x305 等) を終了コード 2 で拒否し続けること
"""
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
PREPARE_TOOL = os.path.join(ROOT, "tools", "prepare_slide.py")
IMG2C_TOOL = os.path.join(ROOT, "tools", "img2c.py")
ASSETS = os.path.join(ROOT, "tools", "assets")
APP = os.path.join(ROOT, "src", "app")
W, H = 480, 272

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:  # pragma: no cover
    HAVE_PIL = False


def run_prepare(*args):
    return subprocess.run([sys.executable, "-I", PREPARE_TOOL] + list(args), capture_output=True, text=True)


def run_img2c(*args):
    return subprocess.run([sys.executable, "-I", IMG2C_TOOL] + list(args), capture_output=True, text=True)


@unittest.skipUnless(HAVE_PIL, "Pillow が無い")
class TestPrepareSlide(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def test_251x305_rgba_center_black_bars_and_alpha_composite(self):
        src_path = os.path.join(self.d, "src_251x305.png")
        out_path = os.path.join(self.d, "out_480x272.png")

        # 251x305 RGBA: 中央は不透明の赤 (240, 20, 10, 255)、上端帯は完全透明の緑 (0, 255, 0, 0)、
        # 下端帯は半透明の白 (255, 255, 255, 128)
        im = Image.new("RGBA", (251, 305), (240, 20, 10, 255))
        for y in range(40):
            for x in range(251):
                im.putpixel((x, y), (0, 255, 0, 0))
        for y in range(265, 305):
            for x in range(251):
                im.putpixel((x, y), (255, 255, 255, 128))
        im.save(src_path)

        r = run_prepare(src_path, "-o", out_path)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertTrue(os.path.exists(out_path))

        with Image.open(out_path) as out:
            self.assertEqual(out.size, (W, H))
            self.assertEqual(out.mode, "RGB")
            px = out.load()

            # 左右黒帯 x=0..127, x=352..479 (224px を中央配置した残り各 128px)
            for y in range(H):
                for x in range(128):
                    self.assertEqual(px[x, y], (0, 0, 0), "left bar (%d,%d)" % (x, y))
                for x in range(352, W):
                    self.assertEqual(px[x, y], (0, 0, 0), "right bar (%d,%d)" % (x, y))

            # 完全透明領域 (上端 y=5, x=240) は元の緑 (0,255,0) ではなく背景黒 (0,0,0) に合成
            self.assertEqual(px[240, 5], (0, 0, 0))

            # 中央の不透明赤領域 (y=136, x=240)
            r_c, g_c, b_c = px[240, 136]
            self.assertGreater(r_c, 220)
            self.assertLess(g_c, 40)
            self.assertLess(b_c, 30)

            # 半透明白領域 (y=266, x=240) は黒と白の合成で中間グレー付近
            r_s, g_s, b_s = px[240, 266]
            self.assertTrue(100 <= r_s <= 160 and 100 <= g_s <= 160 and 100 <= b_s <= 160,
                            "semi-transparent composite unexpected: %s" % ((r_s, g_s, b_s),))

    def test_bg_white_option(self):
        src_path = os.path.join(self.d, "src.png")
        out_path = os.path.join(self.d, "out_white.png")
        im = Image.new("RGBA", (251, 305), (0, 0, 0, 0))
        for y in range(100, 200):
            for x in range(50, 200):
                im.putpixel((x, y), (0, 0, 255, 255))
        im.save(src_path)

        r = run_prepare(src_path, "-o", out_path, "--bg", "#FFFFFF")
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        with Image.open(out_path) as out:
            self.assertEqual(out.size, (W, H))
            self.assertEqual(out.mode, "RGB")
            px = out.load()
            self.assertEqual(px[0, 0], (255, 255, 255))
            self.assertEqual(px[479, 271], (255, 255, 255))
            self.assertEqual(px[240, 5], (255, 255, 255))
            self.assertGreater(px[240, 136][2], 220)

    def test_landscape_image_top_bottom_bars(self):
        src_path = os.path.join(self.d, "wide.png")
        out_path = os.path.join(self.d, "wide_out.png")
        # 960x272 -> 480x136, 上下に各 68px の黒帯 (y=0..67, y=204..271)
        im = Image.new("RGB", (960, 272), (0, 200, 0))
        im.save(src_path)

        r = run_prepare(src_path, "-o", out_path)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        with Image.open(out_path) as out:
            self.assertEqual(out.size, (W, H))
            px = out.load()
            for x in (0, 240, 479):
                self.assertEqual(px[x, 0], (0, 0, 0))
                self.assertEqual(px[x, 67], (0, 0, 0))
                self.assertEqual(px[x, 204], (0, 0, 0))
                self.assertEqual(px[x, 271], (0, 0, 0))
                self.assertGreater(px[x, 136][1], 180)

    def test_invalid_bg_rejected(self):
        src_path = os.path.join(self.d, "src.png")
        out_path = os.path.join(self.d, "out.png")
        Image.new("RGB", (100, 100), (0, 0, 0)).save(src_path)
        for bad in ("red", "#12345", "ZZZZZZ"):
            r = run_prepare(src_path, "-o", out_path, "--bg", bad)
            self.assertNotEqual(r.returncode, 0)
            self.assertFalse(os.path.exists(out_path))

    def test_img2c_still_rejects_251x305(self):
        src_path = os.path.join(self.d, "src_251x305.png")
        Image.new("RGBA", (251, 305), (10, 20, 30, 255)).save(src_path)
        r = run_img2c(src_path, "--index", "3", "--out-dir", self.d)
        self.assertEqual(r.returncode, 2, r.stdout + r.stderr)
        self.assertFalse(os.path.exists(os.path.join(self.d, "img_03.c")))

    def test_committed_slide03_assets_match_pipeline(self):
        src_asset = os.path.join(ASSETS, "slide_03_src.png")
        mid_asset = os.path.join(ASSETS, "slide_03_480x272.png")
        c_asset = os.path.join(APP, "img_03.c")
        self.assertTrue(os.path.exists(src_asset))
        self.assertTrue(os.path.exists(mid_asset))

        gen_png = os.path.join(self.d, "slide_03_480x272.png")
        r1 = run_prepare(src_asset, "-o", gen_png)
        self.assertEqual(r1.returncode, 0, r1.stdout + r1.stderr)

        with Image.open(gen_png) as g_im, Image.open(mid_asset) as m_im:
            self.assertEqual(m_im.size, (W, H))
            self.assertEqual(m_im.mode, "RGB")
            self.assertEqual(g_im.tobytes(), m_im.tobytes(), "slide_03_480x272.png が prepare_slide.py の出力と不一致")

        r2 = run_img2c(mid_asset, "--index", "3", "--out-dir", self.d)
        self.assertEqual(r2.returncode, 0, r2.stdout + r2.stderr)
        if os.path.exists(c_asset):       # v2(製品デモ)では img_03.c は削除済み。P2 ギャラリーで再生成した時だけ照合する
            with open(os.path.join(self.d, "img_03.c"), "rb") as f1, open(c_asset, "rb") as f2:
                self.assertEqual(f1.read(), f2.read(), "src/app/img_03.c が img2c.py の出力と不一致")


if __name__ == "__main__":
    unittest.main()
