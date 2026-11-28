# e2 studio の起動手順の推定 その2(MI, non-stop): launch 系 monitor の後に interrupt で停止通知を出させ、hbreak main → continue
# 前提: サーバー起動済み。書込みあり。終了状態: 走行中の可能性(最後に gdb-run.ps1 が切断)
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
-interpreter-exec console "monitor prg_download_start"
-target-download
-interpreter-exec console "monitor prg_download_end"
-interpreter-exec console "monitor launch_complete"
-interpreter-exec console "monitor all_launches_complete"
-exec-interrupt --all
#waitout \*stopped 40
-thread-info
-break-insert -t -h main
-exec-continue --thread 1
#waitout \*stopped.*breakpoint 15
-thread-info
-data-evaluate-expression g_app_dbg.tick
-gdb-exit
