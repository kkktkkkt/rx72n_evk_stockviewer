#!/usr/bin/env python3
"""test_img2c.py - tools/img2c.py のテスト(設計 §14 H9)。実行: python3 -I tests/host/test_img2c.py [-v]
  - 出力配列の要素数 = 480*272(Cソースの初期化子の数を数える。足りないと 0 埋めされて気づけないため)
  - 既知ピクセルの RGB565 値(期待値はこのテスト内で独立に計算)
  - 480x272 以外は 終了コード 2、Pillow 無しは 3(この環境は Pillow あり)
  - 生成済み src/app/img_NN.c が --testpattern の出力と一致する
"""
import os
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
TOOL = os.path.join(ROOT, "tools", "img2c.py")
APP = os.path.join(ROOT, "src", "app")
W, H = 480, 272

try:
    from PIL import Image
    HAVE_PIL = True
except ImportError:  # pragma: no cover
    HAVE_PIL = False


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def read_file(path, binary=False):
    with open(path, "rb" if binary else "r", **({} if binary else {"encoding": "utf-8"})) as f:
        return f.read()


def run_tool(*args):
    return subprocess.run([sys.executable, "-I", TOOL] + list(args), capture_output=True, text=True)


def parse_c(path):
    """Cソースから (配列名, 宣言サイズ式, 値のリスト, 全文) を取り出す"""
    txt = read_file(path)
    m = re.search(r"const uint16_t (\w+)\[(\w+)\][^=]*=\s*\{(.*)\};", txt, re.S)
    assert m, "配列定義が見つからない: " + path
    vals = [int(v, 16) for v in re.findall(r"0x[0-9A-Fa-f]{4}", m.group(3))]
    return m.group(1), m.group(2), vals, txt


