param(
    [Parameter(Mandatory = $true)]
    [string]$InputFile,
    [string]$OutputDirectory = "",
    [ValidateRange(1, 8)]
    [int]$Fps = 6,
    [ValidateRange(1, 10)]
    [int]$Seconds = 10
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
foreach ($quality in @(3, 5, 7, 9)) {
    Get-ChildItem -LiteralPath $output -Filter "f*.jpg" -File -ErrorAction SilentlyContinue |
        Remove-Item -Force
    $pattern = Join-Path $output "f%03d.jpg"
    & ffmpeg -hide_banner -loglevel error -y -stream_loop -1 -i $source -t $Seconds `
        -vf "fps=$Fps,scale=320:172:force_original_aspect_ratio=decrease,pad=320:172:(ow-iw)/2:(oh-ih)/2:black" `
        -q:v $quality -start_number 0 $pattern
    if ($LASTEXITCODE -ne 0) {
        throw "ffmpeg failed with exit code $LASTEXITCODE."
    }

    $frames = @(Get-ChildItem -LiteralPath $output -Filter "f*.jpg" -File | Sort-Object Name)
    $totalBytes = ($frames | Measure-Object -Property Length -Sum).Sum
    $largestFrame = ($frames | Measure-Object -Property Length -Maximum).Maximum
    if ($frames.Count -eq $expectedFrames -and $totalBytes -le 1450000 -and $largestFrame -le 32768) {
        $accepted = $true
        break
    }
}

if (-not $accepted) {
    throw "Could not fit the animation below 1.45 MB and 32 KB per frame."
}

$manifest = [ordered]@{
    version = 1
    width = 320
    height = 172
    fps = $Fps
    frames = $expectedFrames
    total_bytes = [int]$totalBytes
}
$manifestJson = $manifest | ConvertTo-Json -Compress
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($manifestPath, $manifestJson, $utf8NoBom)

Write-Output "Animation ready: $expectedFrames frames, $totalBytes bytes"
Write-Output "Upload with: platformio run --target uploadfs"
