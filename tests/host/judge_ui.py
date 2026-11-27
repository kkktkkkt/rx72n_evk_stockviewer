# judge_ui.py - UART 受信ログ(製品デモ v2)のオフライン判定。design-uart.md §10.8 の UT10〜UT19 と §10.9 の書式。
#
# 使い方(1コマンド):  python -I tests/host/judge_ui.py <受信ログ>            ← ログから該当する UT を自動で選ぶ
#                      python -I tests/host/judge_ui.py <受信ログ> --ut UT13  ← UT を指定(前提が満たされなければ FAIL)
#   受信ログ = tools\serial-read.ps1 の出力 / serial ジョブの outbox ログ。BOM があれば取る。-Stamp 付き
#   (行頭に "HH:MM:SS.mmm ")でも付きなしでも受け付ける。"# tx 'c'" の行(-Stamp 時に出る送信記録)は無視する。
#   オプション: --tol-ms N(デモ台本の時刻の許容誤差。-Stamp あり 200 / なし 100(+[hb] の区間幅))
#               --strict-edges(先頭と末尾の行を無条件に捨てる。既定は「途中で切れている可能性がある行だけ」捨てる:
#                              先頭は書式に fullmatch しなければ捨てる / 末尾はログが改行で終わっていなければ捨てる)
#   出力: 共通(C1〜C4)と UT10〜UT19 の PASS / FAIL / 対象外。1つでも FAIL なら終了コード 1。
#   判定できないこと: 画面の見た目、実時間そのもの(-Stamp が無い時は [hb] の tick 区間でしか時刻を見ない)。
import re
import sys

SCR = r'(home|fill|reheat|usage|standby)'
BTN = r'(h_dn|h_up|h_fill|h_reheat|h_usage|h_power|h_demo|f_stop|f_done|f_home|r_stop|r_done|r_home|u_back|s_wake|h_wx)'
SRC = r'(touch|uart|demo)'
_HB = (r' tick=(\d+) scr=(\d+) demo=([01]) frm=(\d+) uf=(\d+) err=(\d+) detail=(\d+) pts=(\d+) x=(\d+) y=(\d+)'
       r' i2c_fail=(\d+) streak=(\d+) found=(\d+) drop=(\d+)')
PAT = {
    'boot': r'\[boot\] fw=rx72n_gcc build=\S+ ch=2 baud=115200',
    'init': r'\[init\] stage=(lcd|touch|timer|loop) ok=([01]) err=(\d+) detail=(\d+)',
    'hb': r'\[hb\]' + _HB,
    'st': r'\[st\]' + _HB + r' xmin=(\d+) xmax=(\d+) ymin=(\d+) ymax=(\d+)',
    'ui': r'\[ui\] scr=' + SCR + r' set=(\d+) bath10=(\d+) fill10=(\d+) flow10=(\d+) tr=([01]) btn=(\w+) ph=(none|press|flash)',
    'perf': r'\[perf\] n=(\d+) copy_us=(\d+) home_us=(\d+) slide_us=(\d+)',
    'evs': r'\[ev\] scr=' + SCR + r' from=' + SCR + r' src=' + SRC,
    'evb': r'\[ev\] btn=' + BTN + r' st=(down|act) src=' + SRC,
    'evv': r'\[ev\] set=(\d+) src=' + SRC,
    'evd': r'\[ev\] done=(fill|reheat)',
    'evm': r'\[ev\] demo=(start|abort) src=' + SRC,
    'eve': r'\[ev\] demo=end',
    'help': r'\[help\] n=next p=prev s=state d=demo \+=up -=down x=wx \?=help',
    # 製品に天気の取得を組み込んだビルド(APP_WX = 1。design-uart.md §12・§13)が出す行。書式だけを見る(値の判定は UX0〜UX6)
    'wifi': r'\[wifi\] step=(reset|boot|at|echo|gmr|mode|qap|lapopt|scan|join|ip|dns|ping_ip|ping_dns|wx_mux|wx_close|wx_tcp|wx_send|wx_recv) res=(start|ok|fail|timeout|skip) try=(\d+) ms=(\d+) code=(\d+)',
    'wifir': r'\[wifi\] result=(pass|fail) stage=(none|reset|boot|at|echo|gmr|mode|qap|lapopt|scan|join|ip|dns|ping_ip|ping_dns|wx_mux|wx_close|wx_tcp|wx_send|wx_recv) ping_ip=(\d+) ping_dns=(\d+)',
    'wifiv': r'\[wifiv\] key=(ver|sdk|ip|gw|mask|dns|ping|scan|ap|oth) val=([\x21-\x7e]{0,40})',
    'wxok': r'\[wx\] res=ok src=(boot|timer|touch|uart) temp10=(-?\d{1,3}) wmo=(\d{1,2}) kind=(sun|partly|cloud|fog|rain|snow|thunder|unknown) http=(\d{3}) ms=(\d+) sync=([01]) jst=(\d{6}|none) obs=(\d{4}) closed=([01])',
    'wxfail': r'\[wx\] res=fail src=(boot|timer|touch|uart) err=([1-8]) http=(\d{1,3}) ms=(\d+)',
    'wxreq': r'\[wx\] req=(ignored|busy|off) src=(touch|uart)',
    'wxst': r'\[wx\] net=(off|conn|on|err) busy=([01]) have=([01]) temp10=(-?\d{1,3}) kind=(sun|partly|cloud|fog|rain|snow|thunder|unknown|none) age=(-1|\d+) clk=(\d{6}|none) date=(\d{8}|none) wd=([0-69]) sync=([01])',
    'err': r'\[err\] code=(\d+) detail=(\d+)',
    'scan': r'\[scan\] n=(\d+)( a\d=0x[0-9a-f]{2})*',
}
RX = {k: re.compile(v) for k, v in PAT.items()}
STAMP = re.compile(r'(\d\d):(\d\d):(\d\d)\.(\d{3}) (.*)', re.S)
TXLINE = re.compile(r'(\d\d:\d\d:\d\d\.\d{3} )?# .*')      # tx 記録・「# reset released」「# end」などのコメント行
LINE_MAX = 255      # CRLF と NUL を含む(APPLOG_LINE_MAX 256 未満)

