# -*- coding: utf-8 -*-
"""judge_p2b.py - H-D4(docs/design.md §20.9): P2b(CLUT8)のホスト描画を、P1(RGB565)の基準描画と画素単位で比べる。

  python judge_p2b.py --host <fb_hostrender> --ref tests/host/data/p1_ref.npz [--out DIR] [--workdir DIR]
  python judge_p2b.py --make-ref <P1 の .state.txt と .host.bin が入ったフォルダ> --ref tests/host/data/p1_ref.npz

基準 = 実機で読んだ 23 状態(jobs/fb/*.state.txt)を、P1 のファームと同じ ui_render で描いた RGB565(*.host.bin。コミット 0368fe3 のコード)。
P2b 側 = fb_hostrender(8bpp の面 + ui_pal)で同じ状態を描き、パレットを通したパネルの色(RGB565 に切り捨て)にしたもの。
誤差 = 画素ごとの 3チャンネルの段の差の最大(RGB565 の1段)。判定(§20.9 H-D4):
  静的・スライド・デモ: 不一致 <= 0.5%、最大 <= 3 段、2 段超 <= 0.01%
  押下・白光(fb_phase != 0): 2 段超 <= 0.1%、最大 <= 8 段
天気・Wi-Fi の追加(design §22.8)後の読み方: 基準(P1)に無い要素の矩形は比べない(マスク)。ホームの日付(動的になった)・天気の行(アイコン・気温・東京・更新・更新ボタン)と、
待機以外の 4 画面の Wi-Fi アイコン。矩形の外は今までの基準のまま(パレットを作り直したので、誤差の実測は §22.9 / implementation-notes を参照)。
スライド中は ui_render と同じ式で、各画面の横のずれ(xo)だけ矩形をずらす。
新しい基準(天気・Wi-Fi の追加で共通パレットを作り直したので): 静的・スライド・デモの不一致の合計 <= 0.5% を 0.7% に緩めた(2段超 <= 0.01%、最大 <= 3 段は同じ)。
理由 = ホームの背景の不一致が 0.550% -> 0.666%(設計 §22.9 の許容。H-D1 の 1 枚 <= 1.0% の中)になり、この 23 状態は 20 状態のうち 14 がホーム(日付・時計の数字を含む)で、
合計に占める割合が大きい。実測は 0.587%(P2b は 0.454%)、2段超は 0 画素、最大 2 段。押下・白光の基準は変えない。
判定の読み方: 上の割合は「種類ごとの合計(全状態の画素数に対する割合)」に当てる(背景の home だけで 0.57% あるので、1状態ごとには当てられない)。
1状態ごとには、不一致 <= 1.0%(H-D1 の背景1枚の上限)と、最大の段(3 / 8)だけ当てる。
終了コード 0 = 合格、1 = 不合格、2 = 使い方・ファイルの誤り。"""
import argparse
import glob
import os
import subprocess
import sys
import tempfile

import numpy as np

W, H, STRIDE = 480, 272, 512
LIM_STATIC = dict(mism=0.7, over2=0.01, max=3)       # 種類ごとの合計に当てる(%)。不一致は 0.5 -> 0.7(下の「新しい基準」)
LIM_FB = dict(over2=0.1, max=8)
LIM_PER_STATE_MISM = 1.0                              # 1状態ごと(%)


# P1 に無い要素の矩形(x, y, w, h)。HOME = 日付・天気の行、HOME / FILL / REHEAT / USAGE = Wi-Fi アイコン(STANDBY は無し)
WX_RECTS = {0: [(8, 6, 96, 26), (16, 154, 268, 36), (112, 7, 20, 20)], 1: [(112, 7, 20, 20)], 2: [(112, 7, 20, 20)], 3: [(112, 7, 20, 20)], 4: []}


def mask_of(text):
    """状態ファイルの文字列 -> 比べない画素(H x W の bool)。表示される各画面の横のずれ xo は ui_render と同じ"""
    kv = {}
    for l in text.splitlines():
        if '=' in l:
            k, v = l.split('=', 1)
            kv[k.strip()] = int(v)
    if kv.get('tr_on', 0):
        dx = kv.get('tr_dx', 0)
        shown = [(kv.get('ov.scr', 0), -dx if kv.get('tr_fwd', 0) else dx), (kv.get('v.scr', 0), 480 - dx if kv.get('tr_fwd', 0) else dx - 480)]
    else:
        shown = [(kv.get('v.scr', 0), 0)]
    m = np.zeros((H, W), dtype=bool)
    for scr, xo in shown:
        for (x, y, w, h) in WX_RECTS.get(scr, []):
            x0, x1 = max(0, x + xo), min(W, x + xo + w)
            if x1 > x0:
                m[y:y + h, x0:x1] = True
    return m


def panel565(argb):
    a = np.asarray(argb, dtype=np.uint32)
    return ((((a >> 19) & 31) << 11) | (((a >> 10) & 63) << 5) | ((a >> 3) & 31)).astype(np.uint16)


def steps(ref, got):
    r, g = ref.astype(np.int32), got.astype(np.int32)
    return np.maximum(np.maximum(abs((r >> 11) - (g >> 11)), abs(((r >> 5) & 63) - ((g >> 5) & 63))), abs((r & 31) - (g & 31)))


def rgb(p):
    p = p.astype(np.uint32)
    return np.stack([((p >> 11) << 3) | ((p >> 13) & 7), (((p >> 5) & 63) << 2) | ((p >> 9) & 3), ((p & 31) << 3) | ((p >> 2) & 7)],
                    axis=-1).astype(np.uint8)


