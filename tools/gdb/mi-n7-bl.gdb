# [Phase 7a の G-N1。夜間消灯ファーム N-product の P67(バックライト)を読む] 前提: サーバー起動済み。N-product.elf のシンボルを使う(gdb-run に -NoElfArg -Mi -NonStop)。
# 接続でリセット → 走らせる(約 25 秒。起動 → 同期 → 窓の判定)→ 停止 → s_night と PORT6 を読む → P67 を手で Low / High にして(カメラ用の待ち)読み直す。
# 終了状態: 停止(接続を切るとき)。rfp-cli -sig -run で走らせ直す。PORT6 を手で書くのは試験(PODR のみ。ファームのコードは変えない)。
-gdb-set confirm off
-interpreter-exec console "file D:/workspace/rx72n/rx72n_stockview/jobs/artifacts/N-product.elf"
-gdb-set non-stop on
-gdb-set mi-async on
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#waitout \*stopped 8
-interpreter-exec console "monitor enable_execute_on_connect"
-exec-continue --thread 1
#sleep 25
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== A_STOPPED_FIRMWARE_STATE\n"
-interpreter-exec console "print g_app_dbg.tick"
-interpreter-exec console "print s_night"
-interpreter-exec console "print s_bl_ok"
-interpreter-exec console "print/x {unsigned char}0x8c006"
-interpreter-exec console "print/x {unsigned char}0x8c026"
-interpreter-exec console "print/x {unsigned char}0x8c046"
-interpreter-exec console "echo ==== MARK_A_HOLD\n"
#sleep 10
-interpreter-exec console "set {unsigned char}0x8c026 = {unsigned char}0x8c026 & 0x7f"
-interpreter-exec console "echo ==== MARK_B_LOW\n"
-interpreter-exec console "print/x {unsigned char}0x8c026"
-interpreter-exec console "print/x {unsigned char}0x8c046"
#sleep 10
-interpreter-exec console "set {unsigned char}0x8c026 = {unsigned char}0x8c026 | 0x80"
-interpreter-exec console "echo ==== MARK_C_HIGH\n"
-interpreter-exec console "print/x {unsigned char}0x8c026"
-interpreter-exec console "print/x {unsigned char}0x8c046"
#sleep 10
-interpreter-exec console "echo ==== MARK_D_END\n"
-gdb-exit
