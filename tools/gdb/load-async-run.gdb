# 前提: server-start 済み。nonstop:true。書込みあり。
# 狙い: continue -a を非同期(&)で出し、gdb をブロックさせずに tick と PC を読む。
# 判定: tick が増える=走行中。
set pagination off
set confirm off
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
echo [1] load\n
load
echo [2] PC after load\n
monitor getPC
echo [3] continue -a & (async)\n
continue -a &
python import time; time.sleep(3.0)
echo [4] info threads\n
info threads
echo [5] tick 1\n
print g_app_dbg.tick
python import time; time.sleep(2.0)
echo [6] tick 2\n
print g_app_dbg.tick
monitor is_target_stopped,0
monitor getPC
python import time; time.sleep(20.0)
echo [7] tick 3 (20s later)\n
print g_app_dbg.tick
