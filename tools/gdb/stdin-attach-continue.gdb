# 前提: ターゲットが走行中(rfp write / reset-run の直後)。load しない・書込みなし。SI=true NS=true
# 狙い: 走行中に接続 → gdb が止める(本物の停止通知が出るはず)→ 変数を読む → continue で再開、が GO を出すか。サーバーログの <FFWMCUCmd_GO> で判定
set pagination off
set confirm off
monitor console_log_enable
echo [1] 接続直後のスレッド(停止通知が処理されれば frame 表示)\n
info threads
python import time; time.sleep(1.5)
info threads
echo [2] tick(止めた時点)\n
print g_app_dbg.tick
info registers pc
echo [3] continue &\n
set debug remote 1
continue &
python import time; time.sleep(3.0)
set debug remote 0
info threads
monitor is_target_stopped,0
print g_app_dbg.tick
python import time; time.sleep(1.0)
print g_app_dbg.tick
quit
