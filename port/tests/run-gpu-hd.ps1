param([string]$Clang = "clang")
$ErrorActionPreference = "Stop"
$portRoot = Split-Path $PSScriptRoot -Parent
Push-Location $portRoot
try {
    New-Item -ItemType Directory -Force build/tests | Out-Null
    & $Clang --target=x86_64-w64-mingw32 -O2 -Iinclude -Isrc tests/gpu_hd.c src/render.c src/render_gpu.c -o build/tests/gpu_hd.exe
    if ($LASTEXITCODE -ne 0) { throw "Renderer test compilation failed: $LASTEXITCODE" }
    & ./build/tests/gpu_hd.exe
    $testExit = $LASTEXITCODE
    if ($testExit -eq 2) { Write-Warning "D3D11 unavailable; GPU tests not verified." }
    exit $testExit
} finally { Pop-Location }
