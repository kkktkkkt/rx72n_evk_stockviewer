# test_judge_ui.py - judge_ui.py(UART ログの判定スクリプト)自身の試験。
#   使い方: python -I tests/host/test_judge_ui.py -v      (環境変数 JUDGE_TRACE_DIR = trace_ui の出力フォルダ。run.sh S17 が設定する)
#   1. tests/host/data の固定ログ(trace_ui が本物の app_main から作ったもの)が、期待どおりの UT で PASS する
#   2. JUDGE_TRACE_DIR の新しいログ(製品の今の書式)でも PASS する = 設計の正規表現と製品の書式が合っている
#   3. 受信の途中切れ・BOM・時刻あり/なし
#   4. ログを壊す(順序・時刻・値・書式・長さ・欠落)と、狙った UT / 共通項目が FAIL になる
import contextlib
import io
import os
import re
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import judge_ui  # noqa: E402

DATA = os.path.join(HERE, 'data')
TRACE = os.environ.get('JUDGE_TRACE_DIR')
TMP = tempfile.mkdtemp(prefix='judge_ui_')
STAMPED = ['ut10', 'ut11', 'ut12', 'ut13', 'ut14', 'ut15', 'ut16', 'ut17', 'ut18', 'ut19']
PLAIN = ['ut10', 'ut13', 'ut15', 'ut19']
MAIN_UT = {'ut10': 'UT10', 'ut11': 'UT11', 'ut12': 'UT12', 'ut13': 'UT13', 'ut14': 'UT14', 'ut15': 'UT15',
           'ut16': 'UT16', 'ut17': 'UT17', 'ut18': 'UT18', 'ut19': 'UT19'}


def read(path):
    with open(path, encoding='utf-8', newline='') as f:
        return f.read()


def judge(text, *extra, name='x.log'):
    """text を一時ファイルに書いて judge_ui.main を実行。(終了コード, 出力) を返す"""
    path = os.path.join(TMP, name)
    with open(path, 'w', encoding='utf-8', newline='') as f:
        f.write(text)
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        rc = judge_ui.main([path] + list(extra))
    return rc, buf.getvalue()


def status(out, ident):
    m = re.search(r'^(PASS|FAIL|対象外)\s+%s\b' % ident, out, re.M)
    return m.group(1) if m else None


def lines(text):
    return text.split('\r\n') if '\r\n' in text else text.split('\n')


