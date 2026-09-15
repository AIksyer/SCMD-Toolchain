param(
    [ValidateSet('Debug','Release','Sanitize')]
    [string]$Config = 'Release',
    [switch]$NoTest,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path $PSScriptRoot -Parent
Set-Location $Root

$Preset = switch ($Config) {
    'Release'  { 'clang-release' }
    'Sanitize' { 'clang-sanitize' }
    default    { 'clang-debug' }
}
$Flavor = switch ($Config) {
    'Release'  { 'release' }
    'Sanitize' { 'sanitize' }
    default    { 'debug' }
}
$BuildDir = Join-Path $Root "out/build/$Preset"
$DistDir = Join-Path $Root "dist/$Flavor"
$ExeSuffix = if ($env:OS -eq 'Windows_NT') { '.exe' } else { '' }
$Scmdc = Join-Path $DistDir ("scmdc" + $ExeSuffix)
$Scmdsim = Join-Path $DistDir ("scmdsim" + $ExeSuffix)
$Vcs16as = Join-Path $DistDir ("vcs16as" + $ExeSuffix)
$Vcs16run = Join-Path $DistDir ("vcs16run" + $ExeSuffix)
$Vcs16dump = Join-Path $DistDir ("vcs16dump" + $ExeSuffix)
$Vcs16scmd = Join-Path $DistDir ("vcs16scmd" + $ExeSuffix)

function Invoke-NativeChecked {
    param(
        [Parameter(Mandatory=$true)][string]$Program,
        [Parameter(ValueFromRemainingArguments=$true)][string[]]$Arguments
    )
    & $Program @Arguments
    $code = $LASTEXITCODE
    if ($code -ne 0) {
        throw ("Native command failed with exit code {0}: {1} {2}" -f $code, $Program, ($Arguments -join ' '))
    }
}

if ($Clean) {
    if (Test-Path $BuildDir) { Remove-Item -Recurse -Force $BuildDir }
    if (Test-Path $DistDir) { Remove-Item -Recurse -Force $DistDir }
}

Invoke-NativeChecked cmake --preset $Preset
# Build the complete public tool surface. Tests exercise vCS-16/2 as well as
# scmdc/scmdsim, so a partial tool build would make CTest report misleading
# missing-executable failures.
Invoke-NativeChecked cmake --build --preset $Preset --target scmd_tools

# Do not start CTest if one of the deliverables did not link. This was a 0.9.0
# bug on Windows: a failed native build could be followed by CTest, which then
# produced dozens of misleading "Could not find executable scmdsim.exe" errors.
if (-not (Test-Path -LiteralPath $Scmdc -PathType Leaf)) {
    throw "scmdc was not produced at expected distribution path: $Scmdc"
}
if (-not (Test-Path -LiteralPath $Scmdsim -PathType Leaf)) {
    throw "scmdsim was not produced at expected distribution path: $Scmdsim"
}
foreach ($Tool in @($Vcs16as,$Vcs16run,$Vcs16dump,$Vcs16scmd)) {
    if (-not (Test-Path -LiteralPath $Tool -PathType Leaf)) {
        throw "vCS-16/2 tool was not produced at expected distribution path: $Tool"
    }
}

if (-not $NoTest) {
    Invoke-NativeChecked ctest --preset $Preset
}

Write-Host ""
Write-Host "[SCMD] Build tree:   $BuildDir"
Write-Host "[SCMD] Distribution: $DistDir"
Write-Host "[SCMD] Tools:"
foreach ($Tool in @($Scmdc,$Scmdsim,$Vcs16as,$Vcs16run,$Vcs16dump,$Vcs16scmd)) {
    Write-Host "       $Tool"
}
