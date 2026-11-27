# 前提: server-start 済み、nonstop:true。書込みあり。
# 狙い: 生パケット vCont;c でサーバーが GO を E2 Lite に送るか、サーバーログ(FFWコマンド名)で確認する
set pagination off
set confirm off
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
monitor console_log_enable
echo [1] load\n
load
echo [2] raw vCont;c\n
maint packet vCont;c:p1.1
python import time; time.sleep(3.0)
echo [3] after\n
monitor is_target_stopped,0
monitor getPC
print g_app_dbg.tick
maint packet vStopped
maint packet ?
