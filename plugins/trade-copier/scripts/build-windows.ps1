# Compatibility entry point. BuildDirectory is relative to ../mt5,
# just as when invoking the MT5 script directly.
[CmdletBinding()]
param(
    [string]$SdkRoot = '',
    [string]$BuildDirectory = '',
    [switch]$UiOnly,
    [switch]$Executor
)

$ErrorActionPreference = 'Stop'
$platformScript = Join-Path $PSScriptRoot '..\mt5\scripts\build-windows.ps1'
& $platformScript @PSBoundParameters
exit $LASTEXITCODE
