# 前提: server-start 済み、nonstop:true。書込みあり。
# 狙い: set debug remote 1 で、再開パケットとサーバーの応答(停止通知)を記録する。
set pagination off
set confirm off
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
echo [1] load\n
load
echo [2] hbreak main\n
hbreak main
info breakpoints
monitor printBkPts
echo [3] debug remote ON, continue -a (同期。止まらなければジョブ側が切る)\n
set debug remote 1
continue -a
echo [4] continue から戻った\n
info threads
monitor getPC
