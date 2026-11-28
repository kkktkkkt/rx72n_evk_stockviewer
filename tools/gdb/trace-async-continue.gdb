# 前提: server-start 済み、nonstop:true。書込みあり。
# 狙い: continue -a & の前後の RSP パケットを set debug remote 1 で記録(stderr に出る)
set pagination off
set confirm off
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
monitor console_log_enable
load
echo [1] debug remote ON\n
set debug remote 1
continue -a &
python import time; time.sleep(3.0)
set debug remote 0
echo [2] end\n
