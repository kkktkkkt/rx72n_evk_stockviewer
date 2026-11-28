# 書込みだけ(フラッシュを書き換える)。nonstop:true。ELF = HardwareDebug\rx72n_stockview.elf
# 終了状態: CPU は先頭 0xffc00000 で【停止】(実行再開は CLI から未成功)。走らせるには、ユーザーが基板のリセットボタンを押す(または USB 電源を入れ直す)。
# 毎回 load する(ユーザー指示)。load は自動でリセットする(load 直後の PC = 0xffc00000 を確認済み)ので、monitor reset は不要。
set pagination off
set confirm off
monitor set_internal_mem_overwrite 0-645
monitor force_rtos_off
echo [1] load\n
load
echo [2] 先頭の読戻し(ELFの .text 先頭と一致するか: 0x800273fd 0xfd000027 0x17800a73 0x76040000)\n
x/4xw 0xffc00000
echo [3] PC(0xffc00000 なら load が自動でリセット済み)\n
monitor getPC
detach
