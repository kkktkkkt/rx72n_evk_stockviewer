# 走行中のプログラムに接続して停止し、tick / err を読むだけ(load なし、continue なし)。接続で CPU は止まる
# 前提: サーバー起動済み。書込みなし。終了状態: 停止(rfp reset-run で復帰)
-gdb-set non-stop on
-gdb-set mi-async on
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#waitout \*stopped 8
-data-evaluate-expression g_app_dbg.tick
-data-evaluate-expression g_app_dbg.err_code
-data-evaluate-expression g_app_dbg.err_detail
-data-evaluate-expression s_log_ready
-data-evaluate-expression s_log_tried
-data-evaluate-expression s_q
-data-evaluate-expression g_app_dbg
-interpreter-exec console "monitor getPC"
-gdb-exit