# 台本(design.md §6.2 / design-uart.md UT13): [ev] 36 本の (本文, デモ開始からの ms)
DEMO_SCRIPT = [
    ('demo=start src=uart', 0),
    ('btn=h_up st=down src=demo', 800), ('btn=h_up st=act src=demo', 950), ('set=43 src=demo', 950),
    ('btn=h_up st=down src=demo', 1500), ('btn=h_up st=act src=demo', 1650), ('set=44 src=demo', 1650),
    ('btn=h_dn st=down src=demo', 2200), ('btn=h_dn st=act src=demo', 2350), ('set=43 src=demo', 2350),
    ('btn=h_fill st=down src=demo', 3200), ('btn=h_fill st=act src=demo', 3350), ('scr=fill from=home src=demo', 3350),
    ('done=fill', 11350),
    ('btn=f_done st=down src=demo', 12800), ('btn=f_done st=act src=demo', 12950), ('scr=home from=fill src=demo', 12950),
    ('btn=h_reheat st=down src=demo', 14200), ('btn=h_reheat st=act src=demo', 14350), ('scr=reheat from=home src=demo', 14350),
    ('done=reheat', 19350),
    ('btn=r_done st=down src=demo', 20800), ('btn=r_done st=act src=demo', 20950), ('scr=home from=reheat src=demo', 20950),
    ('btn=h_usage st=down src=demo', 22400), ('btn=h_usage st=act src=demo', 22550), ('scr=usage from=home src=demo', 22550),
    ('btn=u_back st=down src=demo', 27000), ('btn=u_back st=act src=demo', 27150), ('scr=home from=usage src=demo', 27150),
    ('btn=h_power st=down src=demo', 28600), ('btn=h_power st=act src=demo', 28750), ('scr=standby from=home src=demo', 28750),
    ('btn=s_wake st=down src=demo', 31750), ('scr=home from=standby src=demo', 31750),
    ('demo=end', 33000),
]
FRM_MIN = 200           # 設計 UT13(2026-11-21 更新): デモ中の frm の増加 200〜900(実機の実測 約620)。範囲内は PASS
FRM_HARD_MAX = 900
FRM_NEAR = 800          # 上限に近い(余裕 100 未満)時だけ WARN


class Rec:
    __slots__ = ('kind', 'g', 'text', 'idx', 't', 'lo', 'hi')

    def __init__(self, kind, g, text, idx, t):
        self.kind, self.g, self.text, self.idx, self.t = kind, g, text, idx, t
        self.lo = self.hi = None


def classify(body):
    for k, rx in RX.items():
        m = rx.fullmatch(body)
        if m:
            return k, m.groups()
    return None, None


