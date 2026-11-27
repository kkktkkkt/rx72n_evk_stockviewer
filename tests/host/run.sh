#!/bin/bash
# tests/host/run.sh - ホストPC(gcc)での検証一式。製品コード(src/app, src/rx72n_gcc.c)は変更しない。
#   使い方:  bash tests/host/run.sh            (全部)
#            bash tests/host/run.sh S2 S3      (指定ステップだけ)
#   出力: 各ステップの PASS/FAIL/SKIP と末尾の集計。ビルド生成物は ${BUILD:-/tmp/rx72n-host-build}(プロジェクト外)。
#   注意: ここでの合格は「ホストGCCでの論理・構文の検証」であり、RX用GCCでのビルド成功や実機動作の証明ではない。
#   SKIP = この環境で実行できなかった(例: libasan が無い)。SKIP は合格ではない。
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
APP="$ROOT/src/app"
SMC="$ROOT/src/smc_gen"
BUILD="${BUILD:-/tmp/rx72n-host-build}"
CC="${CC:-gcc}"
PY="${PYTHON:-python3}"
"$PY" -c "import sys" >/dev/null 2>&1 || PY=python
mkdir -p "$BUILD"

STD="-std=gnu99"
WARN="-Wall -Wextra -Wpedantic -Wconversion"

# FIT 実ヘッダの検索パス(HardwareDebug/compile_commands.json の -I と同じ)
FIT_INC="-I$SMC/general -I$SMC/r_pincfg -I$SMC/r_bsp -I$SMC -I$SMC/r_config -I$SMC/r_cmt_rx -I$SMC/r_cmt_rx/src \
 -I$SMC/r_glcdc_rx -I$SMC/r_glcdc_rx/src -I$SMC/r_gpio_rx -I$SMC/r_gpio_rx/src -I$SMC/r_sci_rx -I$SMC/r_sci_rx/src \
 -I$SMC/r_byteq -I$SMC/r_byteq/src -I$SMC/r_sci_iic_rx -I$SMC/r_sci_iic_rx/src -I$SMC/r_qe_display_rx -I$SMC/r_qe_display_rx/src"
# 実 platform.h をホストで通すための定義。__attribute__ を空にするのは、RX の割り込み属性(interrupt)が
# x86 では引数エラーになるため。構文・型の検査が目的で、属性(aligned/packed等)の検査は含まれない。
RXDEF="-D__RX__ -D__RXV3__ -D__RX_LITTLE_ENDIAN__ -D__attribute__(x)="
# MinGW(Windows)では PE/COFF のため --gc-sections が未参照関数を除去せず、ホストで解決できない参照が残ることがある。そのとき再リンクを許す
LDX="-Wl,--allow-multiple-definition"

