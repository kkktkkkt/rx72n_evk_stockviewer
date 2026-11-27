# 実験: all-stop(nonstop:false)で、書込み後の状態でも接続時にレジスタが読めないか。書込みなし。
set pagination off
set confirm off
echo [1] connected (all-stop)\n
info registers pc
info threads
echo [2] stepi x1 (PCが動くか)\n
stepi
info registers pc
echo [3] continue と tick\n
detach
