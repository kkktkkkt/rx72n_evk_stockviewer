# 診断: RSPパケットを表示して、接続時に何が起きるか見る(書込みなし)。noconnect:true、nonstop は指定で切替。
set pagination off
set confirm off
set debug remote 1
eval "target remote localhost:%d", $gdbport
info threads
detach