def parse(text, strict_edges=False):
    """ログ全体 -> (recs, foreign, markers, stamped, notes)。recs = 書式どおりの行(時刻つき)。foreign = 書式に合わない行"""
    text = text.lstrip('﻿')
    complete_end = text.endswith('\n')
    raw = [l for l in re.split(r'\r?\n', text) if l != '']
    # serial-read.ps1 は -Stamp なしでも、送信した文字を「HH:MM:SS.mmm # tx 'c'」の行で出す。判定の対象外
    tx_lines = [l for l in raw if TXLINE.fullmatch(l)]
    raw = [l for l in raw if not TXLINE.fullmatch(l)]
    notes = []
    # 時刻の付き方: 先頭行(途中切れがありうる)を除く全行にスタンプがあれば付きありログ
    probe = raw[1:] if len(raw) > 1 else raw
    stamped = bool(probe) and all(STAMP.fullmatch(l) for l in probe)
    items = []      # (本文, 時刻ms または None, 元の行)
    day = 0
    prev = None
    for l in raw:
        m = STAMP.fullmatch(l) if stamped else None
        if m:
            t = ((int(m.group(1)) * 60 + int(m.group(2))) * 60 + int(m.group(3))) * 1000 + int(m.group(4))
            if prev is not None and t + day < prev - 12 * 3600 * 1000:
                day += 24 * 3600 * 1000
            t += day
            prev = t
            items.append((m.group(5), t, l))
        else:
            items.append((l, None, l))
    # 先頭・末尾の扱い
    drop_first = drop_last = False
    if items:
        if strict_edges:
            drop_first = True
            drop_last = len(items) > 1
        else:
            drop_first = classify(items[0][0])[0] is None
            drop_last = (not complete_end) and len(items) > 1
    markers, recs, foreign = list(tx_lines), [], []
    # 実機の記録は、リセットの前の古い出力(別の走行の [hb] や途中切れの行)から始まる。最後の [boot] から先だけを今回の走行として判定する
    start = 0
    for i, (body, t, orig) in enumerate(items):
        if classify(body)[0] == 'boot':
            start = i
    for i, (body, t, orig) in enumerate(items):
        if i < start:
            continue
        if (i == 0 and drop_first) or (i == len(items) - 1 and drop_last):
            continue
        k, g = classify(body)
        if k is None:
            foreign.append(body)
            continue
        recs.append(Rec(k, g, body, i, t))
    # 付きなしログ: 前後の [hb] の tick(10ms 単位)から時刻の区間を作る
    if not stamped:
        hb_t = [(i, int(r.g[0]) * 10) for i, r in enumerate(recs) if r.kind == 'hb']
        for i, r in enumerate(recs):
            before = [t for j, t in hb_t if j <= i]
            after = [t for j, t in hb_t if j >= i]
            r.lo = before[-1] if before else None
            r.hi = after[0] if after else None
            if r.kind == 'hb':
                r.lo = r.hi = int(r.g[0]) * 10
    else:
        for r in recs:
            r.lo = r.hi = r.t
    if start:
        notes.append('最後の [boot] より前の %d 行は、リセット前の出力として判定から外した' % start)
    return recs, foreign, markers, stamped, notes


# ---------------------------------------------------------------- 判定の部品
class Result:
    def __init__(self, ident, title):
        self.id, self.title = ident, title
        self.checks, self.warns, self.status = [], [], 'PASS'

    def ok(self, cond, desc, info=''):
        self.checks.append((bool(cond), desc, info))

    def warn(self, msg):
        self.warns.append(msg)

    def na(self, why):
        self.status = 'NA'
        self.why = why

    def done(self):
        if self.status != 'NA':
            self.status = 'PASS' if all(c[0] for c in self.checks) and self.checks else 'FAIL'
        return self


def of(recs, kind):
    return [r for r in recs if r.kind == kind]


def hbs(recs):
    return [[int(x) for x in r.g] for r in of(recs, 'hb')]


def ev_text(r):
    return r.text[5:]


def is_ev(r):
    return r.kind in ('evs', 'evb', 'evv', 'evd', 'evm', 'eve')


def scr_evs(recs):
    return [r for r in recs if r.kind == 'evs']


def btn_evs(recs):
    return [r for r in recs if r.kind == 'evb']


def near(r, t0_lo, t0_hi, off, tol):
    """イベント r の時刻区間が、[t0_lo, t0_hi] + off ± tol と重なるか。区間が不明(None)なら通す"""
    if r.lo is None or r.hi is None or t0_lo is None or t0_hi is None:
        return True
    return r.lo <= t0_hi + off + tol and r.hi >= t0_lo + off - tol


def common_hb(res, recs, spec_hb_gap=True):
    h = hbs(recs)
    ok_drop = all(x[13] == 0 for x in h)
    # uf: 起動後の増分が 0(design-uart.md §10.2 注)。旧ビルドは GLCDC の表示開始時の初回検出が 1 回だけ `uf=1` として出る。
    # 最初の増分 1 回は WARN、それ以降(合計 2 以上)は FAIL。新ビルドは全 [hb] が uf=0。[boot] が無いログは、最初の [hb] の値を基準にする
    base = 0 if of(recs, 'boot') else (h[0][4] if h else 0)
    inc = (max(x[4] for x in h) - base) if h else 0
    res.ok(inc <= 1, 'uf の増分が 0(起動後の最初の 1 回だけは WARN)', 'uf の増分 %d(値 %s)' % (inc, sorted({x[4] for x in h})[:5]))
    if inc == 1:
        res.warn('uf が起動後に 1 回だけ増えた(旧ビルドの GLCDC 表示開始時の初回検出。新ビルドでは出ない)')
    res.ok(ok_drop, 'drop がすべて 0', [x[13] for x in h if x[13]][:3])
    return h


