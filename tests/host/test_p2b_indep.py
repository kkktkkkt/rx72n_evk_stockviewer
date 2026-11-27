# -*- coding: utf-8 -*-
"""test_p2b_indep.py - P2b(CLUT8)の独立検証(テスト担当。docs/test-report.md §11)。
天気・Wi-Fi の追加(design §22.8)後: P1 に無い矩形(ホームの日付・天気の行、Wi-Fi アイコン)は judge_p2b.mask_of で比べない。静的の不一致の上限は 0.5% -> 0.7%。
基準は **コミット 0368fe3(P1)の生成物とコード**を git archive で取り出したもの(tests/host/data/p1_ref.npz は使わない)。
  I1 パレット・背景 7 枚: P1 の背景と P2b の背景(パレットを通したパネル色)の誤差が設計 §20.3 の範囲内(不一致 <= 0.3%、1枚 <= 1.0%、最大 <= 2 段)
  I2 固定色: P1 の背景のどれかで 200 画素以上ある色は、すべてパレットにある(誤差 0 になる)
  I3 ui_remap(暗転 l=6 / 白光 l=19): 各パレット色 p について、mix565(p, 黒/白, l) に最も近い(重み R1 G1 B0.6)色の添字になっている。目標色がパレットにあれば厳密
  I4 gen_ui_assets.py の自己検査 check_c8: 正常なら違反なし。壊したパレット(255 色・固定色の差し替え・背景の添字の乱れ・大きな誤差)は違反として検出する
  I5 描画の比較: P1 の fb_hostrender(0368fe3 のコード)と P2b の fb_hostrender に同じ状態(静的・デモ・押下/白光・スライド、乱数。数字の網羅的な走査)を与え、パネル色の誤差が
     静的・デモ・スライド: 最大 <= 3 段、2 段超 <= 0.01%、不一致 <= 0.5% / 押下・白光: 2 段超 <= 0.1%、最大 <= 8 段 / 3 段以上の画素は x >= 430(満タンのバーの右端の角)だけ
使い方: python test_p2b_indep.py [作業フォルダ]    環境変数 CC(既定 gcc)。終了コード 0 = 合格、1 = 不合格、77 = 実行できず(P1 のコミットが無い、gcc が無い)"""
import collections, io, os, random, re, subprocess, sys, tarfile, tempfile
from concurrent.futures import ThreadPoolExecutor
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.dont_write_bytecode = True
import judge_p2b as J          # P1 に無い天気・Wi-Fi の矩形(mask_of)。P1 との比較からは除く
ROOT = os.path.normpath(os.path.join(HERE, '..', '..'))
P1_COMMIT = '0368fe3'
W, H, S = 480, 272, 512
NAMES = ['home', 'fill_run', 'fill_done', 'reheat_run', 'reheat_done', 'usage', 'stby']
FAILS = []


def check(cond, msg):
    print(('  ok   ' if cond else '  NG   ') + msg)
    if not cond:
        FAILS.append(msg)


def num_array(text, dtype):
    body = text[text.index('= {') + 3:text.rindex('}')]
    return np.array([int(x, 0) for x in re.findall(r'0x[0-9A-Fa-f]+|\d+', body)], dtype=dtype)


def pal565_of(pal):
    a = np.asarray(pal, dtype=np.uint32)
    return ((((a >> 19) & 31) << 11) | (((a >> 10) & 63) << 5) | ((a >> 3) & 31)).astype(np.uint16)


def steps(ref, got):
    r, g = ref.astype(np.int32), got.astype(np.int32)
    return np.maximum(np.maximum(abs((r >> 11) - (g >> 11)), abs(((r >> 5) & 63) - ((g >> 5) & 63))), abs((r & 31) - (g & 31)))


def mix565(bg, fg, a5):          # gfx.c の mix565 と同じ式(uint32 の巻き戻りを含む)
    M = 0x07E0F81F
    b = (bg | (bg << 16)) & M
    f = (fg | (fg << 16)) & M
    m = ((((((f - b) & 0xFFFFFFFF) * a5) & 0xFFFFFFFF) >> 5) + b) & M
    return (m | (m >> 16)) & 0xFFFF


def c_table(text, name, count):
    m = re.search(name + r'[^=]*=\s*\{(.*?)\n\};', text, re.S)
    nums = [int(x) for x in re.findall(r'-?\d+', re.sub(r'/\*.*?\*/', '', m.group(1)))]
    assert len(nums) == count, (name, len(nums))
    return nums


