# 読み取り中心(書込みなし)。接続後に何をすればレジスタが読めるかを調べる。
set pagination off
set confirm off
echo [A] connected\n
info target
echo [B] monitor reset\n
monitor reset
echo [C] after reset\n
info registers pc
echo [D] second read\n
info registers pc
echo [E] detach\n
detach
