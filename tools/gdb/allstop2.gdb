# 実験: all-stop で、multiprocess 拡張を切って接続(g パケット前のスレッド選択が E01 になるのを避ける)。書込みなし。
# noconnect:true、nonstop:false で使う。
set pagination off
set confirm off
set remote multiprocess-feature-packet off
set debug remote 1
eval "target remote localhost:%d", $gdbport
set debug remote 0
info threads
info registers pc
detach
