# -*- coding: utf-8 -*-
"""fb2png.py - 実機のフレームバッファ(RGB565 リトルエンディアン、480x272)を PNG にし、基準画像と画素単位で比べる。
使い方:
  python tools/fb2png.py png  <in.bin> <out.png>
  python tools/fb2png.py cmp  <a.(bin|png)> <b.(bin|png)>             # 一致率と不一致の範囲を表示
  python tools/fb2png.py check <gdb.log> <dump接頭辞(例 jobs/fb/A)> [--preview DIR] [--host EXE] [--c8]
     gdb.log の "==== <段>" の後ろの print 出力(s_ui.v など)から状態を読み、(1) 同じ状態をファームと同じ ui_render で PC 上に描いた画像(fb_hostrender)と、
     (2) tools/assets/bath/preview/fw_*.png(既定の対応表)と、段ごとに比べる。dump は <接頭辞>_<段>.bin。
     --c8 = 製品が CLUT8(8bpp。docs/design.md §20)の時。dump は <接頭辞>_<段>.bin(面 = GR2FLM2 から 139,264 バイト)と、パレット
     <接頭辞>_<段>.pal0.bin / .pal1.bin(0xE0800 / 0xE0C00 から各 1,024 バイト。使用中の側は GR2CLUTINT の SEL)。fb_hostrender(8bpp の面 + ui_pal)と、
     添字・パネル色・パレットを比べる(tools/c8png.py の face_diff)。host は tools/fb_hostrender.c を CLUT8 版でビルドしたもの。
RGB565 -> RGB888 は tests/host の PPM 出力と同じ展開(下位ビットに上位ビットを複製)。"""
import os
import re
import struct
import subprocess
import sys
from PIL import Image
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

W, H = 480, 272


def load(path):
    if path.lower().endswith('.bin'):
        d = open(path, 'rb').read()
        if len(d) != W * H * 2:
            raise SystemExit('サイズ違い: %s = %d (期待 %d)' % (path, len(d), W * H * 2))
        px = struct.unpack('<%dH' % (W * H), d)
        out = bytearray()
        for p in px:
            out += bytes((((p >> 11) << 3) | ((p >> 13) & 7),
                          (((p >> 5) & 63) << 2) | ((p >> 9) & 3),
                          ((p & 31) << 3) | ((p >> 2) & 7)))
        return Image.frombytes('RGB', (W, H), bytes(out))
    return Image.open(path).convert('RGB')


