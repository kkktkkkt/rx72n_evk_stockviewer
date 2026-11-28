# 前提: server-start 済み、nonstop:true。
# 仮説: non-stop 接続時にサーバーが「停止中」を通知しないため、gdb がスレッドを(running)と誤認し continue を送らない。
# 狙い: monitor enable_stopped_notify_on_connect を設定して切断→再接続し、info threads が停止表示になるか、continue で GO が出るかを見る
set pagination off
set confirm off
monitor console_log_enable
monitor enable_stopped_notify_on_connect
echo [1] before reconnect\n
info threads
echo [2] reconnect\n
disconnect
python gdb.execute("target remote localhost:%d" % int(gdb.parse_and_eval("$gdbport")))
echo [3] after reconnect\n
info threads
