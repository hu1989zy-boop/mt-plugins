[CmdletBinding()]
param(
  [ValidateSet('all', 'mt4', 'mt5')][string]$Platform = 'all',
  [string]$Mt4SdkRoot = '',
  [string]$Mt5SdkRoot = '',
  [string]$BuildDirectory = '',
  [string]$Generator = 'Visual Studio 17 2022'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$SourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (!$BuildDirectory) { $BuildDirectory = Join-Path $SourceRoot 'build-opening-margin' }
elseif (![IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $SourceRoot $BuildDirectory }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
if (!$Mt4SdkRoot) { $Mt4SdkRoot = Join-Path $SourceRoot '../../vendor/mt4-sdk' }
if (!$Mt5SdkRoot) { $Mt5SdkRoot = Join-Path $SourceRoot '../../vendor/mt5-sdk' }
$SelectedPlatforms = @(if ($Platform -eq 'all') { 'mt4'; 'mt5' } else { $Platform })
$Specs = @{
  mt4 = @{ Architecture = 'Win32'; Sdk = $Mt4SdkRoot; Header = 'include/MT4ServerAPI.h';
           SdkFlag = 'MT4_SERVER_SDK_DIR'; Flag = 'DYNAMIC_LEVERAGE_MT4_BUILD_SERVER'; Dll = 'MT4DynamicLeverage.dll' }
  mt5 = @{ Architecture = 'x64'; Sdk = $Mt5SdkRoot; Header = 'Include/MT5APIServer.h';
           SdkFlag = 'MT5_SDK_ROOT'; Flag = 'DYNAMIC_LEVERAGE_MT5_BUILD_MARGIN_PROBE'; Dll = 'DynamicLeverageMT5MarginProbe.dll' }
}
foreach ($Tool in @('cmake', 'ctest')) {
  if (!(Get-Command $Tool -ErrorAction SilentlyContinue)) { throw "Required tool missing: $Tool" }
}
foreach ($Name in $SelectedPlatforms) {
  $Spec = $Specs[$Name]
  $Spec.Sdk = (Resolve-Path $Spec.Sdk).Path
  if (!(Test-Path (Join-Path $Spec.Sdk $Spec.Header))) { throw "Missing licensed $Name Server SDK" }
}
New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null
$Stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
Start-Transcript -Path (Join-Path $BuildDirectory "validation-$Stamp.log") | Out-Null
try {
  foreach ($Name in $SelectedPlatforms) {
    $Spec = $Specs[$Name]
    $Build = Join-Path $BuildDirectory $Name
    $CMakeArguments = @('-S', (Join-Path $SourceRoot $Name), '-B', $Build,
      '-G', $Generator, '-A', $Spec.Architecture, "-D$($Spec.Flag)=ON",
      "-D$($Spec.SdkFlag)=$($Spec.Sdk)", '-DBUILD_TESTING=ON')
    if ($Name -eq 'mt5') { $CMakeArguments += '-DDYNAMIC_LEVERAGE_MT5_BUILD_SERVER=OFF' }
    & cmake @CMakeArguments
    if ($LASTEXITCODE -ne 0) { throw "$Name configure failed ($LASTEXITCODE)" }
    & cmake --build $Build --config Release --parallel
    if ($LASTEXITCODE -ne 0) { throw "$Name build failed ($LASTEXITCODE)" }
    & ctest --test-dir $Build -C Release --output-on-failure --no-tests=error
    if ($LASTEXITCODE -ne 0) { throw "$Name tests failed ($LASTEXITCODE)" }
    $DllPath = Join-Path $Build "Release/$($Spec.Dll)"
    if (!(Test-Path $DllPath -PathType Leaf)) { throw "Expected DLL missing: $DllPath" }
    Write-Host "Validation build artifact: $DllPath"
  }
  $Manifest = @{
    kind = 'opening-margin-validation'; built_at_utc = [DateTime]::UtcNow.ToString('o')
    platforms = $SelectedPlatforms; native_execution_available = $false; server_demo_verified = $false
    mt4 = 'Compiles the per-order transform with API 5; production callbacks remain disconnected and startup blocked'
    mt5 = 'Read-only exact-account order-copy margin probe; never changes a real order'
    checks = 'Windows build and CTest passed in this run; not a trading-server acceptance'
  }
  $Manifest | ConvertTo-Json -Depth 4 | Set-Content (Join-Path $BuildDirectory "validation-$Stamp.json") -Encoding UTF8
  Write-Host 'Validation sources built. This is not a deployable dynamic-leverage executor.'
  Write-Host 'Read docs/opening-margin-test-guide.md before Demo testing.'
} finally {
  Stop-Transcript | Out-Null
}