# ---------------------------------------------------------------- P1 の取り出し
def git_archive(workdir):
    p = subprocess.run(['git', '-C', ROOT, 'archive', '--format=tar', P1_COMMIT, 'src/app', 'tools/fb_hostrender.c'], capture_output=True)
    if p.returncode != 0:
        return None
    d = os.path.join(workdir, 'p1')
    os.makedirs(d, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(p.stdout)) as t:
        t.extractall(d)
    return d


def build(cc, src_root, out, extra_inc=''):
    app = os.path.join(src_root, 'src', 'app')
    srcs = [os.path.join(src_root, 'tools', 'fb_hostrender.c')] + [os.path.join(app, f) for f in
            ['ui_render.c', 'ui_logic.c', 'gfx.c', 'ui_gfx_data.c', 'ui_layout.c', 'nav_logic.c'] + ['ui_bg_%s.c' % n for n in NAMES]]
    r = subprocess.run([cc, '-std=gnu99', '-O1', '-I', app] + srcs + ['-o', out], capture_output=True, text=True)
    if r.returncode != 0:
        print(r.stdout, r.stderr)
    return r.returncode == 0


# ---------------------------------------------------------------- 状態の生成
def mkstate(d):
    keys = ['v.scr', 'v.set', 'v.bath', 'v.fill', 'v.flow', 'v.clock', 'v.bar', 'ov.scr', 'ov.set', 'ov.bath', 'ov.fill', 'ov.flow', 'ov.clock', 'ov.bar',
            'tr_on', 'tr_fwd', 'tr_dx', 'demo', 'fb_btn', 'fb_phase']
    return ''.join('%s=%d\n' % (k, d.get(k, 0)) for k in keys)


def rnd_vals(rng, scr, pre):
    return {pre + 'scr': scr, pre + 'set': rng.randint(35, 48), pre + 'bath': rng.choice([rng.randint(0, 999), rng.randint(300, 480), rng.randint(380, 480)]),
            pre + 'fill': rng.choice([rng.randint(0, 1000), rng.randint(0, 1000), 1000, rng.randint(0, 120)]), pre + 'flow': rng.randint(0, 999),
            pre + 'clock': rng.randint(0, 1439), pre + 'bar': rng.choice([rng.randint(0, 640), 640, 0])}


BTN = {0: range(1, 8), 1: range(8, 11), 2: range(11, 14), 3: [14], 4: [15]}


