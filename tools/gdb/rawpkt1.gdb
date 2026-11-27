# 実験: 生のRSPパケットで step / continue が効くか(PCの変化で判定)。書込みなし。nonstop:true
set pagination off
set confirm off
monitor reset
echo [1] reset 後 PC\n
monitor getPC
echo [2] vCont;s:p1.1 (1命令)\n
maint packet vCont;s:p1.1
python import time; time.sleep(1.0)
monitor getPC
monitor is_target_stopped,0
echo [3] vCont;s:p1.1 もう1回\n
maint packet vCont;s:p1.1
python import time; time.sleep(1.0)
monitor getPC
echo [4] vCont;c\n
maint packet vCont;c
python import time; time.sleep(2.0)
monitor getPC
monitor is_target_stopped,0
print g_app_dbg.tick
echo [5] c\n
maint packet c
python import time; time.sleep(2.0)
monitor getPC
monitor is_target_stopped,0
print g_app_dbg.tick
detach
