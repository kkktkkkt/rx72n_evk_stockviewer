# 前提: server-start 済み、nonstop:true。書込みあり。
# 狙い: サーバー独自の monitor resume(Eclipse が使う制御コマンド)で再開できるか。
set pagination off
set confirm off
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
echo [1] load\n
load
echo [2] printBkPts\n
monitor printBkPts
monitor getPC
echo [3] monitor resume\n
monitor resume
python import time; time.sleep(2.0)
echo [4] after resume\n
monitor is_target_stopped,0
monitor getPC
print g_app_dbg.tick
python import time; time.sleep(2.0)
print g_app_dbg.tick
echo [5] monitor start_resume\n
monitor start_resume
python import time; time.sleep(2.0)
monitor is_target_stopped,0
monitor getPC
print g_app_dbg.tick
