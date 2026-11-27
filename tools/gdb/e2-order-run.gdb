# e² studio の gdb コンソール(jobs/e2studio-gdb-console.txt)の順序を再現して、GO(<FFWMCUCmd_GO>)が出るか見る。nonstop:true。書込みあり
# 順序: set_io_access_width(71行) → console_log_enable → init 2行 → load → hbreak main(e² studio は tbreak main + ハードウェアBP) → continue
set pagination off
set confirm off
source D:/workspace/rx72n/rx72n_stockview/tools/gdb/e2-io-access-width.gdb
monitor console_log_enable
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
echo [1] load\n
load
echo [2] hbreak main\n
hbreak main
echo [3] raw vCont;c\n
maint packet vCont;c:p1.1
python import time; time.sleep(3.0)
monitor is_target_stopped,0
monitor getPC
print g_app_dbg.tick
python import time; time.sleep(1.0)
print g_app_dbg.tick
