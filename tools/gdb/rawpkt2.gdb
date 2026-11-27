# 実験: hbreak main を入れて vCont;c で main まで走るか。書込みなし。nonstop:true
set pagination off
set confirm off
set breakpoint always-inserted on
monitor reset
echo [1] reset 後 PC\n
monitor getPC
hbreak main
info breakpoints
echo [2] vCont;c\n
maint packet vCont;c:p1.1
python import time; time.sleep(2.0)
echo [3] 2秒後\n
monitor getPC
monitor is_target_stopped,0
print g_app_dbg.tick
echo [4] 追加の停止通知があるか(vStopped)\n
maint packet vStopped
delete breakpoints
detach
