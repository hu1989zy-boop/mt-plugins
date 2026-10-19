param(
  [string]$SdkRoot = (Join-Path $PSScriptRoot "../../../../vendor/mt5-sdk"),
  [string]$BuildDir = (Join-Path $PSScriptRoot "../build-windows"),
  [ValidateSet("Release", "Debug")][string]$Configuration = "Release"
)
$ErrorActionPreference = "Stop"
$SourceDir = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$SdkRoot = (Resolve-Path $SdkRoot).Path
cmake -S $SourceDir -B $BuildDir -A x64 "-DMT5_SDK_ROOT=$SdkRoot" -DCLOSEDALLOC_MT5_BUILD_SERVER=ON -DBUILD_TESTING=ON
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
# Rebuild the core as well as the DLL when switching /MD to /MT. Do not reuse
# object/library files from an earlier runtime configuration.
cmake --build $BuildDir --config $Configuration --clean-first --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }
ctest --test-dir $BuildDir -C $Configuration --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Tests failed" }
Write-Host "Built $BuildDir/$Configuration/ClosedTradeAllocatorMT5.dll"
