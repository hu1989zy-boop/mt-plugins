# Compatibility entry point. BuildDirectory is relative to plugins/trade-copier/mt5,
# just as when invoking the MT5 script directly.
[CmdletBinding()]
param(
    [string]$SdkRoot = '',
    [string]$BuildDirectory = '',
    [switch]$UiOnly,
    [switch]$Executor
)

$ErrorActionPreference = 'Stop'
$pluginScript = Join-Path $PSScriptRoot '..\plugins\trade-copier\scripts\build-windows.ps1'
& $pluginScript @PSBoundParameters
exit $LASTEXITCODE
