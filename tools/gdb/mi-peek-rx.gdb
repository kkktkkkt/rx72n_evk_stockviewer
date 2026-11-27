# UART 受信(PC -> MCU)試験: 接続(リセット) -> continue(GB、load なし) -> 別プロセスが COM7 へ 'n' を送る(約12秒後) -> interrupt -> image_index と rx 状態を読む。読むだけ
# 前提: サーバー起動済み。load しない。終了状態: 停止(rfp reset-run で走行へ)
-gdb-set non-stop on
-gdb-set mi-async on
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#waitout \*stopped 8
-interpreter-exec console "monitor enable_execute_on_connect"
-exec-continue --thread 1
#sleep 20
-exec-interrupt --all
#sleep 2
-data-evaluate-expression g_app_dbg.tick
-data-evaluate-expression g_app_dbg.image_index
-interpreter-exec console "print s_rx_ev_seq"
-interpreter-exec console "print s_rx_ev_val"
-interpreter-exec console "print *s_log_hdl"
-interpreter-exec console "x/bx 0x8a044"
-interpreter-exec console "x/bx 0x8a042"
-gdb-exit
