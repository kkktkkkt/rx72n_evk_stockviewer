param([string]$Port = 'COM7', [int]$Baud = 115200, [int]$Seconds = 15, [switch]$Stamp,
      [string]$Tx = '', [double]$TxAtSec = 2, [int]$TxGapMs = 50)
# COM を $Seconds 秒受信し、受信文字だけを標準出力に出す。-Stamp なら行ごとに受信時刻(HH:mm:ss.fff)を先頭に付ける。
# 送信(任意。docs/cli-debug.md §9 の job-runner の serial ジョブと同じ意味):
#   -Tx 文字列(許可文字は [npsd+-?x] の1〜200文字。それ以外はエラー)、-TxAtSec(開いてから送り始めるまでの秒。0 <= TxAtSec < Seconds)、
#   -TxGapMs(1文字ごとの間隔 0〜1000。0 なら全文字を1回の書込み)。送信した文字は時刻付きで標準出力に "# tx 'c'" として出す(-Stamp 時)。
if ($Tx -ne '') {
    if ($Tx -cnotmatch '^[npsd+\-?xb]{1,200}$') { throw "Tx は [npsd+-?xb] の1〜200文字のみ: '$Tx'" }
    if ($TxAtSec -lt 0 -or $TxAtSec -ge $Seconds) { throw "TxAtSec は 0 以上 Seconds 未満: $TxAtSec" }
    if ($TxGapMs -lt 0 -or $TxGapMs -gt 1000) { throw "TxGapMs は 0〜1000: $TxGapMs" }
}
$p = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$p.ReadTimeout = 200
$p.Open()
function Out-Rx($s) {
    if (-not $Stamp) { [Console]::Out.Write($s); return }
    $script:buf += $s
    while (($i = $script:buf.IndexOf("`n")) -ge 0) {
        [Console]::Out.WriteLine((Get-Date -Format 'HH:mm:ss.fff') + ' ' + $script:buf.Substring(0, $i).TrimEnd("`r"))
        $script:buf = $script:buf.Substring($i + 1)
    }
}
try {
    $start = Get-Date
    $end = $start.AddSeconds($Seconds)
    $script:buf = ''
    $txIdx = 0
    $txNext = $start.AddSeconds($TxAtSec)
    while ((Get-Date) -lt $end) {
        if ($Tx -ne '' -and $txIdx -lt $Tx.Length -and (Get-Date) -ge $txNext) {
            if ($TxGapMs -eq 0) { $p.Write($Tx); [Console]::Out.WriteLine((Get-Date -Format 'HH:mm:ss.fff') + " # tx '$Tx'"); $txIdx = $Tx.Length }
            else {
                $c = [string]$Tx[$txIdx]; $p.Write($c); $txIdx++
                [Console]::Out.WriteLine((Get-Date -Format 'HH:mm:ss.fff') + " # tx '$c'")
                $txNext = (Get-Date).AddMilliseconds($TxGapMs)
            }
        }
        $s = $p.ReadExisting()
        if ($s) { Out-Rx $s }
        Start-Sleep -Milliseconds 10
    }
} finally { $p.Close() }
