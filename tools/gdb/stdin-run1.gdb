set pagination off
set confirm off
set print pretty on
monitor force_rtos_off
echo [1] 事前 tick\n
print g_app_dbg.tick
info threads
echo [2] reset\n
monitor reset
echo [3] interrupt -a\n
interrupt -a
info threads
echo [4] continue -a &\n
continue -a &
python import time; time.sleep(2.0)
echo [5] 2秒後\n
info threads
monitor getPC
monitor is_target_stopped,0
print g_app_dbg.tick
python import time; time.sleep(1.0)
echo [6] 3秒後\n
print g_app_dbg.tick
detach
quit
