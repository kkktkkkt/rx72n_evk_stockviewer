# 実験: target extended-remote → load → monitor reset → run(vRun)。all-stop(nonstop:false) と noconnect:true で使う。書込みあり。
# 判定: jobs/server.out.log に <FFWMCUCmd_GO> が出るか。tick が増えるか。
set pagination off
set confirm off
set debug remote 1
eval "target extended-remote localhost:%d", $gdbport
set debug remote 0
monitor console_log_enable
echo [1] 接続直後\n
info threads
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
echo [2] load\n
load
echo [3] monitor reset\n
monitor reset
echo [4] run &\n
python gdb.execute("run &")
python import time; time.sleep(3.0)
echo [5] 3秒後\n
info threads
print g_app_dbg.tick
python import time; time.sleep(2.0)
print g_app_dbg.tick