# ---------------------------------------------------------------- 共通(C1〜C4)
def judge_common(recs, foreign, args):
    out = []
    r1 = Result('C1', '書式: すべての行が §10.9(と §6.4)の正規表現に fullmatch(異物 0)')
    r1.ok(not foreign, '書式に合わない行が 0', foreign[:3])
    out.append(r1.done())
    r2 = Result('C2', '行の長さ: 255 バイト未満(CRLF 込み)')
    longest = max([len(r.text) + 2 for r in recs] + [0])
    r2.ok(longest < LINE_MAX + 1, '最長 %d バイト' % longest, longest)
    out.append(r2.done())
    r3 = Result('C3', '[hb]: 項目の範囲(scr 0..4)、frm / uf / drop が減らない、tick 差 100〜110(130 超は不合格)')
    h = hbs(recs)
    r3.ok(all(0 <= x[1] <= 4 for x in h), 'scr が 0〜4', [x[1] for x in h if not 0 <= x[1] <= 4][:3])
    r3.ok(all(b[3] >= a[3] and b[4] >= a[4] and b[13] >= a[13] for a, b in zip(h, h[1:])), 'frm / uf / drop が減らない', '')
    bad = []
    for a, b in zip(h, h[1:]):
        d = (b[0] - a[0]) & 0xFFFFFFFF
        if 100 <= d <= 110:
            continue
        if d <= 130:
            r3.warn('tick 差 %d(110 超〜130)' % d)
        else:
            bad.append(d)
    r3.ok(not bad, 'tick 差が 130 以下', bad[:3])
    out.append(r3.done())
    r4 = Result('C4', '[hb] が 1 本以上ある(受信できている)')
    r4.ok(len(h) >= 1, '[hb] %d 本' % len(h), len(h))
    out.append(r4.done())
    wl = [r for r in recs if r.kind in ('wifi', 'wifir', 'wifiv', 'wxok', 'wxfail', 'wxreq', 'wxst')]
    if wl:      # 天気の取得を組み込んだ製品(APP_WX = 1)のログ
        r5 = Result('C5', '製品の Wi-Fi・天気の行: res=start と key=oth を出さない(行キュー 8 行の drop を避ける。design.md §22.4 の 6)、[wx] res=ok の値が範囲内')
        r5.ok(not [r.text for r in wl if r.kind == 'wifi' and r.g[1] == 'start'], '[wifi] ... res=start が 0', [r.text for r in wl if r.kind == 'wifi' and r.g[1] == 'start'][:2])
        r5.ok(not [r.text for r in wl if r.kind == 'wifiv' and r.g[0] == 'oth'], '[wifiv] key=oth が 0', [r.text for r in wl if r.kind == 'wifiv' and r.g[0] == 'oth'][:2])
        oks = [r for r in wl if r.kind == 'wxok']
        r5.ok(all(-600 <= int(r.g[1]) <= 600 and 0 <= int(r.g[2]) <= 99 for r in oks), '[wx] res=ok の temp10 が -600〜600、wmo が 0〜99', [r.text for r in oks][:2])
        out.append(r5.done())
    return out


# ---------------------------------------------------------------- UT10 / UT11
def ut_screens(recs, ident, title, to_list):
    res = Result(ident, title)
    from_list = ['home'] + to_list[:-1]
    ss = scr_evs(recs)
    res.ok(len(ss) == 5, '[ev] scr= が 5 本', len(ss))
    res.ok([r.g[0] for r in ss] == to_list, 'to が順に ' + ' '.join(to_list), [r.g[0] for r in ss])
    res.ok([r.g[1] for r in ss] == from_list, 'from が順に ' + ' '.join(from_list), [r.g[1] for r in ss])
    res.ok(all(r.g[2] == 'uart' for r in ss), 'すべて src=uart', [r.g[2] for r in ss])
    res.ok(not btn_evs(recs), 'btn= の [ev] が無い(n p は仮想押下を作らない)', len(btn_evs(recs)))
    h = common_hb(res, recs)
    res.ok(bool(h) and h[-1][1] == 0, '最後の [hb] が scr=0', h[-1][1] if h else None)
    return res.done()


