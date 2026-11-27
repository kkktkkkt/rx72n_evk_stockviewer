# 前提: server-start 済み、nonstop:true。書込みあり。
# 仮説: サーバーの内部状態が「走行中」のため vCont;c が無視される。先に vCont;t(停止要求)で内部状態を「停止」にしてから vCont;c を送ると GO が出る。
set pagination off
set confirm off
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
monitor console_log_enable
load
python
import gdb, time
for pk in ["vCont;t:p1.1", "vStopped", "vStopped", "vCont;c:p1.1"]:
    print("[try] " + pk)
    try:
        gdb.execute("maint packet " + pk, to_string=False)
    except Exception as e:
        print("  ERR", e)
    time.sleep(1.5)
end
monitor is_target_stopped,0
monitor getPC
print g_app_dbg.tick