class FixtureTests(unittest.TestCase):
    def check_pass(self, text, name, expect_ut, *extra):
        rc, out = judge(text, *extra)
        self.assertEqual(rc, 0, name + '\n' + out)
        for c in ('C1', 'C2', 'C3', 'C4'):
            self.assertEqual(status(out, c), 'PASS', name + '\n' + out)
        self.assertEqual(status(out, expect_ut), 'PASS', name + '\n' + out)
        if expect_ut != 'UT18':
            self.assertEqual(status(out, 'UT18'), 'PASS', name + '\n' + out)    # ログは起動から始まるので UT18 も該当
        # 他の UT(UT18 以外)は対象外のまま
        for u in ['UT%d' % i for i in range(10, 20)]:
            if u not in (expect_ut, 'UT18'):
                self.assertEqual(status(out, u), '対象外', u + '\n' + out)
        return out

    def test_fixtures_stamped(self):
        for n in STAMPED:
            self.check_pass(read(os.path.join(DATA, n + '.stamp.log')), n + '.stamp', MAIN_UT[n])

    def test_fixtures_plain(self):
        for n in PLAIN:
            self.check_pass(read(os.path.join(DATA, n + '.log')), n, MAIN_UT[n])

    @unittest.skipUnless(TRACE and os.path.isdir(TRACE), 'JUDGE_TRACE_DIR が無い(run.sh S17 から実行)')
    def test_fresh_traces_from_firmware(self):
        for n in STAMPED:
            self.check_pass(read(os.path.join(TRACE, n + '.stamp.log')), 'fresh ' + n + '.stamp', MAIN_UT[n])
            self.check_pass(read(os.path.join(TRACE, n + '.log')), 'fresh ' + n, MAIN_UT[n])

    def test_forced_ut_and_all(self):
        t = read(os.path.join(DATA, 'ut10.stamp.log'))
        rc, out = judge(t, '--ut', 'UT11')               # 順送りのログを逆送りとして判定 → FAIL
        self.assertEqual(rc, 1)
        self.assertEqual(status(out, 'UT11'), 'FAIL')
        rc, out = judge(t, '--ut', 'all')
        self.assertEqual(rc, 1)                           # 他の UT は前提が無いので FAIL
        self.assertEqual(status(out, 'UT10'), 'PASS')
        rc, out = judge(t, '--ut', 'UT99')
        self.assertEqual(rc, 2)

    def test_demo_frm_in_range_is_pass_without_warning(self):
        rc, out = judge(read(os.path.join(DATA, 'ut13.stamp.log')))     # ホストの模擬は約 613(設計 UT13: 200〜900 は PASS)
        self.assertEqual(rc, 0)
        self.assertNotIn('frm の増加', out)

    def test_demo_frm_near_limit_warns_and_over_fails(self):
        base = lines(read(os.path.join(DATA, 'ut13.stamp.log')))

        def with_extra(extra):                                           # デモが終わった後の [hb] から frm を extra だけ増やす
            ls = []
            for l in base:
                m = re.search(r'\[hb\] tick=(\d+) .*frm=(\d+)', l)
                if m and int(m.group(1)) >= 3500:
                    l = l.replace('frm=%s ' % m.group(2), 'frm=%d ' % (int(m.group(2)) + extra))
                ls.append(l)
            return '\r\n'.join(ls)
        rc, out = judge(with_extra(237))                                 # 613 + 237 = 850
        self.assertEqual(rc, 0, out)
        self.assertIn('WARN: frm の増加 850', out)
        rc, out = judge(with_extra(300))                                 # 913 > 900
        self.assertEqual(rc, 1, out)
        self.assertEqual(status(out, 'UT13'), 'FAIL')

    def test_uf_first_increment_warns_later_fails(self):
        base = lines(read(os.path.join(DATA, 'ut10.stamp.log')))

        def with_uf(f):                                                  # f(tick) = その [hb] の uf
            ls = []
            for l in base:
                m = re.search(r'\[hb\] tick=(\d+) .*uf=(\d+)', l)
                if m:
                    l = l.replace(' uf=%s ' % m.group(2), ' uf=%d ' % f(int(m.group(1))))
                ls.append(l)
            return '\r\n'.join(ls)
        rc, out = judge(with_uf(lambda t: 0))                            # 新ビルド: 全 [hb] が uf=0
        self.assertEqual(rc, 0, out)
        self.assertNotIn('uf が起動後', out)
        rc, out = judge(with_uf(lambda t: 1 if t >= 200 else 0))         # 旧ビルド: tick=200 から uf=1(最初の増分 1 回)
        self.assertEqual(rc, 0, out)
        self.assertIn('WARN: uf が起動後に 1 回だけ増えた', out)
        # 2 回目の増分(合計 2)は FAIL
        rc2, out2 = judge(with_uf(lambda t: 0 if t < 200 else (1 if t < 700 else 2)))
        self.assertEqual(rc2, 1, out2)
        self.assertEqual(status(out2, 'UT10'), 'FAIL')
        # [boot] が無いログ(途中から受信)は、最初の [hb] の uf を基準にする: uf=1 のまま一定なら増分 0
        t = '\r\n'.join(l for l in lines(with_uf(lambda t: 1)) if '[boot]' not in l and '[init]' not in l and '[perf]' not in l)
        rc3, out3 = judge(t)
        self.assertNotIn('uf が起動後', out3)


