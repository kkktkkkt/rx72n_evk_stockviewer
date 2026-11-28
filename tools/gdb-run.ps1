param(
    [Parameter(Mandatory = $true)][string]$Script,   # gdb(またはMI)コマンドを1行ずつ書いたファイル。標準入力へ流す
    [string]$Tag = 'run',
    [switch]$Mi,                  # --interpreter=mi2 で起動(e2 studio と同じ)
    [switch]$NonStop,             # -iex "set non-stop on"
    [string]$ServerExtra = '',    # サーバー引数の末尾に足す(例: "-p 61234")
    [int]$SettleSec = 6,          # 「ターゲット接続状況 - OK」が2回出た後の待ち
    [int]$GdbTimeout = 90,        # gdb 全体の上限秒
    [int]$SerialSec = 0,          # >0 なら gdb 起動前から COM7 を受信(別プロセス)
    [string]$ComPort = 'COM7',
    [switch]$NoElfArg,            # ELF をコマンドラインに渡さない
    [switch]$KeepServer,          # 終了時にサーバーを止めない(通常は止める)
    [int]$Repeat = 1,             # >1 なら同じ内容を N 回続けて実行する(各回でサーバーも作り直す)
    [switch]$Verify               # mi-run.gdb 用の合否判定(ログ解析)
)
# 前提: e2studio / rfp-cli / 他の e2-server-gdb / rx-elf-gdb が居ない(居れば中止する。他人のプロセスは止めない)。
# 動作: e2-server-gdb 起動 -> 「GDB: <port>」と「接続状況 - OK」x2 を待つ -> SettleSec 待つ -> rx-elf-gdb 起動 -> $Script を1行ずつ標準入力へ -> 後始末(自分が起動したプロセスだけ止める)
# スクリプト内の特別な行: "#sleep N"(N秒待つ) / "#waitout <regex> <秒>"(gdb出力にregexが出るまで待つ) / "#end"(標準入力を閉じる) / "$PORT" "$ELF" を置換
# 終了状態: サーバーと gdb は止める。基板が走っているかはスクリプト次第(ログと UART で判定する)
# ログ: jobs\gdbrun-<Tag>-<時刻>.{server,gdb,serial}.log
$ErrorActionPreference = 'Stop'
if ($Repeat -gt 1) {
    for ($i = 1; $i -le $Repeat; $i++) {
        "=== 実行 $i / $Repeat"
        $a = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $PSCommandPath, '-Script', $Script, '-Tag', "$Tag-$i", '-SettleSec', $SettleSec, '-GdbTimeout', $GdbTimeout, '-SerialSec', $SerialSec, '-ComPort', $ComPort)
        if ($Mi) { $a += '-Mi' }; if ($NonStop) { $a += '-NonStop' }; if ($Verify) { $a += '-Verify' }
        if ($ServerExtra) { $a += @('-ServerExtra', $ServerExtra) }
        & powershell @a
        Start-Sleep -Seconds 5
    }
    return
}
$Project = Split-Path $PSScriptRoot -Parent
$jobs = Join-Path $Project 'jobs'
$ts = Get-Date -Format 'yyyyMMdd-HHmmss'
$base = Join-Path $jobs "gdbrun-$Tag-$ts"
$dbgDir = 'C:\Users\kkktk\.eclipse\com.renesas.platform_1953403423\DebugComp\RX'
$srvExe = Join-Path $dbgDir 'e2-server-gdb.exe'
$gdbExe = Join-Path $dbgDir 'rx-elf-gdb.exe'
$elf = Join-Path $Project 'HardwareDebug\rx72n_stockview.elf'
$scriptPath = if ([IO.Path]::IsPathRooted($Script)) { $Script } else { Join-Path $Project $Script }
if (-not (Test-Path $scriptPath)) { throw "スクリプトが無い: $scriptPath" }

$busy = Get-Process e2studio, rfp-cli, e2-server-gdb, rx-elf-gdb -ErrorAction SilentlyContinue
if ($busy) { throw ('E2 Lite を使っているプロセスが居る。中止: ' + (($busy | % { "$($_.ProcessName)#$($_.Id)" }) -join ', ')) }

$pyBin = Get-ChildItem 'D:\Renesas\e2stuido\eclipse\plugins' -Directory -Filter 'com.renesas.python3.win32*' | Select-Object -First 1
$env:PATH = "$dbgDir;" + (Join-Path $pyBin.FullName 'bin') + ';' + $env:PATH

