# 前提: server-start 済み、nonstop:true。書込みあり。
# 狙い: 1命令ステップ後に continue すると GO(FFWMCUCmd_GO 系)がサーバーから E2 Lite へ送られるか、サーバーログで確認する
set pagination off
set confirm off
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
monitor console_log_enable
echo [1] load\n
load
echo [2] 1 step (raw)\n
maint packet vCont;s:p1.1
python import time; time.sleep(1.0)
monitor getPC
echo [3] continue -a &\n
continue -a &
python import time; time.sleep(3.0)
monitor is_target_stopped,0
monitor getPC
print g_app_dbg.tick
echo [4] end\n
