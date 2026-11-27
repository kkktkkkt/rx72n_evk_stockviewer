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

FAILS=0; PASSES=0; SKIPS=0; FAILED_LIST=""
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

STDC_FILES="nav_logic touch_decode app_logfmt app_logq gfx ui_logic ui_render"

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
  echo "### S2 nav_logic 単体テスト(H1〜H7): 左右半分モード + 次へのみモード(v2 では app_main から使わない。P2 用に残す)"
  rc=0
  cc_nowarn $STD $WARN -I"$APP" -I"$HERE" "$HERE/test_nav.c" "$APP/nav_logic.c" -o "$BUILD/test_nav" || rc=1
  [ $rc -eq 0 ] && { "$BUILD/test_nav" || rc=1; }
  cc_nowarn $STD $WARN -DNAV_MODE=1 -I"$APP" -I"$HERE" "$HERE/test_nav.c" "$APP/nav_logic.c" -o "$BUILD/test_nav_next" || rc=1
  [ -x "$BUILD/test_nav_next" ] && { "$BUILD/test_nav_next" || rc=1; }
  result S2 $rc
fi

# ---------------------------------------------------------------- S3
if sel S3; then
  echo "### S3 touch_decode 単体テスト(H8)"
  rc=0
  cc_nowarn $STD $WARN -I"$APP" -I"$HERE" "$HERE/test_touch_decode.c" "$APP/touch_decode.c" "$APP/nav_logic.c" -o "$BUILD/test_touch_decode" || rc=1
  [ $rc -eq 0 ] && { "$BUILD/test_touch_decode" || rc=1; }
  result S3 $rc
fi

# S4: 旧 UI の画面アセット(gen_ui_assets.py)の検査。S28(gen_stock_assets.py)に置き換え

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
  echo "### S8 app_logfmt 単体テスト(U-H1〜U-H6 + design-uart.md §10.6: 各行の文字列・境界値・小バッファ・build・コマンド・周期・新しい [ev] [ui] [perf])"
  rc=0
  cc_nowarn $STD $WARN -I"$APP" -I"$HERE" "$HERE/test_logfmt.c" "$APP/app_logfmt.c" -o "$BUILD/test_logfmt" || rc=1
  [ $rc -eq 0 ] && { "$BUILD/test_logfmt" || rc=1; }
  result S8 $rc
fi

# ---------------------------------------------------------------- S9
if sel S9; then
  echo "### S9 app_logq 単体テスト(U-H7)と nav_step 単体テスト(U-H8)"
  rc=0
  cc_nowarn $STD $WARN -I"$APP" -I"$HERE" "$HERE/test_logq.c" "$APP/app_logq.c" -o "$BUILD/test_logq" || rc=1
  [ -x "$BUILD/test_logq" ] && { "$BUILD/test_logq" || rc=1; }
  cc_nowarn $STD $WARN -I"$APP" -I"$HERE" "$HERE/test_nav_step.c" "$APP/nav_logic.c" -o "$BUILD/test_nav_step" || rc=1
  [ -x "$BUILD/test_nav_step" ] && { "$BUILD/test_nav_step" || rc=1; }
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

# S11: 旧 UI の app_main 結合試験(test_app_main.c)。旧 UI 前提のため Phase 1 で廃止。Phase 4 の結合で新しく書く