# ---------------------------------------------------------------- UT12
def ut12(recs):
    res = Result('UT12', '温度の上下と上下限')
    bt = [(r.g[0], r.g[1], r.g[2]) for r in btn_evs(recs)]
    exp = [('h_up', 'down', 'uart'), ('h_up', 'act', 'uart')] * 8 + [('h_dn', 'down', 'uart'), ('h_dn', 'act', 'uart')] * 2
    res.ok(bt == exp, 'btn の [ev] が h_up の down/act 8 組 → h_dn の down/act 2 組(src=uart)', len(bt))
    sets = [int(r.g[0]) for r in of(recs, 'evv')]
    res.ok(sets == [43, 44, 45, 46, 47, 48, 47, 46], 'set= が 43 44 45 46 47 48 47 46(7・8 回目の + では出ない)', sets)
    res.ok(all(r.g[1] == 'uart' for r in of(recs, 'evv')), 'set= がすべて src=uart', '')
    uis = of(recs, 'ui')
    res.ok(any(int(r.g[1]) == 46 for r in uis), '[ui] に set=46 がある', [r.g[1] for r in uis])
    res.ok(len(of(recs, 'st')) >= 1, '[st] がある(s コマンド)', len(of(recs, 'st')))
    common_hb(res, recs)
    return res.done()


# ---------------------------------------------------------------- UT13
def demo_sequence(recs):
    """demo=start src=uart から 36 本の [ev]。(start のレコード, その 36 本)。無ければ (None, [])"""
    for i, r in enumerate(recs):
        if r.kind == 'evm' and r.g[0] == 'start' and r.g[1] == 'uart':
            evs = [x for x in recs[i:] if is_ev(x)]
            return r, evs
    return None, []


def ut13(recs, args, stamped):
    res = Result('UT13', 'デモの全走')
    start, evs = demo_sequence(recs)
    res.ok(start is not None, 'demo=start src=uart がある', '')
    if start is None:
        return res.done()
    seq = [ev_text(r) for r in evs[:36]]
    exp = [t for t, _ in DEMO_SCRIPT]
    res.ok(len(evs) == 36, 'start から [ev] がちょうど 36 本(btn 21、set 3、scr 8、done 2、demo 2)', len(evs))
    res.ok(seq == exp, '順序と本文が §6.2 のとおり(scr は fill home reheat home usage home standby home)',
           [(i, a, b) for i, (a, b) in enumerate(zip(seq, exp)) if a != b][:2])
    cnt = lambda p: sum(1 for r in evs if ev_text(r).startswith(p))
    res.ok((cnt('btn='), cnt('set='), cnt('scr='), cnt('done='), cnt('demo=')) == (21, 3, 8, 2, 2),
           '内訳 btn 21 / set 3 / scr 8 / done 2 / demo 2', (cnt('btn='), cnt('set='), cnt('scr='), cnt('done='), cnt('demo=')))
    # 時刻
    tol = args.tol_ms if args.tol_ms is not None else (200 if stamped else 100)
    bad = []
    for (txt, off), r in zip(DEMO_SCRIPT, evs):
        if stamped:
            if abs((r.t - start.t) - off) > tol:
                bad.append((txt, r.t - start.t, off))
        elif not near(r, start.lo, start.hi, off, tol):
            bad.append((txt, (r.lo, r.hi), off))
    res.ok(not bad, '各 [ev] の時刻が台本の時刻表 ± %d ms%s' % (tol, '(-Stamp)' if stamped else '([hb] の tick 区間)'), bad[:2])
    # [hb] の demo=1
    h = hbs(recs)
    runs, cur = [], 0
    first = last = None
    for i, x in enumerate(h):
        if x[2] == 1:
            if cur == 0:
                first = i
            cur += 1
            last = i
        elif cur:
            runs.append((first, last, cur))
            cur = 0
    if cur:
        runs.append((first, last, cur))
    res.ok(len(runs) == 1 and 31 <= runs[0][2] <= 35, '[hb] の demo=1 が 31〜35 本連続(1 区間)', [r[2] for r in runs])
    if len(runs) == 1:
        a, b, _ = runs[0]
        if a >= 1 and b + 1 < len(h):
            d = h[b + 1][3] - h[a - 1][3]
            res.ok(FRM_MIN <= d <= FRM_HARD_MAX, 'デモ中の frm の増加が %d〜%d' % (FRM_MIN, FRM_HARD_MAX), d)
            if FRM_NEAR < d <= FRM_HARD_MAX:
                res.warn('frm の増加 %d は上限 %d に近い(余裕が 100 未満。表示キーの無駄な変化が増えていないか確認)' % (d, FRM_HARD_MAX))
        else:
            res.warn('デモの前後の [hb] が無く frm の増加を判定できない')
    res.ok(bool(h) and h[-1][1] == 0 and h[-1][2] == 0, '最後の [hb] が scr=0 demo=0', h[-1][1:3] if h else None)
    common_hb(res, recs)
    res.ok(not of(recs, 'err'), '[err] が無い', [r.text for r in of(recs, 'err')][:2])
    return res.done()


