# 前提: server-start 済み。nonstop:true。書込みあり(フラッシュを書き換える)。ELF = HardwareDebug\rx72n_stockview.elf
# 狙い: gdbセッションを切らずに保持して走らせる。保持中(約40秒)にユーザーがLCDを見て、画面が出たかを教える。
# 手順: load → PC確認 → raw vCont;c → 40秒保持(5秒ごとに tick を読むだけ。monitor は使わない) → 最後に PC/状態 → detach
# 終了状態: detach 後に CPU が止まるか、LCD が残るかを観察する(未確認)。
set pagination off
set confirm off
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
echo [1] load\n
load
echo [2] load 直後の PC(0xffc00000 のはず)\n
monitor getPC
echo [3] 実行再開(raw vCont;c)。ここから40秒保持。LCDを見てもらう\n
maint packet vCont;c:p1.1
python import time; time.sleep(5.0)
print g_app_dbg.tick
python import time; time.sleep(5.0)
print g_app_dbg.tick
python import time; time.sleep(5.0)
print g_app_dbg.tick
python import time; time.sleep(5.0)
print g_app_dbg.tick
python import time; time.sleep(5.0)
print g_app_dbg.tick
python import time; time.sleep(5.0)
print g_app_dbg.tick
python import time; time.sleep(5.0)
print g_app_dbg.tick
python import time; time.sleep(5.0)
echo [4] 40秒後\n
print g_app_dbg.tick
monitor getPC
monitor is_target_stopped,0
echo [5] detach\n
detach
