# 前提: ターゲットが【走行中】であること(ユーザーが基板のリセットボタンを押した直後など)。書込みなし。
# 使い方: SI=true NS=true bash tools/fresh-gdb.sh(stdin:true と nonstop:true)。標準入力から読むので、最後は quit。
# 狙い: 走行中のターゲットに接続すると、gdb が vCont;t で止める → 停止通知(%Stop)を、行間のイベントループで受け取り、
#       スレッドが「stopped」になるはず(未確認)。その後 reset → continue -a & で走らせ直し、tick が増えるか見る。
# 終了状態: 走行中のまま detach(detach で走り続けるかは未確認)。
set pagination off
set confirm off
set print pretty on
monitor force_rtos_off
echo [1] 接続直後のスレッド状態(stopped なら成功)\n
info threads
echo [2] 接続時点の tick(走行を止めた時点の値。0より大きければ走っていた)\n
print g_app_dbg.tick
info registers pc
echo [3] 変数(止まった状態で読む)\n
print g_app_dbg
echo [4] reset → continue -a &\n
monitor reset
continue -a &
python import time; time.sleep(2.0)
echo [5] 2秒後\n
info threads
print g_app_dbg.tick
monitor is_target_stopped,0
python import time; time.sleep(1.0)
echo [6] 3秒後(2秒後より増えていれば走行中)\n
print g_app_dbg.tick
echo [7] detach\n
detach
quit