# ---------------------------------------------------------------- UT14 / UT15
def after_abort(recs):
    ab = [r for r in recs if r.kind == 'evm' and r.g[0] == 'abort']
    return ab[0] if ab else None


def ut14(recs, args, stamped):
    res = Result('UT14', 'デモの中断(UART)')
    start, _ = demo_sequence(recs)
    ab = after_abort(recs)
    res.ok(start is not None, 'demo=start src=uart がある', '')
    res.ok(ab is not None and ab.g[1] == 'uart', 'demo=abort src=uart がある', ab.text if ab else None)
    if start is None or ab is None:
        return res.done()
    i = recs.index(ab)
    nxt = [r for r in recs[i + 1:] if is_ev(r)]
    res.ok(bool(nxt) and ev_text(nxt[0]) == 'scr=home from=fill src=uart', '直後に scr=home from=fill src=uart(湯はり中に中断)',
           ev_text(nxt[0]) if nxt else None)
    res.ok(len(nxt) == 1, 'abort の後の [ev] はその 1 本だけ', len(nxt))
    tol = args.tol_ms if args.tol_ms is not None else (300 if stamped else 100)
    if stamped:
        d = ab.t - start.t
        res.ok(abs(d - 5600) <= tol + 200, '開始から約 5.6 秒で中断(± %d ms)' % (tol + 200), d)
    else:
        res.ok(near(ab, start.lo, start.hi, 5600, tol + 200), '開始から約 5.6 秒で中断([hb] の tick 区間)', (ab.lo, ab.hi))
    after_hb = [r for r in recs[i:] if r.kind == 'hb']
    res.ok(all(int(r.g[1]) == 0 and int(r.g[2]) == 0 for r in after_hb[1:]) and len(after_hb) >= 2,
           '中断後の [hb](最初の 1 本を除く)が scr=0 demo=0', [r.text for r in after_hb[:2]])
    common_hb(res, recs)
    return res.done()


def ut15(recs, args, stamped):
    res = Result('UT15', 'デモの中断(実タッチ)')
    start, _ = demo_sequence(recs)
    ab = after_abort(recs)
    res.ok(start is not None, 'demo=start src=uart がある', '')
    res.ok(ab is not None and ab.g[1] == 'touch', 'demo=abort src=touch がある', ab.text if ab else None)
    if ab is None:
        return res.done()
    i = recs.index(ab)
    nxt = [r for r in recs[i + 1:] if is_ev(r)]
    res.ok(bool(nxt) and ev_text(nxt[0]) == 'scr=home from=fill src=touch', '直後に scr=home from=fill src=touch',
           ev_text(nxt[0]) if nxt else None)
    res.ok(not [r for r in nxt if r.kind in ('evb', 'evv')], '中断したタッチによる btn= / set= が出ない(以後の [ev] に btn・set なし)',
           [ev_text(r) for r in nxt if r.kind in ('evb', 'evv')][:3])
    after_hb = [r for r in recs[i:] if r.kind == 'hb']
    res.ok(len(after_hb) >= 2 and all(int(r.g[2]) == 0 for r in after_hb[1:]), '以後の [hb] が demo=0', [r.text for r in after_hb[:2]])
    common_hb(res, recs)
    return res.done()


# ---------------------------------------------------------------- UT16
def ut16(recs):
    res = Result('UT16', '通常速度での完了')
    ss = scr_evs(recs)
    res.ok([(r.g[0], r.g[1], r.g[2]) for r in ss] == [('fill', 'home', 'uart'), ('reheat', 'fill', 'uart')],
           'scr が fill(from=home)→ reheat(from=fill)、src=uart', [(r.g[0], r.g[1]) for r in ss])
    done_r = [r for r in recs if r.kind == 'evd' and r.g[0] == 'reheat']
    res.ok(len(done_r) == 1, 'done=reheat が 1 本', len(done_r))
    res.ok(not [r for r in recs if r.kind == 'evd' and r.g[0] == 'fill'], 'done=fill が出ない(32 秒未満で離れる)', '')
    if len(done_r) == 1 and len(ss) == 2:
        a, b = recs.index(ss[1]), recs.index(done_r[0])
        n = len([r for r in recs[a:b] if r.kind == 'hb'])
        res.ok(14 <= n <= 18, 'scr=reheat から done=reheat までの [hb] が 16 ± 2 本', n)
    else:
        res.ok(False, 'scr=reheat から done=reheat までの [hb] が 16 ± 2 本', '前提のイベントが無い')
    common_hb(res, recs)
    return res.done()


