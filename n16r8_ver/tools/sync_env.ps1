$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$docEnvPath = Join-Path $repoRoot ".note\doc_env"
$targetJsPath = Join-Path $PSScriptRoot "..\data\env.js"

$config = @{
    LOCATIONIQ_API_KEY  = ""
    MAPBOX_API_KEY      = ""
    DEFAULT_MAP_LAT     = "10.880539600217121"
    DEFAULT_MAP_LON     = "106.80541333694616"
}

if (Test-Path $docEnvPath) {
    Get-Content $docEnvPath | ForEach-Object {
        $line = $_.Trim()
        if ($line -and -not $line.StartsWith("#") -and $line.Contains("=")) {
            $parts = $line.Split("=", 2)
            $config[$parts[0].Trim()] = $parts[1].Trim()
        }
    }
}

$jsContent = @"
// Auto-generated from .note/doc_env - DO NOT COMMIT
window.MOTOMAP_ENV = {
  LOCATIONIQ_API_KEY: "$($config['LOCATIONIQ_API_KEY'])",
  MAPBOX_API_KEY: "$($config['MAPBOX_API_KEY'])",
  DEFAULT_LAT: $($config['DEFAULT_MAP_LAT']),
  DEFAULT_LON: $($config['DEFAULT_MAP_LON'])
};
"@

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllText($targetJsPath, $jsContent, $utf8NoBom)
Write-Output "[ENV] Synced .note/doc_env -> n16r8_ver/data/env.js successfully."
