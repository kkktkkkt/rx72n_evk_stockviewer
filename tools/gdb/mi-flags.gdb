# 停止通知(enable_stopped_notify_on_connect)で gdb を停止状態に戻しながら、各 monitor フラグ後の -exec-continue を試す(MI, non-stop, load なし)
# 判定: 各回の monitor getPC(0xffc00000 から動けば走った)と tick。前提: サーバー起動済み。書込みなし。終了状態: 停止中
-gdb-set non-stop on
-gdb-set mi-async on
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor console_log_enable"
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#sleep 1
-interpreter-exec console "echo ==== enable_execute_on_connect\n"
-interpreter-exec console "monitor enable_execute_on_connect"
-exec-continue --thread 1
#sleep 2
-interpreter-exec console "monitor getPC"
-interpreter-exec console "monitor is_target_stopped,0"
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#sleep 1
-interpreter-exec console "echo ==== ignore_continue_during_connect\n"
-interpreter-exec console "monitor ignore_continue_during_connect"
-exec-continue --thread 1
#sleep 2
-interpreter-exec console "monitor getPC"
-interpreter-exec console "monitor is_target_stopped,0"
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#sleep 1
-interpreter-exec console "echo ==== skip_continue\n"
-interpreter-exec console "monitor skip_continue"
-exec-continue --thread 1
#sleep 2
-interpreter-exec console "monitor getPC"
-interpreter-exec console "monitor is_target_stopped,0"
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#sleep 1
-interpreter-exec console "echo ==== prg_download_start_on_connect\n"
-interpreter-exec console "monitor prg_download_start_on_connect"
-exec-continue --thread 1
#sleep 2
-interpreter-exec console "monitor getPC"
-interpreter-exec console "monitor is_target_stopped,0"
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#sleep 1
-gdb-exit
