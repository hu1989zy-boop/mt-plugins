# Run in Visual Studio 2022/2026 Developer PowerShell, or use the .cmd launcher
# from a Native Tools Command Prompt. The generated target is Win32/x86. Builds locally; never installs
# a DLL, starts MT4, connects to an account, or changes a Windows service.
[CmdletBinding()]
param(
    [string]$SdkRoot = '',
    [string]$BuildDirectory = '',
    [switch]$UiOnly,
    [switch]$Executor
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$repositoryRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $projectRoot))
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
        # If Windows cannot start the process, do not inherit a previous zero.
        $global:LASTEXITCODE = $null
        & $Program @Arguments 2>&1 | ForEach-Object {
            $line = $_.ToString()
            Add-Content -LiteralPath $buildLog -Value $line -Encoding UTF8 -ErrorAction Stop
            Write-Host $line
        }
        $nativeExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedPreference
    }
    if ($null -eq $nativeExitCode -or $nativeExitCode -ne 0) {
        throw "$Program failed with exit code $nativeExitCode. See $buildLog"
    }
}

function Get-NativeText {
    param([string]$Program, [string[]]$Arguments)
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        $global:LASTEXITCODE = $null
        $lines = & $Program @Arguments 2>&1
        $nativeExitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $savedPreference
    }
    $result = ($lines | ForEach-Object { $_.ToString() }) -join "`n"
    if ($null -eq $nativeExitCode -or $nativeExitCode -ne 0) { throw "$Program failed ($nativeExitCode): $result" }
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
        default { throw 'Open the Native Tools Command Prompt or Developer PowerShell for VS 2022/2026, then run this script again.' }
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
        CMAKE_GENERATOR_PLATFORM = 'Win32'
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

function Assert-Win32Binary {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Required artifact was not produced: $Path" }
    $stream = [IO.File]::OpenRead($Path)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5A4D) { throw "Artifact is not a PE binary: $Path" }
        $stream.Position = 0x3C
        $peOffset = $reader.ReadUInt32()
        if ($peOffset -gt ($stream.Length - 6)) { throw "Invalid PE header offset: $Path" }
        $stream.Position = $peOffset
        if ($reader.ReadUInt32() -ne 0x00004550 -or $reader.ReadUInt16() -ne 0x014C) {
            throw "Artifact must be an x86/Win32 PE binary: $Path"
        }
    } finally {
        $reader.Dispose()
        $stream.Dispose()
    }
}

