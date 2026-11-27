# 前提: server-start 済み、nonstop:true。書込みなし。サーバーの monitor コマンド一覧と接続情報を調べる
set pagination off
set confirm off
echo [1] monitor help\n
monitor help
echo [2] qSupported\n
maint packet qSupported:multiprocess+;swbreak+;hwbreak+;vContSupported+;non-stop+
echo [3] vCont?\n
maint packet vCont?
echo [4] qXfer features\n
maint packet qfThreadInfo
maint packet qAttached
maint packet ?
