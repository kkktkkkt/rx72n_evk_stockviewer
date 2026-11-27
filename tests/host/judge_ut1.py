# UT1 判定(設計 §6.3〜§6.5)。使い方: python3 -I judge_ut1.py <serial ジョブのログ>
import re, sys
HB = re.compile(r'\[hb\] tick=(\d+) img=(\d+) err=(\d+) detail=(\d+) pts=(\d+) x=(\d+) y=(\d+) i2c_fail=(\d+) streak=(\d+) found=(\d+) drop=(\d+)')
text = open(sys.argv[1], encoding='utf-8', errors='replace').read().lstrip('﻿')
lines = [l for l in re.split(r'\r?\n', text) if l != '']
body = lines[1:-1] if len(lines) > 2 else lines            # 最初と最後の行は途中切れがありうる(§6.3)
hb = [m for m in (HB.fullmatch(l) for l in body) if m]
foreign = [l for l in body if not l.startswith('[')]       # 異物 = '[' で始まらない非空行
v = [[int(x) for x in m.groups()] for m in hb]
res, warn = [], []
res.append(('(1) [hb] が 13〜16 本', 13 <= len(hb) <= 16, len(hb)))
bad = []
for a, b in zip(v, v[1:]):
    d = (b[0] - a[0]) & 0xFFFFFFFF
    if 100 <= d <= 110: continue
    if d <= 130 or (b[7] - a[7]) >= 10: warn.append(('tick差', d)); continue
    bad.append(d)
res.append(('(2) tick 差が 100〜110(130超は不合格)', not bad, bad or 'ok'))
res.append(('(3) 異物の行が 0', not foreign, foreign[:3]))
res.append(('(4) drop がすべて 0', all(x[10] == 0 for x in v), [x[10] for x in v if x[10]][:3]))
res.append(('(5) err=0・i2c_fail 一定・streak=0', bool(v) and all(x[2] == 0 and x[8] == 0 for x in v) and len({x[7] for x in v}) == 1, 'ok'))
for n, ok, info in res: print(('PASS ' if ok else 'FAIL ') + n, '' if ok else info)
for w in warn: print('WARN', w)
sys.exit(0 if all(ok for _, ok, _ in res) else 1)
