# 書込みなし。リセットして走らせ、2秒後に状態を読む。nonstop:true で使う。
set pagination off
set confirm off
set print pretty on
monitor force_rtos_off
echo [1] 事前: tick と MSTPCRC\n
print/x g_app_dbg.tick
x/xw 0x00080018
echo [2] monitor reset\n
monitor reset
monitor getPC
echo [2b] continue -a (non-stop。これが無いとリセット後に停止したまま。未試験)\n
continue -a
python import time; time.sleep(2.0)
echo [3] 2秒後 tick(1回目)\n
print/x g_app_dbg.tick
monitor is_target_stopped,0
monitor getPC
python import time; time.sleep(1.0)
echo [4] 1秒後 tick(2回目)\n
print/x g_app_dbg.tick
print g_app_dbg
echo [5] MSTPCRC 実行後\n
x/xw 0x00080018
echo [6] detach\n
detach
