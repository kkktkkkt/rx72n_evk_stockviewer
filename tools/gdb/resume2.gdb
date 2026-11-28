# 実験: reset → continue -a & (非同期) → PC・ブレーク・tick を見る。書込みなし。nonstop:true
# 前提: 書込み済み。終了状態: 不明(実験)。
set pagination off
set confirm off
monitor force_rtos_off
echo [1] 事前 tick / ブレーク\n
print g_app_dbg.tick
info breakpoints
echo [2] reset\n
monitor reset
monitor getPC
echo [3] continue -a & (非同期)\n
continue -a &
python import time; time.sleep(2.0)
echo [4] 2秒後\n
monitor getPC
monitor is_target_stopped,0
print g_app_dbg.tick
info breakpoints
python import time; time.sleep(1.0)
echo [5] 3秒後 tick\n
print g_app_dbg.tick
monitor getPC
monitor is_target_stopped,0
info threads
detach
