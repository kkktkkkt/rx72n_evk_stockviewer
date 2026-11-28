# monitor enable_stopped_notify_on_connect 等の試験(MI, non-stop, load なし)。*stopped が来るか、GO が出るかで判定
# 前提: サーバー起動済み。書込みなし。終了状態: 走行中の可能性
-gdb-set non-stop on
-gdb-set mi-async on
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor console_log_enable"
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
-interpreter-exec console "monitor printCoreStatus"
-exec-interrupt --all
#waitout \*stopped 4
-interpreter-exec console "monitor notify_resumed"
-exec-interrupt --all
#waitout \*stopped 4
-thread-info
-exec-continue --thread 1
#sleep 5
-data-evaluate-expression g_app_dbg.tick
-gdb-exit
