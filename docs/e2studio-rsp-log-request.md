# e² studio の「走り出し」の gdb パケットを記録する依頼(第2回、ユーザー向け)

結果(2026-11-20): 前回のログで、e² studio は `set_io_access_width`(71行)→ load → main に一時ブレーク → 再開、の順だった。CLI で同じ順にしても、サーバーが「走らせる命令(GO)」を出さない。e² studio が再開の時に gdb からサーバーへ送る**パケット**を見たい。

## 準備
1. e² studio の「デバッグの構成」→ `rx72n_gcc HardwareDebug` → **Startup(スタートアップ)** タブの **Initialization Commands(初期化コマンド)** 欄に、次の2行を入れる(欄が空なら、そのまま)。
   ```
   set debug remote 1
   monitor console_log_enable
   ```
   (同じ内容の設定ファイル: `docs/rx72n_gcc HardwareDebug (RSPlog).launch`。元の `.launch` は触っていない)

## 実行
2. デバッグを起動する(書込み → main で停止)。
3. コンソールの文字は消さない。再開(Resume、F8)を **1回** 押す。LCD が出るのを確認する。
4. 一時停止(Suspend)を押す。デバッグを終了する。

## 保存してほしいもの
- **gdb のコンソール全文**を `jobs\e2studio-gdb-console2.txt` に(今回は `[remote] Sending packet: ...` の行がたくさん出る。長くなるが、全部)。
- サーバーのコンソール全文を `jobs\e2studio-server-console2.txt` に。

## 私が見る所
- Resume の直後の `Sending packet:` の並び(`vCont;...` の書式、前後の `qRcmd`/`Z1`/`vStopped` など)。
- load の前後と、`main` で止まる直前のパケット。CLI で何が足りないかを比べる。