Add-Type -TypeDefinition @'
using System; using System.Diagnostics; using System.IO; using System.Text;
public class GdbDrv {
  public Process P; public StringBuilder Out = new StringBuilder(); StreamWriter log; object lk = new object(); DateTime t0 = DateTime.Now;
  string Stamp() { return (DateTime.Now - t0).TotalSeconds.ToString("000.00"); }
  public GdbDrv(string exe, string args, string logPath) {
    log = new StreamWriter(logPath, false, new UTF8Encoding(true));
    ProcessStartInfo si = new ProcessStartInfo(exe, args);
    si.UseShellExecute = false; si.CreateNoWindow = true;
    si.RedirectStandardInput = true; si.RedirectStandardOutput = true; si.RedirectStandardError = true;
    si.StandardOutputEncoding = Encoding.UTF8; si.StandardErrorEncoding = Encoding.UTF8;
    P = new Process(); P.StartInfo = si;
    P.OutputDataReceived += delegate(object s, DataReceivedEventArgs e) { if (e.Data != null) lock (lk) { Out.AppendLine(e.Data); log.WriteLine("[" + Stamp() + "] O " + e.Data); log.Flush(); } };
    P.ErrorDataReceived += delegate(object s, DataReceivedEventArgs e) { if (e.Data != null) lock (lk) { Out.AppendLine(e.Data); log.WriteLine("[" + Stamp() + "] E " + e.Data); log.Flush(); } };
    P.Start(); P.BeginOutputReadLine(); P.BeginErrorReadLine();
  }
  public void Send(string l) { lock (lk) { log.WriteLine("[" + Stamp() + "] > " + l); log.Flush(); } try { P.StandardInput.WriteLine(l); P.StandardInput.Flush(); } catch (Exception) { } }
  public void CloseIn() { try { P.StandardInput.Close(); } catch (Exception) { } }
  public string Snap() { lock (lk) { return Out.ToString(); } }
  public void Note(string s) { lock (lk) { log.WriteLine("[" + Stamp() + "] # " + s); log.Flush(); } }
  public void Close() { lock (lk) { log.Flush(); log.Close(); } }
}
'@

$srvArgs = '-g E2LITE -t R5F572NN -uConnectionTimeout= 30 -uClockSrcHoco= 0 -uInputClock= 16 -uPTimerClock= 240000000 -uAllowClockSourceInternal= 1 -uUseFine= 1 -uFineBaudRate= 1.50 -w 0 -z 0 -uRegisterSetting= 0 -uModePin= 0 -uChangeStartupBank= 0 -uStartupBank= 0 -uDebugMode= 0 -uExecuteProgram= 0 -uIdCode= FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF -uresetOnReload= 1 -n 0 -uWorkRamAddress= 1000 -uverifyOnWritingMemory= 0 -uProgReWriteIRom= 0 -uProgReWriteDFlash= 0 -uhookWorkRamAddr= 0x7fb40 -uhookWorkRamSize= 0x4c0 -uOSRestriction= 0 -l -uCore= SINGLE_CORE|enabled|1|main -uSyncMode= async -uFirstGDB= main --japanese --gdbVersion= 16.2'
if ($ServerExtra) { $srvArgs += ' ' + $ServerExtra }