def gen_states(seed=20261122):
    rng = random.Random(seed)
    out = []
    for i in range(200):
        out.append(('static', rnd_vals(rng, i % 5, 'v.')))
    for i in range(40):
        d = rnd_vals(rng, i % 4, 'v.'); d['demo'] = 1; out.append(('static', d))
    for scr in range(5):
        for b in BTN[scr]:
            for ph in (1, 2):
                for demo in (0, 1):
                    d = rnd_vals(rng, scr, 'v.'); d.update({'fb_btn': b, 'fb_phase': ph, 'demo': demo}); out.append(('press', d))
    for i in range(120):
        a, b = rng.sample(range(5), 2)
        d = rnd_vals(rng, b, 'v.'); d.update(rnd_vals(rng, a, 'ov.'))
        d.update({'tr_on': 1, 'tr_fwd': rng.randint(0, 1), 'tr_dx': rng.choice([1, 17, 60, 120, 200, 240, 300, 359, 400, 479, 480]), 'demo': rng.randint(0, 1)})
        pressed = rng.random() < 0.3
        if pressed:
            d.update({'fb_btn': rng.choice(list(BTN[a]) + list(BTN[b])), 'fb_phase': rng.randint(1, 2)})
        out.append(('press' if pressed else 'slide', d))
    base = lambda scr, **kw: dict({'v.scr': scr, 'v.set': 42, 'v.bath': 400, 'v.clock': 600, 'v.flow': 85}, **kw)   # 数字の走査
    for f in range(0, 1001, 7):
        out.append(('static', base(1, **{'v.fill': f, 'v.bath': 380 + f % 60})))
    for b in range(300, 500, 5):
        for s in (35, 42, 48):
            out.append(('static', base(2, **{'v.bath': b, 'v.set': s})))
    for fl in range(0, 1000, 11):
        out.append(('static', base(3, **{'v.flow': fl, 'v.bar': 640})))
    for c in range(0, 1440, 37):
        out.append(('static', base(0, **{'v.clock': c, 'v.set': 35 + c % 14, 'v.bath': (c * 7) % 1000})))
        out.append(('static', base(4, **{'v.clock': c})))
    return out


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    work = sys.argv[1] if len(sys.argv) > 1 else tempfile.mkdtemp(prefix='p2b_indep_')
    os.makedirs(work, exist_ok=True)
    cc = os.environ.get('CC', 'gcc')
    p1 = git_archive(work)
    if p1 is None:
        print('SKIP: git archive %s が使えない(P1 のコミットが無い)' % P1_COMMIT)
        return 77
    lay = open(os.path.join(ROOT, 'src', 'app', 'ui_layout.c'), encoding='utf-8').read()
    pal = np.array([int(x, 16) for x in re.findall(r'0x[0-9A-Fa-f]{8}', lay[lay.index('ui_pal[UI_PAL_N]'):lay.index('}', lay.index('ui_pal[UI_PAL_N]'))])], dtype=np.uint32)
    check(pal.size == 256, 'ui_layout.c の ui_pal が 256 エントリー(%d)' % pal.size)
    pp = pal565_of(pal)

    print('### I1 背景7枚: P1(%s)との誤差' % P1_COMMIT)
    tot = mis = o2 = 0; mx = 0; flat = set(); worst = 0.0
    for n in NAMES:
        a = num_array(open(os.path.join(p1, 'src', 'app', 'ui_bg_%s.c' % n), encoding='utf-8').read(), np.uint16)
        b = num_array(open(os.path.join(ROOT, 'src', 'app', 'ui_bg_%s.c' % n), encoding='utf-8').read(), np.uint8)
        check(a.size == b.size == W * H, 'ui_bg_%s: 画素数 %d(P1)/ %d(P2b)' % (n, a.size, b.size))
        e = steps(a, pp[b]).reshape(H, W)
        if n == 'home':           # 日付(動的になった)と天気の行は P1 に無い。その矩形は比べない
            e = e[~J.mask_of('v.scr=0\n')]
        tot += e.size; mis += int((e > 0).sum()); o2 += int((e > 2).sum()); mx = max(mx, int(e.max())); worst = max(worst, 100.0 * (e > 0).sum() / e.size)
        v, c = np.unique(a, return_counts=True)
        flat |= set(v[c >= 200].tolist())
    print('  不一致 %d / %d = %.4f%%、1枚の最大 %.3f%%、最大 %d 段' % (mis, tot, 100.0 * mis / tot, worst, mx))
    check(100.0 * mis / tot <= 0.3, '背景7枚の不一致 <= 0.3%')
    check(worst <= 1.0, '背景1枚の不一致 <= 1.0%')
    check(mx <= 2 and o2 == 0, '背景の最大 <= 2 段')

    print('### I2 固定色: P1 の背景で 200 画素以上ある色(%d 色)がパレットにある' % len(flat))
    check(flat <= set(pp.tolist()), '平らな色がすべてパレットにある(欠け: %s)' % sorted(hex(c) for c in flat - set(pp.tolist())))

    print('### I3 ui_remap が「目標色に最も近い添字」(独立に再計算)')
    rm = np.array(c_table(lay, r'ui_remap\[2\]\[UI_PAL_N\]', 512)).reshape(2, 256)
    comp = np.stack([pp.astype(np.int64) >> 11, (pp.astype(np.int64) >> 5) & 63, pp.astype(np.int64) & 31], -1)
    wts = np.array([1.0, 1.0, 0.6])
    for k, (fg, l, nm) in enumerate(((0x0000, 6, '暗転'), (0xFFFF, 19, '白光'))):
        nonopt = exact_miss = 0
        for p in range(256):
            t = mix565(int(pp[p]), fg, l)
            tc = np.array([t >> 11, (t >> 5) & 63, t & 31])
            dd = comp - tc
            sq = ((dd * wts) ** 2).sum(1)
            if sq[rm[k][p]] > sq.min() + 1e-9:
                nonopt += 1
            if (np.abs(dd).max(1) == 0).any() and np.abs(dd[rm[k][p]]).max() != 0:
                exact_miss += 1
        check(nonopt == 0 and exact_miss == 0, '%s: 最近傍でない %d 個、目標色がパレットにあるのに厳密でない %d 個' % (nm, nonopt, exact_miss))

    print('### I4 gen_ui_assets.py の自己検査(check_c8)が、壊したパレットを検出する')
    sys.dont_write_bytecode = True
    sys.path.insert(0, os.path.join(ROOT, 'tools'))
    import gen_ui_assets as G
    a = G.build(G.Fonts(G.DEFAULT_FONT))
    c8 = G.build_c8(a)
    check(G.check_c8(a, c8) == [], '正常なパレット: 違反なし')
    m = dict(c8); m['pal'] = c8['pal'][:255]
    check(any('256' in e for e in G.check_c8(a, m)), '255 エントリー → 検出')
    m = dict(c8); pc = c8['pal565'].copy(); fc = c8['fixed'][0]; pc[c8['lut'][fc]] ^= 1; m['pal565'] = pc
    check(any('固定色' in e for e in G.check_c8(a, m)), '固定色を 1 段ずらす → 検出')
    m = dict(c8); bi = dict(c8['bg_idx']); h = bi['home'].copy(); rng = np.random.RandomState(1); sel = rng.choice(h.size, 3000, replace=False)
    h.ravel()[sel] = (h.ravel()[sel].astype(np.int32) + 37) % 256; bi['home'] = h; m['bg_idx'] = bi
    errs = G.check_c8(a, m)
    check(any('不一致' in e for e in errs), '背景 home の添字を 3000 画素乱す → 不一致の上限で検出(%s)' % '; '.join(errs)[:80])
    m = dict(c8); bi = dict(c8['bg_idx']); h = bi['stby'].copy(); fl = h.ravel(); far = int(np.argmax(steps(np.full(256, int(c8['pal565'][fl[0]]), dtype=np.uint16), c8['pal565'])))
    fl[0] = far; bi['stby'] = h; m['bg_idx'] = bi
    check(any('最大誤差' in e for e in G.check_c8(a, m)), '1 画素だけ遠い色にする → 最大誤差で検出')

    print('### I5 P1(%s のコード)との描画の比較' % P1_COMMIT)
    fb1, fb2 = os.path.join(work, 'fb_p1.exe'), os.path.join(work, 'fb_p2b.exe')
    if not (build(cc, p1, fb1) and build(cc, ROOT, fb2)):
        print('SKIP: fb_hostrender をビルドできない(%s)' % cc)
        return 77
    states = gen_states()

    def one(i):
        kind, d = states[i]
        t = tempfile.mkdtemp(dir=work)
        sf = os.path.join(t, 's.txt'); open(sf, 'w').write(mkstate(d))
        subprocess.check_call([fb1, sf, os.path.join(t, 'a.bin')]); subprocess.check_call([fb2, sf, os.path.join(t, 'b.c8')])
        x = np.fromfile(os.path.join(t, 'a.bin'), dtype='<u2').reshape(H, W)
        f = np.fromfile(os.path.join(t, 'b.c8'), dtype=np.uint8)
        assert f.size == S * H
        e = steps(x, pp[f.reshape(H, S)[:, :W]])
        mk = J.mask_of(mkstate(d))
        e = np.where(mk, 0, e)                   # P1 に無い矩形は 0 段(比べない)
        ys, xs = np.nonzero(e >= 3)
        return kind, int(e.size - mk.sum()), int((e > 0).sum()), int((e > 1).sum()), int((e > 2).sum()), int(e.max()), int(xs.min()) if xs.size else 999
    agg = collections.defaultdict(lambda: [0, 0, 0, 0, 0, 0]); minx3 = 999
    with ThreadPoolExecutor(4) as ex:
        for kind, n, m1, m2, m3, mxe, x3 in ex.map(one, range(len(states))):
            s = agg[kind]; s[0] += 1; s[1] += n; s[2] += m1; s[3] += m2; s[4] += m3; s[5] = max(s[5], mxe)
            if kind == 'static':                 # スライド中はバーの右端が左へずれる
                minx3 = min(minx3, x3)
    for k, s in agg.items():
        print('  %-6s %4d 枚: 不一致 %.3f%% 1段超 %d 2段超 %d (%.4f%%) 最大 %d 段' % (k, s[0], 100.0 * s[2] / s[1], s[3], s[4], 100.0 * s[4] / s[1], s[5]))
    s = [x + y for x, y in zip(agg['static'], agg['slide'])]; s[5] = max(agg['static'][5], agg['slide'][5])   # 静的 + スライド(押下なし)
    check(100.0 * s[2] / s[1] <= 0.7 and 100.0 * s[4] / s[1] <= 0.01 and s[5] <= 3, '静的・デモ・スライド・数字の走査: 不一致 <= 0.7%(P2b は 0.5%。judge_p2b.py の「新しい基準」)、2段超 <= 0.01%、最大 <= 3 段')
    check(minx3 >= 430, '静的な画面では、3 段以上の画素は x >= 430(満タンのバーの右端の角)だけ(最小 x = %d)' % minx3)
    s = agg['press']
    check(100.0 * s[4] / s[1] <= 0.1 and s[5] <= 8, '押下・白光: 2段超 <= 0.1%、最大 <= 8 段')
    print('RESULT:', 'FAIL %d' % len(FAILS) if FAILS else 'PASS')
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