FAILS=0; PASSES=0; SKIPS=0; SUSPECTS=0; FAILED_LIST=""
ONLY=("$@")
sel() { [ ${#ONLY[@]} -eq 0 ] && return 0; local s; for s in "${ONLY[@]}"; do [ "$s" = "$1" ] && return 0; done; return 1; }
result() { # name rc
  if [ "$2" -eq 0 ]; then echo "==> PASS  $1"; PASSES=$((PASSES+1)); else echo "==> FAIL  $1"; FAILS=$((FAILS+1)); FAILED_LIST="$FAILED_LIST $1"; fi; echo; }
skip() { echo "==> SKIP (未実施) $1: $2"; SKIPS=$((SKIPS+1)); echo; }

# 警告が1行でも出たら失敗にするコンパイル。 $1=出力ファイル(-c なら .o) 残り=gccの引数
cc_nowarn() {
  local out; out="$("$CC" "$@" 2>&1)"; local rc=$?
  if [ -n "$out" ]; then echo "$out"; [ $rc -eq 0 ] && rc=1; fi
  return $rc
}

# 生成データ(ui_strips.c, ui_gfx_data.c, ui_layout.c)を1回だけコンパイルしておく($BUILD/ui/*.o)
UIOBJ="$BUILD/ui"
UIDATA="ui_gfx_data ui_layout ui_strips"
build_ui_objs() {
  mkdir -p "$UIOBJ"
  local f
  for f in $UIDATA; do
    if [ ! -f "$UIOBJ/$f.o" ] || [ "$APP/$f.c" -nt "$UIOBJ/$f.o" ] || [ "$APP/ui_assets.h" -nt "$UIOBJ/$f.o" ]; then
      cc_nowarn $STD $WARN -fno-pic -I"$APP" -c "$APP/$f.c" -o "$UIOBJ/$f.o" || return 1
    fi
  done
  return 0
}

STDC_FILES="touch_decode app_logfmt app_logq gfx ui_logic ui_render stk_core"

# ---------------------------------------------------------------- S1
if sel S1; then
  echo "### S1 標準Cのみの製品コード($STDC_FILES)の静的条件"
  rc=0
  for std in "-std=gnu99" "-std=c99 -pedantic-errors" "-std=c11"; do
    for f in $STDC_FILES; do
      echo "  [$std] $f.c"
      cc_nowarn $std $WARN -I"$APP" -c "$APP/$f.c" -o "$BUILD/$f.o" || rc=1
    done
  done
  echo "  platform.h / FIT ヘッダを include していないこと"
  SRCS=""; for f in $STDC_FILES; do SRCS="$SRCS $APP/$f.c"; [ -f "$APP/$f.h" ] && SRCS="$SRCS $APP/$f.h"; done
  if grep -nE '#\s*include\s*[<"](platform|r_[a-z_0-9]+_if)\.h[>"]' $SRCS; then rc=1; fi
  echo "  include 一覧:"; grep -hE '^\s*#\s*include' $SRCS | sort | uniq -c
  echo "  グローバル/静的データが無いこと(nm: D/d/B/b/C が無い。const の台本表は r なので可。MinGW のセクション名シンボル .data/.bss は除く)"
  OBJS=""; for f in $STDC_FILES; do cc_nowarn $STD -I"$APP" -c "$APP/$f.c" -o "$BUILD/$f.o" || rc=1; OBJS="$OBJS $BUILD/$f.o"; done
  if nm $OBJS | grep -E ' [DdBbCc] ' | grep -vE ' [DdBbCc] \.'; then rc=1; else echo "  (データシンボルなし)"; fi
  result S1 $rc
fi

# ---------------------------------------------------------------- S2
if sel S2; then
  echo "### S2 H-12 タップ検出(ui_logic.c): 押下 2 標本・離し 3 標本で UI_EV_TAP 1 回、2 点以上と 3,000ms 超は無視、連続タップ、座標は使わない、標本なしは状態を変えない、32bit の巻き戻り、イベントキュー 8 件"
  rc=0
  cc_nowarn $STD $WARN -I"$APP" -I"$HERE" "$HERE/test_ui_tap.c" "$APP/ui_logic.c" -o "$BUILD/test_ui_tap" || rc=1
  [ $rc -eq 0 ] && { "$BUILD/test_ui_tap" || rc=1; }
  result S2 $rc
fi

# ---------------------------------------------------------------- S3
if sel S3; then
  echo "### S3 touch_decode 単体テスト(H8)と、decode の出力が ui_update で画面のどこでもタップ 1 回になること"
  rc=0
  cc_nowarn $STD $WARN -I"$APP" -I"$HERE" "$HERE/test_touch_decode.c" "$APP/touch_decode.c" "$APP/ui_logic.c" -o "$BUILD/test_touch_decode" || rc=1
  [ $rc -eq 0 ] && { "$BUILD/test_touch_decode" || rc=1; }
  result S3 $rc
fi

# S4: 旧 UI の画面アセットの検査(gen_ui_assets.py は Phase 4 で削除)。S28(gen_stock_assets.py)に置き換え

# ---------------------------------------------------------------- S5
if sel S5; then
  echo "### S5 tools/img2c.py & tools/prepare_slide.py(H9。P2 ギャラリー用に残してある)"
  rc=0
  "$PY" -I "$HERE/test_img2c.py" -v 2>&1 | tail -25; [ "${PIPESTATUS[0]}" -eq 0 ] || rc=1
  "$PY" -I "$HERE/test_prepare_slide.py" -v 2>&1 | tail -25; [ "${PIPESTATUS[0]}" -eq 0 ] || rc=1
  result S5 $rc
fi

# ---------------------------------------------------------------- S6
if sel S6; then
  echo "### S6 lcd_panel.c ホスト・ハーネス(実FITヘッダ + 実 r_glcdc_private.c のパラメータ検査。2面・入れ替え・アンダーフロー。H-D6: GR2 = CLUT8)"
  rc=0
  build_ui_objs || rc=1      # lcd_panel.c が ui_pal(生成データ)を参照する
  out="$("$CC" $STD -Wall -Wextra $RXDEF -ffunction-sections -fdata-sections -I"$APP" -I"$HERE" $FIT_INC \
        "$HERE/test_lcd_panel.c" "$SMC/r_glcdc_rx/src/r_glcdc_private.c" "$UIOBJ"/*.o \
        -Wl,--gc-sections $LDX -o "$BUILD/test_lcd_panel" 2>&1)"; crc=$?
  # lcd_panel.c / test_lcd_panel.c 由来の警告だけ表示(FIT側の警告は対象外)
  echo "$out" | grep -E "(src/app/|tests/host/)" | head -20
  [ $crc -eq 0 ] || { echo "$out" | head -30; rc=1; }
  if echo "$out" | grep -E "(src/app/|tests/host/).*warning" >/dev/null; then echo "  NG: 製品コード側に警告"; rc=1; fi
  [ -x "$BUILD/test_lcd_panel" ] && { "$BUILD/test_lcd_panel" || rc=1; }
  result S6 $rc
fi

# ---------------------------------------------------------------- S7
if sel S7; then
  echo "### S7 ハード依存部の -fsyntax-only(P項目の事前確認。RX用GCCでのビルドではない)"
  rc=0
  SMC_NO_BSP_EARLY="-I$SMC/general -I$SMC/r_pincfg -I$SMC -I$SMC/r_config -I$SMC/r_cmt_rx -I$SMC/r_glcdc_rx -I$SMC/r_gpio_rx -I$SMC/r_sci_rx -I$SMC/r_byteq -I$SMC/r_sci_iic_rx"
  HW="lcd_panel touch_ft5x app_main app_debug"
  check_hw() { # label, extra flags, include style
    local label="$1"; shift
    local f out
    for f in $HW; do
      out="$("$CC" -fsyntax-only $STD -Wall -Wextra "$@" "$APP/$f.c" 2>&1)"; local crc=$?
      local mine; mine="$(echo "$out" | grep -E "src/app/.*(warning|error)")"
      if [ $crc -ne 0 ] || [ -n "$mine" ]; then echo "  NG [$label] $f.c"; echo "$out" | head -15; rc=1; else echo "  ok [$label] $f.c"; fi
    done
  }
  echo " -- A: FIT 実ヘッダ + 実 platform.h"
  check_hw "A" $RXDEF -I"$APP" $FIT_INC
  echo " -- A + APP_LOG_ENABLE=1 (SCI2)"
  check_hw "A log2" $RXDEF -DAPP_LOG_ENABLE=1 -I"$APP" $FIT_INC
  echo " -- A + APP_LOG_ENABLE=1 (SCI1)"
  check_hw "A log1" $RXDEF -DAPP_LOG_ENABLE=1 -DAPP_LOG_SCI_CH=1 -I"$APP" $FIT_INC
  echo " -- A + APP_LOG_ENABLE=0 (UARTログ無効。app_logq 等が未使用でも警告が出ない: 設計 U-H11)"
  check_hw "A log0" $RXDEF -DAPP_LOG_ENABLE=0 -I"$APP" $FIT_INC
  check_hw "B log0" -DAPP_LOG_ENABLE=0 -I"$ROOT/tests/stubs" -I"$APP" $SMC_NO_BSP_EARLY
  echo " -- A + APP_STK_PREVIEW=1 (8 状態を 3 秒ごとに表示。H-13)"
  check_hw "A preview1" $RXDEF -DAPP_STK_PREVIEW=1 -I"$APP" $FIT_INC
  check_hw "A preview1 wx1" $RXDEF -DAPP_STK_PREVIEW=1 -DAPP_WX=1 -I"$APP" $FIT_INC
  echo " -- A + APP_WX = 0 / 1(既定)/ APP_WIFI_TEST = 1 / APP_STK_FAULT = 2 (H-13。app_main.c・lcd_panel.c・touch_ft5x.c・app_debug.c)"
  check_hw "A wx0" $RXDEF -DAPP_WX=0 -I"$APP" $FIT_INC
  check_hw "A wx1" $RXDEF -DAPP_WX=1 -I"$APP" $FIT_INC
  check_hw "A wifitest1" $RXDEF -DAPP_WIFI_TEST=1 -I"$APP" $FIT_INC
  check_hw "A fault2" $RXDEF -DAPP_STK_FAULT=2 -I"$APP" $FIT_INC
  check_hw "A preview0 wx0" $RXDEF -DAPP_STK_PREVIEW=0 -DAPP_WX=0 -I"$APP" $FIT_INC
  echo " -- A + APP_PERF_BENCH=0 (計測なし)"
  check_hw "A perf0" $RXDEF -DAPP_PERF_BENCH=0 -I"$APP" $FIT_INC
  echo " -- A + -Wconversion(製品コードの警告のみ数える。情報用)"
  for f in $HW; do
    "$CC" -fsyntax-only $STD -Wall -Wextra -Wconversion $RXDEF -I"$APP" $FIT_INC "$APP/$f.c" 2>&1 | grep -E "src/app/.*warning" | head -5
  done
  echo " -- B: 仮 platform.h(tests/stubs) + FIT の実ヘッダ(platform.h だけ差し替え)"
  SMC_NO_BSP="-I$SMC/general -I$SMC/r_pincfg -I$SMC -I$SMC/r_config -I$SMC/r_cmt_rx -I$SMC/r_glcdc_rx -I$SMC/r_gpio_rx -I$SMC/r_sci_rx -I$SMC/r_byteq -I$SMC/r_sci_iic_rx"
  check_hw "B" -I"$ROOT/tests/stubs" -I"$APP" $SMC_NO_BSP
  check_hw "B log2" -DAPP_LOG_ENABLE=1 -I"$ROOT/tests/stubs" -I"$APP" $SMC_NO_BSP
  echo " -- A: APP_LOG_SCI_CH=3 は #error になること(想定どおりの失敗)"
  if "$CC" -fsyntax-only $STD $RXDEF -DAPP_LOG_ENABLE=1 -DAPP_LOG_SCI_CH=3 -I"$APP" $FIT_INC "$APP/app_debug.c" 2>&1 | grep -q "APP_LOG_SCI_CH"; then echo "  ok (#error 発生)"; else echo "  NG: #error が出ない"; rc=1; fi
  result S7 $rc
fi

# ---------------------------------------------------------------- S8
if sel S8; then
  echo "### S8 app_logfmt 単体テスト(U-H1〜U-H6: 各行の文字列・境界値・小バッファ・build・コマンド・周期・[ev] btn=tap src=...・[help]・[perf])"
  rc=0
  cc_nowarn $STD $WARN -I"$APP" -I"$HERE" "$HERE/test_logfmt.c" "$APP/app_logfmt.c" -o "$BUILD/test_logfmt" || rc=1
  [ $rc -eq 0 ] && { "$BUILD/test_logfmt" || rc=1; }
  result S8 $rc
fi

# ---------------------------------------------------------------- S9
if sel S9; then
  echo "### S9 app_logq 単体テスト(U-H7)"
  rc=0
  cc_nowarn $STD $WARN -I"$APP" -I"$HERE" "$HERE/test_logq.c" "$APP/app_logq.c" -o "$BUILD/test_logq" || rc=1
  [ -x "$BUILD/test_logq" ] && { "$BUILD/test_logq" || rc=1; }
  result S9 $rc
fi

# 実 platform.h / FIT ヘッダを使い、r_sci_rx を偽物(tests/stubs/fake_sci.h)に差し替えて製品の .c を #include する試験の共通ビルド
build_fit_test() { # $1=出力 $2=テストの.c  残り=追加ソース(コマンド行の追加フラグは EXTRA_CFLAGS)
  local out="$1" src="$2"; shift 2
  local o; o="$("$CC" $STD -Wall -Wextra $RXDEF ${EXTRA_CFLAGS:-} -I"$APP" -I"$HERE" $FIT_INC "$src" "$@" $LDX -o "$out" 2>&1)"; local crc=$?
  # 製品コード(src/app)と試験コード(tests/host)由来の警告・エラーだけを見る(FIT 側の既存警告は対象外)
  echo "$o" | grep -E "(src/app/|tests/host/|tests/stubs/).*(warning|error)" | head -20
  if echo "$o" | grep -E "(src/app/|tests/host/|tests/stubs/).*warning" >/dev/null; then return 1; fi
  [ $crc -eq 0 ] || echo "$o" | grep -E "error|undefined reference" | head -10
  return $crc
}

# ---------------------------------------------------------------- S10
if sel S10; then
  echo "### S10 app_debug.c のログ部(キュー・BUSY 再送・満杯破棄・受信・コールバック)を r_sci_rx の偽物で試験"
  rc=0
  build_fit_test "$BUILD/test_app_debug" "$HERE/test_app_debug.c" "$APP/app_logq.c" || rc=1
  [ -x "$BUILD/test_app_debug" ] && { "$BUILD/test_app_debug" || rc=1; }
  result S10 $rc
fi

# S11: 旧 UI の app_main 結合試験。Phase 1 で廃止。株価ビューの app_main 結合試験は S34

# ---------------------------------------------------------------- S12
if sel S12; then
  echo "### S12 -fsanitize=address,undefined で S8〜S10 と S2/S3/S13/S14/S29/S30/S34 のテストを再実行(1件でも検出したら失敗)"
  rc=0
  SAN="-O1 -g -fno-omit-frame-pointer -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 -D__NO_INLINE__ -fsanitize=address,undefined -fno-sanitize-recover=all"
  # この環境で sanitizer がリンクできるか(MinGW には libasan / libubsan が無いことが多い)
  echo 'int main(void){return 0;}' > "$BUILD/san_probe.c"
  if ! "$CC" $SAN "$BUILD/san_probe.c" -o "$BUILD/san_probe" >/dev/null 2>&1; then
    skip S12 "この環境では $CC の -fsanitize=address,undefined がリンクできない(libasan / libubsan が無い)"
  else
    run_san() { # 名前 実行ファイル
      echo "  [san] $1"; timeout 300 "$2" > "$BUILD/$1.san.log" 2>&1; local r=$?; tail -1 "$BUILD/$1.san.log"; [ $r -eq 0 ] || { head -30 "$BUILD/$1.san.log"; rc=1; }
    }
    build_ui_objs || rc=1
    cc_nowarn $STD $WARN $SAN -I"$APP" -I"$HERE" "$HERE/test_logfmt.c" "$APP/app_logfmt.c" -o "$BUILD/san_logfmt" || rc=1
    [ -x "$BUILD/san_logfmt" ] && run_san logfmt "$BUILD/san_logfmt"
    cc_nowarn $STD $WARN $SAN -I"$APP" -I"$HERE" "$HERE/test_logq.c" "$APP/app_logq.c" -o "$BUILD/san_logq" || rc=1
    [ -x "$BUILD/san_logq" ] && run_san logq "$BUILD/san_logq"
    cc_nowarn $STD $WARN $SAN -I"$APP" -I"$HERE" "$HERE/test_ui_tap.c" "$APP/ui_logic.c" -o "$BUILD/san_ui_tap" || rc=1
    [ -x "$BUILD/san_ui_tap" ] && run_san ui_tap "$BUILD/san_ui_tap"
    cc_nowarn $STD $WARN $SAN -I"$APP" -I"$HERE" "$HERE/test_touch_decode.c" "$APP/touch_decode.c" "$APP/ui_logic.c" -o "$BUILD/san_touch" || rc=1
    [ -x "$BUILD/san_touch" ] && run_san touch_decode "$BUILD/san_touch"
    cc_nowarn $STD $WARN $SAN -I"$APP" -I"$HERE" "$HERE/test_gfx.c" "$APP/gfx.c" -o "$BUILD/san_gfx" || rc=1
    [ -x "$BUILD/san_gfx" ] && run_san gfx "$BUILD/san_gfx"
    cc_nowarn $STD $WARN $SAN -DAPP_STK_PREVIEW=1 -I"$APP" -I"$HERE" "$HERE/test_ui_stock.c" "$APP/ui_render.c" "$APP/ui_logic.c" "$APP/stk_core.c" "$APP/gfx.c" "$APP/ui_preview.c" "$UIOBJ"/*.o -o "$BUILD/san_ui_stock" || rc=1
    [ -x "$BUILD/san_ui_stock" ] && { echo "  [san] ui_stock"; timeout 300 "$BUILD/san_ui_stock" "$HERE/data/states" > "$BUILD/ui_stock.san.log" 2>&1 || { head -30 "$BUILD/ui_stock.san.log"; rc=1; }; tail -1 "$BUILD/ui_stock.san.log"; }
    cc_nowarn $STD $WARN $SAN -I"$APP" -I"$HERE" "$HERE/test_stk_core.c" "$APP/stk_core.c" -o "$BUILD/san_stk_core" || rc=1
    [ -x "$BUILD/san_stk_core" ] && run_san stk_core "$BUILD/san_stk_core"
    cc_nowarn $STD $SAN -DAPP_WIFI_TEST=1 -I"$APP" -I"$HERE" "$HERE/test_wx_stock.c" "$APP/wx_core.c" "$APP/stk_core.c" -o "$BUILD/san_wx_stock" || rc=1
    [ -x "$BUILD/san_wx_stock" ] && { echo "  [san] wx_stock"; timeout 300 "$BUILD/san_wx_stock" "$HERE/data" > "$BUILD/wx_stock.san.log" 2>&1 || { head -30 "$BUILD/wx_stock.san.log"; rc=1; }; tail -1 "$BUILD/wx_stock.san.log"; }
    EXTRA_CFLAGS="$SAN" build_fit_test "$BUILD/san_app_debug" "$HERE/test_app_debug.c" "$APP/app_logq.c" || rc=1
    [ -x "$BUILD/san_app_debug" ] && run_san app_debug "$BUILD/san_app_debug"
    result S12 $rc
  fi
fi

# ---------------------------------------------------------------- S13
if sel S13; then
  echo "### S13 gfx 単体テスト(H-G1: gfx_a8 の精度・濃度 / H-G2: blit・fillrect の切り取り)"
  rc=0
  cc_nowarn $STD $WARN -I"$APP" -I"$HERE" "$HERE/test_gfx.c" "$APP/gfx.c" -o "$BUILD/test_gfx" || rc=1
  [ $rc -eq 0 ] && { "$BUILD/test_gfx" || rc=1; }
  result S13 $rc
fi

# ---------------------------------------------------------------- S14
if sel S14; then
  echo "### S14 株価ビューの描画と ui_logic(docs/design.md H-8、H-12 の一部): test_ui_stock(全画素を書く・面の外に書かない・最悪の幅・24 状態の CRC・現在値の x・表示の決定・色・タップ検出)と、ホストの C 描画(fb_hostrender)と Python のプレビュー(--emit-c8)が x < 480 で 0 バイト差(H-8 ⑥)"
  rc=0
  build_ui_objs || rc=1
  cc_nowarn $STD $WARN -DAPP_STK_PREVIEW=1 -I"$APP" -I"$HERE" "$HERE/test_ui_stock.c" "$APP/ui_render.c" "$APP/ui_logic.c" "$APP/stk_core.c" "$APP/gfx.c" "$APP/ui_preview.c" "$UIOBJ"/*.o -o "$BUILD/test_ui_stock" || rc=1
  [ -x "$BUILD/test_ui_stock" ] && { "$BUILD/test_ui_stock" "$HERE/data/states" || rc=1; }
  cc_nowarn $STD $WARN -I"$APP" -c "$ROOT/tools/fb_hostrender.c" -o "$BUILD/fb_hostrender.o" || rc=1
  cc_nowarn $STD -O1 -I"$APP" "$ROOT/tools/fb_hostrender.c" "$APP/ui_render.c" "$APP/ui_logic.c" "$APP/stk_core.c" "$APP/gfx.c" "$UIOBJ"/*.o -o "$BUILD/fb_hostrender" || rc=1
  rm -rf "$BUILD/h8"; mkdir -p "$BUILD/h8/c" "$BUILD/h8/py"
  "$PY" -I "$ROOT/tools/gen_stock_assets.py" --emit-c8 "$BUILD/h8/py" --preview-dir "$BUILD/h8/prev" >/dev/null || rc=1
  if [ -x "$BUILD/fb_hostrender" ]; then
    for n in 0 1 2 3 4 5 6 7; do
      "$BUILD/fb_hostrender" "$HERE/data/states/S$n.state" "$BUILD/h8/c/S$n.c8" --pal "$BUILD/h8/c/S$n.pal" || rc=1
      if cmp -s "$BUILD/h8/c/S$n.c8" "$BUILD/h8/py/S$n.c8"; then echo "  H-8 ⑥ S$n: C と Python が 139,264 バイト中 0 バイト差(x < 480 は 130,560 画素)"; else echo "  NG: S$n の C 描画と Python のプレビューが違う"; cmp -l "$BUILD/h8/c/S$n.c8" "$BUILD/h8/py/S$n.c8" | head -3; rc=1; fi
    done
    cmp -s "$BUILD/h8/c/S1.pal" "$BUILD/h8/py/stock.pal" || { echo "  NG: ui_pal と Python のパレットが違う"; rc=1; }
  fi
  result S14 $rc
fi

# S15: 旧 UI の試験。Phase 1(株価ビュー)で廃止(docs/design.md §7.1)

# S16: 旧 UI の試験。Phase 1(株価ビュー)で廃止(docs/design.md §7.1)

# S17: 旧 UI の試験。Phase 1(株価ビュー)で廃止(docs/design.md §7.1)

# S18: CLUT8 の実験(clut8_pat / clut8_test)。Phase 4 でファイルごと削除(実機での確認は済み)

# S19: 旧 UI の試験。Phase 1(株価ビュー)で廃止(docs/design.md §7.1)

# S20: 旧 UI の試験。Phase 1(株価ビュー)で廃止(docs/design.md §7.1)

# S21: 旧 UI の試験。Phase 1(株価ビュー)で廃止(docs/design.md §7.1)

# ---------------------------------------------------------------- S22
if sel S22; then
  echo "### S22 Wi-Fi(ESP32 AT)試作(docs/design.md §21.7 の H-W1〜H-W13)と株価の取得(H-10): wifi_core の標準C条件・APP_WIFI_TEST = 0 かつ APP_WX = 0 で中身が消える・単体試験(行・分類・値・CWJAP・書式・状態機械・秘密がログに出ない・銘柄ごとの取得・err=9・タイムアウト・CLOSED の途切れ・quiet)を専用モード(APP_WIFI_TEST=1)と製品(APP_WX=1)の 2 通りで・静的検査(wifi_secret.h の include 範囲、.gitignore)"
  rc=0
  for std in "-std=gnu99" "-std=c99 -pedantic-errors" "-std=c11"; do
    cc_nowarn $std $WARN -DAPP_WIFI_TEST=1 -I"$APP" -c "$APP/wifi_core.c" -o "$BUILD/wifi_core.o" || rc=1
    cc_nowarn $std $WARN -DAPP_WIFI_TEST=1 -I"$APP" -c "$APP/wx_core.c" -o "$BUILD/wx_core.o" || rc=1       # 天気の取得(docs/design.md §22)。標準 C
    cc_nowarn $std $WARN -DAPP_WX=1 -I"$APP" -c "$APP/wifi_core.c" -o "$BUILD/wifi_core_wx.o" || rc=1        # APP_WX = 1 でも中身がある(APP_WIFI_ON)
    cc_nowarn $std $WARN -DAPP_WX=1 -I"$APP" -c "$APP/wx_core.c" -o "$BUILD/wx_core_wx.o" || rc=1
  done
  echo "  H-W8 / H-X11: wifi_core.c/.h・wx_core.c/.h と wifi_sw.h が platform.h / FIT ヘッダ / board_pins.h を include していないこと"
  if grep -nE '#\s*include\s*[<"](platform|board_pins|r_[a-z_0-9]+_if)\.h[>"]' "$APP/wifi_core.c" "$APP/wifi_core.h" "$APP/wx_core.c" "$APP/wx_core.h" "$APP/wifi_sw.h"; then rc=1; fi
  echo "  グローバル/静的データが無いこと(nm。const の表は r)"
  if nm "$BUILD/wifi_core.o" "$BUILD/wx_core.o" | grep -E ' [DdBbCc] ' | grep -vE ' [DdBbCc] \.'; then rc=1; else echo "  (データシンボルなし)"; fi
  echo "  APP_WIFI_TEST = 0 かつ APP_WX = 0(取得なし)では wifi_core.o / wx_core.o が空(.text 等のシンボルなし)。APP_WX = 1(既定)だけでも中身がある"
  cc_nowarn $STD $WARN -DAPP_WX=0 -I"$APP" -c "$APP/wifi_core.c" -o "$BUILD/wifi_core0.o" || rc=1
  cc_nowarn $STD $WARN -DAPP_WX=0 -I"$APP" -c "$APP/wx_core.c" -o "$BUILD/wx_core0.o" || rc=1
  if nm "$BUILD/wifi_core0.o" "$BUILD/wx_core0.o" | grep -E ' [TtDdRrBbCc] ' | grep -vE ' [TtDdRrBbCc] \.'; then echo "  NG: 0 でシンボルが残る"; rc=1; fi
  if ! nm "$BUILD/wifi_core_wx.o" "$BUILD/wx_core_wx.o" | grep -qE ' [Tt] (wifi_core_start|wx_parse_http)'; then echo "  NG: APP_WX = 1 で中身が無い"; rc=1; fi
  echo "  wifi_sw.h: 既定は APP_WX = 1・APP_WIFI_TEST = 0・APP_STK_PREVIEW = 0・APP_STK_FAULT = 0、プレビュー(APP_STK_PREVIEW = 1)の既定は APP_WX = 0、APP_STK_FAULT が 0〜3 でなければ #error(H-13)"
  d="$(printf '#include "wifi_sw.h"\n_Static_assert(APP_WX == 1 && APP_WIFI_TEST == 0 && APP_STK_PREVIEW == 0 && APP_STK_FAULT == 0 && APP_WX_PRODUCT == 1 && APP_WIFI_ON == 1, "default");\n' | "$CC" -fsyntax-only -x c -std=gnu11 -I"$APP" - 2>&1)" || { echo "  NG: 既定のスイッチ: $d"; rc=1; }
  d="$(printf '#include "wifi_sw.h"\n_Static_assert(APP_WX == 0 && APP_WX_PRODUCT == 0 && APP_WIFI_ON == 0, "preview");\n' | "$CC" -fsyntax-only -x c -std=gnu11 -DAPP_STK_PREVIEW=1 -I"$APP" - 2>&1)" || { echo "  NG: プレビューの既定: $d"; rc=1; }
  d="$(printf '#include "wifi_sw.h"\n_Static_assert(APP_WX == 1 && APP_WX_PRODUCT == 0 && APP_WIFI_ON == 1, "wifitest");\n' | "$CC" -fsyntax-only -x c -std=gnu11 -DAPP_WIFI_TEST=1 -I"$APP" - 2>&1)" || { echo "  NG: 専用モード: $d"; rc=1; }
  if echo '#include "wifi_sw.h"' | "$CC" -fsyntax-only -x c -DAPP_STK_FAULT=4 -I"$APP" - >/dev/null 2>&1; then echo "  NG: APP_STK_FAULT = 4 で #error が出ない"; rc=1; else echo "  ok (APP_STK_FAULT = 4 は #error)"; fi
  cc_nowarn $STD $WARN -DAPP_WIFI_TEST=1 -I"$APP" -I"$HERE" "$HERE/test_wifi_core.c" "$APP/wifi_core.c" "$APP/wx_core.c" "$APP/stk_core.c" -o "$BUILD/test_wifi_core" || rc=1
  [ -x "$BUILD/test_wifi_core" ] && { timeout 300 "$BUILD/test_wifi_core" || rc=1; }
  cc_nowarn $STD $WARN -DAPP_WX=1 -I"$APP" -I"$HERE" "$HERE/test_wifi_core.c" "$APP/wifi_core.c" "$APP/wx_core.c" "$APP/stk_core.c" -o "$BUILD/test_wifi_core_wx" || rc=1     # 製品(接続先は Yahoo の SSL だけ。z の step は範囲に入らない)
  [ -x "$BUILD/test_wifi_core_wx" ] && { timeout 300 "$BUILD/test_wifi_core_wx" || rc=1; }
  echo "  APP_STK_BADAP(0〜9。範囲外は #error)。0 = コードが消える(ダミーの文字列が wifi_core.o に無い)。2 = 最初の 2 回だけダミーで接続(test_wifi_core.c の t_badap)。変異: 判定を 1 つずらすと失敗"
  if echo '#include "wifi_sw.h"' | "$CC" -fsyntax-only -x c -DAPP_STK_BADAP=10 -I"$APP" - >/dev/null 2>&1; then echo "  NG: APP_STK_BADAP = 10 で #error が出ない"; rc=1; else echo "  ok (APP_STK_BADAP = 10 は #error)"; fi
  if echo '#include "wifi_sw.h"' | "$CC" -fsyntax-only -x c -DAPP_STK_BADAP=-1 -I"$APP" - >/dev/null 2>&1; then echo "  NG: APP_STK_BADAP = -1 で #error が出ない"; rc=1; else echo "  ok (APP_STK_BADAP = -1 は #error)"; fi
  if strings "$BUILD/wifi_core_wx.o" | grep -q "stk-badap"; then echo "  NG: APP_STK_BADAP = 0 の wifi_core.o にダミーの文字列が残っている"; rc=1; else echo "  ok (APP_STK_BADAP = 0 ではダミーの文字列なし)"; fi
  for n in 1 2 9; do
    cc_nowarn $STD $WARN -DAPP_WX=1 -DAPP_STK_BADAP=$n -I"$APP" -I"$HERE" "$HERE/test_wifi_core.c" "$APP/wifi_core.c" "$APP/wx_core.c" "$APP/stk_core.c" -o "$BUILD/test_wifi_core_badap$n" || rc=1
    [ -x "$BUILD/test_wifi_core_badap$n" ] && { timeout 300 "$BUILD/test_wifi_core_badap$n" | tail -3 | grep -E "FAIL|APP_STK_BADAP|checks"; [ "${PIPESTATUS[0]}" -eq 0 ] || rc=1; }
  done
  mkdir -p "$BUILD/mut_badap"
  sed 's/c->badap_n <= (uint8_t)APP_STK_BADAP/c->badap_n < (uint8_t)APP_STK_BADAP/' "$APP/wifi_core.c" > "$BUILD/mut_badap/wifi_core.c"
  if cmp -s "$APP/wifi_core.c" "$BUILD/mut_badap/wifi_core.c"; then echo "  NG: 変異を入れられなかった(APP_STK_BADAP)"; rc=1
  else
    cc_nowarn $STD -Wall -Wextra -DAPP_WX=1 -DAPP_STK_BADAP=2 -I"$APP" -I"$HERE" "$HERE/test_wifi_core.c" "$BUILD/mut_badap/wifi_core.c" "$APP/wx_core.c" "$APP/stk_core.c" -o "$BUILD/test_wifi_core_badap_mut" || rc=1
    if [ -x "$BUILD/test_wifi_core_badap_mut" ] && "$BUILD/test_wifi_core_badap_mut" >/dev/null 2>&1; then echo "  NG: 変異 [ダミーの回数を 1 つ少なく] で試験が失敗しない"; rc=1; else echo "  ok: 変異 [ダミーの回数を 1 つ少なく] で試験が失敗した"; fi
  fi
  echo "  H-W8: wifi_secret.h を include するのは wifi_at.c だけ(ファイル名だけ出す。中身は読まない)"
  inc="$(grep -rlE '^\s*#\s*include\s*[<"]wifi_secret\.h[>"]' "$ROOT/src" --include='*.c' --include='*.h' --exclude=wifi_secret.h --exclude-dir=smc_gen 2>/dev/null | sed 's#.*/##' | sort -u)"
  echo "  include している: ${inc:-(なし)}"
  for f in $inc; do [ "$f" = "wifi_at.c" ] || { echo "  NG: $f が wifi_secret.h を include している"; rc=1; }; done
  echo "  H-W9: wifi_secret.h が .gitignore 済みで、追跡されていない / ひな形は無視されていない"
  if git -C "$ROOT" rev-parse --git-dir >/dev/null 2>&1; then
    git -C "$ROOT" check-ignore -q src/app/wifi_secret.h || { echo "  NG: wifi_secret.h が .gitignore に無い"; rc=1; }
    if git -C "$ROOT" ls-files --error-unmatch src/app/wifi_secret.h >/dev/null 2>&1; then echo "  NG: wifi_secret.h が追跡されている"; rc=1; fi
    if git -C "$ROOT" check-ignore -q src/app/wifi_secret.h.example; then echo "  NG: ひな形が無視されている"; rc=1; fi
  else
    echo "  (git リポジトリではない。H-W9 は未実施)"; rc=1
  fi
  result S22 $rc
fi

# ---------------------------------------------------------------- S23
if sel S23; then
  echo "### S23 Wi-Fi(ESP32 AT)のハード側(wifi_at.c): APP_WX = 0 かつ APP_WIFI_TEST = 0 で中身が消える・app_main.c の構文・SCI1 と EN と専用ループを偽の r_sci_rx / GPIO / CMT / ESP32 で試験(既定 / 秘密なし / 自動開始の3種)・秘密の置き場所"
  rc=0
  echo "  APP_WIFI_TEST = 0 かつ APP_WX = 0(取得なし)では wifi_at.o が空(実 platform.h + FIT ヘッダ。wifi_secret.h は読まない)"
  o="$("$CC" $STD -Wall -Wextra $RXDEF -DAPP_WX=0 -I"$APP" $FIT_INC -c "$APP/wifi_at.c" -o "$BUILD/wifi_at0.o" 2>&1)"; crc=$?
  echo "$o" | grep -E "src/app/" | head -10
  [ $crc -eq 0 ] || { echo "$o" | head -10; rc=1; }
  if echo "$o" | grep -E "src/app/.*warning" >/dev/null; then rc=1; fi
  if nm "$BUILD/wifi_at0.o" | grep -E ' [TtDdRrBbCc] ' | grep -vE ' [TtDdRrBbCc] \.'; then echo "  NG: 0 でシンボルが残る"; rc=1; fi
  echo "  APP_WIFI_TEST = 1 の app_main.c / wifi_at.c(WIFI_AT_HOST_TEST: 実物の wifi_secret.h は読まない。WIFI_SSID / WIFI_PASS は試験用のダミー)の -fsyntax-only"
  for f in app_main wifi_at; do
    o="$("$CC" -fsyntax-only $STD -Wall -Wextra $RXDEF -DAPP_WIFI_TEST=1 -DWIFI_AT_HOST_TEST '-DWIFI_SSID="s"' '-DWIFI_PASS="p"' -I"$APP" $FIT_INC "$APP/$f.c" 2>&1)"; crc=$?
    if [ $crc -ne 0 ] || echo "$o" | grep -E "src/app/.*(warning|error)" >/dev/null; then echo "  NG $f.c"; echo "$o" | head -10; rc=1; else echo "  ok $f.c"; fi
  done
  echo "  結合試験 test_wifi_at.c(3種: 既定 / -DT_NOSECRET / -DAPP_WIFI_AUTOSTART=1)"
  W23="$APP/wifi_core.c $APP/wx_core.c $APP/stk_core.c $APP/app_logfmt.c"
  EXTRA_CFLAGS="-DAPP_WIFI_TEST=1 -DWIFI_AT_HOST_TEST" build_fit_test "$BUILD/test_wifi_at" "$HERE/test_wifi_at.c" $W23 || rc=1
  [ -x "$BUILD/test_wifi_at" ] && { timeout 300 "$BUILD/test_wifi_at" || rc=1; }
  EXTRA_CFLAGS="-DAPP_WIFI_TEST=1 -DWIFI_AT_HOST_TEST -DT_NOSECRET" build_fit_test "$BUILD/test_wifi_at_ns" "$HERE/test_wifi_at.c" $W23 || rc=1
  [ -x "$BUILD/test_wifi_at_ns" ] && { timeout 300 "$BUILD/test_wifi_at_ns" || rc=1; }
  EXTRA_CFLAGS="-DAPP_WIFI_TEST=1 -DWIFI_AT_HOST_TEST -DAPP_WIFI_AUTOSTART=1" build_fit_test "$BUILD/test_wifi_at_au" "$HERE/test_wifi_at.c" $W23 || rc=1
  [ -x "$BUILD/test_wifi_at_au" ] && { timeout 300 "$BUILD/test_wifi_at_au" || rc=1; }
  echo "  秘密の置き場所: WIFI_SSID / WIFI_PASS を参照するのは wifi_at.c だけ(wifi_core.h は使い方のコメントだけ。ひな形 .example を除く。ファイル名だけ出す。中身は読まない)"
  refs="$(grep -rlE 'WIFI_(SSID|PASS)' "$APP" --include='*.c' --include='*.h' --exclude=wifi_secret.h 2>/dev/null | sed 's#.*/##' | sort -u)"
  echo "  参照している: ${refs:-(なし)}"
  for f in $refs; do
    case "$f" in
      wifi_at.c) ;;
      wifi_core.h) if grep -E 'WIFI_(SSID|PASS)' "$APP/wifi_core.h" | grep -vE '^[[:space:]]*\*' >/dev/null; then echo "  NG: wifi_core.h のコメント以外に WIFI_SSID / WIFI_PASS がある"; rc=1; fi ;;   # 使い方の説明コメントだけ
      *) echo "  NG: $f が WIFI_SSID / WIFI_PASS を参照している"; rc=1 ;;
    esac
  done
  result S23 $rc
