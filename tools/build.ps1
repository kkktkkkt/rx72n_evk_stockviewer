# build.ps1  --  ステータス: 未実行・未検証 (2026-11-19 作成)
# 使い方:
#   .\tools\build.ps1 -Mode Headless   # e2 studio をヘッドレス起動してビルド (e2 studio を閉じてから)
#   .\tools\build.ps1 -Mode Make       # HardwareDebug の生成済みMakefileでビルド (事前にe2 studioで1回ビルドしておく)
# 結果(成功/失敗/直した点)は docs/build-and-debug.md の「実行記録」に追記すること。
param(
    [ValidateSet('Headless', 'Make')][string]$Mode = 'Headless',
    [string]$Workspace = 'D:\workspace\rx72n',
    [string]$Project = 'rx72n_stockview',
    [string]$Config = 'HardwareDebug',
    [string]$E2StudioC = 'D:\Renesas\e2stuido\eclipse\e2studioc.exe',
    [string]$MakeExe = '',          # Make モードで make.exe を明示する場合
    [string]$ToolchainBin = '',     # Make モードで rx-elf-gcc.exe のあるbinフォルダを明示する場合
    [switch]$Clean,
    [switch]$KeepGoing,             # Make モードで make -k(エラーがあっても他のファイルをコンパイルし続ける)
    [int]$TimeoutSec = 900,         # Headless モードの上限。e2studioc はビルド後に終了しないことがあるため
    [switch]$Force                  # e2 studio が起動中でも続行する
)

$ErrorActionPreference = 'Continue'   # exe の stderr 出力(警告)で止めない。失敗は終了コードで判定する
$projDir = Join-Path $Workspace $Project
$buildDir = Join-Path $projDir $Config

if ($Mode -eq 'Headless') {
    if (-not (Test-Path $E2StudioC)) { throw "e2studioc.exe が見つからない: $E2StudioC  (.\tools\find-tools.ps1 で探す)" }
    if ((Get-Process -Name 'e2studio' -ErrorAction SilentlyContinue) -and -not $Force) {
        throw 'e2 studio が起動中。同じワークスペースはロックされるので閉じるか、-Force で続行する。'
    }
    $buildTarget = if ($Clean) { '-cleanBuild' } else { '-build' }
    $outFile = Join-Path $env:TEMP 'rx72n_headless_build.log'
    Remove-Item $outFile, ($outFile + '.err') -ErrorAction SilentlyContinue
    $p = Start-Process -FilePath $E2StudioC -PassThru -NoNewWindow -RedirectStandardOutput $outFile -RedirectStandardError ($outFile + '.err') `
        -ArgumentList @('-nosplash', '-application', 'org.eclipse.cdt.managedbuilder.core.headlessbuild', '-data', $Workspace, $buildTarget, "$Project/$Config")
    $deadline = (Get-Date).AddSeconds($TimeoutSec); $doneAt = $null; $result = $null
    while (-not $p.HasExited -and (Get-Date) -lt $deadline) {
        Start-Sleep -Seconds 2
        $txt = Get-Content $outFile -Raw -ErrorAction SilentlyContinue
        if (-not $result -and $txt -match 'Build (Finished|Failed)') { $result = $Matches[1]; $doneAt = Get-Date }
        if ($doneAt -and ((Get-Date) - $doneAt).TotalSeconds -gt 15) { break }   # 結果が出てから15秒待って、残っていれば終了させる
    }
    if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue; Get-Process -Name 'e2studio','e2studioc' -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue }
    Get-Content $outFile, ($outFile + '.err') -ErrorAction SilentlyContinue | Where-Object { $_ -notmatch '^Warning: NLS unused message' }
    $txt = Get-Content $outFile -Raw -ErrorAction SilentlyContinue
    if ($txt -match 'Build Finished') { $global:LASTEXITCODE = 0 } else { $global:LASTEXITCODE = 1 }
    if ($LASTEXITCODE -ne 0) { throw "ヘッドレスビルド失敗 (exit $LASTEXITCODE)" }
}
else {
    if (-not (Test-Path (Join-Path $buildDir 'makefile'))) {
        throw "Makefile が無い: $buildDir  (e2 studio で一度ビルドして生成させる)"
    }
    if ($ToolchainBin -ne '') { $env:PATH = "$ToolchainBin;$env:PATH" }
    if ($MakeExe -eq '') { $MakeExe = 'make' }
    if ($Clean) { & $MakeExe -C $buildDir clean }
    $mk = @('-C', $buildDir, '-j', 'all'); if ($KeepGoing) { $mk += '-k' }
    & $MakeExe @mk
    if ($LASTEXITCODE -ne 0) { throw "make 失敗 (exit $LASTEXITCODE)" }
}

$elf = Join-Path $buildDir "$Project.elf"
if (Test-Path $elf) { Write-Host "OK: $elf ($((Get-Item $elf).Length) bytes)" }
else { Write-Warning "ELFが見つからない: $elf" }
