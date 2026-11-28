# 実験: (a) vStopped で通知を消化してから vCont;c (b) QNonStop:0 にして c。書込みなし。nonstop:true
set pagination off
set confirm off
monitor reset
monitor getPC
echo [a] vStopped → vCont;c\n
maint packet vStopped
maint packet vStopped
maint packet vCont;c:p1.1
python import time; time.sleep(1.5)
monitor getPC
monitor is_target_stopped,0
echo [b] ? の応答\n
maint packet ?
echo [c] QNonStop:0 → c\n
maint packet QNonStop:0
maint packet c
python import time; time.sleep(1.5)
monitor getPC
monitor is_target_stopped,0
print g_app_dbg.tick
detach