fi

# ---------------------------------------------------------------- S24
if sel S24; then
  echo "### S24 株価の取得の論理(docs/design.md H-6・H-11): wx_core の単体試験(要求 144 / 145 / 145 バイト・接続先・[stk] の書式 R-STK-*・取得の予定表(3 銘柄・失敗分のやり直し・429・全部失敗・周期・更新要求・32bit の巻き戻り)・Date: と時刻・ソフト時計)。専用モード(APP_WIFI_TEST=1)と製品(APP_WX=1)の 2 通り。wifi_core の WX step・範囲(H-10)は S22、wifi_at は S23 / S26"
  rc=0
  cc_nowarn $STD $WARN -DAPP_WIFI_TEST=1 -I"$APP" -I"$HERE" "$HERE/test_wx_core.c" "$APP/wx_core.c" "$APP/stk_core.c" -o "$BUILD/test_wx_core" || rc=1
  [ -x "$BUILD/test_wx_core" ] && { timeout 300 "$BUILD/test_wx_core" || rc=1; }
  cc_nowarn $STD $WARN -DAPP_WX=1 -I"$APP" -I"$HERE" "$HERE/test_wx_core.c" "$APP/wx_core.c" "$APP/stk_core.c" -o "$BUILD/test_wx_core_wx" || rc=1
  [ -x "$BUILD/test_wx_core_wx" ] && { timeout 300 "$BUILD/test_wx_core_wx" || rc=1; }
  result S24 $rc
