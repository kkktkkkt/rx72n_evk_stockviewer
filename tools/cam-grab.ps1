param([Parameter(Mandatory = $true)][string]$Out,   # 出力 JPG のパス(jobs\cam\ に置く。人物が写りうるので docs\ や git 管理下に置かない)
      [string]$Crop = '',                             # 任意。ffmpeg の crop 指定 "w:h:x:y"(LCD 付近の切り出し。切り出した後 2 倍に拡大)
      [int]$Frames = 5,                               # 先頭の捨てフレーム数(露出が安定してから最後の1枚を保存)
      [int]$Rot = 0,                                 # 下の Eq と同じ行
      [string]$Eq = '')                               # 任意。ffmpeg の eq フィルタ引数(例 brightness=-0.3:contrast=2。白飛び対策。最後に適用)
#                                # 1=時計回り90度 2=反時計回り90度(LCD が横倒しに写る構図用。crop の後に適用)
# LCD 撮影用: Logicool HD Webcam C510(DirectShow)から1枚保存する。ffmpeg のパスはこの1か所だけ。
# 使い方(パスは / 区切りか PowerShell から): -Out D:/workspace/rx72n/rx72n_stockview/jobs/cam/x.jpg [-Crop 200:200:720:520 -Rot 2]
$ffmpeg = 'F:\prog\x_stock_sequence\ffmpeg-2026-08-06-git-95c43d7df7-essentials_build\bin\ffmpeg.exe'
$cam = 'Logicool HD Webcam C510'
$a = @('-hide_banner', '-loglevel', 'error', '-y', '-f', 'dshow', '-video_size', '1280x720', '-i', "video=$cam", '-frames:v', "$Frames")
$vf = @()
if ($Crop) { $vf += "crop=$Crop" }
if ($Rot -eq 1) { $vf += 'transpose=1' } elseif ($Rot -eq 2) { $vf += 'transpose=2' }
if ($Crop) { $vf += 'scale=iw*2:ih*2:flags=lanczos' }
if ($Eq) { $vf += "eq=$Eq" }
if ($vf.Count) { $a += @('-vf', ($vf -join ',')) }
$a += @('-update', '1', $Out)
$dir = Split-Path $Out -Parent
if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Force $dir | Out-Null }
$p = Start-Process -FilePath $ffmpeg -ArgumentList (($a | % { if ($_ -match '\s') { '"' + $_ + '"' } else { $_ } }) -join ' ') -PassThru -NoNewWindow
if (-not $p.WaitForExit(30000)) { Stop-Process -Id $p.Id -Force; throw 'ffmpeg タイムアウト(30秒)' }
if (-not (Test-Path $Out)) { throw "出力なし: $Out" }
(Get-Item $Out).FullName
