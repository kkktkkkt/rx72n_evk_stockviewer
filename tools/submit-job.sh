#!/bin/bash
# submit-job.sh  --  LLM(Linux VM)側からジョブを出して結果を待つ。ステータス: 未検証
# 使い方: tools/submit-job.sh <action> ['<argsのJSON>'] [待ち秒数]
#   例: tools/submit-job.sh ping
#       tools/submit-job.sh build '{"mode":"Make"}' 300
#       tools/submit-job.sh gdb '{"script":"tools/gdb/read-vars.gdb","timeoutSec":30}' 60
#       tools/submit-job.sh serial '{"port":"COM7","baud":115200,"seconds":15}' 40
#   rfp(Renesas Flash Programmer。mode は help|doc|list|write|reset-run だけ):
#       tools/submit-job.sh rfp '{"mode":"write"}' 175        # 消去+書込み+ベリファイ+走行(HardwareDebug\rx72n_stockview.mot 固定。約65秒)
#       tools/submit-job.sh rfp '{"mode":"reset-run"}' 120    # 書込みなし。リセットして走らせるだけ(gdb 接続で止めた後に使う)
#       tools/submit-job.sh rfp '{"mode":"list","device":"RX72x"}' 120   # tool / interface 一覧(読み取りのみ)
#   gdb(接続でCPUが止まる。変数を読むだけ):  NS=true tools/fresh-gdb.sh tools/gdb/attach-read.gdb 40 70  ; tools/submit-job.sh server-stop '{}' 30
#   serial の送信(任意): tx=送る文字([nps?x] のみ、1〜200文字) txAtSec=開いてから送るまでの秒(既定2、seconds未満) txGapMs=1文字ごとの間隔ms(既定50、0なら1回で全部) dtr=true で DTR を立てる
#       tools/submit-job.sh serial '{"port":"COM7","baud":115200,"seconds":12,"tx":"nnnp","txAtSec":3,"txGapMs":400}' 40    # 遠隔で画像切替(受信は seconds の終わりまで。ログは受信分のみ)
#       tools/submit-job.sh serial '{"port":"COM7","baud":115200,"seconds":14,"tx":"nnnnnnnnnn","txAtSec":3,"txGapMs":0}' 40  # 連打(1回の書込み)
set -e
cd "$(dirname "$0")/.."
action="$1"; args="${2:-{\}}"; wait="${3:-120}"
[ -n "$action" ] || { echo "usage: $0 <action> [argsJSON] [waitSec]"; exit 2; }
# ランナーが生きているか(runner.alive が10秒以内)
if [ -f jobs/runner.alive ]; then
  age=$(( $(date +%s) - $(date -d "$(cat jobs/runner.alive | tr -d '\r\n')" +%s 2>/dev/null || echo 0) ))
  [ "$age" -lt 15 ] || echo "警告: runner.alive が ${age}秒前。ランナーが止まっている可能性" >&2
else
  echo "警告: jobs/runner.alive が無い。ユーザーが tools\\job-runner.ps1 を起動していない" >&2
fi
id="$(date +%H%M%S)-$action"
printf '{"action":"%s","args":%s}\n' "$action" "$args" > "jobs/inbox/.$id.tmp"
mv "jobs/inbox/.$id.tmp" "jobs/inbox/$id.json"   # 書き終えてから見せる
echo "submitted: $id"
for i in $(seq 1 "$wait"); do
  if [ -f "jobs/outbox/$id.result.json" ]; then
    echo "--- result"; cat "jobs/outbox/$id.result.json"
    echo "--- log"; cat "jobs/outbox/$id.log" 2>/dev/null
    exit 0
  fi
  sleep 1
done
echo "タイムアウト(${wait}s): 結果なし。jobs/inbox/$id.json が残っていればランナー未起動" >&2
exit 1
