# P2b(CLUT8 製品)の UART を使わない確認(UART 無音のときの代替): 接続(リセット) -> applog_fmt_perf で止めて [perf] の引数を読む -> 走行 3秒 -> interrupt -> GLCDC レジスタ・OFSM・パレット(0xE0800/0xE0C00)・状態変数・表示面を dump(ホーム) -> 走行 10秒 -> g_app_dbg(uf など)
# 前提: サーバー起動済み。load なし(書込み済み)。jobs/fb/dev/ がある。終了状態: 停止(gdb 切断で STOP。rfp reset-run で走行へ)
-gdb-set non-stop on
-gdb-set mi-async on
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#waitout \*stopped 8
-interpreter-exec console "monitor enable_execute_on_connect"
-break-insert -h applog_fmt_perf
-exec-continue --thread 1
#waitnew breakpoint-hit 30
-interpreter-exec console "echo ==== at_perf_fmt\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "info registers r1 r2 r3 r4"
-interpreter-exec console "x/3wx $sp"
-interpreter-exec console "print g_app_dbg"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->STMON.LONG"
-exec-continue --thread 1
#sleep 3
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== regs\n"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2FLM3.LONG"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2FLM5.LONG"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2FLM6.LONG"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2FLMRD.LONG"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2AB1.LONG"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2AB2.LONG"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2AB3.LONG"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2VEN.LONG"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2BASE.LONG"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->BGEN.LONG"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->STMON.LONG"
-interpreter-exec console "x/2wx 0xFE7F5D00"
-interpreter-exec console "print s_back"
-interpreter-exec console "print &ui_pal"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/ui_pal_rom.bin &ui_pal[0] (&ui_pal[0]+256)"
-interpreter-exec console "echo ==== home\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_back"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.ov"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.tr_fwd"
-interpreter-exec console "print s_ui.tr_dx"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print g_app_dbg"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/A_home.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/A_home.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/A_home.pal1.bin 0xe0c00 0xe1000"
#sync d_home 120
-exec-continue --thread 1
#sleep 10
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== after10s\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print g_app_dbg"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->STMON.LONG"
-gdb-exit
