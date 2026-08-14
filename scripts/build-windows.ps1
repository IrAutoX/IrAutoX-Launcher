param(
    [string]$Configuration = "Release",
    [string]$QtRoot = $env:Qt6_DIR
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot "build"
$distDir = Join-Path $repoRoot "dist"

& (Join-Path $PSScriptRoot "embed-vazirmatn.ps1")

cmake -S $repoRoot -B $buildDir -G "Visual Studio 17 2022" -A x64 -DIRAUTOX_BUILD_TESTS=ON
cmake --build $buildDir --config $Configuration --parallel
ctest --test-dir $buildDir -C $Configuration --output-on-failure

if (Test-Path $distDir) { Remove-Item $distDir -Recurse -Force }
cmake --install $buildDir --config $Configuration --prefix $distDir
windeployqt --release --compiler-runtime --no-translations --no-opengl-sw (Join-Path $distDir "IrAutoXLauncher.exe")
windeployqt --release --compiler-runtime --no-translations --no-opengl-sw (Join-Path $distDir "IrAutoXUpdater.exe")
Copy-Item (Join-Path $repoRoot "THIRD_PARTY_NOTICES.md") (Join-Path $distDir "THIRD_PARTY_NOTICES.md") -Force
Copy-Item (Join-Path $repoRoot "LICENSE") (Join-Path $distDir "LICENSE.txt") -Force

Write-Host "Portable build with Launcher + Updater is ready at $distDir"
