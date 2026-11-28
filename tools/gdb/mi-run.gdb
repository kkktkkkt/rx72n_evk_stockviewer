# 標準手順(MI, non-stop): 停止通知 -> enable_execute_on_connect -> load -> hbreak main -> continue(main で停止) -> continue(走行) -> interrupt -> tick 比較
# 前提: tools\gdb-run.ps1 -Mi で起動(サーバー起動済み)。ELF 書込みあり。終了状態: 最後は interrupt で停止(gdb-run.ps1 が切断。CPU は停止のまま。走らせ直しは rfp reset-run)
-gdb-set non-stop on
-gdb-set mi-async on
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor console_log_enable"
-interpreter-exec console "monitor set_internal_mem_overwrite 0-645"
-interpreter-exec console "monitor force_rtos_off"
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#waitout \*stopped 8
-interpreter-exec console "monitor enable_execute_on_connect"
-target-download
-break-insert -t -h main
-exec-continue --thread 1
#waitout breakpoint-hit 30
-interpreter-exec console "monitor getPC"
-data-evaluate-expression g_app_dbg.tick
-exec-continue --thread 1
#sleep 8
-exec-interrupt --all
#sleep 3
-thread-info
-data-evaluate-expression g_app_dbg.tick
#sleep 2
-data-evaluate-expression g_app_dbg.tick
-gdb-exit
