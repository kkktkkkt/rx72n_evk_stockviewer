# [X7 用。docs/design.md §22.13。実機の表示面を天気つき製品(APP_WX=1)で読む] 前提: サーバー起動済み、HardwareDebug の ELF ではなく prod_wx の ELF のシンボルを使う(gdb-run に -NoElfArg -Mi -NonStop)。COM7 は gdb-run の #uart が使う(-SerialSec 無し)。
# 各段: 状態を作る(set var で s_sched.net / s_wx_have / s_wx_ok_ms / s_clk.synced / s_wx_temp / s_wx_kind を書く。load はしない)→ 0.6 秒走らせる → interrupt → s_ui.* を print → 表示面とパレットを dump(jobsb\W_<段>.*)。
# 終了状態: 走行(最後に gdb が切断 = 停止。rfp -sig -run で走らせ直す)。書き換えた値は最後に元へ戻す。
-gdb-set confirm off
-interpreter-exec console "file D:/workspace/rx72n/rx72n_stockview/jobs/wxbuild/prod_wx/rx72n_stockview.elf"
-gdb-set non-stop on
-gdb-set mi-async on
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#waitout \*stopped 8
-interpreter-exec console "monitor enable_execute_on_connect"
-exec-continue --thread 1
#sleep 14
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "set $n0 = s_sched.net"
-interpreter-exec console "set $h0 = s_wx_have"
-interpreter-exec console "set $o0 = s_wx_ok_ms"
-interpreter-exec console "set $s0 = s_clk.synced"
-interpreter-exec console "set $t0 = s_wx_temp"
-interpreter-exec console "set $k0 = s_wx_kind"
-interpreter-exec console "echo ORIG\n"
-interpreter-exec console "print s_sched"
-interpreter-exec console "print s_wx_have"
-interpreter-exec console "print s_wx_ok_ms"
-interpreter-exec console "print s_clk"
-interpreter-exec console "print s_wx_temp"
-interpreter-exec console "print s_wx_kind"
-interpreter-exec console "print s_tick"
-exec-continue --thread 1
#sleep 0.6
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== live1\n"
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
-interpreter-exec console "print s_ui.wx"
-interpreter-exec console "print s_ui.spin"
-interpreter-exec console "print g_app_dbg"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_live1.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_live1.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_live1.pal1.bin 0xe0c00 0xe1000"
#sync d_live1 120
-interpreter-exec console "set var s_sched.net = 0"
-exec-continue --thread 1
#sleep 0.6
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== offline\n"
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
-interpreter-exec console "print s_ui.wx"
-interpreter-exec console "print s_ui.spin"
-interpreter-exec console "print g_app_dbg"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_offline.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_offline.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_offline.pal1.bin 0xe0c00 0xe1000"
#sync d_offline 120
-interpreter-exec console "set var s_sched.net = 1"
-exec-continue --thread 1
#sleep 0.6
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== conn\n"
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
-interpreter-exec console "print s_ui.wx"
-interpreter-exec console "print s_ui.spin"
-interpreter-exec console "print g_app_dbg"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_conn.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_conn.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_conn.pal1.bin 0xe0c00 0xe1000"
#sync d_conn 120
-interpreter-exec console "set var s_sched.net = 3"
-exec-continue --thread 1
#sleep 0.6
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== err\n"
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
-interpreter-exec console "print s_ui.wx"
-interpreter-exec console "print s_ui.spin"
-interpreter-exec console "print g_app_dbg"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_err.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_err.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_err.pal1.bin 0xe0c00 0xe1000"
#sync d_err 120
-interpreter-exec console "set var s_sched.net = 2"
-interpreter-exec console "set var s_wx_temp = -34"
-interpreter-exec console "set var s_wx_kind = 4"
-exec-continue --thread 1
#sleep 0.6
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== online_neg_rain\n"
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
-interpreter-exec console "print s_ui.wx"
-interpreter-exec console "print s_ui.spin"
-interpreter-exec console "print g_app_dbg"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_online_neg_rain.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_online_neg_rain.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_online_neg_rain.pal1.bin 0xe0c00 0xe1000"
#sync d_online_neg_rain 120
-interpreter-exec console "set var s_wx_temp = 105"
-interpreter-exec console "set var s_wx_kind = 6"
-exec-continue --thread 1
#sleep 0.6
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== thunder_105\n"
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
-interpreter-exec console "print s_ui.wx"
-interpreter-exec console "print s_ui.spin"
-interpreter-exec console "print g_app_dbg"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_thunder_105.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_thunder_105.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_thunder_105.pal1.bin 0xe0c00 0xe1000"
#sync d_thunder_105 120
-interpreter-exec console "set var s_wx_temp = -105"
-interpreter-exec console "set var s_wx_kind = 5"
-exec-continue --thread 1
#sleep 0.6
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== snow_m105\n"
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
-interpreter-exec console "print s_ui.wx"
-interpreter-exec console "print s_ui.spin"
-interpreter-exec console "print g_app_dbg"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_snow_m105.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_snow_m105.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_snow_m105.pal1.bin 0xe0c00 0xe1000"
#sync d_snow_m105 120
-interpreter-exec console "set var s_wx_kind = 7"
-exec-continue --thread 1
#sleep 0.6
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== unk_kind\n"
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
-interpreter-exec console "print s_ui.wx"
-interpreter-exec console "print s_ui.spin"
-interpreter-exec console "print g_app_dbg"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_unk_kind.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_unk_kind.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_unk_kind.pal1.bin 0xe0c00 0xe1000"
#sync d_unk_kind 120
-interpreter-exec console "set var s_wx_temp = $t0"
-interpreter-exec console "set var s_wx_kind = $k0"
-interpreter-exec console "set var s_wx_ok_ms = s_wx_ok_ms - 1860000"
-exec-continue --thread 1
#sleep 0.6
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== stale\n"
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
-interpreter-exec console "print s_ui.wx"
-interpreter-exec console "print s_ui.spin"
-interpreter-exec console "print g_app_dbg"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_stale.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_stale.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_stale.pal1.bin 0xe0c00 0xe1000"
#sync d_stale 120
-interpreter-exec console "set var s_wx_ok_ms = $o0"
-exec-continue --thread 1
#sleep 0.6
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== stale_back\n"
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
-interpreter-exec console "print s_ui.wx"
-interpreter-exec console "print s_ui.spin"
-interpreter-exec console "print g_app_dbg"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_stale_back.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_stale_back.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_stale_back.pal1.bin 0xe0c00 0xe1000"
#sync d_stale_back 120
-interpreter-exec console "set var s_wx_have = 0"
-exec-continue --thread 1
#sleep 0.6
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== nohave\n"
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
-interpreter-exec console "print s_ui.wx"
-interpreter-exec console "print s_ui.spin"
-interpreter-exec console "print g_app_dbg"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_nohave.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_nohave.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_nohave.pal1.bin 0xe0c00 0xe1000"
#sync d_nohave 120
-interpreter-exec console "set var s_wx_have = $h0"
-interpreter-exec console "set var s_clk.synced = 0"
-exec-continue --thread 1
#sleep 0.6
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== unsync\n"
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
-interpreter-exec console "print s_ui.wx"
-interpreter-exec console "print s_ui.spin"
-interpreter-exec console "print g_app_dbg"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_unsync.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_unsync.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_unsync.pal1.bin 0xe0c00 0xe1000"
#sync d_unsync 120
-interpreter-exec console "set var s_clk.synced = $s0"
-exec-continue --thread 1
#sleep 0.6
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== resync\n"
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
-interpreter-exec console "print s_ui.wx"
-interpreter-exec console "print s_ui.spin"
-interpreter-exec console "print g_app_dbg"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_resync.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_resync.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_resync.pal1.bin 0xe0c00 0xe1000"
#sync d_resync 120
-exec-continue --thread 1
#uart x
#sleep 0.09
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== press1\n"
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
-interpreter-exec console "print s_ui.wx"
-interpreter-exec console "print s_ui.spin"
-interpreter-exec console "print g_app_dbg"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_press1.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_press1.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_press1.pal1.bin 0xe0c00 0xe1000"
#sync d_press1 120
-exec-continue --thread 1
#uart x
#sleep 0.45
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== spin1\n"
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
-interpreter-exec console "print s_ui.wx"
-interpreter-exec console "print s_ui.spin"
-interpreter-exec console "print g_app_dbg"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_spin1.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_spin1.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_spin1.pal1.bin 0xe0c00 0xe1000"
#sync d_spin1 120
-exec-continue --thread 1
#sleep 0.2
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== spin2\n"
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
-interpreter-exec console "print s_ui.wx"
-interpreter-exec console "print s_ui.spin"
-interpreter-exec console "print g_app_dbg"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_spin2.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_spin2.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_spin2.pal1.bin 0xe0c00 0xe1000"
#sync d_spin2 120
-exec-continue --thread 1
#sleep 1.5
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== home_end\n"
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
-interpreter-exec console "print s_ui.wx"
-interpreter-exec console "print s_ui.spin"
-interpreter-exec console "print g_app_dbg"
-interpreter-exec console "set $fb = ((struct st_glcdc *)0xe0000)->GR2FLM2"
-interpreter-exec console "print/x ((struct st_glcdc *)0xe0000)->GR2CLUTINT.LONG"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_home_end.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_home_end.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/W_home_end.pal1.bin 0xe0c00 0xe1000"
#sync d_home_end 120
-interpreter-exec console "set var s_sched.net = $n0"
-interpreter-exec console "set var s_wx_have = $h0"
-interpreter-exec console "set var s_wx_ok_ms = $o0"
-interpreter-exec console "set var s_clk.synced = $s0"
-interpreter-exec console "set var s_wx_temp = $t0"
-interpreter-exec console "set var s_wx_kind = $k0"
-exec-continue --thread 1
#sleep 1
-gdb-exit