class RealLogTests(unittest.TestCase):
    """実機で記録した UART ログ(tools/ut-run.ps1 の時刻つき記録。リセット前の古い出力・# コメント行・BOM を含む。旧ビルドで uf=1)"""

    def test_real_logs(self):
        for n, ut in (('real_ut10', 'UT10'), ('real_ut12', 'UT12'), ('real_ut13', 'UT13'), ('real_ut17', 'UT17')):
            rc, out = judge(read(os.path.join(DATA, n + '.stamp.log')))
            self.assertEqual(rc, 0, n + out)
            self.assertEqual(status(out, ut), 'PASS', n + out)
            self.assertEqual(status(out, 'UT18'), 'PASS', n + out)
            self.assertEqual(status(out, 'C1'), 'PASS', n + out)
            self.assertIn('WARN: uf が起動後に 1 回だけ', out, n)
            self.assertNotIn('frm の増加', out, n)                          # 約 620 は 200〜900 の範囲内

    def test_real_log_segmentation_and_comments(self):
        rc, out = judge(read(os.path.join(DATA, 'real_ut13.stamp.log')))
        self.assertIn('最後の [boot] より前の', out)                          # リセット前の [hb](tick が大きい)を捨てた
        self.assertIn('異物 0', out)                                          # '# reset released' や '# end' は異物にしない
        # 分割しなければ tick が逆戻りして C3 が落ちる(= 分割が効いている)ことを、[boot] 行を消して確認
        ls = [l for l in lines(read(os.path.join(DATA, 'real_ut13.stamp.log'))) if '[boot]' not in l]
        rc, out = judge('\r\n'.join(ls))
        self.assertEqual(rc, 1, out)

    def test_real_log_new_build_uf0(self):
        t = re.sub(r' uf=1 ', ' uf=0 ', read(os.path.join(DATA, 'real_ut13.stamp.log')))
        rc, out = judge(t)
        self.assertEqual(rc, 0, out)
        self.assertNotIn('uf が起動後', out)

    def test_real_log_second_uf_increment_fails(self):
        t = read(os.path.join(DATA, 'real_ut13.stamp.log'))
        i = t.rindex('[hb]')
        t = t[:i] + t[i:].replace(' uf=1 ', ' uf=2 ', 1)
        rc, out = judge(t, '--ut', 'UT13')
        self.assertEqual(rc, 1, out)


