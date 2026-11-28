# 実験(stdin): thread を stopped にした後、未試行の monitor 系を試し、各々の後に continue -a & して GO の有無をサーバーログで見る。SI=true NS=true。書込みあり
set pagination off
set confirm off
monitor console_log_enable
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
load
maint packet vCont;s:p1.1
python import time; time.sleep(1.5)
info threads
echo [1] get_current_cores / get_freerunning_cores / get_disabled_cores\n
monitor get_current_cores
monitor get_freerunning_cores
monitor get_disabled_cores
monitor get_core_go_synced_in_target_DLL
echo [2] notify_resumed → continue -a &\n
monitor notify_resumed
continue -a &
python import time; time.sleep(2.0)
monitor is_target_stopped,0
monitor getPC
echo [3] monitor resume → \n
monitor resume
python import time; time.sleep(2.0)
monitor is_target_stopped,0
monitor getPC
echo [4] reset → step → continue -a &\n
monitor reset
python import time; time.sleep(1.0)
print g_app_dbg.tick
quit
