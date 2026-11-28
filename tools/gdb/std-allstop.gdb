# 標準手順(all-stop): target remote -> load -> hbreak main -> continue。tools\gdb-run.ps1 -Script で使う(標準入力モード)
# 前提: サーバー起動済み・接続状況OK後。書込み(load)あり。終了状態: main で停止 -> continue 後は走行中(gdb-run.ps1 が最後に切断)
set pagination off
set confirm off
target remote localhost:$PORT
info threads
info registers pc
monitor console_log_enable
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
load
info registers pc
hbreak main
continue
#sleep 5
info threads
info registers pc
print g_app_dbg.tick
continue &
#sleep 4
interrupt
#sleep 2
info registers pc
print g_app_dbg.tick
quit
