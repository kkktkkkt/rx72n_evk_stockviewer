# 書込み(load) → 照合 → リセット。server-start 後に gdb ジョブ(nonstop:true)で使う。
# job-runner は先に "target remote localhost:<port>" を実行してからこのファイルを読む。
# 2026-11-19 実機で確認: 全停止(all-stop)だと、走行中のターゲットへの接続時にレジスタ読出しで失敗するため nonstop:true が必須。
set pagination off
set confirm off
echo [1] init commands (.launch の optionInitCommands と同じ)\n
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
echo [2] load\n
load
echo [3] monitor reset (load は済み。再書込みは gdb 上で load を先頭に)\n
monitor reset
echo [5] threads\n
info threads
echo [6] detach\n
detach