# ---------------------------------------------------------------- S12
if sel S12; then
  echo "### S12 -fsanitize=address,undefined で S8〜S10 と S2/S3/S13/S14 のテストを再実行(1件でも検出したら失敗)"
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
    cc_nowarn $STD $WARN $SAN -I"$APP" -I"$HERE" "$HERE/test_nav_step.c" "$APP/nav_logic.c" -o "$BUILD/san_nav_step" || rc=1
    [ -x "$BUILD/san_nav_step" ] && run_san nav_step "$BUILD/san_nav_step"
    cc_nowarn $STD $WARN $SAN -I"$APP" -I"$HERE" "$HERE/test_nav.c" "$APP/nav_logic.c" -o "$BUILD/san_nav" || rc=1
    [ -x "$BUILD/san_nav" ] && run_san nav "$BUILD/san_nav"
    cc_nowarn $STD $WARN $SAN -I"$APP" -I"$HERE" "$HERE/test_touch_decode.c" "$APP/touch_decode.c" "$APP/nav_logic.c" -o "$BUILD/san_touch" || rc=1
    [ -x "$BUILD/san_touch" ] && run_san touch_decode "$BUILD/san_touch"
    cc_nowarn $STD $WARN $SAN -I"$APP" -I"$HERE" "$HERE/test_gfx.c" "$APP/gfx.c" -o "$BUILD/san_gfx" || rc=1
    [ -x "$BUILD/san_gfx" ] && run_san gfx "$BUILD/san_gfx"
    cc_nowarn $STD $WARN $SAN -DAPP_STK_PREVIEW=1 -I"$APP" -I"$HERE" "$HERE/test_ui_stock.c" "$APP/ui_render.c" "$APP/ui_logic.c" "$APP/gfx.c" "$APP/ui_preview.c" "$UIOBJ"/*.o -o "$BUILD/san_ui_stock" || rc=1
    [ -x "$BUILD/san_ui_stock" ] && { echo "  [san] ui_stock"; timeout 300 "$BUILD/san_ui_stock" "$HERE/data/states" > "$BUILD/ui_stock.san.log" 2>&1 || { head -30 "$BUILD/ui_stock.san.log"; rc=1; }; tail -1 "$BUILD/ui_stock.san.log"; }
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
  cc_nowarn $STD $WARN -DAPP_STK_PREVIEW=1 -I"$APP" -I"$HERE" "$HERE/test_ui_stock.c" "$APP/ui_render.c" "$APP/ui_logic.c" "$APP/gfx.c" "$APP/ui_preview.c" "$UIOBJ"/*.o -o "$BUILD/test_ui_stock" || rc=1
  [ -x "$BUILD/test_ui_stock" ] && { "$BUILD/test_ui_stock" "$HERE/data/states" || rc=1; }
  cc_nowarn $STD $WARN -I"$APP" -c "$ROOT/tools/fb_hostrender.c" -o "$BUILD/fb_hostrender.o" || rc=1
  cc_nowarn $STD -O1 -I"$APP" "$ROOT/tools/fb_hostrender.c" "$APP/ui_render.c" "$APP/ui_logic.c" "$APP/gfx.c" "$UIOBJ"/*.o -o "$BUILD/fb_hostrender" || rc=1
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

# ---------------------------------------------------------------- S18
if sel S18; then
  echo "### S18 CLUT8 実験(docs/design.md §19): clut8_pat(H-C8-1〜4, 7)、標準C条件(H-C8-5)、clut8_test + lcd_panel の cfg と段階 0〜5 の模擬走行(H-C8-6)、APP_CLUT8_TEST = 0 で中身が消えること"
  rc=0
  build_ui_objs || rc=1      # test_clut8_cfg が lcd_panel.c(ui_pal を参照)をリンクする
  for std in "-std=gnu99" "-std=c99 -pedantic-errors" "-std=c11"; do
    cc_nowarn $std $WARN -DAPP_CLUT8_TEST=1 -I"$APP" -c "$APP/clut8_pat.c" -o "$BUILD/clut8_pat.o" || rc=1
  done
  echo "  H-C8-5: clut8_pat.c/.h と clut8_sw.h が platform.h / FIT ヘッダを include していないこと"
  if grep -nE '#\s*include\s*[<"](platform|board_pins|r_[a-z_0-9]+_if)\.h[>"]' "$APP/clut8_pat.c" "$APP/clut8_pat.h" "$APP/clut8_sw.h"; then rc=1; fi
  echo "  グローバル/静的データが無いこと(nm。const の c8pat_box_idx は r)"
  if nm "$BUILD/clut8_pat.o" | grep -E ' [DdBbCc] ' | grep -vE ' [DdBbCc] \.'; then rc=1; else echo "  (データシンボルなし)"; fi
  echo "  APP_CLUT8_TEST = 0(既定)では clut8_pat.o が空(.text 等のシンボルなし)"
  cc_nowarn $STD $WARN -I"$APP" -c "$APP/clut8_pat.c" -o "$BUILD/clut8_pat0.o" || rc=1
  if nm "$BUILD/clut8_pat0.o" | grep -E ' [TtDdRrBbCc] ' | grep -vE ' [TtDdRrBbCc] \.'; then echo "  NG: 0 でシンボルが残る"; rc=1; fi
  cc_nowarn $STD $WARN -DAPP_CLUT8_TEST=1 -I"$APP" -I"$HERE" "$HERE/test_clut8_pat.c" "$APP/clut8_pat.c" -lm -o "$BUILD/test_clut8_pat" || rc=1
  [ -x "$BUILD/test_clut8_pat" ] && { "$BUILD/test_clut8_pat" || rc=1; }
  out="$("$CC" $STD -Wall -Wextra $RXDEF -DAPP_CLUT8_TEST=1 -ffunction-sections -fdata-sections -I"$APP" -I"$HERE" $FIT_INC         "$HERE/test_clut8_cfg.c" "$APP/clut8_pat.c" "$APP/app_logfmt.c" "$SMC/r_glcdc_rx/src/r_glcdc_private.c" "$UIOBJ"/*.o         -Wl,--gc-sections $LDX -o "$BUILD/test_clut8_cfg" 2>&1)"; crc=$?
  echo "$out" | grep -E "(src/app/|tests/host/)" | head -20
  [ $crc -eq 0 ] || { echo "$out" | head -30; rc=1; }
  if echo "$out" | grep -E "(src/app/|tests/host/).*warning" >/dev/null; then echo "  NG: 製品コード・試験コード側に警告"; rc=1; fi
  [ -x "$BUILD/test_clut8_cfg" ] && { timeout 300 "$BUILD/test_clut8_cfg" || rc=1; }
  result S18 $rc
fi

# S19: 旧 UI の試験。Phase 1(株価ビュー)で廃止(docs/design.md §7.1)

# S20: 旧 UI の試験。Phase 1(株価ビュー)で廃止(docs/design.md §7.1)

# S21: 旧 UI の試験。Phase 1(株価ビュー)で廃止(docs/design.md §7.1)

# ---------------------------------------------------------------- S22
if sel S22; then
  echo "### S22 Wi-Fi(ESP32 AT)試作(docs/design.md §21.7 の H-W1〜H-W13): wifi_core の標準C条件・APP_WIFI_TEST = 0 で中身が消える・単体試験(行・分類・値・CWJAP・書式・状態機械・秘密がログに出ない)・静的検査(wifi_secret.h の include 範囲、.gitignore)"
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
  echo "  APP_WIFI_TEST = 0 かつ APP_WX = 0(既定)では wifi_core.o / wx_core.o が空(.text 等のシンボルなし)。APP_WX = 1 だけでは中身がある"
  cc_nowarn $STD $WARN -I"$APP" -c "$APP/wifi_core.c" -o "$BUILD/wifi_core0.o" || rc=1
  cc_nowarn $STD $WARN -I"$APP" -c "$APP/wx_core.c" -o "$BUILD/wx_core0.o" || rc=1
  if nm "$BUILD/wifi_core0.o" "$BUILD/wx_core0.o" | grep -E ' [TtDdRrBbCc] ' | grep -vE ' [TtDdRrBbCc] \.'; then echo "  NG: 0 でシンボルが残る"; rc=1; fi
  if ! nm "$BUILD/wifi_core_wx.o" "$BUILD/wx_core_wx.o" | grep -qE ' [Tt] (wifi_core_start|wx_parse_http)'; then echo "  NG: APP_WX = 1 で中身が無い"; rc=1; fi
  echo "  APP_WIFI_TEST と APP_CLUT8_TEST が両方 1 なら #error"
  if echo '#include "wifi_sw.h"' | "$CC" -fsyntax-only -x c -DAPP_WIFI_TEST=1 -DAPP_CLUT8_TEST=1 -I"$APP" - >/dev/null 2>&1; then echo "  NG: #error が出ない"; rc=1; else echo "  ok (#error 発生)"; fi
  cc_nowarn $STD $WARN -DAPP_WIFI_TEST=1 -I"$APP" -I"$HERE" "$HERE/test_wifi_core.c" "$APP/wifi_core.c" "$APP/wx_core.c" -o "$BUILD/test_wifi_core" || rc=1
  [ -x "$BUILD/test_wifi_core" ] && { timeout 300 "$BUILD/test_wifi_core" || rc=1; }
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
  echo "### S23 Wi-Fi(ESP32 AT)試作のハード側(wifi_at.c。docs/design.md §21.4): APP_WIFI_TEST = 0 で中身が消える・app_main.c の構文・SCI1 と EN と専用ループを偽の r_sci_rx / GPIO / CMT / ESP32 で試験(既定 / 秘密なし / 自動開始の3種)・秘密の置き場所"
  rc=0
  echo "  APP_WIFI_TEST = 0(既定。APP_WX = 0)では wifi_at.o が空(実 platform.h + FIT ヘッダ)"
  o="$("$CC" $STD -Wall -Wextra $RXDEF -I"$APP" $FIT_INC -c "$APP/wifi_at.c" -o "$BUILD/wifi_at0.o" 2>&1)"; crc=$?
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
  W23="$APP/wifi_core.c $APP/wx_core.c $APP/app_logfmt.c"
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
  echo "### S24 天気・気温の取得(docs/design.md §22.12 の H-X1〜H-X4・H-X7・H-X11): wx_core の単体試験(要求 190 バイト・HTTP 解析・Date: と時刻・WMO 表・ソフト時計・ログ書式・取得の予定表)。wifi_core の WX step・データモード・範囲(H-X5・H-X6)は S22、wifi_at の x は S23"
  rc=0
  cc_nowarn $STD $WARN -DAPP_WIFI_TEST=1 -I"$APP" -I"$HERE" "$HERE/test_wx_core.c" "$APP/wx_core.c" -o "$BUILD/test_wx_core" || rc=1
  [ -x "$BUILD/test_wx_core" ] && { timeout 300 "$BUILD/test_wx_core" || rc=1; }
  result S24 $rc
fi

# S25: 旧 UI の試験。Phase 1(株価ビュー)で廃止(docs/design.md §7.1)

# ---------------------------------------------------------------- S26
if sel S26; then
  echo "### S26 天気・時刻同期の製品への組み込み(docs/design.md §22.7・§22.12・§22.16 の順 5): APP_WX = 1(APP_WIFI_TEST = 0)の wifi_at.c(wx_init / wx_poll / wx_request / wx_ui / wx_log_state)を偽の ESP32 で試験(秘密あり / なし)、app_main.c の組み込み(偽の wifi_at API)、APP_WX = 0 / APP_WIFI_TEST = 0 の組み合わせで中身が消えること"
  rc=0
  echo "  APP_WX = 1 かつ APP_WIFI_TEST = 0 の wifi_at.c(WIFI_AT_HOST_TEST。秘密あり / なし)・app_main.c の -fsyntax-only"
  for f in app_main wifi_at; do
    for sec in 1 0; do
      if [ $sec = 1 ]; then SECF="-DWIFI_SSID=\"s\" -DWIFI_PASS=\"p\""; else SECF=""; fi
      o="$("$CC" -fsyntax-only $STD -Wall -Wextra $RXDEF -DAPP_WX=1 -DWIFI_AT_HOST_TEST $SECF -I"$APP" $FIT_INC "$APP/$f.c" 2>&1)"; crc=$?
      if [ $crc -ne 0 ] || echo "$o" | grep -E "src/app/.*(warning|error)" >/dev/null; then echo "  NG $f.c (secret=$sec)"; echo "$o" | head -10; rc=1; else echo "  ok $f.c (secret=$sec)"; fi
    done
  done
  echo "  APP_WX = 1 だけ(APP_WIFI_TEST = 0)では wifi_at.o に製品の API が入り、専用モード(wifi_test_main)は入らない。APP_WX = 0 かつ APP_WIFI_TEST = 0 では空(S23)"
  o="$("$CC" $STD -Wall -Wextra $RXDEF -DAPP_WX=1 -DWIFI_AT_HOST_TEST -I"$APP" $FIT_INC -c "$APP/wifi_at.c" -o "$BUILD/wifi_at_wx.o" 2>&1)"; crc=$?
  [ $crc -eq 0 ] || { echo "$o" | head; rc=1; }
  for sym in wx_init wx_poll wx_request wx_ui wx_log_state; do nm "$BUILD/wifi_at_wx.o" | grep -qE " [Tt] _?$sym\$" || { echo "  NG: $sym が無い"; rc=1; }; done
  if nm "$BUILD/wifi_at_wx.o" | grep -qE " [Tt] _?wifi_test_main\$"; then echo "  NG: APP_WX = 1 だけで wifi_test_main が入っている"; rc=1; fi
  echo "  結合試験 test_wifi_at.c の製品版(2種: 秘密あり / -DT_NOSECRET)"
  W23="$APP/wifi_core.c $APP/wx_core.c $APP/app_logfmt.c"
  EXTRA_CFLAGS="-DAPP_WX=1 -DWIFI_AT_HOST_TEST" build_fit_test "$BUILD/test_wifi_at_wx" "$HERE/test_wifi_at.c" $W23 || rc=1
  [ -x "$BUILD/test_wifi_at_wx" ] && { timeout 300 "$BUILD/test_wifi_at_wx" || rc=1; }
  EXTRA_CFLAGS="-DAPP_WX=1 -DWIFI_AT_HOST_TEST -DT_NOSECRET" build_fit_test "$BUILD/test_wifi_at_wx_ns" "$HERE/test_wifi_at.c" $W23 || rc=1
  [ -x "$BUILD/test_wifi_at_wx_ns" ] && { timeout 300 "$BUILD/test_wifi_at_wx_ns" || rc=1; }
  echo "  wifi_core の quiet(製品: res=start と key=oth を出さない)は S22(test_wifi_core.c)"
  result S26 $rc
fi

# ---------------------------------------------------------------- S27
if sel S27; then
  echo "### S27 天気・時刻同期の独立試験(テスト担当の追加): test_wx_indep(ガードページによる範囲外読み出しの検出・全日の暦・ソフト時計・予定表の乱数走行(ms の折り返し)・wifi_core の乱数入力)と、test_ui_geom(当たり判定・要素の配置・スプライトの継ぎ目と区別)"
  rc=0
  cc_nowarn $STD -Wall -Wextra -DAPP_WX=1 -I"$APP" -I"$HERE" "$HERE/test_wx_indep.c" "$APP/wx_core.c" "$APP/wifi_core.c" -o "$BUILD/test_wx_indep" || rc=1
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
  echo "  gen_ui_assets.py(旧)は Phase 4 まで残すが、src/app に書くと生成物を壊すので実行しない"
  result S28 $rc
fi

echo "================ 集計: PASS=$PASSES FAIL=$FAILS SKIP(未実施)=$SKIPS ${FAILED_LIST:+(失敗:$FAILED_LIST)}"
[ $FAILS -eq 0 ]
