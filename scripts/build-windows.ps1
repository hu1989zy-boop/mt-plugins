# Run in Visual Studio 2022/2026 Developer PowerShell, or use the .cmd launcher
# from an x64 Native Tools Command Prompt. Builds locally; never installs
# a DLL, starts MT5, connects to an account, or changes a Windows service.
[CmdletBinding()]
param(
    [string]$SdkRoot = '',
    [string]$BuildDirectory = '',
    [switch]$UiOnly
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$buildRoot = if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    Join-Path $projectRoot 'build-windows'
} else {
    $requestedBuildPath = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
        $BuildDirectory
    } else { Join-Path $projectRoot $BuildDirectory }
    [IO.Path]::GetFullPath($requestedBuildPath)
}
if ($buildRoot.TrimEnd([char[]]'\/') -eq $projectRoot.TrimEnd([char[]]'\/')) {
    throw 'BuildDirectory must be a separate build directory, not the source root.'
}
New-Item -ItemType Directory -Force -Path $buildRoot | Out-Null
$buildLog = Join-Path $buildRoot 'build.log'
Set-Content -LiteralPath $buildLog -Value ('Build started UTC: ' + [DateTime]::UtcNow.ToString('o')) -Encoding UTF8

function Invoke-Checked {
    param([string]$Program, [string[]]$Arguments)
    Write-Host ('> ' + $Program + ' ' + ($Arguments -join ' '))
    $savedPreference = $ErrorActionPreference
    try {
        # Windows PowerShell 5.1 wraps native stderr in ErrorRecord objects.
        # A warning on stderr must not override the program's actual exit code.
        $ErrorActionPreference = 'Continue'
        & $Program @Arguments 2>&1 | ForEach-Object {
            $line = $_.ToString()
            Add-Content -LiteralPath $buildLog -Value $line -Encoding UTF8 -ErrorAction Stop
            Write-Host $line
        }
        $nativeExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedPreference
    }
    if ($nativeExitCode -ne 0) {
        throw "$Program failed with exit code $nativeExitCode. See $buildLog"
    }
}

function Get-NativeText {
    param([string]$Program, [string[]]$Arguments)
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $lines = & $Program @Arguments 2>&1
        $nativeExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedPreference
    }
    $result = ($lines | ForEach-Object { $_.ToString() }) -join "`n"
    if ($nativeExitCode -ne 0) { throw "$Program failed ($nativeExitCode): $result" }
    return $result
}

function Normalize-Directory {
    param([string]$Directory)
    $full = [IO.Path]::GetFullPath($Directory).Replace('/', '\')
    if ($full -eq [IO.Path]::GetPathRoot($full)) { return $full }
    return $full.TrimEnd([char[]]'\/')
}

function Select-BuildEnvironment {
    param([string]$VsVersion, [string]$VsDirectory)
    $major = ($VsVersion -split '\.')[0]
    switch ($major) {
        '17' { $generator = 'Visual Studio 17 2022'; $toolset = 'v143'; $minimum = '3.21' }
        '18' { $generator = 'Visual Studio 18 2026'; $toolset = 'v145'; $minimum = '4.2' }
        default { throw 'Open the x64 Native Tools Command Prompt or Developer PowerShell for VS 2022/2026, then run this script again.' }
    }
    if ([string]::IsNullOrWhiteSpace($VsDirectory) -or
        -not (Test-Path -LiteralPath $VsDirectory -PathType Container)) {
        throw 'VSINSTALLDIR is missing or invalid. Open a Visual Studio developer shell first.'
    }
    return [pscustomobject]@{
        Generator = $generator
        Toolset = $toolset
        MinimumCMake = $minimum
        Instance = Normalize-Directory $VsDirectory
    }
}

function Assert-CompatibleCache {
    param([string]$Directory, $Environment)
    $cachePath = Join-Path $Directory 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) { return }
    $cache = @{}
    foreach ($line in Get-Content -LiteralPath $cachePath) {
        if ($line -match '^(CMAKE_GENERATOR(?:_PLATFORM|_TOOLSET|_INSTANCE)?|CMAKE_HOME_DIRECTORY):[^=]*=(.*)$') {
            $cache[$Matches[1]] = $Matches[2]
        }
    }
    $expected = @{
        CMAKE_GENERATOR = $Environment.Generator
        CMAKE_GENERATOR_PLATFORM = 'x64'
        CMAKE_GENERATOR_TOOLSET = $Environment.Toolset
        CMAKE_GENERATOR_INSTANCE = $Environment.Instance
        CMAKE_HOME_DIRECTORY = Normalize-Directory $projectRoot
    }
    foreach ($key in $expected.Keys) {
        if (-not $cache.ContainsKey($key)) { continue }
        $actual = $cache[$key]
        if (($key -eq 'CMAKE_GENERATOR_INSTANCE' -or $key -eq 'CMAKE_HOME_DIRECTORY') -and $actual) {
            $actual = Normalize-Directory $actual
        }
        if ($actual -ne $expected[$key]) {
            throw "Existing build cache differs at ${key}: '$actual'. Use -BuildDirectory with a NEW directory, or rename '$Directory'. No cache was deleted."
        }
    }
}

