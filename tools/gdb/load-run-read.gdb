# 前提: server-start 済み。nonstop:true で接続。ELF = HardwareDebug\rx72n_stockview.elf(job-runnerが固定で渡す)
# 手順: load(書込み。フラッシュを書き換える) → 先頭の読戻し → monitor reset → 走らせる(maint packet vCont) → tick 2回 → 変数 → detach
# 終了状態: 走行中のまま detach(detach後も走り続けるかは未確認)
# 注意: non-stop では gdb の表示が (running) のままで、continue が効かない。そのため生のRSP(vCont;c)を maint packet で送る。
set pagination off
set confirm off
set print pretty on
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
echo [1] load\n
load
echo [2] 先頭16ワードの読戻し(ELFの .text 先頭と比べる)\n
x/16xw 0xffc00000
echo [3] monitor reset\n
monitor reset
monitor getPC
echo [4] 実行再開(raw vCont;c)\n
maint packet vCont;c:p1.1
python import time; time.sleep(2.0)
echo [5] tick 1回目\n
print g_app_dbg.tick
monitor is_target_stopped,0
python import time; time.sleep(1.0)
echo [6] tick 2回目(増えていれば走行中)\n
print g_app_dbg.tick
echo [7] 変数\n
print g_app_dbg
echo [8] detach\n
detach
