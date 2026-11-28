# 前提: server-start 済み、nonstop:true。書込みあり。
# 狙い: monitor console_log_enable でサーバー側の動作ログを取り、再開時の挙動を見る(jobs/server.out.log)
set pagination off
set confirm off
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
monitor console_log_enable
echo [1] load\n
load
echo [2] 再開(async)\n
continue -a &
python import time; time.sleep(4.0)
monitor is_target_stopped,0
monitor getPC
echo [3] end\n
