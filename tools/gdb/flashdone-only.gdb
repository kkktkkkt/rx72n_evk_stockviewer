# 試験B(書込みなし): load せず raw vFlashDone だけ → raw vCont;c。NS=true
set pagination off
set confirm off
monitor console_log_enable
monitor is_target_connected
monitor is_target_connected
echo [B1] vFlashDone\n
maint packet vFlashDone
python import time; time.sleep(1.5)
echo [B2] vCont;c\n
maint packet vCont;c:p1.1
python import time; time.sleep(3.0)
monitor is_target_stopped,0
print g_app_dbg.tick
python import time; time.sleep(1.0)
print g_app_dbg.tick
quit
