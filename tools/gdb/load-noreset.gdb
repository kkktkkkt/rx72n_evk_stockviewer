# 実験: load の直後(reset しない)に PC を読み、そのまま raw vCont;c で走るか。書込みあり。nonstop:true
set pagination off
set confirm off
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
echo [1] load\n
load
echo [2] load 直後の PC(0xffc00000 なら自動リセット済みの可能性)\n
monitor getPC
monitor is_target_stopped,0
echo [3] 先頭の読戻し\n
x/4xw 0xffc00000
echo [4] vCont;c(resetなし)\n
maint packet vCont;c:p1.1
python import time; time.sleep(2.0)
monitor getPC
monitor is_target_stopped,0
print g_app_dbg.tick
detach
