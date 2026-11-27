# e² studio の「走り出し」通信を記録する依頼(ユーザー向け)

目的: CLI(gdb)からは、書込み後に CPU を走らせられない。e² studio が走らせる時にサーバーが E2 Lite へ送る命令を見て、同じ命令を CLI から出す。

## 準備(2分)
1. job-runner はそのまま。**e² studio のデバッグを起動する前に、私(CLI)のサーバーが止まっていることを確認済み**(`server-stop` 済み)。
2. e² studio で「デバッグの構成」を開き、`rx72n_gcc HardwareDebug` の **Startup(スタートアップ)** タブにある「初期化コマンド」(Initialization Commands)の**先頭の行**に、次の1行を足す(他の行は消さない)。
   `monitor console_log_enable`
   (同じ内容の設定ファイルを `docs/rx72n_gcc HardwareDebug (FFWlog).launch` に置いた。元の `.launch` は触っていない)

## 実行(いつもの操作)
3. デバッグを起動する(書込み → main で停止)。
4. **コンソール(Console)に出る文字は、まだ消さない。**
5. 再開ボタン(Resume、F8)を **1回** 押す。LCD が出る(走り出す)のを確認する。
6. 一時停止(Suspend)を押す。デバッグを終了する。

## 貼ってほしいもの
- コンソールの全文を、テキストファイルに保存して `D:\workspace\rx72n\rx72n_gcc\jobs\e2studio-console.txt` に置く(コンソール右上の「クリア」の隣に、保存/コピーがある。全選択してコピーでもよい)。

## 私が見る所(ユーザーは見なくてよい)
- 5.の「Resume」の直前と直後に出る `<FFWMCUCmd_...>` の並び。特に `<FFWMCUCmd_GO>`(走らせる命令)と、その前に出る命令(ブレーク設定 SetBM、レジスタ設定、`STEP` など)。
- CLI では `vCont;c` が `OK` を返すのに `GO` が出ない。e² studio で GO が出る時に、何が違うかを比べる。

## 教えてほしい質問(1つだけ)
- 「ダウンロード後に RUN コマンドを実行しないと Free RUN しない」の **RUN** は、どのボタン/メニューですか?(例: 再開ボタン(F8)、「リセットして実行」、`Renesas Debug` メニューの何か)
