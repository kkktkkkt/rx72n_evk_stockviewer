# fbdump の予備試験: 接続(リセット) -> 3秒走行 -> 停止 -> GR2FLM2 と s_ui/s_back を読む -> 表示面を dump -> 'n' を送る -> 1.5秒 -> 停止 -> 同様に読む
# 前提: サーバー起動済み。load なし。終了状態: 停止
-gdb-set non-stop on
-gdb-set mi-async on
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#waitout \*stopped 8
-interpreter-exec console "monitor enable_execute_on_connect"
-exec-continue --thread 1
#sleep 3
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== home\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_back"
-interpreter-exec console "print s_flip_pending"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print g_app_dbg"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/t0_home.bin $fb ($fb+261120)"
#sync dump1 120
-exec-continue --thread 1
#uart n
#sleep 1.5
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== after_n\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_back"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print g_app_dbg"
-gdb-exit
