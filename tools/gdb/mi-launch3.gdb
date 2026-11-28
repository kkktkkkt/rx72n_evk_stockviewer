# launch 系 monitor(load なし) → raw step で停止通知 → -exec-continue。GO が出るか(サーバーログ)と UART の tick で判定
# 前提: サーバー起動済み。書込みなし。終了状態: 走行中の可能性(最後に gdb-run.ps1 が切断)
-gdb-set non-stop on
-gdb-set mi-async on
-interpreter-exec console "set debug remote 1"
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor console_log_enable"
-interpreter-exec console "monitor is_target_connected"
-interpreter-exec console "monitor is_target_connected"
-interpreter-exec console "monitor configuration_complete"
-interpreter-exec console "monitor set_internal_mem_overwrite 0-645"
-interpreter-exec console "monitor force_rtos_off"
-interpreter-exec console "monitor launch_complete"
-interpreter-exec console "monitor all_launches_complete"
#sleep 2
-interpreter-exec console "maint packet vCont;s:p1.1"
#waitout \*stopped 8
-thread-info
-exec-continue --thread 1
#sleep 8
-thread-info
-data-evaluate-expression g_app_dbg.tick
#sleep 3
-data-evaluate-expression g_app_dbg.tick
-gdb-exit
