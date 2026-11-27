# 前提: server-start 済み、nonstop:true。書込みあり。
# 狙い: 条件を変えて vCont;c を送り、どの組み合わせで GO がサーバーから出るかをサーバーログの順序で見る
# A: hbreak main 設定後に c / B: step 後に c / C: monitor reset 後に c
set pagination off
set confirm off
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
monitor console_log_enable
load
echo [A] hbreak main + raw c\n
hbreak main
maint packet vCont;c:p1.1
python import time; time.sleep(2.0)
echo [B] step + raw c\n
maint packet vCont;s:p1.1
python import time; time.sleep(1.0)
maint packet vCont;c:p1.1
python import time; time.sleep(2.0)
echo [C] monitor reset + raw c\n
monitor reset
python import time; time.sleep(1.0)
maint packet vCont;c:p1.1
python import time; time.sleep(2.0)
monitor is_target_stopped,0
monitor getPC
print g_app_dbg.tick