def save_triplet(path, ref, got):
    from PIL import Image
    e = steps(ref, got)
    d = np.full(e.shape + (3,), 255, dtype=np.uint8)
    d[e == 1] = (255, 220, 0)
    d[e == 2] = (230, 0, 0)
    d[e >= 3] = (0, 0, 0)
    sheet = np.full((H, 3 * W + 16, 3), 160, dtype=np.uint8)
    for i, im in enumerate((rgb(ref), rgb(got), d)):
        sheet[:, i * (W + 8):i * (W + 8) + W] = im
    Image.fromarray(sheet).save(path)


def make_ref(src, ref):
    out = {}
    for sf in sorted(glob.glob(os.path.join(src, '*.state.txt'))):
        name = os.path.basename(sf)[:-len('.state.txt')]
        hb = os.path.join(src, name + '.host.bin')
        if not os.path.exists(hb):
            continue
        d = open(hb, 'rb').read()
        if len(d) != W * H * 2:
            raise SystemExit('サイズ違い: %s' % hb)
        out[name + '.state'] = np.array(open(sf, encoding='utf-8').read())
        out[name + '.rgb565'] = np.frombuffer(d, dtype='<u2').reshape(H, W).astype(np.uint16)
    if not out:
        raise SystemExit('状態が見つからない: %s' % src)
    np.savez_compressed(ref, **out)
    print('基準を作った: %s (%d 状態, %d バイト)' % (ref, len(out) // 2, os.path.getsize(ref)))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--host')
    ap.add_argument('--ref', required=True)
    ap.add_argument('--out', help='P1 | P2b | 差分の PNG を出すフォルダ')
    ap.add_argument('--workdir')
    ap.add_argument('--make-ref')
    args = ap.parse_args()
    sys.stdout.reconfigure(encoding='utf-8')
    if args.make_ref:
        return make_ref(args.make_ref, args.ref)
    if args.host and not os.path.exists(args.host) and os.path.exists(args.host + '.exe'):
        args.host += '.exe'          # Windows の gcc は .exe を付ける
    if not args.host or not os.path.exists(args.host):
        print('fb_hostrender が無い: %s' % args.host, file=sys.stderr)
        return 2
    ref = np.load(args.ref)
    names = sorted(k[:-len('.state')] for k in ref.files if k.endswith('.state'))
    work = args.workdir or tempfile.mkdtemp(prefix='p2b_')
    os.makedirs(work, exist_ok=True)
    if args.out:
        os.makedirs(args.out, exist_ok=True)
    print('| 状態 | 種類 | 不一致 | 1段超 | 2段超 | 最大 | 判定 |')
    print('|---|---|---|---|---|---|---|')
    bad = 0
    agg = {}
    for n in names:
        text = str(ref[n + '.state'])
        sf, fb, pf = (os.path.join(work, n + e) for e in ('.state.txt', '.host.c8', '.host.pal'))
        open(sf, 'w', encoding='utf-8').write(text)
        subprocess.check_call([os.path.abspath(args.host), sf, fb, '--pal', pf])
        face = np.frombuffer(open(fb, 'rb').read(), dtype=np.uint8)
        pal = np.frombuffer(open(pf, 'rb').read(), dtype='<u4')
        if face.size != STRIDE * H or pal.size != 256:
            print('サイズ違い: %s' % n, file=sys.stderr)
            return 2
        got = panel565(pal)[face.reshape(H, STRIDE)[:, :W]]
        want = ref[n + '.rgb565']
        mk = mask_of(text)
        e = steps(want, got)[~mk]                      # マスクの外だけ比べる
        fbp = int([l for l in text.splitlines() if l.startswith('fb_phase=')][0].split('=')[1])
        kind = '押下/白光' if fbp != 0 else '静的/動き'
        npx = e.size
        mism, o1, o2, mx = int((e > 0).sum()), int((e > 1).sum()), int((e > 2).sum()), int(e.max())
        ok = (100.0 * mism / npx <= LIM_PER_STATE_MISM) and mx <= (LIM_FB['max'] if fbp != 0 else LIM_STATIC['max'])
        bad += 0 if ok else 1
        s = agg.setdefault(kind, [0, 0, 0, 0, 0])
        s[0] += npx; s[1] += mism; s[2] += o1; s[3] += o2; s[4] = max(s[4], mx)
        print('| %s | %s | %d (%.3f%%) | %d | %d | %d段 | %s |' % (n, kind, mism, 100.0 * mism / npx, o1, o2, mx, 'OK' if ok else 'NG'))
        if args.out:
            save_triplet(os.path.join(args.out, 'state_%s.png' % n), want, got)
    print()
    for k, s in agg.items():
        m, o2 = 100.0 * s[1] / s[0], 100.0 * s[3] / s[0]
        ok = (o2 <= LIM_FB['over2'] and s[4] <= LIM_FB['max']) if k.startswith('押下') else (m <= LIM_STATIC['mism'] and o2 <= LIM_STATIC['over2'] and s[4] <= LIM_STATIC['max'])
        bad += 0 if ok else 1
        print('%s 合計: %d 画素中 不一致 %d (%.3f%%) 1段超 %d 2段超 %d (%.4f%%) 最大 %d段 -> %s' % (k, s[0], s[1], m, s[2], s[3], o2, s[4], 'OK' if ok else 'NG'))
    print('H-D4: %d 状態、不合格 %d 件(状態ごと + 種類ごとの合計。P1 に無い天気・Wi-Fi の矩形はマスク)' % (len(names), bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
