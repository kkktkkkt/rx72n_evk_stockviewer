# ひな形: 走行中のプログラムに接続し、停止して変数を読む(nonstop:true で使う)。
# 前提: RFP で書込み+走行済み(`tools/submit-job.sh rfp '{"mode":"write"}'` または `reset-run`)。サーバーは server-start 済み(tools/fresh-gdb.sh が行う)。
# 動作: gdb が接続した時点で CPU は【停止】する(観察済み)。tick はその時点の値で凍結し、2回読んでも同じ。
# 終了状態: CPU は【停止のまま】。走行に戻すには、server-stop の後に `tools/submit-job.sh rfp '{"mode":"reset-run"}'`(書込みなし。リセットして走る。tick は 0 から)。
# 廃止(2026-11-20): 「gdb で reset → continue して走らせる」手順は廃止した。サーバー(e2-server-gdb)は continue に OK を返すが
#   E2 Lite へ GO を送らない(<FFWMCUCmd_GO> が一度も出ない。docs/debug-report.md §4.1b/c)。走らせるのは RFP の -run で行う。
# 使い方: NS=true tools/fresh-gdb.sh tools/gdb/template-run-and-read.gdb 40 70   (その後 server-stop)
set pagination off
set confirm off
set print pretty on
monitor force_rtos_off
echo [1] 接続時の tick(CPUを止めた時点の値。小さければ起動直後、10ms単位)\n
print g_app_dbg.tick
echo [2] 変数\n
print g_app_dbg
echo [3] 状態(stopped が正常)\n
monitor is_target_stopped,0
detach
