# 書込み済みのターゲットから状態を読む(書込みなし)。必ず nonstop:true で使う。
# 状態: 2026-11-19 作成。実機で未実行(E2 Lite が接続不能になったため)。python の sleep は未検証。
set pagination off
set confirm off
set print pretty on
monitor force_rtos_off
echo [1] MSTPCRC (0x00080018) / MSTPCRB(0x00080014) / MSTPCRA(0x00080010)\n
x/xw 0x00080018
x/xw 0x00080014
x/xw 0x00080010
echo [2] g_app_dbg (1回目)\n
print/x g_app_dbg
python import time; time.sleep(1.0)
echo [3] g_app_dbg (約1秒後。tick が増えていれば main ループは走っている)\n
print g_app_dbg.tick
print g_app_dbg.err_code
print g_app_dbg.err_detail
print g_app_dbg.found_count
print/x g_app_dbg.found_addr
print g_app_dbg.raw_points
print g_app_dbg.raw_x
print g_app_dbg.raw_y
print g_app_dbg.x_min
print g_app_dbg.x_max
print g_app_dbg.y_min
print g_app_dbg.y_max
print g_app_dbg.i2c_fail_total
print g_app_dbg.image_index
echo [4] RAM2 書込み→読戻し(0x00800000。元の値は戻す)\n
set $old = *(unsigned int *)0x00800000
printf "old = 0x%08x\n", $old
set *(unsigned int *)0x00800000 = 0x5a5aa5a5
printf "readback = 0x%08x (期待 0x5a5aa5a5)\n", *(unsigned int *)0x00800000
set *(unsigned int *)0x00800000 = $old
printf "restored = 0x%08x\n", *(unsigned int *)0x00800000
echo [5] detach\n
detach