try {
    if ($UiOnly -and $Executor) { throw '-UiOnly and -Executor cannot be combined.' }
    $sharedRoot = Join-Path (Split-Path -Parent $projectRoot) 'shared'
    $sharedCMake = Join-Path $sharedRoot 'cmake\Core.cmake'
    if (-not (Test-Path -LiteralPath $sharedCMake -PathType Leaf)) {
        throw "Incomplete MT4 source tree: missing '$sharedCMake'. Copy the complete shared directory alongside mt4 from the same source revision. Copying mt4 alone is not sufficient."
    }
    # An executor build is the default; -Executor remains an explicit alias.
    $buildExecutor = -not [bool]$UiOnly
    $buildRoot = Normalize-Directory $buildRoot
    $cmake = (Get-Command cmake.exe -ErrorAction Stop).Source
    $ctest = Join-Path (Split-Path -Parent $cmake) 'ctest.exe'
    if (-not (Test-Path -LiteralPath $ctest -PathType Leaf)) { throw "ctest.exe is missing beside $cmake" }
    $buildEnvironment = Select-BuildEnvironment $env:VisualStudioVersion $env:VSINSTALLDIR
    $capabilities = Get-NativeText $cmake @('-E', 'capabilities') | ConvertFrom-Json
    $cmakeVersion = [version]('{0}.{1}.{2}' -f $capabilities.version.major, $capabilities.version.minor, $capabilities.version.patch)
    if ($cmakeVersion -lt [version]$buildEnvironment.MinimumCMake -or
        @($capabilities.generators | Where-Object { $_.name -eq $buildEnvironment.Generator }).Count -eq 0) {
        throw "$($buildEnvironment.Generator) needs CMake $($buildEnvironment.MinimumCMake)+ with this generator. Current: $cmake"
    }
    Assert-CompatibleCache $buildRoot $buildEnvironment
    $selection = "$($buildEnvironment.Generator) / $($buildEnvironment.Toolset) / Win32 / Release /MT / _USE_32BIT_TIME_T"
    Write-Host "Build selection: $selection"
    Add-Content -LiteralPath $buildLog -Value $selection -Encoding UTF8
    $apiVersion = $null
    $headerHash = $null
    $configure = @('-S', $projectRoot, '-B', $buildRoot,
                   '-G', $buildEnvironment.Generator, '-A', 'Win32', '-T', $buildEnvironment.Toolset,
                   "-DCMAKE_GENERATOR_INSTANCE=$($buildEnvironment.Instance)",
                   '-DBUILD_TESTING=ON', '-DMT4_TRADECOPIER_BUILD_WINDOWS_UI=ON')
    $configure += ('-DMT4_TRADECOPIER_BUILD_EXECUTOR=' + $(if ($buildExecutor) { 'ON' } else { 'OFF' }))
    if ($buildExecutor) {
        if ([string]::IsNullOrWhiteSpace($SdkRoot)) {
            $SdkRoot = Join-Path $repositoryRoot 'vendor\mt4-sdk'
        }
        $SdkRoot = Normalize-Directory (Resolve-Path -LiteralPath $SdkRoot -ErrorAction Stop).Path
        $sdkHeader = Join-Path $SdkRoot 'Include\MT4ServerAPI.h'
        $headerText = Get-Content -LiteralPath $sdkHeader -Raw
        $versionMatch = [regex]::Match($headerText, '(?m)^\s*#define\s+ServerApiVersion\s+(\d+)')
        if (-not $versionMatch.Success) { throw 'ServerApiVersion was not found in the MT4 Server API header.' }
        $apiVersion = [int]$versionMatch.Groups[1].Value
        if ($apiVersion -ne 5) {
            throw "This source revision targets MT4 Server API 5; supplied header declares $apiVersion. Do not edit the SDK version macro."
        }
        $headerHash = (Get-FileHash -LiteralPath $sdkHeader -Algorithm SHA256).Hash
        $configure += "-DMT4_SERVER_SDK_DIR=$SdkRoot"
    }

    Invoke-Checked $cmake $configure
    Invoke-Checked $cmake @('--build', $buildRoot, '--config', 'Release', '--parallel')
    $testReport = Join-Path $buildRoot 'test-results.xml'
    Invoke-Checked $ctest @('--test-dir', $buildRoot, '-C', 'Release', '--output-on-failure', '--no-tests=error', '--output-junit', $testReport)

    $managerExe = Join-Path $buildRoot 'windows\Release\MT4TradeCopierManager.exe'
    $executorDll = Join-Path $buildRoot 'server\Release\MT4TradeCopierExecutor.dll'
    $journalExe = Join-Path $buildRoot 'Release\MT4TradeCopierJournal.exe'
    Assert-Win32Binary $managerExe
    $executorExportsVerified = $false
    if ($buildExecutor) {
        Assert-Win32Binary $executorDll
        Assert-Win32Binary $journalExe
        $dumpbin = (Get-Command dumpbin.exe -ErrorAction Stop).Source
        $executorExports = Get-NativeText $dumpbin @('/nologo', '/exports', $executorDll)
        $executorExports | Out-File -LiteralPath (Join-Path $buildRoot 'executor-exports.txt') -Encoding UTF8
        foreach ($entry in @('MtSrvAbout', 'MtSrvStartup', 'MtSrvCleanup',
                              'MtSrvPluginCfgSet', 'MtSrvPluginCfgNext', 'MtSrvPluginCfgTotal',
                              'MtSrvTradesAdd', 'MtSrvTradesAddExt', 'MtSrvTradesAddRequest',
                              'MtSrvTradesUpdate', 'MtSrvTradesCloseBy',
                              'MtSrvTradeRequestApply', 'MtSrvTradeRequestRestore')) {
            if ($executorExports -notmatch "(?m)\s$entry\s*(?:=.+)?$") {
                throw "Required MT4 executor DLL export missing: $entry"
            }
        }
        $executorExportsVerified = $true
    }

    $packageName = if ($buildExecutor) { 'package-executor' } else { 'package-ui' }
    $packageRoot = Join-Path $buildRoot $packageName
    New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
    $manifestPath = Join-Path $packageRoot 'build-manifest.json'
    # Do not leave an old success manifest beside partially replaced binaries.
    if (Test-Path -LiteralPath $manifestPath) { Remove-Item -LiteralPath $manifestPath -Force }
    Copy-Item -LiteralPath $managerExe -Destination $packageRoot -Force
    if ($buildExecutor) {
        Copy-Item -LiteralPath $executorDll -Destination $packageRoot -Force
        Copy-Item -LiteralPath $journalExe -Destination $packageRoot -Force
    }
    Copy-Item -LiteralPath (Join-Path $projectRoot 'config\copier.example.json') -Destination $packageRoot -Force
    Copy-Item -LiteralPath (Join-Path $projectRoot 'config\copier.symbol-routing.example.json') -Destination $packageRoot -Force
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\windows-deployment.md') -Destination $packageRoot -Force
    Copy-Item -LiteralPath (Join-Path $projectRoot 'docs\dealer-execution-0.2.0.md') -Destination $packageRoot -Force
    Copy-Item -LiteralPath (Join-Path $projectRoot 'windows\README.md') -Destination (Join-Path $packageRoot 'editor-readme.md') -Force
    Copy-Item -LiteralPath $testReport -Destination $packageRoot -Force
    $artifactNames = @('MT4TradeCopierManager.exe', 'copier.example.json', 'copier.symbol-routing.example.json', 'windows-deployment.md', 'dealer-execution-0.2.0.md', 'editor-readme.md', 'test-results.xml')
    if ($buildExecutor) { $artifactNames += @('MT4TradeCopierExecutor.dll', 'MT4TradeCopierJournal.exe') }
    $artifacts = foreach ($name in $artifactNames) {
        $artifactPath = Join-Path $packageRoot $name
        [ordered]@{
            name = $name
            bytes = (Get-Item -LiteralPath $artifactPath).Length
            sha256 = (Get-FileHash -LiteralPath $artifactPath -Algorithm SHA256).Hash
        }
    }
    $manifest = [ordered]@{
        schema_version = 1
        built_at_utc = [DateTime]::UtcNow.ToString('o')
        source_version = '0.2.0'
        platform = 'mt4'
        purpose = $(if ($buildExecutor) { 'mt4_server_api_5_same_server_trade_copier' } else { 'mt4_local_management_client' })
        can_copy_trades = [bool]$buildExecutor
        trading_enabled_by_default = $false
        build_script_revision = 3
        request_diagnostics_revision = 2
        dealer_execution = 'server_api_requests_lock_confirm'
        dealer_login_required_when_armed = $true
        default_dealer_login = 0
        readonly_dealer_execution_demo_verified = $false
        shared_core = 'trade-copier/shared/core'
        management_protocol_version = 2
        management_pipe_prefix = 'MT4TradeCopier.Management.v1'
        default_server_key = 1
        default_api_app_id = 3401
        compiler_target = $selection
        visual_studio_instance = $buildEnvironment.Instance
        cmake_version = $capabilities.version.string
        sdk_server_api = $apiVersion
        sdk_header_sha256 = $headerHash
        ctest_passed = $true
        ctest_report = 'test-results.xml'
        executor_exports_verified = $executorExportsVerified
        pe_x86_verified = $true
        management_enabled_by_default = $false
        management_demo_verified = $false
        windows_ui_run_verified = $false
        mt4_loading_verified = $false
        mt4_executor_execution_verified = $false
        cross_server_routing_implemented = $false
        artifacts = @($artifacts)
    }
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding UTF8
    Write-Host ''
    Write-Host "Build and tests passed. Local MT4 artifacts: $packageRoot"
    if ($buildExecutor) {
        Write-Host 'Executor built with Armed=0 by default. Follow windows-deployment.md for Demo validation.'
    } else {
        Write-Host 'UI-only package. It contains no trading executor DLL.'
    }
    Write-Host 'Review test-results.xml for skipped tests. No MT4 service or account was changed by this build.'
} catch {
    $_ | Out-String | Add-Content -LiteralPath $buildLog -Encoding UTF8
    Write-Error $_ -ErrorAction Continue
    exit 1
}