fi

# S25: 旧 UI の試験。Phase 1(株価ビュー)で廃止(docs/design.md §7.1)

# ---------------------------------------------------------------- S26
if sel S26; then
  echo "### S26 株価の取得の製品への組み込み(docs/design.md §5.7・§5.9・§7.5・Phase 3): APP_WX = 1(APP_WIFI_TEST = 0)の wifi_at.c(wx_init / wx_poll / wx_request / stk_ui / wx_log_state)を偽の ESP32 で試験(秘密あり / なし。3 銘柄の周期・失敗分のやり直し・全部失敗の再接続・[stk] ログ・表示値 stk_view_t)、app_main.c の組み込み(偽の wifi_at API)、APP_WX = 0 / APP_WIFI_TEST = 0 の組み合わせで中身が消えること"
  rc=0
  echo "  APP_WX = 1 かつ APP_WIFI_TEST = 0 の wifi_at.c(WIFI_AT_HOST_TEST。秘密あり / なし)・app_main.c の -fsyntax-only"
  for f in app_main wifi_at; do
    for sec in 1 0; do
      if [ $sec = 1 ]; then SECF="-DWIFI_SSID=\"s\" -DWIFI_PASS=\"p\""; else SECF=""; fi
      o="$("$CC" -fsyntax-only $STD -Wall -Wextra $RXDEF -DAPP_WX=1 -DWIFI_AT_HOST_TEST $SECF -I"$APP" $FIT_INC "$APP/$f.c" 2>&1)"; crc=$?
      if [ $crc -ne 0 ] || echo "$o" | grep -E "src/app/.*(warning|error)" >/dev/null; then echo "  NG $f.c (secret=$sec)"; echo "$o" | head -10; rc=1; else echo "  ok $f.c (secret=$sec)"; fi
    done
  done
  echo "  APP_WX = 1 だけ(APP_WIFI_TEST = 0)では wifi_at.o に製品の API が入り、専用モード(wifi_test_main)は入らない。APP_WX = 0 かつ APP_WIFI_TEST = 0 では空(S23)。APP_WX = 1 が既定"
  o="$("$CC" $STD -Wall -Wextra $RXDEF -DAPP_WX=1 -DWIFI_AT_HOST_TEST -I"$APP" $FIT_INC -c "$APP/wifi_at.c" -o "$BUILD/wifi_at_wx.o" 2>&1)"; crc=$?
  [ $crc -eq 0 ] || { echo "$o" | head; rc=1; }
  for sym in wx_init wx_poll wx_request stk_ui wx_log_state; do nm "$BUILD/wifi_at_wx.o" | grep -qE " [Tt] _?$sym\$" || { echo "  NG: $sym が無い"; rc=1; }; done
  if nm "$BUILD/wifi_at_wx.o" | grep -qE " [Tt] _?wifi_test_main\$"; then echo "  NG: APP_WX = 1 だけで wifi_test_main が入っている"; rc=1; fi
  if nm "$BUILD/wifi_at_wx.o" | grep -qE " [Tt] _?wx_ui\$"; then echo "  NG: 旧 UI の wx_ui が残っている"; rc=1; fi
  echo "  結合試験 test_wifi_at.c の製品版(2種: 秘密あり / -DT_NOSECRET)"
  W23="$APP/wifi_core.c $APP/wx_core.c $APP/stk_core.c $APP/app_logfmt.c"
  EXTRA_CFLAGS="-DAPP_WX=1 -DWIFI_AT_HOST_TEST" build_fit_test "$BUILD/test_wifi_at_wx" "$HERE/test_wifi_at.c" $W23 || rc=1
  [ -x "$BUILD/test_wifi_at_wx" ] && { timeout 300 "$BUILD/test_wifi_at_wx" || rc=1; }
  EXTRA_CFLAGS="-DAPP_WX=1 -DWIFI_AT_HOST_TEST -DT_NOSECRET" build_fit_test "$BUILD/test_wifi_at_wx_ns" "$HERE/test_wifi_at.c" $W23 || rc=1
  [ -x "$BUILD/test_wifi_at_wx_ns" ] && { timeout 300 "$BUILD/test_wifi_at_wx_ns" || rc=1; }
  echo "  wifi_core の quiet(製品: res=start・res=ok と key=oth を出さない)は S22(test_wifi_core.c)"
  result S26 $rc
