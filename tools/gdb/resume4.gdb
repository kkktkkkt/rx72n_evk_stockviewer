# 実験: server の monitor 系の再開コマンドで走り出すか。書込みなし。nonstop:true
set pagination off
set confirm off
monitor reset
echo [1] reset 直後\n
monitor getPC
echo [2] monitor resume,0\n
monitor resume,0
monitor is_target_stopped,0
echo [3] monitor start_resume\n
monitor start_resume
monitor is_target_stopped,0
echo [4] tick\n
print g_app_dbg.tick
monitor getPC
echo [5] monitor notify_resumed\n
monitor notify_resumed
monitor is_target_stopped,0
print g_app_dbg.tick
detach