$srv = $null; $gdb = $null; $ser = $null
function Read-Shared($path) {
    try {
        $fs = [IO.File]::Open($path, 'Open', 'Read', 'ReadWrite')
        $sr = New-Object IO.StreamReader($fs, [Text.Encoding]::UTF8); $t = $sr.ReadToEnd(); $sr.Close(); return $t
    } catch { return '' }
}
function Mark($m) { $l = (Get-Date -Format 'HH:mm:ss.fff') + ' ' + $m; $l | Out-File "$base.timeline.log" -Append -Encoding utf8; $l }
try {
    Mark 'サーバー起動(e2-server-gdb 開始)'
    "server: $srvArgs" | Out-File "$base.server.log" -Encoding utf8
    $srv = Start-Process -FilePath $srvExe -ArgumentList $srvArgs -PassThru -WindowStyle Minimized -WorkingDirectory $dbgDir `
        -RedirectStandardOutput "$base.server.out" -RedirectStandardError "$base.server.err"
    $port = $null; $okN = 0
    $deadline = (Get-Date).AddSeconds(75)
    while ((Get-Date) -lt $deadline) {
        if ($srv.HasExited) { throw "e2-server-gdb が終了した(exit=$($srv.ExitCode))" }
        $t = Read-Shared "$base.server.out"
        if ($t -match '(?m)^GDB:\s*(\d+)') { $port = [int]$Matches[1] }
        $okN = ([regex]::Matches($t, '- OK')).Count
        if ($port) { break }
        Start-Sleep -Milliseconds 300
    }
    if (-not $port) { throw 'サーバーのポートが出ない(E2 Lite 不調の疑い。USB 抜き差しをユーザーに依頼)' }
    "port=$port okCount=$okN settle=${SettleSec}s" | Tee-Object -Append "$base.server.log"
    Mark "GDB ポート確認 port=$port($SettleSec 秒待つ)"
    Start-Sleep -Seconds $SettleSec

    if ($SerialSec -gt 0) {
        $ser = Start-Process powershell -PassThru -WindowStyle Hidden -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$PSScriptRoot\serial-read.ps1`" -Port $ComPort -Baud 115200 -Seconds $SerialSec -Stamp" `
            -RedirectStandardOutput "$base.serial.log" -RedirectStandardError "$base.serial.err"
        Start-Sleep -Seconds 2
    }

    $elfFwd = $elf -replace '\\', '/'
    $gargs = '-rx-force-isa=v3 -rx-force-double-fpu -iex "set pagination off" -iex "set confirm off"'
    if ($NonStop) { $gargs += ' -iex "set non-stop on"' }
    if ($Mi) { $gargs += ' --interpreter=mi2' } else { $gargs += ' -q' }
    if (-not $NoElfArg) { $gargs += " `"$elf`"" }
    Mark 'gdb 起動(gdb ログの経過秒の 0 点)'
    $gdb = New-Object GdbDrv($gdbExe, $gargs, "$base.gdb.log")
    $gdb.Note("gdb args: $gargs")
    $lines = Get-Content $scriptPath -Encoding UTF8
    $gdbDeadline = (Get-Date).AddSeconds($GdbTimeout)
    $script:sp = $null; $script:rxBuf = ''; $script:lastLen = 0
    function Pump {
        if ($script:sp -and $script:sp.IsOpen) {
            $s = $script:sp.ReadExisting()
            if ($s) {
                $script:rxBuf += $s
                while (($i = $script:rxBuf.IndexOf("`n")) -ge 0) {
                    ((Get-Date -Format 'HH:mm:ss.fff') + ' ' + $script:rxBuf.Substring(0, $i).TrimEnd("`r")) | Out-File "$base.serial.log" -Append -Encoding utf8
                    $script:rxBuf = $script:rxBuf.Substring($i + 1)
                }
            }
        }
    }
    function Wait-Ms([int]$ms) { $u = (Get-Date).AddMilliseconds($ms); while ((Get-Date) -lt $u) { Pump; Start-Sleep -Milliseconds 20 } }
    function Wait-New([string]$re, [int]$to) {
        $u = (Get-Date).AddSeconds($to)
        while ((Get-Date) -lt $u) { $o = $gdb.Snap(); if ($o.Length -gt $script:lastLen -and $o.Substring($script:lastLen) -match $re) { return $true }; Pump; Start-Sleep -Milliseconds 50 }
        return $false
    }
    foreach ($l in $lines) {
        if ((Get-Date) -gt $gdbDeadline) { $gdb.Note('TIMEOUT: gdb 全体の上限'); break }
        if ($gdb.P.HasExited) { $gdb.Note('gdb が終了した'); break }
        $l = $l.Replace('$PORT', "$port").Replace('$ELF', $elfFwd)
        if ($l -match '^\s*#sleep\s+([\d.]+)') { $gdb.Note("sleep $($Matches[1])"); Wait-Ms ([int]([double]$Matches[1] * 1000)); continue }
        if ($l -match '^\s*#waitout\s+(\S+)\s+(\d+)') {
            $re = $Matches[1]; $to = [int]$Matches[2]; $until = (Get-Date).AddSeconds($to); $hit = $false
            while ((Get-Date) -lt $until) { if ($gdb.Snap() -match $re) { $hit = $true; break }; Pump; Start-Sleep -Milliseconds 200 }
            $gdb.Note("waitout /$re/ -> $hit"); continue
        }
        # "#waitnew <regex> <秒>": 直前に送ったコマンドより後に出た gdb 出力だけで待つ(2回目以降の *stopped を待つ時に使う)
        if ($l -match '^\s*#waitnew\s+(\S+)\s+(\d+)') { $hit = Wait-New $Matches[1] ([int]$Matches[2]); $gdb.Note("waitnew /$($Matches[1])/ -> $hit"); continue }
        # "#sync <名前> [秒]": 目印を echo させ、それ以前に queue したコマンドがすべて終わるまで待つ(dump など長いコマンドの後に使う)
        if ($l -match '^\s*#sync\s+(\w+)(?:\s+(\d+))?') {
            $tok = "SYNC_$($Matches[1])"; $to = if ($Matches[2]) { [int]$Matches[2] } else { 120 }
            $gdb.Send("-interpreter-exec console `"echo $tok\n`""); $hit = Wait-New $tok $to; $gdb.Note("sync $tok -> $hit"); $script:lastLen = $gdb.Snap().Length; continue
        }
        # "#uart <文字列>": COM7 へ1文字ずつ送る(許可文字 [npsd+-?x])。初回に COM7 を開き、以降の待ち中の受信も時刻付きで $base.serial.log へ
        if ($l -match '^\s*#uart\s+(\S+)') {
            $tx = $Matches[1]; if ($tx -cnotmatch '^[npsd+\-?xb]{1,200}$') { $gdb.Note("uart 不許可: $tx"); break }
            if (-not $script:sp) { $script:sp = New-Object System.IO.Ports.SerialPort $ComPort, 115200, 'None', 8, 'One'; $script:sp.ReadTimeout = 50; $script:sp.Open() }
            for ($ci = 0; $ci -lt $tx.Length; $ci++) { $script:sp.Write([string]$tx[$ci]); $null = Mark "uart tx '$($tx[$ci])'"; if ($ci -lt $tx.Length - 1) { Wait-Ms 120 } }
            continue
        }
        if ($l -match '^\s*#end') { $gdb.CloseIn(); break }
        if ($l -match '^\s*#') { continue }
        $gdb.Send($l); $script:lastLen = $gdb.Snap().Length
    }
    if ($script:sp) { Wait-Ms 300; $script:sp.Close() }
    $null = $gdb.P.WaitForExit([int][Math]::Max(1000, ($gdbDeadline - (Get-Date)).TotalMilliseconds))
    if (-not $gdb.P.HasExited) { $gdb.Note('gdb を強制終了'); Stop-Process -Id $gdb.P.Id -Force }
    Mark 'gdb 終了(-gdb-exit。サーバーは vKill を受けて STOP を出す)'
    Start-Sleep -Milliseconds 500
    $gdb.Close()
    if ($ser) { $null = $ser.WaitForExit(([Math]::Max(1, $SerialSec) + 5) * 1000) }
}
finally {
    if ($gdb -and -not $gdb.P.HasExited) { Stop-Process -Id $gdb.P.Id -Force -ErrorAction SilentlyContinue }
    if ($ser -and -not $ser.HasExited) { Stop-Process -Id $ser.Id -Force -ErrorAction SilentlyContinue }
    if ($srv -and -not $KeepServer -and -not $srv.HasExited) { Mark 'サーバー停止(Stop-Process)' | Out-Null; Stop-Process -Id $srv.Id -Force -ErrorAction SilentlyContinue }
    if ($srv) { Start-Sleep -Milliseconds 300; $o = Read-Shared "$base.server.out"; $o | Out-File "$base.server.log" -Append -Encoding utf8
        if (Test-Path "$base.server.err") { Get-Content "$base.server.err" -Raw | Out-File "$base.server.log" -Append -Encoding utf8 } }
    Remove-Item "$base.server.out", "$base.server.err", "$base.serial.err" -ErrorAction SilentlyContinue
    "logs: $base.*"
}
if ($Verify) {
    # 合否: (A) 接続して load まで通った (B) main でブレーク停止 + サーバーに GB (C) 走行後の tick が main 時点より増加 / UART の tick が増加
    $g = Get-Content "$base.gdb.log" -Raw -Encoding UTF8
    $s = Get-Content "$base.server.log" -Raw -Encoding UTF8
    $gb = ([regex]::Matches($s, '<FFWMCUCmd_GB>')).Count
    $bp = $g -match 'reason="breakpoint-hit".*func="main"'
    $v = @([regex]::Matches($g, '\^done,value="(\d+)"') | % { [int]$_.Groups[1].Value })
    $tickUp = ($v.Count -ge 2 -and $v[-1] -gt $v[0])
    $uart = ''
    if ($SerialSec -gt 0 -and (Test-Path "$base.serial.log")) {
        $u = @([regex]::Matches([string](Get-Content "$base.serial.log" -Raw),'\[hb\] tick=(\d+)') | % { [int]$_.Groups[1].Value })
        # 走行前の古い tick(リセット前)が混ざるので、最後の単調増加区間だけで判定する
        $k = 0; for ($i = 1; $i -lt $u.Count; $i++) { if ($u[$i] -le $u[$i - 1]) { $k = $i } }
        $seg = $u.Count - $k
        $uart = " uartHb=$($u.Count) uartUp=$($seg -ge 3)(last run $seg lines)"
    }
    $ok = $bp -and ($gb -ge 2) -and $tickUp
    "VERIFY: $(if ($ok) { 'PASS' } else { 'FAIL' })  breakAtMain=$bp GB=$gb ticks=[$($v -join ',')]$uart"
}
