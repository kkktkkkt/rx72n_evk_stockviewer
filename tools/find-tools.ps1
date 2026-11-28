# find-tools.ps1  --  ステータス: 未実行・未検証 (2026-11-19 作成)
# 目的: ビルド/デバッグに必要なexeの場所を探して表示する。結果は docs/build-and-debug.md の「実行記録」に追記すること。
param(
    [string[]]$Roots = @(
        'D:\Renesas\e2stuido',
        'D:\Renesas',
        (Join-Path $env:USERPROFILE '.eclipse\com.renesas.platform_download'),
        (Join-Path $env:USERPROFILE '.eclipse')
    )
)

$targets = @('e2studioc.exe', 'e2studio.exe', 'e2-server-gdb.exe', 'rx-elf-gdb.exe', 'rx-elf-gcc.exe', 'make.exe')
$found = @{}
foreach ($t in $targets) { $found[$t] = New-Object System.Collections.Generic.List[string] }

foreach ($root in $Roots) {
    if (-not (Test-Path $root)) { continue }
    foreach ($t in $targets) {
        Get-ChildItem -Path $root -Recurse -Filter $t -ErrorAction SilentlyContinue |
            ForEach-Object { if (-not $found[$t].Contains($_.FullName)) { $found[$t].Add($_.FullName) } }
    }
}

foreach ($t in $targets) {
    Write-Host "== $t"
    if ($found[$t].Count -eq 0) { Write-Host '   (見つからない)' }
    else { $found[$t] | ForEach-Object { Write-Host "   $_" } }
}
Write-Host ''
Write-Host '見つかったパスを docs/build-and-debug.md の実行記録に書くこと。'
