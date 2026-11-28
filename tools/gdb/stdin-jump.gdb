# 実験(stdin): thread を stopped にしてから jump / call / until main で再開できるか。SI=true NS=true。書込みあり
set pagination off
set confirm off
monitor console_log_enable
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
load
maint packet vCont;s:p1.1
python import time; time.sleep(1.5)
info threads
echo [1] jump *0xffc00000 &\n
set debug remote 1
python gdb.execute("jump *0xffc00000", from_tty=False, to_string=False) if False else None
set $pc = 0xffc00000
info registers pc
continue -a &
python import time; time.sleep(1.0)
set debug remote 0
monitor is_target_stopped,0
echo [2] 終了\n
quit
