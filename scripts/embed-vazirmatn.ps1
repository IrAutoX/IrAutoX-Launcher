$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent $PSScriptRoot
$outFile = Join-Path $repoRoot "resources\vazirmatn.b64"
$tempFile = Join-Path $env:TEMP "IrAutoX-Vazirmatn-Regular.ttf"
$url = "https://raw.githubusercontent.com/rastikerdar/vazirmatn/master/fonts/ttf/Vazirmatn-Regular.ttf"

Write-Host "Downloading Vazirmatn Regular from the official upstream repository..."
Invoke-WebRequest -Uri $url -OutFile $tempFile -UseBasicParsing
$bytes = [System.IO.File]::ReadAllBytes($tempFile)
$base64 = [Convert]::ToBase64String($bytes)
[System.IO.File]::WriteAllText($outFile, $base64, [System.Text.Encoding]::ASCII)
Remove-Item $tempFile -Force -ErrorAction SilentlyContinue
Write-Host "Embedded Vazirmatn base64 resource: $outFile"
