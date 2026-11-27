# -*- coding: utf-8 -*-
"""c8png.py - CLUT8 実験(docs/design.md §19.4.3)。実機から dump した面(512x272, 8bit)とパレット(256x4バイト, LE 0xAARRGGBB)を PNG にし、基準と比べる。
  python tools/c8png.py png    <face.bin> <pal.bin> <out.png>   480x272 の PNG。RGB565 に切り捨ててから RGB888 へ複製(パネルに出る色)。右の32列が全部 idx 9 かも表示
  python tools/c8png.py cmp    <face.bin> <ref_face.bin>        139,264 バイトの比較(違いの数、最初の違い)
  python tools/c8png.py cmppal <pal.bin> <ref_pal.bin>          1,024 バイトの比較(違うエントリー番号)
  python tools/c8png.py sample <face.bin> <pal.bin>             箱8個の中心(x=35+58k, y=80)の idx と色
  python tools/c8png.py cmpface <dev_face> <dev_pal> <host_face> <host_pal>
                                                                製品(P2b): 表示面とパレットの比較。x < 480 の添字の一致、パネル色の一致(右の 32 列は見ない)。差があれば最初の位置
  python tools/c8png.py sheet  <fb_hostrender> <states_dir> <out.png> [C]
                                                                S0〜S7 のホストの C 描画を 2 列 x 4 行に並べる(V-0 の確認用。判定ではない)。末尾に C を付けると C0〜C11(時計・天気)を 3 列 x 4 行
終了コード: 0 = 一致/正常、1 = 違いあり、2 = 使い方・ファイルの誤り"""
import os
import struct
import sys

W, H, STRIDE = 480, 272, 512
FACE_BYTES = STRIDE * H
BOX_NAMES = ['赤', '緑', '青', '黄', 'シアン', 'マゼンタ', '白', '灰']


def read(path, size):
    d = open(path, 'rb').read()
    if len(d) != size:
        raise SystemExit('サイズ違い: %s = %d (期待 %d)' % (path, len(d), size))
    return d


def load_pal(path):
    return struct.unpack('<256I', read(path, 1024))


def to_panel(argb):
    """ARGB8888 -> パネルに出る色(RGB565 へ切り捨て -> RGB888 へ複製)"""
    r5, g6, b5 = (argb >> 19) & 31, (argb >> 10) & 63, (argb >> 3) & 31
    return ((r5 << 3) | (r5 >> 2), (g6 << 2) | (g6 >> 4), (b5 << 3) | (b5 >> 2))


def cmd_png(face_p, pal_p, out_p):
    from PIL import Image
    face, pal = read(face_p, FACE_BYTES), load_pal(pal_p)
    out = bytearray()
    for y in range(H):
        for x in range(W):
            out += bytes(to_panel(pal[face[y * STRIDE + x]]))
    Image.frombytes('RGB', (W, H), bytes(out)).save(out_p)
    remain = all(face[y * STRIDE + x] == 9 for y in range(H) for x in range(W, STRIDE))
    print('右の余り(x=480..511)が全部 idx 9: %s' % ('はい' if remain else 'いいえ'))
    print('保存: %s' % out_p)
    return 0


