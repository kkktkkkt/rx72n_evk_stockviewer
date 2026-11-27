# job-runner.ps1  --  ステータス: 未実行・未検証 (2026-11-19 作成)
# 目的: ユーザーPC上で常駐し、jobs\inbox\*.json のジョブを順に実行して jobs\outbox\ に結果を書く。
#       LLM(Linux側)はプロジェクトフォルダ経由でジョブを出し、Windowsのexe(ビルド/デバッグ/シリアル)を間接的に動かす。
# 起動: ユーザーがPowerShellで1回だけ実行する(右クリック「PowerShellで実行」でも可)。
#         cd D:\workspace\rx72n\rx72n_stockview ; .\tools\job-runner.ps1
# 停止: そのウィンドウで Ctrl+C、または jobs\STOP というファイルを作る。
# 安全策: ジョブは下の許可リスト(find-tools/build/server-start/server-stop/gdb/serial/ping)だけ。任意コマンドは実行しない。
#         gdbスクリプトは tools\gdb\ 配下に限る。COMポートは COMn のみ。
param(
    [string]$Project = 'D:\workspace\rx72n\rx72n_stockview',
    [int]$PollSec = 2,
    [int]$GdbPort = 61234
)
$ErrorActionPreference = 'Stop'
$jobs = Join-Path $Project 'jobs'
foreach ($d in 'inbox', 'outbox', 'done') { New-Item -ItemType Directory -Force -Path (Join-Path $jobs $d) | Out-Null }
$serverProc = $null
$script:restartRequested = $false

function Find-Exe([string]$name) {
    $cache = Join-Path $jobs 'tools.json'
    $map = @{}
    if (Test-Path $cache) { (Get-Content $cache -Raw | ConvertFrom-Json).PSObject.Properties | ForEach-Object { $map[$_.Name] = $_.Value } }
    if ($map.ContainsKey($name) -and (Test-Path $map[$name])) { return $map[$name] }
    $roots = @((Join-Path $env:USERPROFILE '.eclipse'), 'D:\Renesas\e2stuido', 'D:\Renesas')   # .eclipse(e2 studioが使う本物)を先に
    foreach ($r in $roots) {
        if (-not (Test-Path $r)) { continue }
        $hit = Get-ChildItem -Path $r -Recurse -Filter $name -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($hit) { $map[$name] = $hit.FullName; ($map | ConvertTo-Json) | Set-Content $cache -Encoding UTF8; return $hit.FullName }
    }
    throw "$name が見つからない(find-tools ジョブで探す)"
}

function Get-LaunchValue([string]$key) {
    [xml]$x = Get-Content -LiteralPath (Join-Path $Project 'rx72n_stockview HardwareDebug.launch') -Raw
    ($x.launchConfiguration.stringAttribute | Where-Object { $_.key -eq $key }).value
}

