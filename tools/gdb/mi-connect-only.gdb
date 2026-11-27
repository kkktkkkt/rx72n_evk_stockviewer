# 接続確認だけ(MI, non-stop): 接続 -> getPC -> x/4xw 0xffc00000 -> 切断。load・書込み・continue なし
# 前提: サーバー起動済み。終了状態: 停止(接続でリセットされ CPU が止まる)。走行へは rfp-cli -sig -run
-gdb-set non-stop on
-gdb-set mi-async on
-target-select remote localhost:$PORT
#sleep 2
-interpreter-exec console "monitor getPC"
-interpreter-exec console "x/4xw 0xffc00000"
-gdb-exit