class TestTestpattern(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        r = run_tool("--testpattern", "--out-dir", cls.tmp.name)
        cls.rc = r.returncode
        cls.out = r.stdout + r.stderr

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_exit_ok(self):
        self.assertEqual(self.rc, 0, self.out)

    def test_each_array_has_exact_length(self):
        for i in range(3):
            name, size_expr, vals, txt = parse_c(os.path.join(self.tmp.name, "img_%02d.c" % i))
            self.assertEqual(name, "g_img_%02d" % i)
            self.assertEqual(size_expr, "APP_IMG_PIXELS")
            self.assertEqual(len(vals), W * H, "初期化子の数")
            self.assertEqual(len(vals), 130560)

    def test_const_and_header(self):
        for i in range(3):
            _, _, _, txt = parse_c(os.path.join(self.tmp.name, "img_%02d.c" % i))
            self.assertRegex(txt, r"\bconst uint16_t g_img_%02d\[" % i)
            self.assertIn('#include "images.h"', txt)

    def test_known_pixels_pattern0(self):
        _, _, v, _ = parse_c(os.path.join(self.tmp.name, "img_00.c"))
        px = lambda x, y: v[y * W + x]
        self.assertEqual(px(48, 136), 0xF800)
        self.assertEqual(px(144, 136), 0x07E0)
        self.assertEqual(px(240, 136), 0x001F)
        self.assertEqual(px(336, 136), 0xFFFF)
        self.assertEqual(px(432, 136), 0x0000)
        self.assertEqual(px(0, 0), 0xFFFF)
        self.assertEqual(px(479, 271), 0xFFFF)
        self.assertEqual(px(1, 1), rgb565(255, 0, 255))
        self.assertEqual(px(478, 270), rgb565(255, 128, 0))

    @unittest.skipUnless(os.path.exists(os.path.join(APP, "img_00.c")), "img_00.c は v2(製品デモ)で削除済み。P2 ギャラリーで再生成する時に有効")
    def test_committed_files_match_generator(self):
        for i in range(3):
            gen = read_file(os.path.join(self.tmp.name, "img_%02d.c" % i), True)
            cur = read_file(os.path.join(APP, "img_%02d.c" % i), True)
            self.assertEqual(gen, cur, "src/app/img_%02d.c が img2c.py --testpattern の出力と違う" % i)

    @unittest.skipUnless(os.path.exists(os.path.join(APP, "img_00.c")), "img_00.c は v2(製品デモ)で削除済み")
    def test_committed_array_lengths(self):
        for i in range(3):
            _, _, vals, _ = parse_c(os.path.join(APP, "img_%02d.c" % i))
            self.assertEqual(len(vals), 130560)


@unittest.skipUnless(HAVE_PIL, "Pillow が無い")
class TestImageInput(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.d = self.tmp.name

    def tearDown(self):
        self.tmp.cleanup()

    def _png(self, size, name="in.png", mode="RGB"):
        im = Image.new(mode, size, (0, 0, 0) if mode == "RGB" else (0, 0, 0, 255))
        path = os.path.join(self.d, name)
        return im, path

    def test_known_pixels(self):
        im, path = self._png((W, H))
        pts = {
            (0, 0): (255, 0, 0), (479, 0): (0, 255, 0), (0, 271): (0, 0, 255), (479, 271): (255, 255, 255),
            (240, 136): (255, 128, 0), (1, 0): (8, 4, 8), (2, 0): (7, 3, 7), (3, 0): (248, 252, 248),
            (100, 100): (123, 45, 67),
        }
        for xy, c in pts.items():
            im.putpixel(xy, c)
        im.save(path)
        r = run_tool(path, "--index", "7", "--out-dir", self.d)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        name, size_expr, v, _ = parse_c(os.path.join(self.d, "img_07.c"))
        self.assertEqual(name, "g_img_07")
        self.assertEqual(len(v), W * H)
        for (x, y), c in pts.items():
            self.assertEqual(v[y * W + x], rgb565(*c), "pixel %s %s" % ((x, y), c))
        self.assertEqual(v[0], 0xF800)
        self.assertEqual(v[479], 0x07E0)
        self.assertEqual(v[271 * W], 0x001F)
        self.assertEqual(v[271 * W + 479], 0xFFFF)

    def test_wrong_sizes_exit_nonzero(self):
        for size in [(481, 272), (480, 271), (272, 480), (1, 1), (960, 544), (479, 272), (480, 273)]:
            im, path = self._png(size, "bad_%dx%d.png" % size)
            im.save(path)
            r = run_tool(path, "--index", "0", "--out-dir", self.d)
            self.assertEqual(r.returncode, 2, "%s: rc=%s %s" % (size, r.returncode, r.stderr))
            self.assertFalse(os.path.exists(os.path.join(self.d, "img_00.c")), "不正サイズで出力ファイルができた")

    def test_rgba_and_palette_inputs(self):
        for mode in ("RGBA", "P", "L"):
            im = Image.new(mode if mode != "P" else "RGB", (W, H), 255 if mode == "L" else (10, 20, 30, 255) if mode == "RGBA" else (10, 20, 30))
            if mode == "P":
                im = im.convert("P")
            path = os.path.join(self.d, "m_%s.png" % mode)
            im.save(path)
            r = run_tool(path, "--index", "3", "--out-dir", self.d)
            self.assertEqual(r.returncode, 0, "%s: %s" % (mode, r.stderr))
            _, _, v, _ = parse_c(os.path.join(self.d, "img_03.c"))
            self.assertEqual(len(v), W * H)

    def test_missing_file_and_bad_args(self):
        r = run_tool(os.path.join(self.d, "nothing.png"), "--out-dir", self.d)
        self.assertNotEqual(r.returncode, 0)
        r = run_tool("--out-dir", self.d)
        self.assertNotEqual(r.returncode, 0)
        im, path = self._png((W, H)); im.save(path)
        r = run_tool(path, "--index", "100", "--out-dir", self.d)
        self.assertNotEqual(r.returncode, 0)
        r = run_tool(path, "--index", "-1", "--out-dir", self.d)
        self.assertNotEqual(r.returncode, 0)


if __name__ == "__main__":
    unittest.main()
