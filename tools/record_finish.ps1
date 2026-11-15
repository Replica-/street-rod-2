# Finishes a recording made with the "Record video" launch configuration (.vscode/launch.json): the game wrote
# work/recording/raw.mp4 (picture, SR_VIDEO) and raw.wav (sound, SR_AUDIO_WAV) until it was closed. This joins them,
# cuts 2 seconds off the start and the end, scales to 1920x1080 (the 4:3 picture in the middle), evens out the
# loudness for YouTube and writes work/recording/street_rod_2_<date-time>.mp4, then opens it.
param([double]$Cut = 2.0)
$ErrorActionPreference = 'Stop'
$dir = Join-Path $PSScriptRoot '..\work\recording'
$raw = Join-Path $dir 'raw.mp4'
$wav = Join-Path $dir 'raw.wav'
if (-not (Test-Path $raw) -or -not (Test-Path $wav)) { Write-Host "No recording found in $dir"; exit 1 }
$dur = [double](& ffprobe -v error -show_entries format=duration -of csv=p=0 $raw)
$keep = $dur - 2 * $Cut
if ($keep -le 1) { Write-Host "The recording is too short ($dur s)"; exit 1 }
$out = Join-Path $dir ('street_rod_2_' + (Get-Date -Format 'yyyy-MM-dd_HH-mm-ss') + '.mp4')
$keepText = $keep.ToString([Globalization.CultureInfo]::InvariantCulture)
$cutText = $Cut.ToString([Globalization.CultureInfo]::InvariantCulture)
Write-Host "Recording: $([math]::Round($dur, 1)) s -> keeping $([math]::Round($keep, 1)) s (cut $Cut s at each end)"
& ffmpeg -loglevel error -y -ss $cutText -t $keepText -i $raw -ss $cutText -t $keepText -i $wav `
    -filter_complex "[0:v]scale=1440:1080:flags=lanczos,pad=1920:1080:240:0,setsar=1,fps=30[v];[1:a]aresample=48000,aformat=channel_layouts=stereo,loudnorm=I=-14:TP=-1.5:LRA=11,aresample=48000[a]" `
    -map '[v]' -map '[a]' -c:v libx264 -preset slow -crf 18 -pix_fmt yuv420p -c:a aac -b:a 192k -movflags +faststart $out
if ($LASTEXITCODE -ne 0) { Write-Host 'ffmpeg failed'; exit 1 }
Write-Host "Video: $out"
Start-Process $out
