# enable_execute_on_connect 単独(continue なし)で GB が出るか、その後 continue で出るかを分けて見る(MI, non-stop, load なし)
# 判定: 各段の monitor getPC / is_target_stopped と、サーバーログの GB 出現位置。前提: サーバー起動済み。書込みなし。終了状態: 走行中の可能性
-gdb-set non-stop on
-gdb-set mi-async on
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor console_log_enable"
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#sleep 1
-interpreter-exec console "monitor getPC"
-interpreter-exec console "monitor message A_before_execute_flag"
-interpreter-exec console "monitor enable_execute_on_connect"
#sleep 3
-interpreter-exec console "monitor is_target_stopped,0"
-interpreter-exec console "monitor message B_before_continue"
-exec-continue --thread 1
#sleep 3
-interpreter-exec console "monitor is_target_stopped,0"
-gdb-exit
