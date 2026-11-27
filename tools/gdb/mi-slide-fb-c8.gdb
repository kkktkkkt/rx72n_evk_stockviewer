# [P2b CLUT8] スライド途中の表示面の確認: 接続(リセット) -> 画面送り(n / p)の直後(0.04〜0.20 秒後)に interrupt して dump。S1〜S6。tr_dx は dump より 1 コマ新しいので、fb2png.py check の比較は一致しない場合がある(1 コマ前の状態で再描画して比べる)。
# 前提: サーバー起動済み。COM7 が使える。load なし。dump は jobs/fb/dev/S_<段>.bin(+ .pal0/.pal1)。終了状態: 停止
-gdb-set non-stop on
-gdb-set mi-async on
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#waitout \*stopped 8
-interpreter-exec console "monitor enable_execute_on_connect"
-exec-continue --thread 1
#sleep 3
#uart n
#sleep 0.04
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s1\n"
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
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/S_s1.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/S_s1.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/S_s1.pal1.bin 0xe0c00 0xe1000"
#sync d_s1 120
-exec-continue --thread 1
#sleep 1.5
#uart p
#sleep 0.08
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s2\n"
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
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/S_s2.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/S_s2.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/S_s2.pal1.bin 0xe0c00 0xe1000"
#sync d_s2 120
-exec-continue --thread 1
#sleep 1.5
#uart n
#sleep 0.12
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s3\n"
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
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/S_s3.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/S_s3.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/S_s3.pal1.bin 0xe0c00 0xe1000"
#sync d_s3 120
-exec-continue --thread 1
#sleep 1.5
#uart p
#sleep 0.16
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s4\n"
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
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/S_s4.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/S_s4.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/S_s4.pal1.bin 0xe0c00 0xe1000"
#sync d_s4 120
-exec-continue --thread 1
#sleep 1.5
#uart n
#sleep 0.2
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s5\n"
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
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/S_s5.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/S_s5.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/S_s5.pal1.bin 0xe0c00 0xe1000"
#sync d_s5 120
-exec-continue --thread 1
#sleep 1.5
#uart p
#sleep 0.1
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s6\n"
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
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/S_s6.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/S_s6.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/S_s6.pal1.bin 0xe0c00 0xe1000"
#sync d_s6 120
-exec-continue --thread 1
#sleep 1.5
