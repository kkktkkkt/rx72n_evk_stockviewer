# サーバー引数バリアント試験用(SI=true NS=true、書込みなし)。接続 → raw step で停止通知 → continue → GO/tick で判定。
# 事前: 直前に rfp reset-run 済み(フラッシュのプログラムが入っている)こと。サーバー起動引数は SX で変える
set pagination off
set confirm off
monitor console_log_enable
monitor printSettings
echo [1] 接続直後\n
info threads
print g_app_dbg.tick
echo [2] raw step\n
maint packet vCont;s:p1.1
python import time; time.sleep(1.5)
info threads
echo [3] continue &\n
continue &
python import time; time.sleep(3.0)
info threads
monitor is_target_stopped,0
print g_app_dbg.tick
python import time; time.sleep(1.5)
print g_app_dbg.tick
quit
