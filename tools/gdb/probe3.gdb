# non-stop で接続 → 走行中のままメモリ(g_app_dbg)が読めるか。書込みなし
set pagination off
set confirm off
echo [A] connected (non-stop)\n
info threads
echo [B] monitor is_target_stopped,0\n
monitor is_target_stopped,0
echo [C] print g_app_dbg while running\n
print/x g_app_dbg
echo [D] print &g_app_dbg\n
print &g_app_dbg
echo [E] detach\n
detach