function Invoke-Job($job, [string]$log) {
    switch ($job.action) {
        'restart' { 'restarting (exit 75)' | Out-File $log -Append -Encoding UTF8; $script:restartRequested = $true; return 0 }
        'ping' { 'pong' | Out-File $log -Append -Encoding UTF8; return 0 }
        'find-tools' { & (Join-Path $Project 'tools\find-tools.ps1') *>&1 | Out-File $log -Append -Encoding UTF8; return 0 }
        'build' {
            $mode = if ($job.args.mode) { [string]$job.args.mode } else { 'Make' }
            if ($mode -notin 'Make', 'Headless') { throw "mode不正: $mode" }
            $a = @{ Mode = $mode }
            if ($job.args.clean) { $a['Clean'] = $true }
            if ($job.args.keepGoing) { $a['KeepGoing'] = $true }
            if ($job.args.makeExe) { $a['MakeExe'] = [string]$job.args.makeExe }
            if ($job.args.toolchainBin) { $a['ToolchainBin'] = [string]$job.args.toolchainBin }
            & (Join-Path $Project 'tools\build.ps1') @a *>&1 | Out-File $log -Append -Encoding UTF8
            return $(if ($?) { 0 } else { 1 })
        }
        'server-start' {
            if ($script:serverProc -and -not $script:serverProc.HasExited) { 'すでに起動中' | Out-File $log -Append -Encoding UTF8; return 0 }
            $exe = Find-Exe 'e2-server-gdb.exe'
            $param = Get-LaunchValue 'com.renesas.cdt.core.serverParam'
            # e2 studio が実行時に付ける追加引数(2026-11-20 ユーザー提供ログ)。job.args.serverExtra で上書き可。'-' なら付けない
            $extra = '-l -uCore= SINGLE_CORE|enabled|1|main -uSyncMode= async -uFirstGDB= main --japanese --gdbVersion= 16.2'
            if ($job.args.serverExtra) { $extra = [string]$job.args.serverExtra }
            if ($extra -ne '-') { $param = $param + ' ' + $extra }
            # 試験用: 既存引数の値を置換(二重指定にしない)。許可は uresetOnReload=0|1、uDebugMode=0|1|2 のみ。例 "uresetOnReload=0,uDebugMode=1"
            if ($job.args.serverSub) {
                foreach ($kv in ([string]$job.args.serverSub -split ',')) {
                    if ($kv -match '^(uresetOnReload)=([01])$') { $param = $param -replace '-uresetOnReload=\s*\d+', ('-uresetOnReload= ' + $Matches[2]) }
                    elseif ($kv -match '^(uDebugMode)=([012])$') { $param = $param -replace '-uDebugMode=\s*"?\d*"?', ('-uDebugMode= "' + $Matches[2] + '"') }
                    else { "serverSub 不許可: $kv" | Out-File $log -Append -Encoding UTF8; return 2 }
                }
            }
            $so = Join-Path $jobs 'server.out.log'; $se = Join-Path $jobs 'server.err.log'
            $script:serverProc = Start-Process -FilePath $exe -ArgumentList $param -PassThru -WindowStyle Minimized -WorkingDirectory (Split-Path $exe) `
                -RedirectStandardOutput $so -RedirectStandardError $se
            Start-Sleep -Seconds 6
            "server: $exe`nargs: $param`npid: $($script:serverProc.Id) exited: $($script:serverProc.HasExited)" | Out-File $log -Append -Encoding UTF8
            Get-Content $so, $se -ErrorAction SilentlyContinue | Out-File $log -Append -Encoding UTF8
            return $(if ($script:serverProc.HasExited) { 1 } else { 0 })
        }
        'server-stop' {
            if ($script:serverProc -and -not $script:serverProc.HasExited) { Stop-Process -Id $script:serverProc.Id -Force }
            $script:serverProc = $null; '停止' | Out-File $log -Append -Encoding UTF8
            # 取り残し(孤児)の e2-server-gdb / rx-elf-gdb があるとE2 Liteを掴んだままになる。見つけたら記録して止める
            foreach ($pn in 'e2-server-gdb', 'rx-elf-gdb') {
                Get-Process -Name $pn -ErrorAction SilentlyContinue | ForEach-Object { "孤児を停止: $($_.ProcessName) pid=$($_.Id) start=$($_.StartTime)" | Out-File $log -Append -Encoding UTF8; Stop-Process -Id $_.Id -Force }
            }
            'e2studio系プロセス: ' + ((Get-Process -ErrorAction SilentlyContinue | Where-Object { $_.ProcessName -match 'e2studio|e2-server|rx-elf|DebugComp|e2lite|e2_' } | ForEach-Object { "$($_.ProcessName)#$($_.Id)" }) -join ', ') | Out-File $log -Append -Encoding UTF8
            return 0
        }
        'gdb' {
            $rel = [string]$job.args.script
            $gdbDir = (Resolve-Path (Join-Path $Project 'tools\gdb')).Path
            $full = [System.IO.Path]::GetFullPath((Join-Path $Project $rel))
            if (-not $full.StartsWith($gdbDir + '\', [StringComparison]::OrdinalIgnoreCase)) { throw "gdbスクリプトは tools\gdb\ 配下のみ: $rel" }
            if (-not (Test-Path $full)) { throw "スクリプトが無い: $rel" }
            $timeout = if ($job.args.timeoutSec) { [int]$job.args.timeoutSec } else { 60 }
            $elf = Join-Path $Project 'HardwareDebug\rx72n_stockview.elf'
            $gdb = Find-Exe 'rx-elf-gdb.exe'
            $opts = ((Get-LaunchValue 'org.eclipse.cdt.dsf.gdb.DEBUG_NAME') -split '\s+' | Select-Object -Skip 1)
            $port = if ($job.args.port) { [int]$job.args.port } else { $GdbPort }
            $argList = @($opts) + @('-batch', '-ex', "target remote localhost:$port", '-x', $full, $elf)
            $out = Join-Path $jobs 'gdb.out.tmp'
            # Start-Process は配列を空白で連結するだけ。空白を含む引数は自前で引用符で囲む
            # args.nonstop=true なら接続前に non-stop を有効にする(e2 studio の起動構成は NON_STOP=true)。args.noconnect=true なら target remote をスクリプト側(\$gdbport を使う)に任せる
            $pre = @('-iex', "`"set `$gdbport = $port`"")
            if ($job.args.nonstop) { $pre += @('-iex', '"set non-stop on"') }
            $conn = @()
            if (-not $job.args.noconnect) { $conn = @('-ex', "`"target remote localhost:$port`"") }
            $argList = @($opts) + $pre + @('-batch') + $conn + @('-x', "`"$full`"", "`"$elf`"")
            $gdbDir2 = Split-Path $gdb
            $env:PATH = "$gdbDir2;" + $env:PATH
            "gdb: $gdb`nargs: $($argList -join ' ')" | Out-File $log -Append -Encoding UTF8
            # rx-elf-gdb は python3 の DLL を要求する(無いと 0xC0000135)。e2 studio 同梱のものをPATHに足す
            $pyBin = Get-ChildItem 'D:\Renesas\e2stuido\eclipse\plugins' -Directory -Filter 'com.renesas.python3.win32*' -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($pyBin) { $env:PATH = (Join-Path $pyBin.FullName 'bin') + ';' + $env:PATH }
            # args.stdin=true: -batch/-x を使わず、スクリプトを標準入力から読ませる。gdb のイベントループが各行の間に回るので、
            # interrupt の停止通知や `continue -a &` の結果を、次の行で扱える(-x だとイベントが処理されない)。スクリプトは quit で終えること
            if ($job.args.stdin) {
                $argList = @($opts) + $pre + @('-q') + $conn + @("`"$elf`"")
                $p = Start-Process -FilePath $gdb -ArgumentList $argList -PassThru -NoNewWindow -RedirectStandardInput $full -RedirectStandardOutput $out -RedirectStandardError ($out + '.err')
            } else {
            $p = Start-Process -FilePath $gdb -ArgumentList $argList -PassThru -NoNewWindow -RedirectStandardOutput $out -RedirectStandardError ($out + '.err')
            }
            $null = $p.Handle   # ExitCode が null になるのを防ぐ(PowerShellの癖)
            $killed = $false
            if (-not $p.WaitForExit($timeout * 1000)) { $killed = $true; Stop-Process -Id $p.Id -Force; "[タイムアウト ${timeout}s で gdb を強制終了]" | Out-File $log -Append -Encoding UTF8 }
            Start-Sleep -Milliseconds 300
            "--- gdb stdout" | Out-File $log -Append -Encoding UTF8
            Get-Content $out -ErrorAction SilentlyContinue | Out-File $log -Append -Encoding UTF8
            "--- gdb stderr" | Out-File $log -Append -Encoding UTF8
            Get-Content ($out + '.err') -ErrorAction SilentlyContinue | Out-File $log -Append -Encoding UTF8
            "--- gdb exit: $($p.ExitCode) (killed=$killed)" | Out-File $log -Append -Encoding UTF8
            Remove-Item $out, ($out + '.err') -ErrorAction SilentlyContinue
            return $(if ($killed) { 1 } elseif ($null -ne $p.ExitCode) { [int]$p.ExitCode } else { 1 })
        }
        'serial' {
            # 引数: port, baud, seconds(受信は seconds の終わりまで)。送信(任意): tx(許可文字 [npsd+?x-] のみ・1〜200文字)、
            #   txAtSec(開いてから送り始めるまでの秒。既定2。0 <= txAtSec < seconds)、txGapMs(1文字ごとの間隔。既定50。0なら全文字を1回の書込み)、
            #   dtr(true なら DtrEnable)。ログには受信した文字だけを書く(送信の記録は入れない。設計 docs/design-uart.md §6.1)
            $port = [string]$job.args.port
            if ($port -notmatch '^COM\d{1,3}$') { throw "port不正: $port" }
            $baud = if ($job.args.baud) { [int]$job.args.baud } else { 115200 }
            $sec = if ($job.args.seconds) { [int]$job.args.seconds } else { 10 }
            if ($sec -gt 600) { throw 'seconds は600以下' }
            $tx = $null
            if ($null -ne $job.args.tx -and [string]$job.args.tx -ne '') {
                $tx = [string]$job.args.tx
                if ($tx -cnotmatch '^[npsd+?x-]{1,200}$') { throw 'tx は [npsd+?x-] の1〜200文字のみ' }
            }
            $txAt = if ($null -ne $job.args.txAtSec) { [double]$job.args.txAtSec } else { 2.0 }
            $txGap = if ($null -ne $job.args.txGapMs) { [int]$job.args.txGapMs } else { 50 }
            if ($tx) {
                if ($txAt -lt 0 -or $txAt -ge $sec) { throw "txAtSec は 0 以上 seconds 未満: $txAt" }
                if ($txGap -lt 0 -or $txGap -gt 1000) { throw "txGapMs は 0〜1000: $txGap" }
            }
            $sp = New-Object System.IO.Ports.SerialPort $port, $baud, 'None', 8, 'One'
            $sp.ReadTimeout = 500
            if ($job.args.dtr -eq $true) { $sp.DtrEnable = $true }
            $sp.Open()
            try {
                $start = Get-Date
                $end = $start.AddSeconds($sec)
                $txTime = $start.AddSeconds($txAt)
                $sent = $false
                while ((Get-Date) -lt $end) {
                    if ($tx -and -not $sent -and (Get-Date) -ge $txTime) {
                        $sent = $true
                        if ($txGap -eq 0) { $sp.Write($tx) }
                        else {
                            foreach ($ch in $tx.ToCharArray()) {
                                $sp.Write([string]$ch)
                                $t2 = (Get-Date).AddMilliseconds($txGap)
                                while ((Get-Date) -lt $t2) {   # 待つ間も受信を取りこぼさない
                                    try { $line = $sp.ReadExisting(); if ($line) { $line | Out-File $log -Append -Encoding UTF8 -NoNewline } } catch {}
                                    Start-Sleep -Milliseconds 10
                                }
                            }
                        }
                    }
                    try { $line = $sp.ReadExisting(); if ($line) { $line | Out-File $log -Append -Encoding UTF8 -NoNewline } } catch {}
                    Start-Sleep -Milliseconds 100
                }
            } finally { $sp.Close() }
            return 0
        }
        'server-help' {
            # e2-server-gdb のヘルプ表示だけ(読み取り)。固定の引数 -h / --help のみ。15秒で強制終了し、残ったプロセスも止める
            $exe = Find-Exe 'e2-server-gdb.exe'
            foreach ($h in '-h', '--help') {
                "--- e2-server-gdb $h" | Out-File $log -Append -Encoding UTF8
                $o = Join-Path $jobs 'srvhelp.out.tmp'
                $p = Start-Process -FilePath $exe -ArgumentList $h -PassThru -NoNewWindow -WorkingDirectory (Split-Path $exe) -RedirectStandardOutput $o -RedirectStandardError ($o + '.err')
                $null = $p.Handle
                if (-not $p.WaitForExit(15000)) { Stop-Process -Id $p.Id -Force; '[タイムアウト15s]' | Out-File $log -Append -Encoding UTF8 }
                Start-Sleep -Milliseconds 300
                Get-Content $o, ($o + '.err') -ErrorAction SilentlyContinue | Out-File $log -Append -Encoding UTF8
                Remove-Item $o, ($o + '.err') -ErrorAction SilentlyContinue
                Get-Process -Name 'e2-server-gdb' -ErrorAction SilentlyContinue | ForEach-Object { Stop-Process -Id $_.Id -Force }
            }
            return 0
        }
        'rfp' {
            # Renesas Flash Programmer (rfp-cli.exe)。ホワイトリスト: args.mode = help | doc | list | write | reset-run だけ。任意のパス・追加引数は受け付けない
            $rfpRoot = 'C:\Program Files (x86)\Renesas Electronics\Programming Tools\Renesas Flash Programmer V3.25'
            $mode = [string]$job.args.mode
            if ($mode -notin 'help', 'doc', 'list', 'write', 'reset-run') { throw "rfp mode不正: $mode" }
            $rfp = Join-Path $rfpRoot 'rfp-cli.exe'
            if (-not (Test-Path $rfp)) { throw "rfp-cli.exe が無い: $rfp" }
            "rfp-cli: $rfp" | Out-File $log -Append -Encoding UTF8
            # rfp-cli を固定の引数配列で実行してログへ。timeout 秒で強制終了。戻り値は終了コード(タイムアウトは -1)
            $runRfp = {
                param([string[]]$a, [int]$timeoutSec)
                $o = Join-Path $jobs 'rfp.out.tmp'
                "--- rfp-cli $($a -join ' ')" | Out-File $log -Append -Encoding UTF8
                $p = Start-Process -FilePath $rfp -ArgumentList $a -PassThru -NoNewWindow -RedirectStandardOutput $o -RedirectStandardError ($o + '.err')
                $null = $p.Handle
                $killed = $false
                if (-not $p.WaitForExit($timeoutSec * 1000)) { $killed = $true; Stop-Process -Id $p.Id -Force; "[タイムアウト ${timeoutSec}s で強制終了]" | Out-File $log -Append -Encoding UTF8 }
                Start-Sleep -Milliseconds 300
                Get-Content $o, ($o + '.err') -ErrorAction SilentlyContinue | Out-File $log -Append -Encoding UTF8
                "exit: $(if ($killed) { -1 } else { $p.ExitCode })" | Out-File $log -Append -Encoding UTF8
                Remove-Item $o, ($o + '.err') -ErrorAction SilentlyContinue
                if ($killed) { return -1 } else { return [int]$p.ExitCode }
            }
            if ($mode -eq 'help') { $null = & $runRfp @('-help') 30; return 0 }
            if ($mode -eq 'doc') {
                # インストール先の docs\rfp-cli.md をプロジェクトの jobs\ にコピーする(読み取りだけ)
                Copy-Item -LiteralPath (Join-Path $rfpRoot 'docs\rfp-cli.md') -Destination (Join-Path $jobs 'rfp-cli.md') -Force
                "copied docs\rfp-cli.md -> jobs\rfp-cli.md" | Out-File $log -Append -Encoding UTF8
                return 0
            }
            if ($mode -eq 'list') {
                # 読み取りだけ。device 省略時は -list-devices。device(RXで始まる英数字と / のみ)があれば、そのデバイスの tool / interface / speed / flashoption 一覧
                $dev = [string]$job.args.device
                if (-not $dev) { $null = & $runRfp @('-list-devices') 30; return 0 }
                if ($dev -notmatch '^RX[0-9A-Za-z/]{0,12}$') { throw "device不正: $dev" }
                $null = & $runRfp @('-d', $dev, '-list-tools') 30
                $null = & $runRfp @('-d', $dev, '-t', 'e2l', '-list-interfaces') 30
                $null = & $runRfp @('-d', $dev, '-t', 'e2l', '-list-speeds') 30
                $null = & $runRfp @('-d', $dev, '-t', 'e2l', '-if', 'fine', '-list-flashoptions') 30
                return 0
            }
            # write / reset-run: 固定テンプレート。デバイス RX72x、ツール e2l(E2 Lite)、FINE、ID は既定(FF x32)。ID変更・セキュリティ・ロック系・任意パス・任意の追加引数は許さない
            # write = -a(消去+書込み+ベリファイ) + -run(切断後にリセットを解除して実行)。reset-run = -sig(署名表示のみ。読み取り) + -run
            $mot = Join-Path $Project 'HardwareDebug\rx72n_stockview.mot'
            $base = @('-d', 'RX72x', '-t', 'e2l', '-if', 'fine', '-auth', 'id', 'FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF', '-noquery', '-noprogress')
            # E2 Lite の占有を避ける: e2-server-gdb / rx-elf-gdb / 残った rfp-cli を止める
            foreach ($pn in 'e2-server-gdb', 'rx-elf-gdb', 'rfp-cli') {
                Get-Process -Name $pn -ErrorAction SilentlyContinue | ForEach-Object { "停止: $($_.ProcessName) pid=$($_.Id)" | Out-File $log -Append -Encoding UTF8; Stop-Process -Id $_.Id -Force }
            }
            $script:serverProc = $null
            if ($mode -eq 'write') {
                if (-not (Test-Path $mot)) { throw "mot が無い: $mot" }
                # ビルド中は書かない(inbox に build ジョブが残っていれば拒否)
                if (Get-ChildItem (Join-Path $jobs 'inbox') -Filter '*-build.json' -ErrorAction SilentlyContinue) { throw 'inbox に build ジョブがある。ビルド完了後に出し直す' }
                $mi = Get-Item $mot
                "mot: $($mi.FullName)  更新 $($mi.LastWriteTime.ToString('yyyy-MM-dd HH:mm:ss'))  $($mi.Length) bytes" | Out-File $log -Append -Encoding UTF8
                $rc = & $runRfp ($base + @('-a', '-run', ('"' + $mot + '"'))) 170
            } else {
                $rc = & $runRfp ($base + @('-sig', '-run')) 90
            }
            foreach ($pn in 'rfp-cli') { Get-Process -Name $pn -ErrorAction SilentlyContinue | ForEach-Object { "後片付け: $($_.ProcessName) pid=$($_.Id)" | Out-File $log -Append -Encoding UTF8; Stop-Process -Id $_.Id -Force } }
            return $rc
        }
        default { throw "未対応のaction: $($job.action)" }
    }
}

Write-Host "job-runner 開始: $jobs  (停止: Ctrl+C か jobs\STOP)"
try {
    while ($true) {
        (Get-Date).ToString('o') | Set-Content (Join-Path $jobs 'runner.alive') -Encoding ASCII
        if (Test-Path (Join-Path $jobs 'STOP')) { Remove-Item (Join-Path $jobs 'STOP'); break }
        $next = Get-ChildItem (Join-Path $jobs 'inbox') -Filter '*.json' -ErrorAction SilentlyContinue | Sort-Object Name | Select-Object -First 1
        if (-not $next) { Start-Sleep -Seconds $PollSec; continue }
        $id = $next.BaseName
        $log = Join-Path $jobs "outbox\$id.log"
        $started = Get-Date; $code = 1
        try {
            if ($id -notmatch '^[A-Za-z0-9_-]{1,60}$') { throw "job idが不正: $id" }
            $job = Get-Content $next.FullName -Raw | ConvertFrom-Json
            Write-Host "[$id] $($job.action)"
            $code = Invoke-Job $job $log
        } catch { "ERROR: $($_.Exception.Message)" | Out-File $log -Append -Encoding UTF8; $code = 1 }
        $res = [ordered]@{ id = $id; exit = $code; seconds = [int]((Get-Date) - $started).TotalSeconds; finished = (Get-Date).ToString('o') }
        ($res | ConvertTo-Json) | Set-Content (Join-Path $jobs "outbox\$id.result.json") -Encoding UTF8
        Move-Item $next.FullName (Join-Path $jobs "done\$($next.Name)") -Force
        if ($script:restartRequested) { $global:LASTEXITCODE = 75; break }
    }
} finally {
    if ($serverProc -and -not $serverProc.HasExited) { Stop-Process -Id $serverProc.Id -Force }
    Write-Host 'job-runner 終了'
}
if ($script:restartRequested) { exit 75 }