# ---------------------------------------------------------------- UT17
def ut17(recs):
    res = Result('UT17', '連打でも壊れない')
    h = hbs(recs)
    res.ok(13 <= len(h) <= 16, '[hb] が 13〜16 本', len(h))
    res.ok(all(((b[0] - a[0]) & 0xFFFFFFFF) <= 130 for a, b in zip(h, h[1:])), '連続する tick の差が 130 以下', '')
    res.ok(not [r for r in of(recs, 'err') if int(r.g[0]) == 7], '[err] の code=7 が無い', '')
    common_hb(res, recs)
    sets = [int(r.g[0]) for r in of(recs, 'evv')]
    res.ok(1 <= len(sets) <= 6, 'set= の数が 1〜6', len(sets))
    res.ok(all(35 <= s <= 48 for s in sets), 'set の値が 35〜48(48 を超えない)', sets)
    res.ok(all(r.g[2] == 'uart' for r in btn_evs(recs)), 'btn がすべて src=uart', '')
    return res.done()


# ---------------------------------------------------------------- UT18
def ut18(recs):
    res = Result('UT18', '[perf] と起動')
    kinds = [(r.kind, r.g[0] if r.kind == 'init' else '') for r in recs if r.kind in ('boot', 'init', 'perf')]
    res.ok(kinds == [('boot', ''), ('init', 'lcd'), ('init', 'touch'), ('init', 'timer'), ('init', 'loop'), ('perf', '')],
           '[boot] → [init] lcd touch timer loop → [perf] が 1 本ずつ、この順', kinds)
    inits = of(recs, 'init')
    res.ok(all(r.g[1] == '1' and r.g[2] == '0' and r.g[3] == '0' for r in inits), '[init] がすべて ok=1 err=0 detail=0(ok=0 は不具合として報告)',
           [r.text for r in inits if r.g[1] != '1'][:2])
    perf = of(recs, 'perf')
    if perf:
        n, copy, home, slide = [int(x) for x in perf[0].g]
        res.ok(n == 50, 'n=50', n)
        for name, v, lim in (('copy_us', copy, 8000), ('home_us', home, 12000), ('slide_us', slide, 12000)):
            if v > lim:
                res.warn('%s=%d は目標 %d を超える(不合格ではなく後退案の判断材料)' % (name, v, lim))
    hb = of(recs, 'hb')
    if hb and perf and not 100 <= int(hb[0].g[0]) <= 112:
        # [perf] の測定(約 150 回の描画)の間、メインループは [hb] を出さない。10ms タイマは先に動き出しているので、tick は 100 を超えうる
        res.warn('最初の [hb] の tick=%s(100〜112 でない)。[perf] の測定中はメインループが止まるため起こりうる(v1 の UT2 の条件は v2 では当てはまらない)' % hb[0].g[0])
    return res.done()


# ---------------------------------------------------------------- UT19
def ut19(recs):
    res = Result('UT19', 'タッチでの操作')
    tt = [(r.kind, r.g) for r in recs if is_ev(r)]
    want = [('evb', ('h_up', 'down', 'touch')), ('evb', ('h_up', 'act', 'touch')), ('evv', ('43', 'touch')),
            ('evb', ('h_fill', 'down', 'touch')), ('evb', ('h_fill', 'act', 'touch')), ('evs', ('fill', 'home', 'touch')),
            ('evb', ('f_home', 'down', 'touch')), ('evb', ('f_home', 'act', 'touch')), ('evs', ('home', 'fill', 'touch'))]
    i = 0
    for t in tt:
        if i < len(want) and t == want[i]:
            i += 1
    res.ok(i == len(want), '期待の [ev] が順に出る(h_up → set=43 → h_fill → scr=fill → f_home → scr=home、すべて src=touch)',
           'ここまで %d/%d' % (i, len(want)))
    names = sorted({r.g[0] for r in btn_evs(recs)})
    res.ok(set(names) <= {'h_up', 'h_fill', 'f_home'}, 'btn= が上の 3 個だけ(ボタン外のタッチで btn= が出ていない)', names)
    downs = [r for r in btn_evs(recs) if r.g[1] == 'down' and r.g[0] != 's_wake']
    acts = [r for r in btn_evs(recs) if r.g[1] == 'act']
    res.ok(len(downs) == len(acts), 'down と act の数が一致', (len(downs), len(acts)))
    res.ok(all(r.g[2] == 'touch' for r in btn_evs(recs)), 'btn がすべて src=touch', '')
    common_hb(res, recs)
    return res.done()


