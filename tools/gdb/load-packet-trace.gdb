# 観察用(書込みあり): load 中に gdb が送るパケット種別を見る(サーバーに「ダウンロード開始/終了」を出させる経路かの確認)。NS=true
set pagination off
set confirm off
monitor console_log_enable
monitor is_target_connected
monitor is_target_connected
set debug remote 1
load
set debug remote 0
hbreak main
quit
