# [Phase 7a の G-N1(ファームの動作で P67 が変わる)。UART の b で強制 消灯 → 点灯 → 自動。N-product のシンボル] 前提: サーバー起動済み。gdb-run に -NoElfArg -Mi -NonStop(-SerialSec なし。#uart が COM7 を使う)。
# 接続でリセット → 走らせる → 15 秒後に #uart b → 止めて s_night と PORT6 を読む(消灯)→ 走らせて #uart b(点灯の強制)→ 止めて読む → 走らせて #uart b(自動)→ 止めて読む。PORT6 は読むだけ。
# 終了状態: 停止(接続を切る)。rfp-cli -sig -run で走らせ直す。
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
#sleep 15
#uart b
#sleep 3
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== MARK_OFF_FORCED\n"
-interpreter-exec console "print s_night"
-interpreter-exec console "print/x {unsigned char}0x8c026"
-interpreter-exec console "print/x {unsigned char}0x8c046"
#sleep 8
-exec-continue --thread 1
#sleep 1
#uart b
#sleep 3
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== MARK_ON_FORCED\n"
-interpreter-exec console "print s_night"
-interpreter-exec console "print/x {unsigned char}0x8c026"
-interpreter-exec console "print/x {unsigned char}0x8c046"
#sleep 3
-exec-continue --thread 1
#sleep 1
#uart b
#sleep 3
-exec-interrupt --all
#waitnew \*stopped 8
-interpreter-exec console "echo ==== MARK_AUTO\n"
-interpreter-exec console "print s_night"
-interpreter-exec console "print/x {unsigned char}0x8c026"
-interpreter-exec console "print/x {unsigned char}0x8c046"
-interpreter-exec console "echo ==== MARK_END\n"
-gdb-exit
