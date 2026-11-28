# e2 studio 相当(MI, non-stop)。gdb-run.ps1 -Mi -Script で使う。書込み(-target-download)あり
# 前提: サーバー起動済み。終了状態: -exec-continue 後は走行中(最後に gdb-run.ps1 が切断)
-gdb-set breakpoint pending on
-gdb-set detach-on-fork on
-gdb-set python print-stack none
-gdb-set print object on
-gdb-set print sevenbit-strings on
-gdb-set host-charset UTF-8
-gdb-set target-async on
-gdb-set non-stop on
-gdb-set auto-solib-add on
-interpreter-exec console "set debug remote 1"
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor console_log_enable"
-interpreter-exec console "monitor set_internal_mem_overwrite 0-645"
-interpreter-exec console "monitor force_rtos_off"
-target-download
#sleep 1
-break-insert -t -h main
-exec-continue --thread 1
#waitout \*stopped 20
-thread-info
-data-evaluate-expression g_app_dbg.tick
-exec-continue --thread 1
#sleep 4
-interpreter-exec console "interrupt -a"
#sleep 2
-thread-info
-data-evaluate-expression g_app_dbg.tick
-gdb-exit