def cmd_cmp(a_p, b_p):
    a, b = read(a_p, FACE_BYTES), read(b_p, FACE_BYTES)
    diff = [i for i in range(FACE_BYTES) if a[i] != b[i]]
    print('違うバイト: %d / %d' % (len(diff), FACE_BYTES))
    if diff:
        i = diff[0]
        print('最初の違い: offset %d (x=%d, y=%d)  %d vs %d' % (i, i % STRIDE, i // STRIDE, a[i], b[i]))
    return 1 if diff else 0


def cmd_sheet(host, states_dir, out_p, work=None, prefix='S'):
    """S0〜S7 のホストの C 描画(fb_hostrender)を 2 列 x 4 行の 1 枚の PNG にする(V-0 の確認用。判定ではない)。prefix = 'C' なら C0〜C11 を 3 列 x 4 行(V-C0・V-C1)"""
    import subprocess
    import tempfile
    from PIL import Image
    work = work or tempfile.mkdtemp(prefix='c8sheet_')
    os.makedirs(work, exist_ok=True)
    gap = 16
    cols, count = (3, 12) if prefix == 'C' else (2, 8)
    sheet = Image.new('RGB', (cols * W + (cols + 1) * gap, 4 * H + 5 * gap), (52, 56, 64))
    for n in range(count):
        sf = os.path.join(states_dir, '%s%d.state' % (prefix, n))
        fb, pf = os.path.join(work, '%s%d.c8' % (prefix, n)), os.path.join(work, '%s%d.pal' % (prefix, n))
        subprocess.check_call([os.path.abspath(host), sf, fb, '--pal', pf])
        face, pal = read(fb, FACE_BYTES), load_pal(pf)
        im = Image.new('RGB', (W, H))
        im.putdata([to_panel(pal[face[y * STRIDE + x]]) for y in range(H) for x in range(W)])
        sheet.paste(im, (gap + (n % cols) * (W + gap), gap + (n // cols) * (H + gap)))
    sheet.save(out_p)
    print('保存: %s' % out_p)
    return 0


def cmd_cmppal(a_p, b_p):
    a, b = load_pal(a_p), load_pal(b_p)
    diff = [i for i in range(256) if a[i] != b[i]]
    print('違うエントリー: %d / 256' % len(diff))
    for i in diff[:16]:
        print('  [%d] %08X vs %08X' % (i, a[i], b[i]))
    return 1 if diff else 0


def face_diff(face_a, pal_a, face_b, pal_b):
    """x < 480 の添字の違いの数、最初の違い(x, y, 添字 a, 添字 b)、パネル色の違いの数"""
    n_idx, n_col, first = 0, 0, None
    for y in range(H):
        ra, rb = face_a[y * STRIDE:y * STRIDE + W], face_b[y * STRIDE:y * STRIDE + W]
        if ra == rb:
            continue
        for x in range(W):
            if ra[x] != rb[x]:
                n_idx += 1
                if first is None:
                    first = (x, y, ra[x], rb[x])
                if to_panel(pal_a[ra[x]]) != to_panel(pal_b[rb[x]]):
                    n_col += 1
    return n_idx, first, n_col


def cmd_cmpface(df_p, dp_p, hf_p, hp_p):
    n_idx, first, n_col = face_diff(read(df_p, FACE_BYTES), load_pal(dp_p), read(hf_p, FACE_BYTES), load_pal(hp_p))
    pal_diff = sum(1 for a, b in zip(load_pal(dp_p), load_pal(hp_p)) if a != b)
    print('添字の違い(x < 480): %d / %d  パネル色の違い: %d  パレットの違うエントリー: %d / 256' % (n_idx, W * H, n_col, pal_diff))
    if first:
        print('最初の違い: x=%d y=%d  実機 idx %d vs ホスト idx %d' % first)
    return 1 if (n_idx or pal_diff) else 0


def cmd_sample(face_p, pal_p):
    face, pal = read(face_p, FACE_BYTES), load_pal(pal_p)
    for k in range(8):
        x, y = 35 + 58 * k, 80
        idx = face[y * STRIDE + x]
        r, g, b = to_panel(pal[idx])
        print('箱%d(%s) x=%d y=%d idx=%d  ARGB=%08X  パネル色=#%02X%02X%02X' % (k + 1, BOX_NAMES[k], x, y, idx, pal[idx], r, g, b))
    return 0


def main(argv):
    if len(argv) >= 5 and argv[1] == 'sheet':
        return cmd_sheet(argv[2], argv[3], argv[4], prefix=(argv[5] if len(argv) >= 6 else 'S'))
    cmds = {'png': (cmd_png, 3), 'cmp': (cmd_cmp, 2), 'cmppal': (cmd_cmppal, 2), 'sample': (cmd_sample, 2), 'cmpface': (cmd_cmpface, 4)}
    if len(argv) < 2 or argv[1] not in cmds or len(argv) - 2 != cmds[argv[1]][1]:
        print(__doc__)
        return 2
    return cmds[argv[1]][0](*argv[2:])


if __name__ == '__main__':
    sys.exit(main(sys.argv))
