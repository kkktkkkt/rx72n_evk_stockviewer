# monitor go_all / can_go_all の試験(MI, non-stop, load なし)。GO が出るか(サーバーログ)と tick で判定
# 前提: サーバー起動済み。書込みなし。終了状態: 走行中の可能性
-gdb-set non-stop on
-gdb-set mi-async on
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor console_log_enable"
-interpreter-exec console "monitor can_go_all"
-interpreter-exec console "monitor can_stop_all"
-interpreter-exec console "monitor printCoreStatus"
-interpreter-exec console "monitor printSettings"
-interpreter-exec console "monitor is_target_stopped,0"
-data-evaluate-expression g_app_dbg.tick
-interpreter-exec console "monitor go_all"
#sleep 4
-interpreter-exec console "monitor is_target_stopped,0"
-data-evaluate-expression g_app_dbg.tick
#sleep 2
-data-evaluate-expression g_app_dbg.tick
-gdb-exit
