# 実験(stdin モード): raw step で thread を stopped にし、tbreak main を置いて continue。パケットを全部出す。
# SI=true NS=true。書込みあり。判定: gdb が送る vCont と応答、server.out.log の <FFWMCUCmd_GO>、main で止まるか
set pagination off
set confirm off
monitor console_log_enable
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
load
maint packet vCont;s:p1.1
python import time; time.sleep(1.5)
info threads
echo [A] tbreak main + continue -a (パケット表示)\n
set breakpoint always-inserted on
hbreak main
set debug remote 1
continue -a &
python import time; time.sleep(3.0)
set debug remote 0
info threads
monitor is_target_stopped,0
monitor getPC
print g_app_dbg.tick
echo [B] 終了\n
quit
