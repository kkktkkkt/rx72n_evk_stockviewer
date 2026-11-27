# 実験: all-stop 形式の raw `c` が実際に走らせているか。応答待ちのタイムアウトを try で無視して、その後の tick を見る。書込みなし。nonstop:true
set pagination off
set confirm off
monitor reset
monitor getPC
echo [1] QNonStop:0 → c(応答は停止時まで返らないので、タイムアウトは無視)\n
maint packet QNonStop:0
python
try:
    gdb.execute("maint packet c")
except Exception as e:
    print("c の応答待ちが終了: %s" % e)
end
python import time; time.sleep(1.0)
echo [2] tick(走っていれば 16285 から変わる)\n
print g_app_dbg.tick
python import time; time.sleep(1.0)
print g_app_dbg.tick
monitor getPC
monitor is_target_stopped,0
detach
