# 実験: interrupt -a で停止通知を出させ、前景の continue -a で gdb のイベントループを回して「停止」と認識させる。書込みなし。nonstop:true
set pagination off
set confirm off
monitor force_rtos_off
echo [1] 事前 tick\n
print g_app_dbg.tick
echo [2] monitor reset\n
monitor reset
echo [3] interrupt -a\n
interrupt -a
echo [4] 前景 continue -a (停止通知を待つ。返らなければタイムアウト)\n
continue -a
echo [5] 戻った。スレッド状態\n
info threads
info registers pc
detach
