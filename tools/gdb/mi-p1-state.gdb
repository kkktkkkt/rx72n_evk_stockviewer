# P1 ファームの状態を読む(load なし): 接続(リセット) -> 5秒走行 -> 停止して g_app_dbg 全体を読む -> 再度 10秒走行 -> 停止して読む。UART が使えない時の代替確認
# 前提: サーバー起動済み。書込みなし。終了状態: 停止(rfp-cli -sig -run で走行へ)
-gdb-set non-stop on
-gdb-set mi-async on
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#waitout \*stopped 8
-interpreter-exec console "monitor enable_execute_on_connect"
-exec-continue --thread 1
#sleep 5
-exec-interrupt --all
#sleep 2
-interpreter-exec console "echo ==== run1\n"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 10
-exec-interrupt --all
#sleep 2
-interpreter-exec console "echo ==== run2\n"
-interpreter-exec console "print g_app_dbg"
-gdb-exit
