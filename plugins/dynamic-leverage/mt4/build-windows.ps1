[CmdletBinding()]
param(
  [string]$SdkRoot = '',
  [string]$BuildDirectory = '',
  [string]$Generator = 'Visual Studio 17 2022'
)
$ErrorActionPreference = 'Stop'
if (!$SdkRoot) { $SdkRoot = Join-Path $PSScriptRoot '..\..\..\vendor\mt4-sdk' }
if (!$BuildDirectory) { $BuildDirectory = Join-Path $PSScriptRoot 'build-windows' }
& cmake -S $PSScriptRoot -B $BuildDirectory -G $Generator -A Win32 '-DDYNAMIC_LEVERAGE_MT4_BUILD_SERVER=ON' "-DMT4_SERVER_SDK_DIR=$SdkRoot" '-DBUILD_TESTING=ON'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& cmake --build $BuildDirectory --config Release --parallel
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& ctest --test-dir $BuildDirectory -C Release --output-on-failure
exit $LASTEXITCODE
