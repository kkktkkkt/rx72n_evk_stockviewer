# 実行環境の制約と教訓(LLM向け)

最終更新: 2026-11-19

## 1. 結論

- LLMの実行環境は**Linux**で、ユーザーのWindowsPCのexe(e² studio, ビルド, デバッガ)を**直接は動かせない**。
- e² studio の操作は computer use で試みたが、**2026-11-19の試行ではクリック・キー入力が届かなかった**。
- よって「SCでのコード生成」「ビルド」「書込み・デバッグ」はユーザーPCで実行してもらうのが確実。LLMはファイル作成・調査・ホスト側検証を担当する。

## 2. 実行場所の整理

| ツール | 実体 | Windowsのexeを実行できるか |
|---|---|---|
| `Bash`(Claude側) | クラウドのLinuxコンテナ | 不可。ユーザーのフォルダも見えない |
| `device_bash` | ユーザーPC上のLinux VM。接続フォルダが `$HOME/mnt/<フォルダ名>` に見える(本プロジェクトは `$HOME/mnt/rx72n/rx72n_gcc`) | **不可**(Windowsではない) |
| `device_list_dir` / `device_stage_files` / `device_commit_files` | ユーザーPCの接続フォルダとの間でファイルを移す | ファイル転送のみ |
| computer use(`mcp__remote-devices__computer_*`) | ユーザーPCの画面操作 | 画面操作のみ。後述の問題あり |

ファイルの流れ: Claude側で作成 → `/mnt/user-data/outputs/` に置く → `device_commit_files` でユーザーのフォルダへ書く。ユーザーPCのファイルを Claude側で読む場合は `device_bash` で読むか `device_stage_files` で取り込む。

## 3. computer use の試行記録(2026-11-19)

| 手順 | 結果 |
|---|---|
| `computer_resolve_access` に「e2 studio」 | `d:\renesas\e2_stduio\eclipse\eclipse\e2studio.exe` が返った。**起動中のプロセスは `d:\renesas\e2stuido\eclipse\e2studio.exe`(別パス)** で、許可しても窓が隠れたまま |
| 起動中のパスを指定して再度許可 | 窓は見えるようになった(tier = full) |
| 「コンポーネント」タブをクリック | 画面が変化しない。クリックは「Clicked」と返るが反映されず |
| ファイルメニューのクリック、`alt+f` | 「Claude's own window still has keyboard focus」で拒否。クリックしてもフォーカスが移らない |
| `open_application` | 許可時の別パスの exe を起動しようとして失敗 |
| タスクバーのアイコンをクリック | デスクトップシェルとして拒否される |

**教訓**

1. 許可するのは「実際に起動中の exe のパス」。`computer_resolve_access` の結果と実プロセスが違うことがある。
2. クリックして画面が変わらない場合、同じ操作を繰り返さない。2回試して変わらなければ、ユーザーに依頼する。
3. 次回試すなら、先にユーザーに「Claudeアプリを前面から外し、e² studio を前面に出してから」と頼むと改善する可能性がある(未検証)。

## 4. ネットワーク

| 宛先 | 結果 |
|---|---|
| `raw.githubusercontent.com` | 取得可(連続アクセスで 429 になることがあるので間隔を空ける) |
| `api.github.com`(curl / `gh api`) | 400(プロキシがパスを正規化できない) |
| `github.com/.../tree/...` | 403 |
| `WebFetch` で `github.com/hirakuni45/RX/tree/...` | robots で拒否 |
| `WebFetch` で renesas.com の文書ページ | 取得可だが、ページの一部しか返らないことがある |

FITモジュールのソースは `https://raw.githubusercontent.com/renesas/rx-driver-package/master/source/<モジュール名>/<モジュール名>_vx.xx/...` から取れる(`r_glcdc_rx` で確認済み)。バージョンは `source/<モジュール名>/Makefile` の `VERSION` 行。

## 5. ツールチェーン関連の注意

- プロジェクトのGCCは `14.2.0.202607`。`r_glcdc_rx` v1.62 の対応リストは `14.02.00 202511` まで。SCの警告は内容を確認して判断する。
- e² studio の起動中は、同じワークスペース(`D:\workspace\rx72n`)に対するヘッドレスビルドはロックで失敗する可能性が高い。

## PDF の図を読む(pymupdf。2026-11-21 導入)

- この PC の Python 3.14 に `pymupdf` 2.1 を入れた(`python -m pip install pymupdf`。依存で `pymupdf-layout`、`pymupdf4llm`、`onnxruntime` なども入った)。`import pymupdf` で使う(`import fitz` は非推奨の警告が出る)。
- Read ツールの PDF 表示は `pdftoppm` が無く使えない。代わりに、ページを PNG にして Read で見る。
  ```python
  import pymupdf
  doc = pymupdf.open(r"C:\Users\kkktk\Downloads\r01uh0824jj0120-rx72n.pdf")   # RX72N ハードウェア編 Rev.1.20
  doc[2695-1].get_pixmap(dpi=110).save("p2695.png")                             # ページ番号は印刷番号と同じ(3275ページ)
  ```
- ページの探し方: `doc[i].get_text()` に「図51.6」などが含まれるページを検索する(文字化けする所は英数字の部分で探す)。
- 日本語の本文は、PDF のテキスト抽出では文字化けする。同じ内容の Smart Manual の SQLite(`.eclipse\com.renesas.platform_download\smartmanual\RX72N\RX72N_ja`)を読み取り専用で開くと、日本語が取れる。
- 確認済み: GLCDC の章は p.2688〜2778。図51.6(フレームデータのカラーフォーマット)は p.2695。
- PDF や変換した画像は、リポジトリに入れない(スクラッチ領域に置く)。
