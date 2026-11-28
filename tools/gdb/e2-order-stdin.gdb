# e² studio の順序(io幅 → init → load → hbreak main → resume)。stdin モード(SI=true NS=true)。raw step で thread を stopped にしてから continue。書込みあり
set pagination off
set confirm off
source D:/workspace/rx72n/rx72n_stockview/tools/gdb/e2-io-access-width.gdb
monitor console_log_enable
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
monitor is_target_connected
monitor is_target_connected
echo [1] load\n
load
echo [2] raw step で停止通知を発生\n
maint packet vCont;s:p1.1
python import time; time.sleep(1.5)
info threads
echo [3] hbreak main → continue &\n
hbreak main
continue &
python import time; time.sleep(3.0)
info threads
monitor is_target_stopped,0
monitor getPC
print g_app_dbg.tick
python import time; time.sleep(1.0)
print g_app_dbg.tick
quit
