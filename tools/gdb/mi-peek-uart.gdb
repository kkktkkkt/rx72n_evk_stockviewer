# UART 無音の切り分け: 接続(リセットされる) -> 書込みなしで continue(GB) -> 10秒走行 -> interrupt -> SCI2/ポート/ICU/MSTP とアプリ変数を読む。読むだけ(RAM/周辺の読み出しのみ)
# 前提: サーバー起動済み。load しない。終了状態: 停止(rfp reset-run で走行へ)
-gdb-set non-stop on
-gdb-set mi-async on
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor console_log_enable"
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#waitout \*stopped 8
-interpreter-exec console "monitor enable_execute_on_connect"
-exec-continue --thread 1
#sleep 10
-exec-interrupt --all
#sleep 2
-interpreter-exec console "echo ==== APP\n"
-data-evaluate-expression g_app_dbg.tick
-data-evaluate-expression s_log_ready
-interpreter-exec console "print s_q.count"
-interpreter-exec console "print s_q.head"
-interpreter-exec console "print s_q.tail"
-interpreter-exec console "print s_q.drops"
-interpreter-exec console "print *s_log_hdl"
-interpreter-exec console "print *s_log_hdl->rom"
-interpreter-exec console "echo ==== SCI2 SMR BRR SCR TDR SSR SCMR SEMR\n"
-interpreter-exec console "x/bx 0x8a040"
-interpreter-exec console "x/bx 0x8a041"
-interpreter-exec console "x/bx 0x8a042"
-interpreter-exec console "x/bx 0x8a044"
-interpreter-exec console "x/bx 0x8a046"
-interpreter-exec console "x/bx 0x8a047"
-interpreter-exec console "echo ==== MSTPCRB(bit29=SCI2) 0x80014\n"
-interpreter-exec console "x/wx 0x80014"
-interpreter-exec console "echo ==== PORT1 PDR 0x8c001 PMR 0x8c061 PCR 0x8c0c1 ; P12PFS 0x8c14a P13PFS 0x8c14b\n"
-interpreter-exec console "x/bx 0x8c001"
-interpreter-exec console "x/bx 0x8c061"
-interpreter-exec console "x/bx 0x8c0c1"
-interpreter-exec console "x/bx 0x8c14a"
-interpreter-exec console "x/bx 0x8c14b"
-interpreter-exec console "echo ==== ICU IR[62 RXI2,63 TXI2] IER07 IPR62 IPR63 DTCER63\n"
-interpreter-exec console "x/2bx 0x8703e"
-interpreter-exec console "x/bx 0x87207"
-interpreter-exec console "x/2bx 0x8733e"
-interpreter-exec console "x/bx 0x8713f"
-interpreter-exec console "echo ==== GRPBL0 0x87630 GENBL0 0x87670\n"
-interpreter-exec console "x/wx 0x87630"
-interpreter-exec console "x/wx 0x87670"
-interpreter-exec console "monitor getPC"
-gdb-exit
