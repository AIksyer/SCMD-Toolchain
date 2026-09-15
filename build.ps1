param(
    [ValidateSet('Debug','Release','Sanitize')]
    [string]$Config = 'Release',
    [switch]$NoTest,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $MyInvocation.MyCommand.Path
& (Join-Path $Root 'scripts/build.ps1') -Config $Config -NoTest:$NoTest -Clean:$Clean
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
