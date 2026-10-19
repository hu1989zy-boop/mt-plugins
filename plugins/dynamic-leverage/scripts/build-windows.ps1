[CmdletBinding()]
param(
  [ValidateSet('all', 'mt4', 'mt5')][string]$Platform = 'all',
  [string]$Mt4SdkRoot = '',
  [string]$Mt5SdkRoot = '',
  [string]$BuildDirectory = '',
  [string]$Generator = 'Visual Studio 17 2022',
  [ValidateSet('Release', 'Debug')][string]$Configuration = 'Release'
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$SourceRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (!$BuildDirectory) { $BuildDirectory = Join-Path $SourceRoot 'build-windows' }
elseif (![IO.Path]::IsPathRooted($BuildDirectory)) { $BuildDirectory = Join-Path $SourceRoot $BuildDirectory }
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)
if (!$Mt4SdkRoot) { $Mt4SdkRoot = Join-Path $SourceRoot '../../vendor/mt4-sdk' }
if (!$Mt5SdkRoot) { $Mt5SdkRoot = Join-Path $SourceRoot '../../vendor/mt5-sdk' }
# Keep the manifest shape stable even when PowerShell emits only one value.
$SelectedPlatforms = @(if ($Platform -eq 'all') { 'mt4'; 'mt5' } else { $Platform })
foreach ($Tool in @('cmake', 'ctest')) {
  if (!(Get-Command $Tool -ErrorAction SilentlyContinue)) { throw "Required build tool missing from PATH: $Tool" }
}
$Specs = @{
  mt4 = @{ Architecture = 'Win32'; Sdk = $Mt4SdkRoot; Header = 'include/MT4ServerAPI.h';
           Flag = 'DYNAMIC_LEVERAGE_MT4_BUILD_SERVER'; SdkFlag = 'MT4_SERVER_SDK_DIR'; Dll = 'MT4DynamicLeverage.dll' }
  mt5 = @{ Architecture = 'x64'; Sdk = $Mt5SdkRoot; Header = 'Include/MT5APIServer.h';
           Flag = 'DYNAMIC_LEVERAGE_MT5_BUILD_SERVER'; SdkFlag = 'MT5_SDK_ROOT'; Dll = 'DynamicLeverageMT5.dll' }
}
# Check every requested SDK before generating either build.
foreach ($Name in $SelectedPlatforms) {
  $Spec = $Specs[$Name]
  $Spec.Sdk = (Resolve-Path $Spec.Sdk).Path
  if (!(Test-Path (Join-Path $Spec.Sdk $Spec.Header))) { throw "Missing licensed $Name Server SDK header" }
}
New-Item -ItemType Directory -Path $BuildDirectory -Force | Out-Null
$Stamp = [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
$Transcript = Join-Path $BuildDirectory "build-$Stamp.log"
Start-Transcript -Path $Transcript | Out-Null
try {
  $Artifacts = @()
  foreach ($Name in $SelectedPlatforms) {
    $Spec = $Specs[$Name]
    $Build = Join-Path $BuildDirectory $Name
    $Source = Join-Path $SourceRoot $Name
    & cmake -S $Source -B $Build -G $Generator -A $Spec.Architecture "-D$($Spec.Flag)=ON" "-D$($Spec.SdkFlag)=$($Spec.Sdk)" '-DBUILD_TESTING=ON' '-DDYNAMIC_LEVERAGE_BUILD_RULES_CHECK=ON'
    if ($LASTEXITCODE -ne 0) { throw "$Name CMake configuration failed ($LASTEXITCODE)" }
    & cmake --build $Build --config $Configuration --parallel
    if ($LASTEXITCODE -ne 0) { throw "$Name compilation failed ($LASTEXITCODE)" }
    & ctest --test-dir $Build -C $Configuration --output-on-failure --no-tests=error
    if ($LASTEXITCODE -ne 0) { throw "$Name tests failed ($LASTEXITCODE); no deployment package produced" }
    $Dll = Join-Path $Build "$Configuration/$($Spec.Dll)"
    $Checker = Join-Path $Build "shared/$Configuration/DynamicLeverageRulesCheck.exe"
    foreach ($File in @($Dll, $Checker)) {
      if (!(Test-Path $File -PathType Leaf)) { throw "Expected build artifact missing: $File" }
    }
    & $Checker --platform $Name --file (Join-Path $SourceRoot "config/$Name.rules.example.json")
    if ($LASTEXITCODE -ne 0) { throw "$Name example rules failed native validation" }
    $Artifacts += @{ Platform = $Name; Dll = $Dll; Checker = $Checker }
  }
  # Each package has a fresh name. Never overwrite a previously deployed runtime.
  $Package = Join-Path $BuildDirectory "package-$Stamp"
  New-Item -ItemType Directory -Path $Package | Out-Null
  foreach ($Artifact in $Artifacts) {
    $Destination = Join-Path $Package "bin/$($Artifact.Platform)"
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    Copy-Item $Artifact.Dll $Destination
    Copy-Item $Artifact.Checker $Destination
  }
  New-Item -ItemType Directory -Path (Join-Path $Package 'management/web') -Force | Out-Null
  foreach ($File in @('server.py', 'schema.py', 'start-windows.cmd')) {
    Copy-Item (Join-Path $SourceRoot "management/$File") (Join-Path $Package 'management')
  }
  Get-ChildItem (Join-Path $SourceRoot 'management/web') -File | Copy-Item -Destination (Join-Path $Package 'management/web')
  New-Item -ItemType Directory -Path (Join-Path $Package 'config') | Out-Null
  foreach ($File in @('mt4.rules.example.json', 'mt5.rules.example.json', 'parameters.example.ini')) {
    Copy-Item (Join-Path $SourceRoot "config/$File") (Join-Path $Package 'config')
  }
  Copy-Item (Join-Path $SourceRoot 'docs') $Package -Recurse
  Copy-Item (Join-Path $SourceRoot 'docs/windows-quickstart.md') (Join-Path $Package 'README.md')
  $Manifest = @{
    plugin = 'dynamic-leverage'; version = '0.2.0'; configuration = $Configuration
    built_at_utc = [DateTime]::UtcNow.ToString('o'); platforms = $SelectedPlatforms
    checks = 'CMake build, CTest including native DLL load/exports, RulesCheck examples passed'
    server_demo_verified = $false
    native_execution_available = $false
    execution_policy = 'new_positions_only_post_open_volume'
    files = @(Get-ChildItem $Package -Recurse -File | ForEach-Object {
      @{ path = $_.FullName.Substring($Package.Length + 1).Replace('\', '/'); sha256 = (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
    })
  }
  $Manifest | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $Package 'manifest.json') -Encoding UTF8
  Compress-Archive -Path (Join-Path $Package '*') -DestinationPath "$Package.zip"
  Write-Host "Validated build package: $Package.zip"
  Write-Host 'Per-opening margin execution is under validation; legacy account-wide execution is disabled.'
} finally {
  Stop-Transcript | Out-Null
}
