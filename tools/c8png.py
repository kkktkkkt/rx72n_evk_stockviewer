# -*- coding: utf-8 -*-
"""c8png.py - CLUT8 実験(docs/design.md §19.4.3)。実機から dump した面(512x272, 8bit)とパレット(256x4バイト, LE 0xAARRGGBB)を PNG にし、基準と比べる。
  python tools/c8png.py png    <face.bin> <pal.bin> <out.png>   480x272 の PNG。RGB565 に切り捨ててから RGB888 へ複製(パネルに出る色)。右の32列が全部 idx 9 かも表示
  python tools/c8png.py cmp    <face.bin> <ref_face.bin>        139,264 バイトの比較(違いの数、最初の違い)
  python tools/c8png.py cmppal <pal.bin> <ref_pal.bin>          1,024 バイトの比較(違うエントリー番号)
  python tools/c8png.py sample <face.bin> <pal.bin>             箱8個の中心(x=35+58k, y=80)の idx と色
  python tools/c8png.py cmpface <dev_face> <dev_pal> <host_face> <host_pal>
                                                                製品(P2b): 表示面とパレットの比較。x < 480 の添字の一致、パネル色の一致(右の 32 列は見ない)。差があれば最初の位置
  python tools/c8png.py gallery <fb_hostrender> <out.png> [--workdir DIR]
                                                                天気・Wi-Fi の新しい状態(天気 8 種、Wi-Fi 4 状態、更新アイコンの回転 4 段階、未取得・エラー・古い値・負の気温、押下)を
                                                                ホストの C 描画(fb_hostrender)で描き、見やすく切り出して 1 枚の PNG にする(目視用。判定ではない)
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


def gallery_states():
    """(名前, 状態ファイルの行)の一覧。home 標準 = 同期済み・晴れ・ONLINE。キーは tools/fb_hostrender.c の wx.*"""
    base = ['v.scr=0', 'v.set=42', 'v.bath=400', 'v.clock=804', 'wx.clkext=1', 'wx.clock=804', 'wx.month=11', 'wx.day=25', 'wx.wday=3',
            'wx.have=1', 'wx.net=2', 'wx.temp=126', 'wx.upd=804', 'wx.kind=0']

    def st(**kw):
        d = dict(l.split('=') for l in base)
        d.update({k.replace('_', '.', 1): str(v) for k, v in kw.items()})
        return ['%s=%s' % kv for kv in d.items()]
    out = []
    for i, k in enumerate(['sun', 'partly', 'cloud', 'fog', 'rain', 'snow', 'thunder', 'unknown']):
        out.append(('wx_' + k, st(wx_kind=i)))
    for i, k in enumerate(['off', 'conn', 'on', 'err']):
        out.append(('wifi_' + k, st(wx_net=i)))
    for i in range(4):
        out.append(('spin_%d' % i, st(wx_busy=1, wx_spin=i)))
    out.append(('none', st(wx_have=0, wx_net=0, wx_clock=65535, wx_upd=65535, wx_month=0, v_clock=65535)))
    out.append(('stale', st(wx_stale=1, wx_upd=760, wx_net=3, wx_kind=4)))
    out.append(('negative', st(wx_temp=-12, wx_kind=5)))
    out.append(('minus10', st(wx_temp=-105, wx_kind=6)))
    out.append(('legacy', ['v.scr=0', 'v.set=42', 'v.bath=400', 'v.clock=1305']))          # APP_WX = 0 相当(旧デモ時計・OFFLINE・未取得)
    return out


def cmd_gallery(host, out_p, work):
    import os
    import subprocess
    from PIL import Image
    os.makedirs(work, exist_ok=True)
    tiles = []
    for name, lines in gallery_states():
        sf, fb, pf = (os.path.join(work, 'g_%s%s' % (name, e)) for e in ('.state.txt', '.c8', '.pal'))
        open(sf, 'w').write('\n'.join(lines) + '\n')
        subprocess.check_call([os.path.abspath(host), sf, fb, '--pal', pf])
        face, pal = read(fb, FACE_BYTES), load_pal(pf)
        im = Image.new('RGB', (W, H))
        im.putdata([to_panel(pal[face[y * STRIDE + x]]) for y in range(H) for x in range(W)])
        # 見たい所だけ: ヘッダー左(日付・Wi-Fi。x 0..240, y 0..34)と、天気の行(x 0..290, y 150..190)。3 倍
        hd = im.crop((0, 0, 240, 34)).resize((720, 102), Image.NEAREST)
        wx = im.crop((0, 150, 290, 190)).resize((870, 120), Image.NEAREST)
        t = Image.new('RGB', (870, 102 + 120 + 8), (255, 255, 255))
        t.paste(hd, (0, 0))
        t.paste(wx, (0, 110))
        tiles.append((name, t))
    cols = 2
    rows = (len(tiles) + cols - 1) // cols
    cw, ch = 870 + 10, 230 + 10
    sheet = Image.new('RGB', (cols * cw, rows * ch), (90, 90, 90))
    for i, (name, t) in enumerate(tiles):
        sheet.paste(t, ((i % cols) * cw + 5, (i // cols) * ch + 5))
    sheet.save(out_p)
    print('保存: %s(%s)' % (out_p, ' '.join(n for n, _ in tiles)))
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
    if len(argv) >= 4 and argv[1] == 'gallery':
        work = argv[argv.index('--workdir') + 1] if '--workdir' in argv else os.path.join(os.path.dirname(os.path.abspath(argv[3])), 'gallery_work')
        return cmd_gallery(argv[2], argv[3], work)
    cmds = {'png': (cmd_png, 3), 'cmp': (cmd_cmp, 2), 'cmppal': (cmd_cmppal, 2), 'sample': (cmd_sample, 2), 'cmpface': (cmd_cmpface, 4)}
    if len(argv) < 2 or argv[1] not in cmds or len(argv) - 2 != cmds[argv[1]][1]:
        print(__doc__)
        return 2
    return cmds[argv[1]][0](*argv[2:])


if __name__ == '__main__':
    sys.exit(main(sys.argv))