# ---------------------------------------------------------------- 自動選択
def applicable(recs):
    """ログの中身から該当する UT の id 集合(UT18 は独立)"""
    out = set()
    evm = [r for r in recs if r.kind == 'evm']
    ss, bs = scr_evs(recs), btn_evs(recs)
    if evm:
        if any(r.g[0] == 'abort' and r.g[1] == 'touch' for r in evm):
            out.add('UT15')
        elif any(r.g[0] == 'abort' and r.g[1] == 'uart' for r in evm):
            out.add('UT14')
        elif any(r.kind == 'eve' for r in recs):
            out.add('UT13')
    elif any(r.g[2] == 'touch' for r in bs):
        out.add('UT19')
    elif bs and any(r.g[2] == 'uart' for r in bs):
        out.add('UT12' if any(r.g[0] == 'h_dn' for r in bs) else 'UT17')
    elif ss and all(r.g[2] == 'uart' for r in ss):
        if any(r.kind == 'evd' for r in recs) or len(ss) <= 3:
            out.add('UT16')
        elif ss[0].g[0] == 'fill':
            out.add('UT10')
        elif ss[0].g[0] == 'standby':
            out.add('UT11')
    if of(recs, 'boot'):                  # リセットが受信中にあった(先頭が途中切れで [boot] が無いログは対象外)
        out.add('UT18')
    return out


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description='UART 受信ログの判定(UT10〜UT19)')
    ap.add_argument('log')
    ap.add_argument('--ut', default='auto', help='auto(既定)/ all / UT13,UT14 のようにカンマ区切り')
    ap.add_argument('--tol-ms', type=int, default=None)
    ap.add_argument('--strict-edges', action='store_true')
    args = ap.parse_args(argv)
    try:
        sys.stdout.reconfigure(encoding='utf-8')     # 出力は UTF-8(Git Bash / ジョブのログ)。cp932 のコンソールで化ける時は chcp 65001
    except (AttributeError, ValueError):
        pass
    text = open(args.log, encoding='utf-8', errors='replace').read()
    recs, foreign, markers, stamped, notes = parse(text, args.strict_edges)

    runners = {
        'UT10': ('画面の順送り', lambda: ut_screens(recs, 'UT10', '画面の順送り', ['fill', 'reheat', 'usage', 'standby', 'home'])),
        'UT11': ('逆送り', lambda: ut_screens(recs, 'UT11', '逆送り', ['standby', 'usage', 'reheat', 'fill', 'home'])),
        'UT12': ('温度の上下と上下限', lambda: ut12(recs)),
        'UT13': ('デモの全走', lambda: ut13(recs, args, stamped)),
        'UT14': ('デモの中断(UART)', lambda: ut14(recs, args, stamped)),
        'UT15': ('デモの中断(実タッチ)', lambda: ut15(recs, args, stamped)),
        'UT16': ('通常速度での完了', lambda: ut16(recs)),
        'UT17': ('連打でも壊れない', lambda: ut17(recs)),
        'UT18': ('[perf] と起動', lambda: ut18(recs)),
        'UT19': ('タッチでの操作', lambda: ut19(recs)),
    }
    if args.ut == 'auto':
        sel = applicable(recs)
    elif args.ut == 'all':
        sel = set(runners)
    else:
        sel = {u.strip().upper() for u in args.ut.split(',')}
        bad = sel - set(runners)
        if bad:
            print('不明な UT: %s' % ', '.join(sorted(bad)))
            return 2

    print('ログ: %s(%s、解析できた行 %d、異物 %d、コメント・tx 行 %d)' % (args.log, '時刻つき' if stamped else '時刻なし', len(recs), len(foreign), len(markers)))
    for n in notes:
        print('注: ' + n)
    results = judge_common(recs, foreign, args)
    for ident in sorted(runners):
        title, fn = runners[ident]
        if ident in sel:
            results.append(fn())
        else:
            r = Result(ident, title)
            r.na('このログの操作と合わない')
            results.append(r)
    npass = nfail = nna = 0
    for r in results:
        label = {'PASS': 'PASS', 'FAIL': 'FAIL', 'NA': '対象外'}[r.status]
        print('%-6s %s  %s' % (label, r.id, r.title))
        if r.status == 'FAIL':
            for ok, desc, info in r.checks:
                if not ok:
                    print('         NG: %s  [%s]' % (desc, info))
        for w in r.warns:
            print('         WARN: %s' % w)
        npass += r.status == 'PASS'
        nfail += r.status == 'FAIL'
        nna += r.status == 'NA'
    print('合計: PASS %d / FAIL %d / 対象外 %d' % (npass, nfail, nna))
    if args.ut == 'auto' and not (sel - {'UT18'}):
        if any(is_ev(r) for r in recs):
            # 操作(ev)があるのに、どの UT の操作にも当てはまらない = 壊れた/想定外のログ。黙って合格にしない
            print('FAIL   UT?   [ev] があるが UT10〜UT17・UT19 のどの操作にも当てはまらない(--ut で UT を指定して詳しく見る)')
            nfail += 1
        else:
            print('注意: [ev] が無い(操作なしのログ)。UT10〜UT17・UT19 は対象外')
    return 0 if nfail == 0 else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
