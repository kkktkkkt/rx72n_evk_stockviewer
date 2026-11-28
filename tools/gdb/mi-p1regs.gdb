# P1(RGB565 製品ファーム)の GLCDC レジスタ読み取り(CLUT8 実験の GR2AB1 / BGEN との比較用)
# 前提: 製品ファームが書込み済み。接続で MCU リセット。終了状態: gdb 切断で CPU 停止(rfp -sig -run で戻す)
-gdb-set non-stop on
-gdb-set mi-async on
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#waitout \*stopped 8
-interpreter-exec console "monitor enable_execute_on_connect"
-exec-continue --thread 1
#sleep 4
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== P1REGS"
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
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->BGEN.LONG"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->STMON.LONG"
#sync p1 60
-gdb-exit
