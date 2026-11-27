# 実験: vCont;c の書式違い。各々の後に PC を見る。書込みなし。nonstop:true
set pagination off
set confirm off
monitor reset
monitor getPC
echo [a] vCont;c:p1.-1\n
maint packet vCont;c:p1.-1
python import time; time.sleep(1.0)
monitor getPC
echo [b] vCont;c:-1\n
maint packet vCont;c:-1
python import time; time.sleep(1.0)
monitor getPC
echo [c] vCont;C00:p1.1\n
maint packet vCont;C00:p1.1
python import time; time.sleep(1.0)
monitor getPC
echo [d] vCont;c:p1.1;t:p1.1 (参考)\n
print g_app_dbg.tick
monitor is_target_stopped,0
detach
