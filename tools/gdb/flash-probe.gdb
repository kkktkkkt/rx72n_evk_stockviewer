# 試験D(書込みなし): vFlash* が対応か空応答かの確認。消去先は RAM(0x00800000)、長さ0 のため実フラッシュは触らない。NS=true
set pagination off
set confirm off
monitor console_log_enable
echo [P1] vFlashErase(RAM, len 0)\n
maint packet vFlashErase:00800000,0
echo [P2] vFlashWrite(RAM, 空データ)\n
maint packet vFlashWrite:00800000:
echo [P3] qSupported\n
maint packet qSupported:multiprocess+;swbreak+;hwbreak+;vContSupported+
echo [P4] vCont?\n
maint packet vCont?
quit
