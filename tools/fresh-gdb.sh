#!/bin/bash
# 使い方: [NS=true] [NC=true] [SI=true] [SX='extra args'] tools/fresh-gdb.sh tools/gdb/xxx.gdb <gdb timeoutSec> <待ち秒>  (NS=nonstop, NC=noconnect(スクリプトで接続), SI=stdinモード)
# server-stop → server-start → サーバログから今回のポートを取り出して gdb ジョブを出す(サーバは1セッション限り。毎回作り直す)
cd "$(dirname "$0")/.."
tools/submit-job.sh server-stop '{}' 20 | grep -E '"exit"'
ARGS="{}"; if [ -n "$SX" ] && [ -n "$SUB" ]; then ARGS="{\"serverExtra\":\"$SX\",\"serverSub\":\"$SUB\"}"; elif [ -n "$SX" ]; then ARGS="{\"serverExtra\":\"$SX\"}"; elif [ -n "$SUB" ]; then ARGS="{\"serverSub\":\"$SUB\"}"; fi; tools/submit-job.sh server-start "$ARGS" 40 | grep -E 'args|exited'
P=""
for i in $(seq 1 60); do   # 接続終了後に "GDB: <port>" が出る(正常時は数秒)。出なければ中止(E2 Lite 不調)
  P=$(grep "^GDB:" jobs/server.out.log | tail -1 | awk '{print $2}')
  [ -n "$P" ] && break; sleep 1
done
echo port=$P
if [ -z "$P" ]; then echo "中止: 60秒待ってもポートが出ない(E2 Liteの初期化失敗の疑い)。gdbジョブは出さない" >&2; tail -n 5 jobs/server.out.log; tools/submit-job.sh server-stop '{}' 20 | grep -E '"exit"'; exit 3; fi
tools/submit-job.sh gdb "{\"script\":\"$1\",\"nonstop\":${NS:-false},\"noconnect\":${NC:-false},\"stdin\":${SI:-false},\"timeoutSec\":$2,\"port\":$P}" $3
echo "--- server.err"; cat jobs/server.err.log; echo "--- server.out tail"; tail -n 4 jobs/server.out.log
