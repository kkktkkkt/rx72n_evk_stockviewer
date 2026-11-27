# フレームバッファの検証(静的な画面): 接続(リセット) -> 各画面を UART の n/+ で順に出し、段ごとに interrupt -> 状態変数を print -> 表示面(GR2FLM2)を dump -> continue
# 前提: サーバー起動済み。COM7 が使える(tools\gdb-run.ps1 の #uart で送信)。load なし。dump は jobs\fb\A_<段>.bin。終了状態: 走行中(最後に gdb-run が切断=停止)
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
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/A_home.bin $fb ($fb+261120)"
#sync d_home 120
-exec-continue --thread 1
#uart n
#sleep 1.5
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== fill\n"
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
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/A_fill.bin $fb ($fb+261120)"
#sync d_fill 120
-exec-continue --thread 1
#uart n
#sleep 1.5
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== reheat\n"
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
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/A_reheat.bin $fb ($fb+261120)"
#sync d_reheat 120
-exec-continue --thread 1
#uart n
#sleep 1.5
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== usage\n"
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
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/A_usage.bin $fb ($fb+261120)"
#sync d_usage 120
-exec-continue --thread 1
#uart n
#sleep 1.2
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== standby\n"
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
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/A_standby.bin $fb ($fb+261120)"
#sync d_standby 120
-exec-continue --thread 1
#uart n
#sleep 1.2
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== home_after\n"
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
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/A_home_after.bin $fb ($fb+261120)"
#sync d_home_after 120
-exec-continue --thread 1
#uart +
#sleep 0.8
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== home_set43\n"
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
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/A_home_set43.bin $fb ($fb+261120)"
#sync d_home_set43 120
-exec-continue --thread 1
-gdb-exit