try {
    $buildRoot = Normalize-Directory $buildRoot
    $cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
    $ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
    if (-not (Test-Path -LiteralPath $ctest -PathType Leaf)) { throw "ctest.exe is missing beside $cmake" }
    $buildEnvironment = Select-BuildEnvironment $env:VisualStudioVersion $env:VSINSTALLDIR
    $capabilities = Get-NativeText $cmake @('-E', 'capabilities') | ConvertFrom-Json
    # Vendor builds can suffix version.string (e.g. -msvc1); compare numeric fields.
    $cmakeVersion = [version]('{0}.{1}.{2}' -f $capabilities.version.major, $capabilities.version.minor, $capabilities.version.patch)
    if ($cmakeVersion -lt [version]$buildEnvironment.MinimumCMake -or
        @($capabilities.generators | Where-Object { $_.name -eq $buildEnvironment.Generator }).Count -eq 0) {
        throw "$($buildEnvironment.Generator) needs CMake $($buildEnvironment.MinimumCMake)+ with this generator. Update the VS C++ CMake component or put a newer CMake on PATH. Current: $cmake"
    }
    Assert-CompatibleCache $buildRoot $buildEnvironment
    $selection = "$($buildEnvironment.Generator) / $($buildEnvironment.Toolset) / x64 / Release /MT"
    Write-Host "Build selection: $selection"
    Add-Content -LiteralPath $buildLog -Value $selection -Encoding UTF8
    $apiVersion = $null
    $headerHash = $null
    $configure = @('-S', $projectRoot, '-B', $buildRoot,
                   '-G', $buildEnvironment.Generator, '-A', 'x64', '-T', $buildEnvironment.Toolset,
                   "-DCMAKE_GENERATOR_INSTANCE=$($buildEnvironment.Instance)",
                   '-DBUILD_TESTING=ON', '-DTRADECOPIER_BUILD_WINDOWS_UI=ON')
    if ($UiOnly) {
        $configure += '-DTRADECOPIER_BUILD_SDK_PROBE=OFF'
    } else {
        if ([string]::IsNullOrWhiteSpace($SdkRoot)) {
            $SdkRoot = Join-Path $projectRoot 'vendor\mt5-sdk'
        }
        $SdkRoot = Normalize-Directory (Resolve-Path -LiteralPath $SdkRoot -ErrorAction Stop).Path
        $sdkHeader = Join-Path $SdkRoot 'Include\MT5APIServer.h'
        $headerText = Get-Content -LiteralPath $sdkHeader -Raw
        $versionMatch = [regex]::Match($headerText, '(?m)^\s*#define\s+MTServerAPIVersion\s+(\d+)')
        if (-not $versionMatch.Success) { throw 'MTServerAPIVersion was not found in the SDK header.' }
        $apiVersion = [int]$versionMatch.Groups[1].Value
        if ($apiVersion -ne 6182) {
            throw "This source revision targets Server API 6182; supplied header declares $apiVersion. Do not edit the SDK version macro."
        }
        $headerHash = (Get-FileHash -LiteralPath $sdkHeader -Algorithm SHA256).Hash
        $configure += @('-DTRADECOPIER_BUILD_SDK_PROBE=ON', "-DMT5_SERVER_SDK_DIR=$SdkRoot")
    }

    Invoke-Checked $cmake $configure
    Invoke-Checked $cmake @('--build', $buildRoot, '--config', 'Release', '--parallel')
    Invoke-Checked $ctest @('--test-dir', $buildRoot, '-C', 'Release', '--output-on-failure', '--no-tests=error')

    $managerExe = Join-Path $buildRoot 'windows\Release\TradeCopierManager.exe'
    if (-not (Test-Path -LiteralPath $managerExe -PathType Leaf)) { throw 'Editor executable was not produced.' }
    $probeDll = Join-Path $buildRoot 'server\Release\TradeCopierSdkProbe.dll'
    $exportsVerified = $false
    if (-not $UiOnly) {
        if (-not (Test-Path -LiteralPath $probeDll -PathType Leaf)) { throw 'Probe DLL was not produced.' }
        $dumpbin = (Get-Command dumpbin.exe -ErrorAction Stop).Source
        $exportText = Get-NativeText $dumpbin @('/nologo', '/exports', $probeDll)
        $exportText | Out-File -LiteralPath (Join-Path $buildRoot 'probe-exports.txt') -Encoding UTF8
        foreach ($entry in @('MTServerAbout', 'MTServerCreate')) {
            if ($exportText -notmatch "(?m)\s$entry\s*(?:=.+)?$") {
                throw "Required DLL export missing: $entry"
            }
        }
        $exportsVerified = $true
    }

    $packageName = if ($UiOnly) { 'package-ui' } else { 'package-probe' }
    $packageRoot = Join-Path $buildRoot $packageName
    New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
    $manifestPath = Join-Path $packageRoot 'build-manifest.json'
    # A failed replacement must never leave a prior success manifest beside
    # partly updated artifacts. Publish a new manifest only after all hashes.
    if (Test-Path -LiteralPath $manifestPath) { Remove-Item -LiteralPath $manifestPath -Force }
    Copy-Item -LiteralPath $managerExe -Destination (Join-Path $packageRoot 'TradeCopierManager.exe') -Force
    if (-not $UiOnly) {
        Copy-Item -LiteralPath $probeDll -Destination (Join-Path $packageRoot 'TradeCopierSdkProbe.dll') -Force
    }
    Copy-Item -LiteralPath (Join-Path $projectRoot 'config\copier.example.json') -Destination $packageRoot -Force
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\windows-test-deployment.md') -Destination $packageRoot -Force
    Copy-Item -LiteralPath (Join-Path $projectRoot 'windows\README.md') -Destination (Join-Path $packageRoot 'editor-readme.md') -Force
    if (-not $UiOnly) {
        Copy-Item -LiteralPath (Join-Path $projectRoot 'server\README.md') -Destination (Join-Path $packageRoot 'probe-readme.md') -Force
    }
    $artifacts = @()
    $artifactNames = @('TradeCopierManager.exe')
    if (-not $UiOnly) { $artifactNames += 'TradeCopierSdkProbe.dll' }
    foreach ($name in $artifactNames) {
        $artifactPath = Join-Path $packageRoot $name
        $artifacts += [ordered]@{
            name = $name
            bytes = (Get-Item -LiteralPath $artifactPath).Length
            sha256 = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash
        }
    }
    $manifest = [ordered]@{
        schema_version = 1
        built_at_utc = [DateTime]::UtcNow.ToString('o')
        source_version = '0.2.0'
        purpose = 'offline_configuration_and_read_only_sdk_probe'
        can_copy_trades = $false
        build_script_revision = 2
        compiler_target = $selection
        visual_studio_instance = $buildEnvironment.Instance
        cmake_version = $capabilities.version.string
        sdk_server_api = $apiVersion
        sdk_header_sha256 = $headerHash
        unit_tests_passed = $true
        probe_exports_verified = $exportsVerified
        windows_ui_run_verified = $false
        mt5_loading_verified = $false
        artifacts = $artifacts
    }
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    Write-Host ''
    Write-Host "Build and tests passed. Local artifacts: $packageRoot"
    Write-Host 'This package does not copy trades. No MT5 service or account was changed.'
    Write-Host 'Next: follow docs/windows-test-deployment.md for the manual test-server loading check.'
} catch {
    $_ | Out-String | Add-Content -LiteralPath $buildLog -Encoding UTF8
    Write-Error $_ -ErrorAction Continue
    exit 1
}
