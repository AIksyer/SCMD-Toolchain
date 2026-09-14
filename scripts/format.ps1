$ErrorActionPreference = 'Stop'
$Root = Split-Path $PSScriptRoot -Parent
Set-Location $Root
if (-not (Get-Command clang-format -ErrorAction SilentlyContinue)) {
    throw 'clang-format not found'
}
Get-ChildItem src,include -Recurse -File | Where-Object { $_.Extension -in '.c','.h' } | ForEach-Object {
    clang-format -i $_.FullName
}
Write-Host '[SCMD] Formatted C sources.'