class ReceiveTests(unittest.TestCase):
    def setUp(self):
        self.stamped = read(os.path.join(DATA, 'ut13.stamp.log'))
        self.plain = read(os.path.join(DATA, 'ut13.log'))

    def test_bom(self):
        rc, out = judge('﻿' + self.plain)
        self.assertEqual(rc, 0, out)
        self.assertEqual(status(out, 'UT13'), 'PASS')

    def test_lf_only(self):
        rc, out = judge(self.plain.replace('\r\n', '\n'))
        self.assertEqual(rc, 0, out)

    def test_partial_first_and_last_line(self):
        # 先頭は行の途中から、末尾は行の途中で切れる
        t = self.plain[9:]
        t = t[:-25]
        rc, out = judge(t)
        self.assertEqual(rc, 0, out)
        self.assertEqual(status(out, 'UT13'), 'PASS')             # [boot] 等が欠けても UT13 は判定できる
        self.assertEqual(status(out, 'UT18'), '対象外')            # [boot] が無い(途中から受信)ので UT18 は対象外
        # 途中切れの行を厳密に捨てる/捨てないに関わらず、異物(C1)にならない
        self.assertEqual(status(out, 'C1'), 'PASS')
        rc2, out2 = judge(t, '--strict-edges')
        self.assertEqual(rc2, 0, out2)

    def test_partial_stamped(self):
        t = self.stamped[17:]                              # スタンプの途中から始まる
        t = t[:-31]
        rc, out = judge(t)
        self.assertEqual(rc, 0, out)
        self.assertEqual(status(out, 'UT13'), 'PASS')

    def test_tx_markers_in_unstamped_log(self):
        # serial-read.ps1 は -Stamp なしでも「HH:MM:SS.mmm # tx 'c'」を出す。device の行は時刻なし
        ls = lines(self.plain)
        ls.insert([i for i, l in enumerate(ls) if 'demo=start' in l][0], "12:00:03.000 # tx 'd'")
        rc, out = judge('\r\n'.join(ls))
        self.assertEqual(rc, 0, out)
        self.assertIn('時刻なし', out)
        self.assertIn('コメント・tx 行 1', out)
        self.assertEqual(status(out, 'UT13'), 'PASS')

    def test_tx_markers_ignored(self):
        rc, out = judge(read(os.path.join(DATA, 'ut12.stamp.log')))
        self.assertIn('コメント・tx 行 11', out)
        self.assertEqual(rc, 0, out)

    def test_stamp_midnight_wrap(self):
        # 23:59:50 から 00:00:20 へまたぐ(デモ全体の時刻をずらす)
        ls = lines(self.stamped)
        out_l = []
        for l in ls:
            m = re.match(r'(\d\d):(\d\d):(\d\d)\.(\d{3}) (.*)', l)
            if not m:
                out_l.append(l)
                continue
            t = ((int(m.group(1)) * 60 + int(m.group(2))) * 60 + int(m.group(3))) * 1000 + int(m.group(4))
            t = (t - 12 * 3600 * 1000 + 24 * 3600 * 1000 - 12 * 1000) % (24 * 3600 * 1000)   # 12:00:00 → 23:59:48
            out_l.append('%02d:%02d:%02d.%03d %s' % (t // 3600000, t // 60000 % 60, t // 1000 % 60, t % 1000, m.group(5)))
        rc, out = judge('\r\n'.join(out_l))
        self.assertEqual(rc, 0, out)
        self.assertEqual(status(out, 'UT13'), 'PASS')


class MutationTests(unittest.TestCase):
    """壊したログが、狙った項目で FAIL になる(判定が甘くないことの確認)"""

    @staticmethod
    def load(n, stamped=True):
        return lines(read(os.path.join(DATA, n + ('.stamp.log' if stamped else '.log'))))

    def run_j(self, ls, *extra):
        return judge('\r\n'.join(ls), *extra)

    def fails(self, ls, ident, *extra):
        if ident.startswith('UT') and '--ut' not in extra:
            extra = extra + ('--ut', ident)          # 操作者が「どの UT を走らせたか」を指定した状況(壊れたログは自動選択に頼れない)
        rc, out = self.run_j(ls, *extra)
        self.assertEqual(rc, 1, out)
        self.assertEqual(status(out, ident), 'FAIL', out)
        return out

    def idx(self, ls, pat, nth=0):
        hits = [i for i, l in enumerate(ls) if re.search(pat, l)]
        return hits[nth]

    # ---- 共通
    def test_foreign_line(self):
        ls = self.load('ut10')
        ls.insert(10, '12:00:03.500 ?!garbage')
        self.fails(ls, 'C1')

    def test_wrong_hb_format_old(self):
        ls = self.load('ut10')
        i = self.idx(ls, r'\[hb\]', 3)
        ls[i] = re.sub(r'scr=\d+ demo=\d frm=\d+ uf=\d+', 'img=0', ls[i])       # 旧形式(img=)
        self.fails(ls, 'C1')

    def test_line_too_long(self):
        ls = self.load('ut10')
        ls.insert(3, '12:00:00.000 [boot] fw=rx72n_gcc build=' + 'x' * 230 + ' ch=2 baud=115200')
        self.fails(ls, 'C2')

    def test_hb_scr_out_of_range(self):
        ls = self.load('ut10')
        i = self.idx(ls, r'\[hb\]', 3)
        ls[i] = re.sub(r'scr=\d', 'scr=7', ls[i])
        self.fails(ls, 'C3')

    def test_tick_gap(self):
        ls = self.load('ut10')
        i = self.idx(ls, r'\[hb\] tick=500 ')
        j = self.idx(ls, r'\[hb\] tick=600 ')
        for k in range(j, len(ls)):
            m = re.search(r'tick=(\d+)', ls[k])
            if m and '[hb]' in ls[k]:
                ls[k] = ls[k].replace('tick=%s ' % m.group(1), 'tick=%d ' % (int(m.group(1)) + 50))
        self.fails(ls, 'C3')

    # ---- UT10 / UT11
    def test_ut10_order(self):
        ls = self.load('ut10')
        a, b = self.idx(ls, r'scr=fill from=home'), self.idx(ls, r'scr=reheat from=fill')
        ls[a], ls[b] = ls[b], ls[a]
        self.fails(ls, 'UT10')

    def test_ut10_missing_and_src(self):
        ls = self.load('ut10')
        del ls[self.idx(ls, r'scr=usage from=reheat')]
        self.fails(ls, 'UT10')
        ls = self.load('ut10')
        i = self.idx(ls, r'scr=usage from=reheat')
        ls[i] = ls[i].replace('src=uart', 'src=touch')
        self.fails(ls, 'UT10')

    def test_ut10_drop_uf_btn(self):
        ls = self.load('ut10')
        i = self.idx(ls, r'\[hb\]', 4)
        ls[i] = ls[i].replace('drop=0', 'drop=1')
        self.fails(ls, 'UT10')
        ls = self.load('ut10')
        i = self.idx(ls, r'\[hb\]', 6)
        ls[i] = ls[i].replace('uf=0', 'uf=2')
        self.fails(ls, 'UT10')
        ls = self.load('ut10')
        ls.insert(self.idx(ls, r'scr=fill from=home') + 1, '12:00:03.100 [ev] btn=h_fill st=down src=uart')
        self.fails(ls, 'UT10')

    def test_ut10_final_scr(self):
        ls = self.load('ut10')
        i = max(k for k, l in enumerate(ls) if '[hb]' in l)
        ls[i] = ls[i].replace('scr=0', 'scr=4')
        self.fails(ls, 'UT10')

    def test_auto_mode_does_not_pass_unrecognized_ops(self):
        ls = self.load('ut10')
        a, b = self.idx(ls, r'scr=fill from=home'), self.idx(ls, r'scr=reheat from=fill')
        ls[a], ls[b] = ls[b], ls[a]
        del ls[self.idx(ls, r'scr=usage from=reheat')]                 # 4 本(順送りでも 16 でもない)で順序も崩れている
        rc, out = self.run_j(ls)
        self.assertEqual(rc, 1, out)
        # 操作のないログ(UT1 相当)は 0
        idle = [l for l in self.load('ut10') if '[ev]' not in l]
        self.assertEqual(self.run_j(idle)[0], 0)

    def test_ut11_wrong_direction(self):
        ls = self.load('ut11')
        self.assertEqual(status(self.run_j(ls)[1], 'UT11'), 'PASS')
        self.fails(self.load('ut10'), 'UT11', '--ut', 'UT11')

    # ---- UT12
    def test_ut12_values(self):
        ls = self.load('ut12')
        i = self.idx(ls, r'set=48 ')
        ls[i] = ls[i].replace('set=48', 'set=49')
        self.fails(ls, 'UT12')
        ls = self.load('ut12')
        del ls[self.idx(ls, r'set=44 ')]
        self.fails(ls, 'UT12')
        ls = self.load('ut12')
        i = self.idx(ls, r'\[ui\]')
        ls[i] = ls[i].replace('set=46', 'set=45')
        self.fails(ls, 'UT12')

    # ---- UT13
    def test_ut13_missing_event(self):
        ls = self.load('ut13')
        del ls[self.idx(ls, r'done=reheat')]
        self.fails(ls, 'UT13')

    def test_ut13_extra_event(self):
        ls = self.load('ut13')
        ls.insert(self.idx(ls, r'demo=end'), '12:00:36.000 [ev] set=44 src=demo')
        self.fails(ls, 'UT13')

    def test_ut13_time_shift_stamped(self):
        ls = self.load('ut13')
        i = self.idx(ls, r'btn=u_back st=down')
        m = re.match(r'12:00:(\d\d)\.(\d{3}) (.*)', ls[i])
        ls[i] = '12:00:%02d.%s %s' % (int(m.group(1)) + 1, m.group(2), m.group(3))      # 1 秒遅れ
        self.fails(ls, 'UT13')
        out = judge('\r\n'.join(ls), '--tol-ms', '1500')[1]                               # 許容を広げれば通る
        self.assertEqual(status(out, 'UT13'), 'PASS', out)

    def test_ut13_time_shift_plain(self):
        ls = self.load('ut13', stamped=False)
        i = self.idx(ls, r'btn=u_back st=down')
        j = self.idx(ls, r'btn=u_back st=act')
        k = self.idx(ls, r'btn=h_power st=down')
        ls.insert(k, ls.pop(i))                                   # u_back の down を h_power の後ろ(= 順序違反かつ時刻違反)へ
        self.fails(ls, 'UT13')
        # 順序は同じで、[hb] を挟む位置だけがずれる: 台本より 5 秒早く出た扱い
        ls = self.load('ut13', stamped=False)
        i = self.idx(ls, r'done=reheat')
        e = ls.pop(i)
        j = self.idx(ls, r'\[hb\] tick=1400 ')
        ls.insert(j, e)                                            # 19.35 秒のはずが tick 1400(= 11〜14 秒台)の前
        self.fails(ls, 'UT13')

    def test_ut13_hb_demo_flag(self):
        ls = self.load('ut13')
        for k, l in enumerate(ls):
            if '[hb]' in l:
                ls[k] = l.replace('demo=1', 'demo=0')
        self.fails(ls, 'UT13')

    def test_ut13_frm_range(self):
        ls = self.load('ut13')
        for k, l in enumerate(ls):
            if '[hb]' in l:
                ls[k] = re.sub(r'frm=\d+', 'frm=1', l)
        self.fails(ls, 'UT13')                                     # 増加 0 → 画面が動いていない
        ls = self.load('ut13')
        for k, l in enumerate(ls):
            if '[hb]' in l:
                m = re.search(r'tick=(\d+)', l)
                ls[k] = re.sub(r'frm=\d+', 'frm=%d' % int(m.group(1)) * 1, l)      # 100 tick ごとに +100 → 33 秒で 3300
        self.fails(ls, 'UT13')

    def test_ut13_err_line(self):
        ls = self.load('ut13')
        ls.insert(self.idx(ls, r'btn=h_fill st=down'), '12:00:06.100 [err] code=8 detail=7')
        self.fails(ls, 'UT13')

    def test_ut13_end_hb(self):
        ls = self.load('ut13')
        i = max(k for k, l in enumerate(ls) if '[hb]' in l)
        ls[i] = ls[i].replace('scr=0', 'scr=1')
        self.fails(ls, 'UT13')

    # ---- UT14 / UT15
    def test_ut14_wrong(self):
        ls = self.load('ut14')
        del ls[self.idx(ls, r'scr=home from=fill')]
        self.fails(ls, 'UT14')
        ls = self.load('ut14')
        ls.insert(len(ls) - 1, '12:00:16.000 [ev] set=43 src=demo')     # 末尾の空要素(最後の CRLF)の前
        self.fails(ls, 'UT14')
        ls = self.load('ut14')
        i = self.idx(ls, r'demo=abort')
        ls[i] = ls[i].replace('src=uart', 'src=touch')
        self.fails(ls, 'UT14')

    def test_ut14_abort_time(self):
        ls = self.load('ut14')
        for k in range(len(ls)):
            if re.search(r'demo=abort|scr=home from=fill', ls[k]):
                m = re.match(r'12:00:(\d\d)\.(\d{3}) (.*)', ls[k])
                ls[k] = '12:00:%02d.%s %s' % (int(m.group(1)) + 3, m.group(2), m.group(3))
        self.fails(ls, 'UT14')

    def test_ut15_touch_btn_after_abort(self):
        ls = self.load('ut15')
        i = self.idx(ls, r'scr=home from=fill src=touch')
        ls.insert(i + 1, '12:00:14.100 [ev] btn=h_fill st=down src=touch')
        self.fails(ls, 'UT15')
        ls = self.load('ut15')
        i = self.idx(ls, r'demo=abort')
        ls[i] = ls[i].replace('src=touch', 'src=uart')
        self.fails(ls, 'UT15', '--ut', 'UT15')

    # ---- UT16
    def test_ut16_hb_count(self):
        ls = self.load('ut16')
        i = self.idx(ls, r'scr=reheat from=fill')
        n = 0
        for k in range(len(ls) - 1, i, -1):
            if '[hb]' in ls[k] and n < 5 and k < self.idx(ls, r'done=reheat'):
                del ls[k]
                n += 1
        self.fails(ls, 'UT16')

    def test_ut16_done_fill(self):
        ls = self.load('ut16')
        ls.insert(self.idx(ls, r'scr=reheat from=fill'), '12:00:03.500 [ev] done=fill')
        self.fails(ls, 'UT16')

    # ---- UT17
    def test_ut17(self):
        ls = self.load('ut17')
        i = self.idx(ls, r'set=')
        ls[i] = re.sub(r'set=\d+', 'set=49', ls[i])
        self.fails(ls, 'UT17')
        ls = self.load('ut17')
        ls.insert(self.idx(ls, r'\[hb\]', 5), '12:00:05.100 [err] code=7 detail=3')
        self.fails(ls, 'UT17')
        ls = self.load('ut17')
        del ls[self.idx(ls, r'\[hb\]', 5)]                          # 1 本欠ける → tick 差 200
        self.fails(ls, 'C3')
        ls = self.load('ut17')
        for _ in range(3):
            del ls[-1]
        self.fails(ls, 'UT17')                                      # [hb] 数が足りない(13 未満にするため更に削る)

    # ---- UT18
    def test_ut18_init_ok0(self):
        ls = self.load('ut18')
        i = self.idx(ls, r'stage=touch')
        ls[i] = ls[i].replace('ok=1 err=0 detail=0', 'ok=0 err=3 detail=5')
        self.fails(ls, 'UT18')

    def test_ut18_order_and_perf_missing(self):
        ls = self.load('ut18')
        a, b = self.idx(ls, r'stage=lcd'), self.idx(ls, r'stage=touch')
        ls[a], ls[b] = ls[b], ls[a]
        self.fails(ls, 'UT18')
        ls = self.load('ut18')
        del ls[self.idx(ls, r'\[perf\]')]
        self.fails(ls, 'UT18', '--ut', 'UT18')

    def test_ut18_perf_over_is_warning_only(self):
        ls = self.load('ut18')
        i = self.idx(ls, r'\[perf\]')
        ls[i] = re.sub(r'slide_us=\d+', 'slide_us=20000', ls[i])
        rc, out = self.run_j(ls)
        self.assertEqual(rc, 0, out)
        self.assertEqual(status(out, 'UT18'), 'PASS')
        self.assertIn('WARN: slide_us=20000', out)

    def test_ut18_first_hb_tick_is_warning_only(self):
        ls = self.load('ut18')
        for k, l in enumerate(ls):                                   # [perf] の測定で最初の [hb] が遅れた状況: 全部 +200
            m = re.search(r'\[hb\] tick=(\d+) ', l)
            if m:
                ls[k] = l.replace('tick=%s ' % m.group(1), 'tick=%d ' % (int(m.group(1)) + 200))
        rc, out = self.run_j(ls)
        self.assertEqual(rc, 0, out)
        self.assertEqual(status(out, 'UT18'), 'PASS')
        self.assertIn('WARN: 最初の [hb] の tick=300', out)

    # ---- UT19
    def test_ut19(self):
        ls = self.load('ut19')
        ls.insert(self.idx(ls, r'btn=f_home st=down'), '12:00:10.000 [ev] btn=h_demo st=down src=touch')
        self.fails(ls, 'UT19')                                      # ボタン外のはずの場所で btn= が出た
        ls = self.load('ut19')
        del ls[self.idx(ls, r'btn=h_fill st=act')]
        self.fails(ls, 'UT19')
        ls = self.load('ut19')
        i = self.idx(ls, r'set=43')
        ls[i] = ls[i].replace('set=43', 'set=44')
        self.fails(ls, 'UT19')


class ProductWxTests(unittest.TestCase):
    """天気の取得を組み込んだ製品(APP_WX = 1。design-uart.md §13)のログ: [wifi] [wifiv] [wx] の行を受け付け、res=start・key=oth・範囲外の値は C5 で不合格"""
    WX = [
        '[wx] net=conn busy=1 have=0 temp10=0 kind=none age=-1 clk=none date=none wd=9 sync=0',
        '[wifi] step=reset res=ok try=1 ms=20 code=0',
        '[wifi] result=pass stage=none ping_ip=0 ping_dns=0',
        '[wifiv] key=ip val=192.168.1.23',
        '[wx] req=ignored src=uart',
        '[wx] res=ok src=boot temp10=127 wmo=0 kind=sun http=200 ms=830 sync=1 jst=132413 obs=1330 closed=1',
        '[wx] res=fail src=timer err=1 http=0 ms=1000',
        '[wx] net=on busy=0 have=1 temp10=127 kind=sun age=0 clk=132413 date=20261125 wd=3 sync=1',
    ]

    load = staticmethod(MutationTests.load)
    run_j = MutationTests.run_j
    fails = MutationTests.fails
    idx = MutationTests.idx

    def with_wx(self, extra=()):
        ls = self.load('ut10')
        i = self.idx(ls, r'\[hb\]', 2)
        stamp = ls[i - 1][:13]                                        # 直前の行と同じ時刻(単調を保つ)
        return ls[:i] + [stamp + b for b in self.WX + list(extra)] + ls[i:]

    def test_product_lines_are_accepted(self):
        rc, out = self.run_j(self.with_wx())
        self.assertEqual(rc, 0, out)
        self.assertEqual(status(out, 'C1'), 'PASS', out)
        self.assertEqual(status(out, 'C5'), 'PASS', out)
        self.assertEqual(status(out, 'UT10'), 'PASS', out)

    def test_no_wx_lines_means_no_c5(self):
        rc, out = self.run_j(self.load('ut10'))
        self.assertEqual(rc, 0, out)
        self.assertIsNone(status(out, 'C5'))

    def test_res_start_fails(self):
        self.fails(self.with_wx(['[wifi] step=wx_mux res=start try=1 ms=0 code=0']), 'C5')

    def test_key_oth_fails(self):
        self.fails(self.with_wx(['[wifiv] key=oth val=aaaa_999']), 'C5')

    def test_value_out_of_range_fails(self):
        self.fails(self.with_wx(['[wx] res=ok src=uart temp10=601 wmo=0 kind=sun http=200 ms=830 sync=1 jst=132413 obs=1330 closed=1']), 'C5')
        self.fails(self.with_wx(['[wx] res=ok src=uart temp10=127 wmo=100 kind=sun http=200 ms=830 sync=1 jst=132413 obs=1330 closed=1']), 'C1')

    def test_bad_wx_format_fails(self):
        self.fails(self.with_wx(['[wx] res=ok src=boot temp10=12.7 wmo=0']), 'C1')
        self.fails(self.with_wx(['[wx] req=later src=uart']), 'C1')

    def test_h_wx_button_event(self):
        ls = self.with_wx(['[ev] btn=h_wx st=down src=uart', '[ev] btn=h_wx st=act src=uart'])
        rc, out = self.run_j(ls)
        self.assertEqual(status(out, 'C1'), 'PASS', out)


if __name__ == '__main__':
    unittest.main()
