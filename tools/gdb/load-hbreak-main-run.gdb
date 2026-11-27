# 前提: server-start 済み。nonstop:true。書込みあり。ELF = HardwareDebug\rx72n_stockview.elf
# 狙い: e² studio の起動手順(.launch: non-stop / main に hbreak / 到達後に再開)を CLI で再現する。
# 手順: load → hbreak main → continue -a → main で止まるのを待つ → hbreak 削除 → continue -a → tick を2回読む → 保持
# 判定: tick が増える(かつ LCD にカラーバー)なら走行中。detach はしない(切断で止まる仮説の切り分けのため、最後まで保持)。
set pagination off
set confirm off
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
echo [1] load\n
load
echo [2] load 直後の PC\n
monitor getPC
echo [3] hbreak main + 再開\n
hbreak main
continue -a
python import time; time.sleep(3.0)
echo [4] 状態(main で止まっているか)\n
info threads
monitor is_target_stopped,0
monitor getPC
echo [5] hbreak を消して再度 RUN\n
delete breakpoints
continue -a
python import time; time.sleep(3.0)
echo [6] tick 1回目\n
print g_app_dbg.tick
python import time; time.sleep(2.0)
echo [7] tick 2回目(増えていれば走行中)\n
print g_app_dbg.tick
monitor is_target_stopped,0
monitor getPC
echo [8] 30秒保持(LCDを見る)\n
python import time; time.sleep(30.0)
print g_app_dbg.tick
echo [9] 終了(detach しない。タイムアウトでジョブ側が切断する)\n
