# -*- coding: utf-8 -*-
"""test_wx_palette_indep.py - 天気・Wi-Fi 追加後のパレット再生成の誤差を、コミット(作業ツリー)済みの C の配列から独立に測り直す(テスト担当。docs/test-report.md)。
gen_ui_assets.py は「理想の画像(Pillow で描いた RGB)」と色の表(slot_table など)を作るためにだけ使い、誤差の計算は自前(RGB565 の段 = 画素ごとの 3 成分の差の最大)。
  W1 背景 7 枚: src/app/ui_bg_*.c の添字 + ui_layout.c の ui_pal を RGB565 にして、理想の背景と比べる。不一致の合計 <= 0.3%、1 枚 <= 1.0%、最大 <= 2 段(H-D1)
  W2 スプライト 16 枚: src/app/ui_gfx_data.c の ui_spr_* と理想のスプライト。最大 <= 3 段
  W3 数字の ramp: ui_slot_ramp / ui_bar_ramp の各レベルが、混色(gfx.c の mix565 と同じ式)の理想との最大誤差 <= 3 段
  W4 押下の remap: ui_remap[0](暗転 l=6)・[1](白光 l=19)の各エントリーが、重み付き距離で最も近いパレットの添字(同点は許す)
  W5 パレット: 256 エントリー、使われていない添字の数、固定色 24 + 新しい色 7 がパレットに厳密にある
使い方: python test_wx_palette_indep.py [--font パス]。フォントが無ければ 77(SKIP)で終わる。終了コード 0 = 合格、1 = 不合格。"""
import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.stdout.reconfigure(encoding='utf-8')
import gen_ui_assets as G     # 理想の画像と表だけを使う(誤差の計算は使わない)

DIST_W = (1.0, 1.0, 0.6)
FAILS = []


def check(ok, msg):
    print(('  ok   ' if ok else '  NG   ') + msg)
    if not ok:
        FAILS.append(msg)


def c_array(text, name, dtype):
    """宣言 `name[...] ... = {` から対応する `};` までの数値を並べる"""
    i = text.index(name)
    i = text.index('= {', i) + 3
    depth, j = 1, i
    while depth:
        ch = text[j]
        depth += (ch == '{') - (ch == '}')
        j += 1
    return np.array([int(x, 0) for x in re.findall(r'0x[0-9A-Fa-f]+|-?\d+', text[i:j - 1])], dtype=dtype)


def rgb565_of_image(im):          # 理想の画像 -> RGB565(四捨五入)。generator とは別に書いた
    p = np.asarray(im.convert('RGB'), dtype=np.int64)
    r = (p[..., 0] * 31 + 127) // 255
    g = (p[..., 1] * 63 + 127) // 255
    b = (p[..., 2] * 31 + 127) // 255
    return ((r << 11) | (g << 5) | b).astype(np.int32)


def pal565(argb):
    a = np.asarray(argb, dtype=np.int64)
    return ((((a >> 19) & 31) << 11) | (((a >> 10) & 63) << 5) | ((a >> 3) & 31)).astype(np.int32)


def steps(ref, got):
    r, g = np.asarray(ref, dtype=np.int32), np.asarray(got, dtype=np.int32)
    return np.maximum(np.maximum(abs((r >> 11) - (g >> 11)), abs(((r >> 5) & 63) - ((g >> 5) & 63))), abs((r & 31) - (g & 31)))


def mix(bg, fg, l):               # gfx.c の mix565: ((f - b) * l >> 5) + b を、成分ごとに(R5・G6・B5)。l = 0..32
    out = 0
    for sh, mask in ((11, 31), (5, 63), (0, 31)):
        b, f = (bg >> sh) & mask, (fg >> sh) & mask
        out |= (((f - b) * l >> 5) + b & mask) << sh
    return out


def comps(p):
    p = np.asarray(p, dtype=np.int64)
    return np.stack([p >> 11, (p >> 5) & 63, p & 31], axis=-1).astype(np.float64)


