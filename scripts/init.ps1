param(
    [switch]$NoGit
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path $PSScriptRoot -Parent
Set-Location $Root

function Require-Tool([string]$Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Missing required tool: $Name"
    }
}

Require-Tool clang
Require-Tool clang++
Require-Tool cmake
Require-Tool ninja

Write-Host "[SCMD] clang:" (clang --version | Select-Object -First 1)
Write-Host "[SCMD] clang++:" (clang++ --version | Select-Object -First 1)
Write-Host "[SCMD] cmake:" (cmake --version | Select-Object -First 1)
Write-Host "[SCMD] ninja:" (ninja --version)

if (-not $NoGit -and -not (Test-Path .git)) {
    if (Get-Command git -ErrorAction SilentlyContinue) {
        git init | Out-Host
        Write-Host "[SCMD] Initialized git repository."
    } else {
        Write-Host "[SCMD] git not found; skipping git init."
    }
}

cmake --preset clang-debug
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE"
}
Write-Host ""
Write-Host "[SCMD] Project initialized."
Write-Host "Build (Debug):  .\scripts\build.ps1 -Config Debug"
Write-Host "Build outputs: .\dist\debug\scmdc.exe + scmdsim.exe"
