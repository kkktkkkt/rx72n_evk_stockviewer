# 前提: server-start 済み、nonstop:true。書込みあり。
# 狙い: 「ダウンロード後に RUN コマンドを実行しないと Free RUN しない」(ユーザー情報)。vCont;c 以外の RUN 系パケット/コマンドで GO が出るか、サーバーログで見る
set pagination off
set confirm off
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
monitor console_log_enable
load
python
import gdb, time
for pk in ["vRun;", "vRun;;", "R00", "vAttach;1", "qRcmd," + "run".encode().hex(), "qRcmd," + "go".encode().hex(), "qRcmd," + "RUN".encode().hex()]:
    print("[try] " + pk)
    try:
        gdb.execute("maint packet " + pk, to_string=False)
    except Exception as e:
        print("  ERR", e)
    time.sleep(1.5)
end
echo [jump]\n
python
import gdb, time
try:
    gdb.execute("jump *0xffc00000")
except Exception as e:
    print("  ERR jump", e)
time.sleep(1.5)
end
monitor is_target_stopped,0
monitor getPC
print g_app_dbg.tick
