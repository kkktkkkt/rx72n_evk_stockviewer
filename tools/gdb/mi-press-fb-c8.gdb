# [P2b CLUT8 版。mi-press-fb.gdb から機械的に作った(261120 → 139264、dump 行の後に .pal0.bin / .pal1.bin と GR2CLUTINT の print)。dump は jobs/fb/dev/。比較: fb2png.py check <gdb.log> jobs/fb/dev/<A|B|C|D> --c8 --host jobs/fb/p2b/fb_hostrender_c8.exe]
# 押下表示(暗転)と離しの白光のフレームバッファ確認: 接続(リセット) -> UART の + (仮想タップ 150ms) の直後に interrupt して dump(押下中)、別の + で少し遅れて interrupt(白光中を狙う)
# 前提: サーバー起動済み。COM7 が使える。load なし。dump は jobs\fb\C_<段>.bin。終了状態: 停止
-gdb-set non-stop on
-gdb-set mi-async on
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#waitout \*stopped 8
-interpreter-exec console "monitor enable_execute_on_connect"
-exec-continue --thread 1
#sleep 3
#uart +
#sleep 0.01
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== press_up\n"
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
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/C_press_up.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/C_press_up.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/C_press_up.pal1.bin 0xe0c00 0xe1000"
#sync d_press_up 120
-exec-continue --thread 1
#sleep 1
#uart -
#sleep 0.03
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== press_dn\n"
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
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/C_press_dn.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/C_press_dn.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/C_press_dn.pal1.bin 0xe0c00 0xe1000"
#sync d_press_dn 120
-exec-continue --thread 1
#sleep 1
#uart +
#sleep 0.17
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== flash_up\n"
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
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/C_flash_up.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/C_flash_up.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/C_flash_up.pal1.bin 0xe0c00 0xe1000"
#sync d_flash_up 120
-exec-continue --thread 1
#sleep 1
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== idle_after\n"
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
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/C_idle_after.bin $fb ($fb+139264)"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/C_idle_after.pal0.bin 0xe0800 0xe0c00"
-interpreter-exec console "dump binary memory D:/workspace/rx72n/rx72n_stockview/jobs/fb/dev/C_idle_after.pal1.bin 0xe0c00 0xe1000"
#sync d_idle_after 120
-exec-continue --thread 1
-gdb-exit
