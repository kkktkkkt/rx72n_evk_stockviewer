# 停止通知が来る状態から、server の各 monitor フラグを試して -exec-continue で GO が出るか見る(MI, non-stop, load なし)
# 判定: tick が増えるか / monitor getPC / サーバーログの GO。前提: サーバー起動済み。書込みなし。終了状態: 不定
-gdb-set non-stop on
-gdb-set mi-async on
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor console_log_enable"
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#waitout \*stopped 5
-interpreter-exec console "monitor launch_complete"
-interpreter-exec console "monitor all_launches_complete"
-exec-continue --thread 1
#sleep 2
-interpreter-exec console "monitor getPC"
-exec-interrupt --all
#sleep 1
-interpreter-exec console "monitor enable_execute_on_connect"
-exec-continue --thread 1
#sleep 2
-interpreter-exec console "monitor getPC"
-exec-interrupt --all
#sleep 1
-interpreter-exec console "monitor ignore_continue_during_connect"
-exec-continue --thread 1
#sleep 2
-interpreter-exec console "monitor getPC"
-exec-interrupt --all
#sleep 1
-data-evaluate-expression g_app_dbg.tick
-gdb-exit