fi

# ---------------------------------------------------------------- S27
if sel S27; then
  echo "### S27 取得・時刻同期の独立試験(テスト担当の追加): test_wx_indep(ガードページによる範囲外読み出しの検出・全日の暦・ソフト時計・予定表の乱数走行(ms の折り返し)・wifi_core の乱数入力)"
  rc=0
  cc_nowarn $STD -Wall -Wextra -DAPP_WX=1 -I"$APP" -I"$HERE" "$HERE/test_wx_indep.c" "$APP/wx_core.c" "$APP/wifi_core.c" "$APP/stk_core.c" -o "$BUILD/test_wx_indep" || rc=1
  [ -x "$BUILD/test_wx_indep" ] && { timeout 300 "$BUILD/test_wx_indep" || rc=1; }
  result S27 $rc
fi

# ---------------------------------------------------------------- S28
if sel S28; then
  echo "### S28 株価ビューのアセット生成(docs/design.md §3.11、H-9 の前半): gen_stock_assets.py --check(パレット 256・固定色が厳密・最大誤差 <= 3 段・グリフ・矩形の重なり・フッター幅)/ 2 回の生成で同一 / コミット済みのプレビュー PNG と一致"
  rc=0
  "$PY" -I "$ROOT/tools/gen_stock_assets.py" --check || rc=1
  rm -rf "$BUILD/stock_prev"; mkdir -p "$BUILD/stock_prev"
  "$PY" -I "$ROOT/tools/gen_stock_assets.py" --preview-dir "$BUILD/stock_prev" >/dev/null || rc=1
  n=0
  for f in "$BUILD"/stock_prev/*.png; do
    b="$(basename "$f")"; n=$((n+1))
    cmp -s "$f" "$ROOT/tools/assets/stock/preview/$b" || { echo "  NG: コミット済み tools/assets/stock/preview/$b が生成物と違う(再生成が必要)"; rc=1; }
  done
  echo "  比較したプレビュー PNG = $n(sheet + state_S0〜S7 + strips + header_footer + palette = 12)"
  [ "$n" -eq 12 ] || rc=1
  echo "  C の生成物(--emit-c)が 2 回で同一、コミット済みの src/app と一致(CR は無視)"
  rm -rf "$BUILD/stock_c1" "$BUILD/stock_c2"
  "$PY" -I "$ROOT/tools/gen_stock_assets.py" --emit-c --out-dir "$BUILD/stock_c1" --preview-dir "$BUILD/stock_c1/prev" >/dev/null || rc=1
  "$PY" -I "$ROOT/tools/gen_stock_assets.py" --emit-c --out-dir "$BUILD/stock_c2" --preview-dir "$BUILD/stock_c2/prev" >/dev/null || rc=1
  n=0
  for b in ui_assets.h ui_layout.c ui_gfx_data.c ui_strips.c ui_preview.c; do
    n=$((n+1))
    cmp -s "$BUILD/stock_c1/$b" "$BUILD/stock_c2/$b" || { echo "  NG: 再実行で出力が違う: $b"; rc=1; }
    tr -d '\r' < "$APP/$b" | cmp -s "$BUILD/stock_c1/$b" - || { echo "  NG: コミット済み src/app/$b がジェネレータの出力と違う(再生成が必要)"; rc=1; }
  done
  echo "  比較した C ファイル = $n(5)"
  echo "  生成物のコンパイル: gnu99 / c99 -pedantic-errors / c11、-Wall -Wextra -Wpedantic -Wconversion で警告ゼロ(APP_STK_PREVIEW = 0 / 1)"
  for std in "-std=gnu99" "-std=c99 -pedantic-errors" "-std=c11"; do
    for f in ui_layout ui_gfx_data ui_strips ui_preview; do
      cc_nowarn $std $WARN -I"$APP" -c "$APP/$f.c" -o "$BUILD/gen_$f.o" || rc=1
    done
    cc_nowarn $std $WARN -DAPP_STK_PREVIEW=1 -I"$APP" -c "$APP/ui_preview.c" -o "$BUILD/gen_ui_preview1.o" || rc=1
  done
  echo "  RAM に入るデータが無い(nm の D/d/B/b/C が無い。const は r)"
  if nm "$BUILD"/gen_ui_layout.o "$BUILD"/gen_ui_gfx_data.o "$BUILD"/gen_ui_strips.o "$BUILD"/gen_ui_preview1.o | grep -E ' [DdBbCc] ' | grep -vE ' [DdBbCc] \.'; then echo "  NG: RAM に入るデータがある"; rc=1; else echo "  (データシンボルなし)"; fi
  result S28 $rc
fi

# ---------------------------------------------------------------- S29
if sel S29; then
  echo "### S29 株価の数値(docs/design.md H-1・H-2・H-5): stk_core の標準 C 条件(gnu99 / c99 -pedantic-errors / c11、警告ゼロ)・float / double が無い・test_stk_core(前日比・騰落率の丸め、整形、市場の状態)"
  rc=0
  for std in "-std=gnu99" "-std=c99 -pedantic-errors" "-std=c11"; do
    cc_nowarn $std $WARN -I"$APP" -c "$APP/stk_core.c" -o "$BUILD/stk_core.o" || rc=1
  done
  echo "  stk_core.c / .h が platform.h / FIT ヘッダを include していないこと"
  if grep -nE '#\s*include\s*[<"](platform|board_pins|r_[a-z_0-9]+_if)\.h[>"]' "$APP/stk_core.c" "$APP/stk_core.h"; then rc=1; fi
  echo "  float / double が無い(stk_core / wx_core)・stk_core に浮動小数点の書式(%f %e %g)が無いこと(コメントを除いて grep。H-2)"
  for f in stk_core.c stk_core.h wx_core.c wx_core.h; do
    if "$CC" -x c -fpreprocessed -dD -E -P "$APP/$f" 2>/dev/null | grep -nE '\b(float|double)\b'; then echo "  NG: $f に float / double"; rc=1; fi
  done
  if "$CC" -x c -fpreprocessed -dD -E -P "$APP/stk_core.c" 2>/dev/null | grep -nE '%[-0-9.]*[feEgG]'; then echo "  NG: stk_core.c に浮動小数点の書式"; rc=1; fi   # wx_core.c の %5E は URL の符号化なので対象外
  echo "  グローバル/静的データが無いこと(nm。const は r)"
  if nm "$BUILD/stk_core.o" | grep -E ' [DdBbCc] ' | grep -vE ' [DdBbCc] \.'; then rc=1; else echo "  (データシンボルなし)"; fi
  cc_nowarn $STD -Wall -Wextra -Wconversion -I"$APP" -I"$HERE" "$HERE/test_stk_core.c" "$APP/stk_core.c" -o "$BUILD/test_stk_core" || rc=1
  [ -x "$BUILD/test_stk_core" ] && { timeout 300 "$BUILD/test_stk_core" || rc=1; }
  result S29 $rc
fi

# ---------------------------------------------------------------- S30
if sel S30; then
  echo "### S30 株価の応答の解析(docs/design.md H-3・H-4): test_wx_stock(実応答 6 つの期待値との一致・あらゆる長さに切る(禁止ページ)・合成の応答・HTTP の状態・date:・変異ファズ)と、保存した実応答に個人に紐づくヘッダーが無いこと"
  rc=0
  echo "  tests/host/data/yahoo_*.http のヘッダーは 状態行・date・content-type だけ(Set-Cookie・Cookie・x- や y- の ID などが無い)"
  nf=0
  for f in "$HERE"/data/yahoo_*.http; do
    nf=$((nf+1))
    bad="$(tr -d '\r' < "$f" | sed -n '1,/^$/p' | grep -vEi '^(HTTP/1\.0 200 OK|date: .*|content-type: .*|)$')"
    if [ -n "$bad" ]; then echo "  NG: $(basename "$f") のヘッダーに想定外の行: $bad"; rc=1; fi
    if grep -aiE 'set-cookie|^cookie' "$f" >/dev/null; then echo "  NG: $(basename "$f") に cookie"; rc=1; fi
  done
  echo "  保存した実応答 = $nf(6 のはず)"
  [ "$nf" -eq 6 ] || rc=1
  cc_nowarn $STD -Wall -Wextra -Wconversion -DAPP_WIFI_TEST=1 -I"$APP" -I"$HERE" "$HERE/test_wx_stock.c" "$APP/wx_core.c" "$APP/stk_core.c" -o "$BUILD/test_wx_stock" || rc=1
  [ -x "$BUILD/test_wx_stock" ] && { timeout 300 "$BUILD/test_wx_stock" "$HERE/data" || rc=1; }
  result S30 $rc
fi

# ---------------------------------------------------------------- S31
if sel S31; then
  echo "### S31 株価ビュー Phase 1・2 のテスト担当による独立試験(test_stk_indep): ±0.005% ちょうどの丸め・別の割り算との乱数比較・符号 + − ▲ ▼ の連結・状態ファイルの数値・市場の状態(別モデル・東京の 1 日・週末・NY)・描画の領域(箱の外に描かない・区切り線と左バーとフッターが無傷・箱が重ならない)・不正な表示キーの描画・色(RGB565 の厳密一致・コントラスト)・解析の差分ファズ"
  rc=0
  build_ui_objs || rc=1
  cc_nowarn $STD -Wall -Wextra -Wconversion -DAPP_WIFI_TEST=1 -I"$APP" -I"$HERE" "$HERE/test_stk_indep.c" "$APP/ui_render.c" "$APP/ui_logic.c" "$APP/stk_core.c" "$APP/gfx.c" "$APP/wx_core.c" "$UIOBJ/ui_gfx_data.o" "$UIOBJ/ui_layout.o" "$UIOBJ/ui_strips.o" -lm -o "$BUILD/test_stk_indep" || rc=1
  [ -x "$BUILD/test_stk_indep" ] && { timeout 300 "$BUILD/test_stk_indep" "$HERE/data" || rc=1; }
  result S31 $rc
fi

# S32(不具合の可能性の別枠)は、設計の修正で SUSPECT が 0 になったので、試験を S31(test_stk_indep の I)へ格上げして廃止した。
# 新しい「不具合の可能性」を見つけたら、落ちる試験を test_stk_indep の t_suspect に足し、ここへ(--suspect を走らせ、落ちたら SUSPECTS を増やす)作り直す。

# ---------------------------------------------------------------- S33
if sel S33; then
  echo "### S33 変異試験(Phase 3: 取得と予定表の試験が本当に効いているか。docs/design.md H-6・H-10): 製品コードの複製($BUILD/mut)に 1 か所だけ誤りを入れ、試験が失敗することを確かめる。製品のファイルは書き換えない"
  rc=0
  MUT="$BUILD/mut"
  mut_run() { # $1=名前 $2=変えるファイル $3=sed 式 $4=試験(.c) $5..=リンクするソース(MUT の中の名前)
    local name="$1" file="$2" expr="$3" tsrc="$4" f srcs=""
    shift 4
    rm -rf "$MUT"; mkdir -p "$MUT"
    cp "$APP/wx_core.h" "$APP/wx_core.c" "$APP/stk_core.h" "$APP/stk_core.c" "$APP/wifi_sw.h" "$APP/wifi_core.h" "$APP/wifi_core.c" "$MUT/"
    cp "$MUT/$file" "$MUT/$file.orig"
    sed -i "$expr" "$MUT/$file"
    if cmp -s "$MUT/$file" "$MUT/$file.orig"; then echo "  NG: 変異が当たっていない: $name"; rc=1; return; fi
    for f in "$@"; do srcs="$srcs $MUT/$f"; done
    if ! "$CC" $STD -DAPP_WIFI_TEST=1 -I"$MUT" -I"$APP" -I"$HERE" "$HERE/$tsrc" $srcs -o "$MUT/t" >"$MUT/cc.log" 2>&1; then echo "  NG: 変異版がコンパイルできない: $name"; head -5 "$MUT/cc.log"; rc=1; return; fi
    if timeout 300 "$MUT/t" >"$MUT/run.log" 2>&1; then echo "  NG: 変異 [$name] を試験が検出しなかった(合格してしまった)"; rc=1
    else echo "  ok: 変異 [$name] で試験が失敗した($(grep -c 'FAIL' "$MUT/run.log") 件)"; fi
  }
  echo "  まず無変更の複製で試験が通ること(変異試験の前提)"
  rm -rf "$MUT"; mkdir -p "$MUT"
  cp "$APP/wx_core.h" "$APP/wx_core.c" "$APP/stk_core.h" "$APP/stk_core.c" "$APP/wifi_sw.h" "$APP/wifi_core.h" "$APP/wifi_core.c" "$MUT/"
  "$CC" $STD -DAPP_WIFI_TEST=1 -I"$MUT" -I"$APP" -I"$HERE" "$HERE/test_wx_core.c" "$MUT/wx_core.c" "$MUT/stk_core.c" -o "$MUT/t0" >/dev/null 2>&1 && "$MUT/t0" >/dev/null 2>&1 || { echo "  NG: 無変更の複製で test_wx_core が通らない"; rc=1; }
  "$CC" $STD -DAPP_WIFI_TEST=1 -I"$MUT" -I"$APP" -I"$HERE" "$HERE/test_wifi_core.c" "$MUT/wifi_core.c" "$MUT/wx_core.c" "$MUT/stk_core.c" -o "$MUT/t1" >/dev/null 2>&1 && "$MUT/t1" >/dev/null 2>&1 || { echo "  NG: 無変更の複製で test_wifi_core が通らない"; rc=1; }
  # 予定表(H-6)
  mut_run "やり直しの回数 2 回 → 3 回"        wx_core.c 's/s->retry_n < WX_RETRY_MAX/s->retry_n <= WX_RETRY_MAX/' test_wx_core.c wx_core.c stk_core.c
  mut_run "429 の待ち 300 秒 → 20 秒"          wx_core.c 's/wait_ms(s, WX_ACT_FETCH, WX_RATE_MS, now_ms)/wait_ms(s, WX_ACT_FETCH, WX_RETRY_MS, now_ms)/' test_wx_core.c wx_core.c stk_core.c
  mut_run "周期の判定 >= 10 分 → > 10 分"      wx_core.c 's/(uint32_t)(now_ms - s->t_fetch0) >= WX_INTERVAL_MS/(uint32_t)(now_ms - s->t_fetch0) > WX_INTERVAL_MS/' test_wx_core.c wx_core.c stk_core.c
  mut_run "全部失敗の再接続の条件(応答なしだけ)を外す" wx_core.c 's/(s->nomask == WX_SYM_ALL)/(1)/' test_wx_core.c wx_core.c stk_core.c
  mut_run "周期の始まりを銘柄 1 からにする"    wx_core.c 's/s->todo      = (uint8_t)WX_SYM_ALL;/s->todo      = (uint8_t)(WX_SYM_ALL \& 6u);/' test_wx_core.c wx_core.c stk_core.c
  # wifi_core(H-10)
  mut_run "SSL の接続の待ち 20 秒 → 10 秒"      wifi_core.c 's/#define SSL_TCP_TIMEOUT_MS (20000u)/#define SSL_TCP_TIMEOUT_MS (10000u)/' test_wifi_core.c wifi_core.c wx_core.c stk_core.c
  mut_run "ready の err=9 を err=8 にする"      wifi_core.c 's/c->wx.err  = 9u;/c->wx.err  = 8u;/' test_wifi_core.c wifi_core.c wx_core.c stk_core.c
  mut_run "結果に銘柄 sym を入れない"           wifi_core.c 's/c->wx.sym      = c->sym;//' test_wifi_core.c wifi_core.c wx_core.c stk_core.c
  # 書式(H-11)
  mut_run "[stk] res=ok のキー名 http= を変える" wx_core.c 's/ http=%u ms=%lu sync=/ htp=%u ms=%lu sync=/' test_wx_core.c wx_core.c stk_core.c
  result S33 $rc
fi

# ---------------------------------------------------------------- S34
if sel S34; then
  echo "### S34 app_main.c の結合試験(Phase 4。H-12・H-13): 偽の LCD・タッチ・CMT・ログ・wifi_at API で app_main() を 10ms ずつ走らせ、起動の行の順・面 0 を lcd_init の前に描く・stk_ui の値が ui_render で描かれ変わった時だけ入れ替わる・[ev] btn=tap src=touch|uart と wx_request(src)・s ? と n p d + - の扱い・[hb] を、製品 / APP_WX=0 / APP_STK_PREVIEW=1 / LCD 初期化失敗 / タッチ初期化失敗 の 5 通りで確かめる"
  rc=0
  build_ui_objs || rc=1
  A34="$APP/ui_render.c $APP/ui_logic.c $APP/stk_core.c $APP/gfx.c $APP/app_logfmt.c $APP/ui_preview.c $UIOBJ/ui_gfx_data.o $UIOBJ/ui_layout.o $UIOBJ/ui_strips.o"
  for v in "prod:" "nowx:-DAPP_WX=0" "preview:-DAPP_STK_PREVIEW=1" "lcdfail:-DT_LCD_FAIL" "touchfail:-DT_TOUCH_FAIL"; do
    name="${v%%:*}"; flags="${v#*:}"
    echo "  -- $name ${flags}"
    EXTRA_CFLAGS="$flags" build_fit_test "$BUILD/test_app_main_$name" "$HERE/test_app_main.c" $A34 || rc=1
    [ -x "$BUILD/test_app_main_$name" ] && { timeout 300 "$BUILD/test_app_main_$name" | tail -3 | grep -E "FAIL|checks" ; [ "${PIPESTATUS[0]}" -eq 0 ] || rc=1; }
  done
  result S34 $rc
fi

# ---------------------------------------------------------------- S35
if sel S35; then
  echo "### S35 APP_STK_FAULT(docs/design.md §7.4・受入 U-6): 0〜3 で wx_build_request が、銘柄 n-1 だけ銘柄コードの後ろに ZZ を付け(長さ +2)、ほかの銘柄は固定の要求(144 / 145 / 145 バイト)のまま。4 は #error"
  rc=0
  for n in 0 1 2 3; do
    cc_nowarn $STD $WARN -DAPP_WX=1 -DAPP_STK_FAULT=$n -I"$APP" -I"$HERE" "$HERE/test_stk_fault.c" "$APP/wx_core.c" "$APP/stk_core.c" -o "$BUILD/test_stk_fault$n" || rc=1
    [ -x "$BUILD/test_stk_fault$n" ] && { "$BUILD/test_stk_fault$n" | tail -2 | grep -E "FAIL|APP_STK_FAULT|checks"; [ "${PIPESTATUS[0]}" -eq 0 ] || rc=1; }
  done
  result S35 $rc
fi

# ---------------------------------------------------------------- S36
if sel S36; then
  echo "### S36 変異試験(Phase 4): 製品コードの複製に 1 か所だけ誤りを入れ、試験が失敗することを確かめる(状態の優先順位 = フッター、更新要求の最小間隔、タップの src、stk_ui → 表示へのつなぎ)。製品のファイルは書き換えない"
  rc=0
  MUT2="$BUILD/mut2"
  mut2_prep() { # $1=名前 $2=APP の中のファイル $3=sed 式。$MUT2 に変異版を作る。当たらなければ失敗
    rm -rf "$MUT2"; mkdir -p "$MUT2/src/app" "$MUT2/tests/host"
    find "$APP" -maxdepth 1 -name '*.h' ! -name wifi_secret.h -exec cp {} "$MUT2/src/app/" \;     # wifi_secret.h は複製しない
    cp "$HERE/test_framework.h" "$HERE/test_app_main.c" "$MUT2/tests/host/"
    cp "$APP/$2" "$MUT2/src/app/$2"; cp "$MUT2/src/app/$2" "$MUT2/mut.orig"
    sed -i "$3" "$MUT2/src/app/$2"
    if cmp -s "$MUT2/src/app/$2" "$MUT2/mut.orig"; then echo "  NG: 変異が当たっていない: $1"; rc=1; return 1; fi
    return 0
  }
  mut2_verdict() { # $1=名前 $2=試験の終了コード
    if [ "$2" -eq 0 ]; then echo "  NG: 変異 [$1] を試験が検出しなかった(合格してしまった)"; rc=1; else echo "  ok: 変異 [$1] で試験が失敗した"; fi
  }
  build_ui_objs || rc=1
  # (1) フッターの優先順位: 起動中(F1)は取得中(F2)より強い。変異 = 取得中なら起動中にしない
  if mut2_prep "フッターの優先順位(起動中 > 取得中)を入れ替える" ui_render.c 's/if (v->have_any == 0u) {/if ((v->have_any == 0u) \&\& (v->busy == 0u)) {/'; then
    "$CC" $STD -DAPP_STK_PREVIEW=1 -I"$MUT2/src/app" -I"$HERE" "$HERE/test_ui_stock.c" "$MUT2/src/app/ui_render.c" "$APP/ui_logic.c" "$APP/stk_core.c" "$APP/gfx.c" "$APP/ui_preview.c" "$UIOBJ"/*.o -o "$MUT2/t" >"$MUT2/cc.log" 2>&1 || { echo "  NG: 変異版がコンパイルできない"; head -5 "$MUT2/cc.log"; rc=1; }
    if [ -x "$MUT2/t" ]; then "$MUT2/t" "$HERE/data/states" >"$MUT2/run.log" 2>&1; mut2_verdict "フッターの優先順位" $?; fi
  fi
  # (2) 更新要求の最小間隔(5 秒): 変異 = 間隔を見ない
  rm -rf "$MUT2"; mkdir -p "$MUT2"
  cp "$APP/wx_core.h" "$APP/wx_core.c" "$APP/stk_core.h" "$APP/stk_core.c" "$APP/wifi_sw.h" "$APP/wifi_core.h" "$APP/wifi_core.c" "$MUT2/"
  cp "$MUT2/wx_core.c" "$MUT2/wx_core.c.orig"
  sed -i 's/< WX_MIN_GAP_MS) {/< 0u) {/' "$MUT2/wx_core.c"
  if cmp -s "$MUT2/wx_core.c" "$MUT2/wx_core.c.orig"; then echo "  NG: 変異が当たっていない: 更新要求の最小間隔"; rc=1
  else
    "$CC" $STD -DAPP_WIFI_TEST=1 -I"$MUT2" -I"$APP" -I"$HERE" "$HERE/test_wx_core.c" "$MUT2/wx_core.c" "$MUT2/stk_core.c" -o "$MUT2/t" >"$MUT2/cc.log" 2>&1 || { echo "  NG: 変異版がコンパイルできない"; head -5 "$MUT2/cc.log"; rc=1; }
    if [ -x "$MUT2/t" ]; then "$MUT2/t" >"$MUT2/run.log" 2>&1; mut2_verdict "更新要求の最小間隔(5 秒)を外す" $?; fi
  fi
  # (3)(4) app_main: タップの src の取り違え、stk_ui の値を表示へ渡さない
  A36="$APP/ui_render.c $APP/ui_logic.c $APP/stk_core.c $APP/gfx.c $APP/app_logfmt.c $APP/ui_preview.c $UIOBJ/ui_gfx_data.o $UIOBJ/ui_layout.o $UIOBJ/ui_strips.o"
  run_main_mut() { # $1=名前 $2=sed 式
    if mut2_prep "$1" app_main.c "$2"; then
      EXTRA_CFLAGS="" build_fit_test "$MUT2/t" "$MUT2/tests/host/test_app_main.c" $A36 >"$MUT2/cc.log" 2>&1 || { echo "  NG: 変異版がコンパイルできない"; head -5 "$MUT2/cc.log"; rc=1; return; }
      "$MUT2/t" >"$MUT2/run.log" 2>&1; mut2_verdict "$1" $?
    fi
  }
  run_main_mut "タップの src(touch / uart)を取り違える" 's/(e->src == (uint8_t)UI_SRC_TOUCH) ? "touch" : "uart"/(e->src == (uint8_t)UI_SRC_TOUCH) ? "uart" : "touch"/'
  run_main_mut "stk_ui の値を ui_set_view に渡さない" 's/^    ui_set_view(&s_ui, &s_view);$/    (void)s_view;/'
  result S36 $rc
fi

# ---------------------------------------------------------------- S37
if sel S37; then
  echo "### S37 旧 UI の残骸が無いこと(Phase 4。docs/design.md §7.1): 削除するファイル・シンボル(clut8_* / nav_t / nav_update / wx_ui / ui_wx_t / APP_CLUT8_TEST / applog_fmt_ui)が src/app・tools・tests/host に残っていない"
  rc=0
  for f in src/app/nav_logic.c src/app/clut8_test.c src/app/clut8_test.h src/app/clut8_pat.c src/app/clut8_pat.h src/app/clut8_sw.h tools/gen_ui_assets.py tools/fb2png.py tools/wx_compare_p2b.py tools/wx_mock.py tools/clut8_ref.c tests/host/judge_ut1.py tests/host/test_nav.c tests/host/test_nav_step.c tests/host/test_clut8_cfg.c tests/host/test_clut8_pat.c; do
    [ -e "$ROOT/$f" ] && { echo "  NG: 削除するはずの $f が残っている"; rc=1; }
  done
  hits="$(grep -rnE '\b(nav_t|nav_init|nav_update|nav_step|nav_index|ui_wx_t|wx_ui|APP_CLUT8_TEST|clut8_test|clut8_pat|applog_fmt_ui|applog_ui_t|APPLOG_CMD_(PLUS|MINUS))\b' "$APP" "$ROOT/tools" "$HERE" --include='*.c' --include='*.h' --include='*.py' --include='*.sh' --exclude=run.sh 2>/dev/null | grep -vE 'wifi_secret\.h')"
  if [ -n "$hits" ]; then echo "$hits" | head -10; echo "  NG: 旧 UI のシンボルが残っている"; rc=1; else echo "  (旧 UI のシンボルなし)"; fi
  result S37 $rc
fi

# ---------------------------------------------------------------- S38
if sel S38; then
  echo "### S38 時計・天気画面のアセット生成(docs/design.md §13、H-C8 の前半): gen_clock_assets.py --check(パレット 8 枚 226 色・誤差 0・字・アイコンの層・配置・コントラスト)/ 2 回の生成で同一 / コミット済みのプレビュー PNG と一致 / 株価の生成物が変わらない"
  rc=0
  "$PY" -I "$ROOT/tools/gen_clock_assets.py" --check || rc=1
  rm -rf "$BUILD/clk_prev1" "$BUILD/clk_prev2" "$BUILD/clk_c8a" "$BUILD/clk_c8b"; mkdir -p "$BUILD/clk_prev1" "$BUILD/clk_prev2"
  "$PY" -I "$ROOT/tools/gen_clock_assets.py" --preview-dir "$BUILD/clk_prev1" --emit-c8 "$BUILD/clk_c8a" >/dev/null || rc=1
  "$PY" -I "$ROOT/tools/gen_clock_assets.py" --preview-dir "$BUILD/clk_prev2" --emit-c8 "$BUILD/clk_c8b" >/dev/null || rc=1
  n=0
  for f in "$BUILD"/clk_prev1/*.png; do
    b="$(basename "$f")"; n=$((n+1))
    cmp -s "$f" "$BUILD/clk_prev2/$b" || { echo "  NG: 再実行で出力が違う: $b"; rc=1; }
    cmp -s "$f" "$ROOT/tools/assets/clock/preview/$b" || { echo "  NG: コミット済み tools/assets/clock/preview/$b が生成物と違う(再生成が必要)"; rc=1; }
  done
  echo "  比較したプレビュー PNG = $n(sheet + icons + state_C0〜C11 + palette 8 = 22)"
  [ "$n" -eq 22 ] || rc=1
  m=0
  for f in "$BUILD"/clk_c8a/*; do
    m=$((m+1)); cmp -s "$f" "$BUILD/clk_c8b/$(basename "$f")" || { echo "  NG: 再実行で .c8 / .pal が違う: $(basename "$f")"; rc=1; }
  done
  echo "  2 回の --emit-c8 で同一のファイル = $m(C0〜C11.c8 12 + clk_*.pal 8 = 20)"
  [ "$m" -eq 20 ] || rc=1
  "$PY" -I "$ROOT/tools/gen_stock_assets.py" --check | grep -E '^出力の sha256' | sed 's/(2 回.*//'
  result S38 $rc
fi

echo "================ 集計: PASS=$PASSES FAIL=$FAILS SKIP(未実施)=$SKIPS SUSPECT(不具合の可能性)=$SUSPECTS ${FAILED_LIST:+(失敗:$FAILED_LIST)}"
[ $FAILS -eq 0 ]
