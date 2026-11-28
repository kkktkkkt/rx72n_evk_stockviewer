param([Parameter(Mandatory = $true)][string]$Name,   # jobs\ut\<Name>.log に出す
      [int]$Seconds = 15,                              # リセット解除(rfp 終了)からの受信秒数
      [string]$Tx = '', [double]$TxAfterSec = 3, [int]$TxGapMs = 700,   # 送信(許可文字 [npsd+-?x])。TxAfterSec はリセット解除からの秒
      [string]$Tx2 = '', [double]$Tx2AfterSec = 0,   # 2回目の送信(1回の書込み)。TxAfterSec と別に、リセット解除からの秒で指定する(押下の窓を狙う時)
      [switch]$NoReset,                                # 付けると rfp-cli の reset-run をしない(走行中のまま、開始から計る)
      [string]$Port = 'COM7')
# 単体の実機 UART 試験(docs/design-uart.md §10.8)。COM7 を先に開き、rfp-cli -sig -run でリセット解除(約3秒)し、受信行を時刻付きで記録する。
# 前提: e2studio / e2-server-gdb が居ない。書込みなし。終了状態: 走行中。
$ErrorActionPreference = 'Stop'
if ($Tx -ne '' -and $Tx -cnotmatch '^[npsd+\-?x]{1,200}$') { throw "Tx は [npsd+-?x] のみ: $Tx" }
$log = Join-Path 'D:\workspace\rx72n\rx72n_stockview\jobs\ut' "$Name.log"
'' | Out-File $log -Encoding utf8
function L($s) { ((Get-Date -Format 'HH:mm:ss.fff') + ' ' + $s) | Out-File $log -Append -Encoding utf8 }
$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'; $sp.ReadTimeout = 50; $sp.Open()
$buf = ''
function Pump { $s = $sp.ReadExisting(); if ($s) { $script:buf += $s; while (($i = $script:buf.IndexOf("`n")) -ge 0) { L ($script:buf.Substring(0, $i).TrimEnd("`r")); $script:buf = $script:buf.Substring($i + 1) } } }
try {
    if (-not $NoReset) {
        $rfp = Start-Process 'C:\Program Files (x86)\Renesas Electronics\Programming Tools\Renesas Flash Programmer V3.25\rfp-cli.exe' -PassThru -NoNewWindow `
            -ArgumentList '-d RX72x -t e2l -if fine -auth id FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF -noquery -noprogress -sig -run' -RedirectStandardOutput "$env:TEMP\ut-rfp.txt" -RedirectStandardError "$env:TEMP\ut-rfp.err"
        while (-not $rfp.WaitForExit(30)) { Pump }
        L '# reset released (rfp-cli 終了)'
    } else { L '# no reset' }
    $t0 = Get-Date; $end = $t0.AddSeconds($Seconds); $txi = 0; $next = $t0.AddSeconds($TxAfterSec); $tx2done = ($Tx2 -eq '')
    while ((Get-Date) -lt $end) {
        if ($Tx -ne '' -and $txi -lt $Tx.Length -and (Get-Date) -ge $next) {
            if ($TxGapMs -eq 0) { $sp.Write($Tx); $txi = $Tx.Length; L "# tx all ($($Tx.Length) chars, 1 write)" }
            else { $c = [string]$Tx[$txi]; $sp.Write($c); $txi++; L "# tx '$c'"; $next = (Get-Date).AddMilliseconds($TxGapMs) }
        }
        if (-not $tx2done -and ((Get-Date) - $t0).TotalSeconds -ge $Tx2AfterSec) { $sp.Write($Tx2); $tx2done = $true; L "# tx2 '$Tx2'" }
        Pump; Start-Sleep -Milliseconds 5
    }
    L '# end'
} finally { $sp.Close() }
