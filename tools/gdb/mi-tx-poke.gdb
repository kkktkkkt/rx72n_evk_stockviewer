# 切り分け: 走らせて interrupt の後、CPU 停止中に gdb から SCI2 の TDR(0x8a043)へ 0x55 を書いて送信させ、PC 側 COM7 に届くか見る(書込みは周辺レジスタ1バイトのみ。ELF 書込みなし)
# 前提: サーバー起動済み。終了状態: 停止(rfp reset-run で走行へ)
-gdb-set non-stop on
-gdb-set mi-async on
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor enable_stopped_notify_on_connect"
#waitout \*stopped 8
-interpreter-exec console "monitor enable_execute_on_connect"
-exec-continue --thread 1
#sleep 8
-exec-interrupt --all
#sleep 2
-interpreter-exec console "x/bx 0x8a044"
-interpreter-exec console "set {unsigned char}0x8a043 = 0x55"
#sleep 0.4
-interpreter-exec console "set {unsigned char}0x8a043 = 0x55"
#sleep 0.4
-interpreter-exec console "set {unsigned char}0x8a043 = 0x55"
#sleep 0.4
-interpreter-exec console "set {unsigned char}0x8a043 = 0x55"
#sleep 0.4
-interpreter-exec console "set {unsigned char}0x8a043 = 0x55"
#sleep 0.4
-interpreter-exec console "set {unsigned char}0x8a043 = 0x55"
#sleep 0.4
-interpreter-exec console "set {unsigned char}0x8a043 = 0x55"
#sleep 0.4
-interpreter-exec console "set {unsigned char}0x8a043 = 0x55"
#sleep 0.4
-interpreter-exec console "set {unsigned char}0x8a043 = 0x55"
#sleep 0.4
-interpreter-exec console "set {unsigned char}0x8a043 = 0x55"
#sleep 0.4
-interpreter-exec console "x/bx 0x8a044"
-gdb-exit
