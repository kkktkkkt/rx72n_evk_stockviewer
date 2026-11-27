# debug.ps1  --  ステータス: 未実行・未検証 (2026-11-19 作成)
# 目的: E2 Lite (オンボード) で ELF を書き込んで実行する。サーバ引数は .launch から毎回読み取る。
# 使い方: .\tools\debug.ps1 [-Server <e2-server-gdb.exe>] [-Gdb <rx-elf-gdb.exe>] [-Port 61234]
# 結果は docs/build-and-debug.md の「実行記録」に追記すること。ポート番号(既定61234)は未確認。
param(
    [string]$Server = '',
    [string]$Gdb = '',
    [int]$Port = 61234,
    [string]$Project = 'D:\workspace\rx72n\rx72n_stockview',
    [string]$Elf = '',
    [switch]$NoRun            # 書込みだけして実行しない
)

$ErrorActionPreference = 'Stop'
if ($Elf -eq '') { $Elf = Join-Path $Project 'HardwareDebug\rx72n_stockview.elf' }
if (-not (Test-Path $Elf)) { throw "ELFが無い: $Elf  (先に build.ps1)" }
if ($Server -eq '' -or $Gdb -eq '') { throw '-Server と -Gdb を指定する(場所は .\tools\find-tools.ps1 で探す)' }

# .launch から実設定を取り出す
$launch = Join-Path $Project 'rx72n_stockview HardwareDebug.launch'
[xml]$x = Get-Content -LiteralPath $launch -Raw
function Get-Attr([string]$key) {
    ($x.launchConfiguration.stringAttribute | Where-Object { $_.key -eq $key }).value
}
$serverParam = Get-Attr 'com.renesas.cdt.core.serverParam'
$initCmds = (Get-Attr 'com.renesas.cdt.core.optionInitCommands') -split "`n" | Where-Object { $_.Trim() -ne '' }
$gdbName = Get-Attr 'org.eclipse.cdt.dsf.gdb.DEBUG_NAME'   # 例: rx-elf-gdb -rx-force-isa=v3 -rx-force-double-fpu
$gdbArgs = ($gdbName -split '\s+' | Select-Object -Skip 1)

Write-Host "serverParam: $serverParam"
Write-Host "gdb options: $gdbArgs"

$srv = Start-Process -FilePath $Server -ArgumentList $serverParam -PassThru -WindowStyle Minimized
try {
    Start-Sleep -Seconds 5
    $cmds = @("target remote localhost:$Port") + $initCmds + @('load', 'monitor reset')
    if (-not $NoRun) { $cmds += 'continue' }
    $gdbArgList = @($gdbArgs) + @('-batch')
    foreach ($c in $cmds) { $gdbArgList += @('-ex', $c) }
    $gdbArgList += $Elf
    & $Gdb @gdbArgList
}
finally {
    if ($srv -and -not $srv.HasExited) { Stop-Process -Id $srv.Id -Force }
}