def clusters(a, b, cell=8):
    """不一致画素の範囲(bbox)を、近いもの同士(セル cell 画素、隣接を含む)でまとめて返す"""
    pa, pb = a.load(), b.load()
    cells = {}
    n = 0
    for y in range(H):
        for x in range(W):
            if pa[x, y] != pb[x, y]:
                n += 1
                cells.setdefault((x // cell, y // cell), []).append((x, y))
    seen, out = set(), []
    for k in cells:
        if k in seen:
            continue
        stack, pts = [k], []
        seen.add(k)
        while stack:
            c = stack.pop()
            pts += cells[c]
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    nb = (c[0] + dx, c[1] + dy)
                    if nb in cells and nb not in seen:
                        seen.add(nb)
                        stack.append(nb)
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        out.append((len(pts), min(xs), min(ys), max(xs), max(ys)))
    out.sort(reverse=True)
    return n, out


def compare(a, b):
    n, cl = clusters(a, b)
    return (1.0 - n / float(W * H)) * 100.0, n, cl


def fmt_cl(cl, k=4):
    s = '; '.join('%dpx (%d,%d)-(%d,%d)' % c for c in cl[:k])
    if len(cl) > k:
        s += ' ...(計%d塊)' % len(cl)
    return s


def parse_struct(s):
    """gdb の print 出力 "{scr = 0 '\\000', set = 42 '*', ...}" から 名前=整数 を取る"""
    return {m.group(1): int(m.group(2)) for m in re.finditer(r'(\w+) = (-?\d+)', s)}


def parse_stages(logpath):
    txt = open(logpath, encoding='utf-8-sig', errors='replace').read()
    stages, cur = {}, None
    for line in txt.splitlines():
        m = re.search(r'O ~"==== (\w+)\\n"', line)
        if m:
            cur = m.group(1)
            stages[cur] = []
            continue
        m = re.search(r'O ~"\$\d+ = (.*)\\n"$', line)
        if m and cur is not None:
            v = m.group(1).replace('\\\\', '\\').replace('\\"', '"')
            stages[cur].append(v)
    return stages


NAMES = ['tick', 'back', 'gr2flm2', 'v', 'ov', 'tr_on', 'tr_fwd', 'tr_dx', 'demo', 'fb_btn', 'fb_phase', 'dbg']
# 天気・Wi-Fi 入り(design §22.8)の gdb ログ: fb_phase の次に `print s_ui.wx` と `print s_ui.spin` を足したもの(長さで見分ける。従来のログも読める)
NAMES_WX = NAMES[:-1] + ['wx', 'spin', 'dbg']
WX_KEYS = {'net': 'wx.net', 'busy': 'wx.busy', 'have': 'wx.have', 'kind': 'wx.kind', 'temp_x10': 'wx.temp', 'stale': 'wx.stale',
           'clk_ext': 'wx.clkext', 'upd_min': 'wx.upd', 'clock_min': 'wx.clock', 'month': 'wx.month', 'day': 'wx.day', 'wday': 'wx.wday'}


def first_int(s):
    return int(re.match(r'-?\d+', s).group(0))


def stage_state(vals):
    d = dict(zip(NAMES_WX if len(vals) >= len(NAMES_WX) else NAMES, vals))
    st = {'tick': first_int(d['tick']), 'gr2': int(d['gr2flm2'], 16)}
    for k in ('v', 'ov'):
        for kk, vv in parse_struct(d[k]).items():
            st['%s.%s' % (k, kk)] = vv
    for k in ('tr_on', 'tr_fwd', 'tr_dx', 'demo', 'fb_btn', 'fb_phase'):
        st[k] = first_int(d[k])
    if 'wx' in d:
        for kk, vv in parse_struct(d['wx']).items():
            if kk in WX_KEYS:
                st[WX_KEYS[kk]] = vv
        st['wx.spin'] = first_int(d['spin'])
    dbg = parse_struct(d['dbg'])
    st['frames'] = dbg.get('frames')
    st['uf'] = dbg.get('uf')
    return st


PREVIEW = {'home': 'fw_home.png', 'fill': 'fw_fill_run.png', 'reheat': 'fw_reheat_run.png',
           'usage': 'fw_usage.png', 'standby': 'fw_standby.png'}
KEYMAP = [('v.scr', 'v.scr'), ('v.set', 'v.set'), ('v.bath_x10', 'v.bath'), ('v.fill_x10', 'v.fill'), ('v.flow_x10', 'v.flow'),
          ('v.clock_min', 'v.clock'), ('v.bar_ms', 'v.bar'), ('ov.scr', 'ov.scr'), ('ov.set', 'ov.set'), ('ov.bath_x10', 'ov.bath'),
          ('ov.fill_x10', 'ov.fill'), ('ov.flow_x10', 'ov.flow'), ('ov.clock_min', 'ov.clock'), ('ov.bar_ms', 'ov.bar')]


def write_state(path, st):
    """fb_hostrender の状態ファイル。wx.* は gdb ログに s_ui.wx があった時だけ(無ければ ui_init の既定 = OFFLINE・未取得・旧デモ時計)"""
    with open(path, 'w') as fo:
        for src, dst in KEYMAP:
            fo.write('%s=%d\n' % (dst, st.get(src, 0)))
        for k in ('tr_on', 'tr_fwd', 'tr_dx', 'demo', 'fb_btn', 'fb_phase'):
            fo.write('%s=%d\n' % (k, st[k]))
        for k in sorted(k for k in st if k.startswith('wx.')):
            fo.write('%s=%d\n' % (k, st[k]))


def check_c8(argv):
    """CLUT8 版の check。実機の表示面 + パレットと、ホスト描画(同じ状態・同じ ui_render・同じ ui_pal)を、添字まで比べる(H-D / G-D2)"""
    import c8png
    log, prefix = argv[2], argv[3]
    host = argv[argv.index('--host') + 1] if '--host' in argv else 'jobs/fb/fb_hostrender.exe'
    print('| 段 | tick | 表示面 GR2FLM2 | 状態 | 添字の違い(x<480) | パネル色の違い | パレット(使用中の側) |')
    print('|---|---|---|---|---|---|---|')
    for name, vals in parse_stages(log).items():
        f = '%s_%s.bin' % (prefix, name)
        if len(vals) < len(NAMES) or not os.path.exists(f):         # 従来(12 個)でも wx 入り(14 個)でも読める
            continue
        st = stage_state(vals)
        face = c8png.read(f, c8png.FACE_BYTES)
        pals = [c8png.load_pal(p) for p in ('%s_%s.pal0.bin' % (prefix, name), '%s_%s.pal1.bin' % (prefix, name)) if os.path.exists(p)]
        sf = '%s_%s.state.txt' % (prefix, name)
        write_state(sf, st)
        hf, hp = '%s_%s.host.c8' % (prefix, name), '%s_%s.host.pal' % (prefix, name)
        subprocess.check_call([os.path.abspath(host), sf, hf, '--pal', hp])
        hface, hpal = c8png.read(hf, c8png.FACE_BYTES), c8png.load_pal(hp)
        which = [i for i, p in enumerate(pals) if tuple(p) == tuple(hpal)]
        used = pals[which[0]] if which else (pals[0] if pals else hpal)
        n_idx, first, n_col = c8png.face_diff(face, used, hface, hpal)
        s = 'scr=%d set=%d bath=%d fill=%d flow=%d bar=%d tr=%d demo=%d' % (
            st['v.scr'], st['v.set'], st['v.bath_x10'], st['v.fill_x10'], st['v.flow_x10'], st['v.bar_ms'], st['tr_on'], st['demo'])
        print('| %s | %d | 0x%x | %s | %d%s | %d | %s |' % (name, st['tick'], st['gr2'], s, n_idx,
              (' 最初 x=%d y=%d' % first[:2]) if first else '', n_col,
              ('pal%d = ui_pal' % which[0]) if which else 'どちらも ui_pal と違う'))
    return 0


def check(argv):
    if '--c8' in argv:
        return check_c8(argv)
    log, prefix = argv[2], argv[3]
    prev = 'tools/assets/bath/preview'
    host = 'jobs/fb/fb_hostrender.exe'
    if '--preview' in argv:
        prev = argv[argv.index('--preview') + 1]
    if '--host' in argv:
        host = argv[argv.index('--host') + 1]
    print('| 段 | tick | 表示面 GR2FLM2 | 状態 | 実機 vs ホスト再描画(同状態) | 実機 vs preview |')
    print('|---|---|---|---|---|---|')
    for name, vals in parse_stages(log).items():
        f = '%s_%s.bin' % (prefix, name)
        if len(vals) < len(NAMES) or not os.path.exists(f):         # 従来(12 個)でも wx 入り(14 個)でも読める
            continue
        st = stage_state(vals)
        img = load(f)
        img.save('%s_%s.png' % (prefix, name))
        sf = '%s_%s.state.txt' % (prefix, name)
        write_state(sf, st)
        hb = '%s_%s.host.bin' % (prefix, name)
        subprocess.check_call([os.path.abspath(host), sf, hb])
        hi = load(hb)
        hi.save('%s_%s.host.png' % (prefix, name))
        ph, nh, clh = compare(img, hi)
        pv = '-'
        key = name.split('_')[0]
        if key in PREVIEW and os.path.exists(os.path.join(prev, PREVIEW[key])):
            pp, npv, clp = compare(img, load(os.path.join(prev, PREVIEW[key])))
            pv = '%.2f%% (%dpx) %s' % (pp, npv, fmt_cl(clp, 3))
        s = 'scr=%d set=%d bath=%d fill=%d flow=%d bar=%d tr=%d demo=%d' % (
            st['v.scr'], st['v.set'], st['v.bath_x10'], st['v.fill_x10'], st['v.flow_x10'], st['v.bar_ms'], st['tr_on'], st['demo'])
        print('| %s | %d | 0x%x | %s | %.3f%% (%dpx) %s | %s |' % (name, st['tick'], st['gr2'], s, ph, nh, fmt_cl(clh, 3), pv))
    return 0


def main(argv):
    if len(argv) >= 4 and argv[1] == 'png':
        load(argv[2]).save(argv[3])
        return 0
    if len(argv) >= 4 and argv[1] == 'cmp':
        p, n, cl = compare(load(argv[2]), load(argv[3]))
        print('一致率 %.3f%% 不一致 %d画素 %s' % (p, n, fmt_cl(cl)))
        return 0
    if len(argv) >= 4 and argv[1] == 'check':
        return check(argv)
    print(__doc__)
    return 2


if __name__ == '__main__':
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except Exception:
        pass
    sys.exit(main(sys.argv))
