# デモ台本の確認(UART の d で開始 -> 約1秒ごとに interrupt して状態を print、要所で表示面を dump)。時間は gdb で止めている間は進まない
# 前提: サーバー起動済み。COM7 が使える。load なし。dump は jobs\fb\B_<段>.bin。終了状態: 停止(gdb-run が切断)
-gdb-set non-stop on
-gdb-set mi-async on
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#waitout \*stopped 8
-interpreter-exec console "monitor enable_execute_on_connect"
-exec-continue --thread 1
#sleep 3
#uart d
#sleep 0.6
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== demo_press\n"
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
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/B_demo_press.bin $fb ($fb+261120)"
#sync d_demo_press 120
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s01\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s02\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s03\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s04\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s05\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== fill_mid\n"
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
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/B_fill_mid.bin $fb ($fb+261120)"
#sync d_fill_mid 120
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s07\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s08\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s09\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== fill_done\n"
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
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/B_fill_done.bin $fb ($fb+261120)"
#sync d_fill_done 120
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s11\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s12\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s13\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== reheat_mid\n"
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
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/B_reheat_mid.bin $fb ($fb+261120)"
#sync d_reheat_mid 120
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s15\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s16\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== reheat_done\n"
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
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/B_reheat_done.bin $fb ($fb+261120)"
#sync d_reheat_done 120
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s18\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s19\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s20\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s21\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== usage_mid\n"
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
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/B_usage_mid.bin $fb ($fb+261120)"
#sync d_usage_mid 120
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s23\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s24\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s25\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s26\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
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
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/B_standby.bin $fb ($fb+261120)"
#sync d_standby 120
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s28\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s29\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s30\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s31\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== demo_end\n"
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
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/B_demo_end.bin $fb ($fb+261120)"
#sync d_demo_end 120
-exec-continue --thread 1
#sleep 0.9
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== s33\n"
-interpreter-exec console "print s_tick"
-interpreter-exec console "print s_ui.v"
-interpreter-exec console "print s_ui.demo_t0"
-interpreter-exec console "print s_ui.demo_i"
-interpreter-exec console "print s_ui.demo"
-interpreter-exec console "print s_ui.tr_on"
-interpreter-exec console "print s_ui.fb_phase"
-interpreter-exec console "print s_ui.fb_btn"
-interpreter-exec console "print g_app_dbg"
-exec-continue --thread 1
-gdb-exit
