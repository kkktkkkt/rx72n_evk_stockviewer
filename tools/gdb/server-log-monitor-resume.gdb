# 前提: server-start 済み、nonstop:true。書込みあり。
# 狙い: continue を使わず monitor resume だけで GO が E2 Lite に送られるかを、サーバーログ(FFWコマンド名)で確認する
set pagination off
set confirm off
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
monitor console_log_enable
echo [1] load\n
load
echo [2] monitor resume\n
monitor resume
python import time; time.sleep(3.0)
monitor is_target_stopped,0
monitor getPC
print g_app_dbg.tick
echo [3] end\n
