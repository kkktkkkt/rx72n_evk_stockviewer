# アンダーフロー(uf=1)が起動時のどこで出るかの切り分け: 起動シーケンスの要所で止めて GLCDC.STMON(bit2 = GR2UF)を読む。load なし。読み出しのみ(STMON のクリアは poll が行う)
# 要所: touch_init(lcd_init の直後)/ applog_fmt_perf(perf 計測の直後。引数 = 計測値)/ 走行 1.2秒後(最初の uf poll の後)
# 前提: サーバー起動済み。終了状態: 停止
-gdb-set non-stop on
-gdb-set mi-async on
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#waitout \*stopped 8
-interpreter-exec console "monitor enable_execute_on_connect"
-break-insert -h touch_init
-break-insert -h applog_fmt_perf
-exec-continue --thread 1
#waitnew breakpoint-hit 30
-interpreter-exec console "echo ==== at_touch_init\n"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->STMON.LONG"
-interpreter-exec console "print s_tick"
-exec-continue --thread 1
#waitnew breakpoint-hit 30
-interpreter-exec console "echo ==== at_perf_fmt\n"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->STMON.LONG"
-interpreter-exec console "print s_tick"
-interpreter-exec console "info registers r1 r2 r3 r4"
-interpreter-exec console "x/3wx $sp"
-exec-continue --thread 1
#sleep 1.3
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== after_run\n"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->STMON.LONG"
-interpreter-exec console "print g_app_dbg.uf"
-interpreter-exec console "print s_tick"
-gdb-exit