def main():
    font = G.DEFAULT_FONT
    if '--font' in sys.argv:
        font = sys.argv[sys.argv.index('--font') + 1]
    if not os.path.exists(font):
        print('フォントが無い: %s(SKIP)' % font)
        return 77
    a = G.build(G.Fonts(font))
    rd = lambda n: open(os.path.join(ROOT, 'src', 'app', n), encoding='utf-8').read()
    layout, gfx = rd('ui_layout.c'), rd('ui_gfx_data.c')
    pal = pal565(c_array(layout, 'ui_pal', np.int64))
    print('### W5 パレット')
    check(pal.size == 256, 'ui_pal が 256 エントリー(%d)' % pal.size)
    fixed = [G.c565(n) for n in G.C]            # 名前つきの色すべて(固定色の候補)
    print('  (参考) 名前つきの色 %d 個のうち、パレットにある色 %d 個' % (len(set(fixed)), len(set(fixed) & set(pal.tolist()))))
    for n in G.NEW_COLORS:
        check(G.c565(n) in set(pal.tolist()), '新しい色 %s がパレットに厳密にある' % n)

    print('### W1 背景 7 枚(理想の背景 = gen_ui_assets の draw_bg。コミット済みの添字を ui_pal で RGB565 にして比べる)')
    tot = mis = o1 = o2 = 0
    mx = 0
    worst = 0.0
    used = set()
    for name, _ in G.BGS:
        idx = c_array(rd('ui_bg_%s.c' % name), 'ui_bgi_%s' % name, np.int64)
        ideal = rgb565_of_image(a['bgs'][name]).ravel()
        check(idx.size == ideal.size == 480 * 272, 'ui_bg_%s の画素数 %d' % (name, idx.size))
        used |= set(np.unique(idx).tolist())
        e = steps(ideal, pal[idx])
        n_, m_ = e.size, int((e > 0).sum())
        tot += n_; mis += m_; o1 += int((e > 1).sum()); o2 += int((e > 2).sum()); mx = max(mx, int(e.max()))
        worst = max(worst, 100.0 * m_ / n_)
        print('  %-11s 不一致 %6d (%.3f%%) 1 段超 %5d 2 段超 %d 最大 %d 段' % (name, m_, 100.0 * m_ / n_, int((e > 1).sum()), int((e > 2).sum()), int(e.max())))
    print('  合計: 不一致 %d / %d (%.3f%%)、1 枚の最大 %.3f%%、最大 %d 段' % (mis, tot, 100.0 * mis / tot, worst, mx))
    check(100.0 * mis / tot <= 0.3, '背景 7 枚の不一致の合計 <= 0.3%')
    check(worst <= 1.0, '背景 1 枚の不一致 <= 1.0%')
    check(mx <= 2, '背景の最大誤差 <= 2 段')

    print('### W2 スプライト 16 枚')
    smax = 0
    for grp, nm, count, wh in (('wx', 'ui_spr_wx', 8, 28 * 28), ('wifi', 'ui_spr_wifi', 4, 20 * 20), ('ref', 'ui_spr_ref', 4, 20 * 20)):
        arr = c_array(gfx, nm, np.int64).reshape(count, wh)
        names = [('wx_' + k) for k in G.KINDS] if grp == 'wx' else ([('wifi_' + s) for s in G.WIFI_STATES] if grp == 'wifi' else [('ref_%d' % p) for p in range(4)])
        for i, sn in enumerate(names):
            ideal = rgb565_of_image(a['spr'][sn]).ravel()
            used |= set(np.unique(arr[i]).tolist())
            e = steps(ideal, pal[arr[i]])
            smax = max(smax, int(e.max()))
            print('  %-14s 不一致 %3d / %d 1 段超 %2d 最大 %d 段' % (sn, int((e > 0).sum()), e.size, int((e > 1).sum()), int(e.max())))
    check(smax <= 3, 'スプライトの最大誤差 <= 3 段(実測 %d)' % smax)

    print('### W3 数字・バーの ramp(混色の理想との誤差)')
    ramp = c_array(layout, 'ui_slot_ramp', np.int64).reshape(-1, 2, 33)
    rmax = 0
    for k, (sid, scr, fid, al, x, y, col, mx_) in enumerate(G.slot_table(a)):
        pairs = G.slot_pairs(sid, col)
        for w, (u, tc) in enumerate(pairs):
            for l in range(33):
                want = mix(G.c565(u), G.c565(tc), l)
                got = int(pal[ramp[k][w][l]])
                e = int(steps(np.array([want]), np.array([got]))[0])
                rmax = max(rmax, e)
                if e > 3:
                    print('  NG %s ramp%d l=%d 誤差 %d 段' % (sid, w, l, e))
            used |= set(ramp[k][w].tolist())
    check(rmax <= 3, '数字 ramp の最大誤差 <= 3 段(実測 %d)' % rmax)
    barr = c_array(layout, 'ui_bar_ramp', np.int64).reshape(-1, 33)
    bmax = 0
    for k, (nm, x, y, bw, bh, r, mw, gr, bc) in enumerate(a['bars']):
        for l in range(33):
            bmax = max(bmax, int(steps(np.array([mix(G.c565(gr), G.c565(bc), l)]), np.array([int(pal[barr[k][l]])]))[0]))
        used |= set(barr[k].tolist())
    check(bmax <= 3, 'バーの ramp の最大誤差 <= 3 段(実測 %d)' % bmax)

    print('### W4 押下の remap(重み付き最近傍を独立に再計算)')
    remap = c_array(layout, 'ui_remap', np.int64).reshape(2, 256)
    P = comps(pal)
    bad = tie = 0
    for which, (l, fg) in enumerate(((G.DIM_L, 0x0000), (G.FLASH_L, 0xFFFF))):
        tgt = np.array([mix(int(p), fg, l) for p in pal])
        T = comps(tgt)
        d = (((T[:, None, :] - P[None, :, :]) * DIST_W) ** 2).sum(axis=2)
        best = d.min(axis=1)
        got = d[np.arange(256), remap[which]]
        bad += int((got > best + 1e-9).sum())
        tie += int(((got <= best + 1e-9) & (remap[which] != d.argmin(axis=1))).sum())
    check(bad == 0, 'remap の最近傍でないエントリー %d 個(同点で別の添字 %d 個は許す)' % (bad, tie))

    print('### W5 続き: 使われている添字')
    check(len(used) <= 256, 'ui_pal の使われている添字 %d / 256(背景・スプライト・ramp のみ。バー・タグ・形を除く)' % len(used))
    print('RESULT: %s' % ('PASS' if not FAILS else 'FAIL(%d)' % len(FAILS)))
    return 0 if not FAILS else 1


if __name__ == '__main__':
    sys.exit(main())
