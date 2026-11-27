# 前提: ターゲットが走行中(リセットボタン直後など)。接続すると gdb が vCont;t でCPUを【停止】させる(観察済み)。
# よってこれは「接続した瞬間のスナップショット」。tick は接続時の値で凍結する(2回読んでも同じ)。書込みなし。nonstop:true
# 終了状態: CPU は停止のまま(CLIからの再開は未成功)。画面表示は残るが、タッチ切替には再度リセットボタンが必要。
set pagination off
set confirm off
set print pretty on
monitor force_rtos_off
echo [1] tick 1回目\n
print g_app_dbg.tick
python import time; time.sleep(2.0)
echo [2] tick 2回目(2秒後。1回目より増えていれば走行中。10ms単位なので約200増える)\n
print g_app_dbg.tick
monitor is_target_stopped,0
echo [3] 変数\n
print g_app_dbg
echo [4] detach\n
detach
