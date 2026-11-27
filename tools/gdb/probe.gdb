# 読み取りのみ(書込みなし)。接続直後のターゲット状態を見る。
set pagination off
set confirm off
info registers pc
info registers psw
monitor force_rtos_off
info registers pc
detach
