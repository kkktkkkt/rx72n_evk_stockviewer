# 前提: server-start 済み、nonstop:true。書込みあり。
# 狙い: 再開パケットの書き方を変えて送り、どれが FFWMCUCmd_GO をサーバーに出させるかを見る(エラーは無視して次へ)
set pagination off
set confirm off
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
monitor console_log_enable
load
python
import gdb, time
for pk in ["vCont;c", "vCont;c:-1", "vCont;c:p1.-1", "vCont;c:p-1.-1", "vCont;c:p1.1", "vCont;C00:p1.1", "vCont;c:p1.1;c"]:
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
