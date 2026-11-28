# 実験: enable_stopped_notify_on_connect の後に再接続すると、gdb がスレッドを「停止」と認識するか。書込みなし。
# nonstop:true と noconnect:true で使う(接続はこのスクリプトでやる)。
set pagination off
set confirm off
echo [1] 1回目の接続\n
eval "target remote localhost:%d", $gdbport
info threads
monitor enable_stopped_notify_on_connect
echo [2] disconnect\n
disconnect
echo [3] 再接続\n
eval "target remote localhost:%d", $gdbport
info threads
echo [4] レジスタ\n
info registers pc
detach
