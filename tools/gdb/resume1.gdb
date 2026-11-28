# 実験: monitor reset の後、server の monitor resume で走り出すか。書込みなし。nonstop:true
set pagination off
set confirm off
monitor force_rtos_off
monitor is_target_stopped,0
echo [1] monitor reset\n
monitor reset
monitor is_target_stopped,0
echo [2] monitor resume\n
monitor resume
python import time; time.sleep(2.0)
monitor is_target_stopped,0
echo [3] tick 1回目\n
print/x g_app_dbg.tick
python import time; time.sleep(1.0)
echo [4] tick 2回目\n
print/x g_app_dbg.tick
print g_app_dbg.err_code
print g_app_dbg.found_count
detach
