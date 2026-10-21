param(
    [Parameter(Mandatory = $true)]
    [string]$InputFile,
    [string]$OutputDirectory = "",
    [ValidateRange(1, 30)]
    [int]$Fps = 24,
    [ValidateRange(1, 15)]
    [int]$Seconds = 10,
    [int]$Width = 320,
    [int]$Height = 240
)

$ErrorActionPreference = "Stop"

if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) {
    throw "ffmpeg is not installed or not available in PATH."
}

$source = (Resolve-Path -LiteralPath $InputFile).Path
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $PSScriptRoot "..\data\animation"
}
$output = [System.IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $output -Force | Out-Null

Get-ChildItem -LiteralPath $output -Filter "f*.jpg" -File -ErrorAction SilentlyContinue |
    Remove-Item -Force
$manifestPath = Join-Path $output "manifest.json"
if (Test-Path -LiteralPath $manifestPath) {
    Remove-Item -LiteralPath $manifestPath -Force
}

$expectedFrames = $Fps * $Seconds
$accepted = $false
foreach ($quality in @(2, 3, 4, 6, 8, 10)) {
    Get-ChildItem -LiteralPath $output -Filter "f*.jpg" -File -ErrorAction SilentlyContinue |
        Remove-Item -Force
    $pattern = Join-Path $output "f%03d.jpg"
    & ffmpeg -hide_banner -loglevel error -y -stream_loop -1 -i $source -t $Seconds `
        -vf "fps=$Fps,scale=${Width}:${Height}:force_original_aspect_ratio=decrease,pad=${Width}:${Height}:(ow-iw)/2:(oh-ih)/2:black" `
        -q:v $quality -start_number 0 $pattern
    if ($LASTEXITCODE -ne 0) {
        throw "ffmpeg failed with exit code $LASTEXITCODE."
    }

    $frames = @(Get-ChildItem -LiteralPath $output -Filter "f*.jpg" -File | Sort-Object Name)
    $totalBytes = ($frames | Measure-Object -Property Length -Sum).Sum
    $largestFrame = ($frames | Measure-Object -Property Length -Maximum).Maximum
    if ($frames.Count -eq $expectedFrames -and $totalBytes -le 4000000 -and $largestFrame -le 49152) {
        $accepted = $true
        break
    }
}

if (-not $accepted) {
    throw "Could not fit the animation below 4.0 MB and 48 KB per frame. Try adjusting video length or resolution."
}

$manifest = [ordered]@{
    version = 1
    width = $Width
    height = $Height
    fps = $Fps
    frames = $expectedFrames
    total_bytes = [int]$totalBytes
}
$manifestJson = $manifest | ConvertTo-Json -Compress
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($manifestPath, $manifestJson, $utf8NoBom)

Write-Output "Animation ready: $expectedFrames frames @ $Fps FPS ($Width x $Height), $totalBytes bytes"
Write-Output "Preloads 100% into ESP32-S3 PSRAM for ultra-smooth smartphone-like 24 FPS video playback!"
Write-Output "Upload to device with: platformio run -d n16r8_ver --target uploadfs"
