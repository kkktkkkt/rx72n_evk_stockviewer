# ユーザーがネットで見つけた標準手順の再試験: target remote → load → monitor reset → continue(all-stop)。nonstop:false で使う。
# 前提: server-start 済み(e2 studio と同じ追加引数つき)。書込みあり。終了状態: 不明(実験)
# 判定: サーバーログ jobs/server.out.log に <FFWMCUCmd_GO> が出るか(厳密一致)。tick が増えるか。
set pagination off
set confirm off
monitor console_log_enable
echo [1] 接続直後\n
info threads
info registers pc
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
echo [2] load\n
load
echo [3] monitor reset\n
monitor reset
info registers pc
echo [4] continue &\n
python gdb.execute("continue &")
python import time; time.sleep(3.0)
echo [5] 3秒後\n
info threads
print g_app_dbg.tick
python import time; time.sleep(2.0)
print g_app_dbg.tick
