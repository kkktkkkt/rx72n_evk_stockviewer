# 実験(stdin モード): raw の1命令ステップで本物の停止通知(%Stop)を発生させ、gdb のイベントループで受け取り、thread を stopped にしてから continue -a。
# SI=true NS=true で使う(最後は quit)。書込みあり。判定: info threads が (stopped) になるか、server.out.log に <FFWMCUCmd_GO> が出るか。
set pagination off
set confirm off
monitor console_log_enable
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
echo [1] load\n
load
echo [2] raw step\n
maint packet vCont;s:p1.1
python import time; time.sleep(1.5)
echo [3] info threads(停止通知が処理されたか)\n
info threads
python import time; time.sleep(0.5)
info threads
info registers pc
echo [4] continue -a &\n
continue -a &
python import time; time.sleep(3.0)
info threads
monitor is_target_stopped,0
print g_app_dbg.tick
python import time; time.sleep(2.0)
print g_app_dbg.tick
echo [5] 終了\n
quit
