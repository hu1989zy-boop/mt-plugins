# One-time repair for the MT5 0.7.1 PipeServer C2661 build error.
# The existing CMake cache identifies the source tree actually being compiled.
# Run from the same x64 Visual Studio developer shell as build-windows.cmd.
[CmdletBinding()]
param(
    [string]$BuildDirectory = 'D:\MT5Build\TradeCopier-0.7.1',
    [string]$SdkRoot = 'D:\SDK\mt5-sdk',
    [switch]$Rebuild
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
try {
    $buildRoot = (Resolve-Path -LiteralPath $BuildDirectory).Path
    $cachePath = Join-Path $buildRoot 'CMakeCache.txt'
    $entries = @(Get-Content -LiteralPath $cachePath | Where-Object {
        $_ -match '^CMAKE_HOME_DIRECTORY:INTERNAL='
    })
    if ($entries.Count -ne 1) { throw "Cannot identify a unique source directory in $cachePath" }
    $sourceRoot = $entries[0].Substring('CMAKE_HOME_DIRECTORY:INTERNAL='.Length)
    if (-not [IO.Path]::IsPathRooted($sourceRoot)) { throw 'Cached source directory must be absolute.' }
    $sourceRoot = (Resolve-Path -LiteralPath $sourceRoot).Path
    $sourcePath = Join-Path $sourceRoot 'server\executor.cpp'
    $buildScript = Join-Path $sourceRoot 'scripts\build-windows.ps1'
    if (-not (Test-Path -LiteralPath $buildScript -PathType Leaf)) {
        throw "Build script missing: $buildScript"
    }

    $oldCall = 'management_=std::make_unique<management::PipeServer>(info.server_id,parameters_.app,parameters_.dealer_login);'
    $fixedCall = 'management_=std::make_unique<management::PipeServer>(info.server_id,parameters_.app);'
    $dealerCall = 'std::make_unique<Mt5Port>(api,api_gate_,callback_fault_,info.server_id,parameters_.app,parameters_.dealer_login)'
    $bytes = [IO.File]::ReadAllBytes($sourcePath)
    $hasBom = $bytes.Length -ge 3 -and $bytes[0] -eq 239 -and $bytes[1] -eq 187 -and $bytes[2] -eq 191
    $encoding = New-Object System.Text.UTF8Encoding -ArgumentList $hasBom, $true
    $source = [IO.File]::ReadAllText($sourcePath, $encoding)
    $oldCount = [regex]::Matches($source, [regex]::Escape($oldCall)).Count
    $fixedCount = [regex]::Matches($source, [regex]::Escape($fixedCall)).Count
    if (-not $source.Contains($dealerCall)) {
        throw 'Unexpected source revision: Mt5Port DealerLogin call was not found. No changes made.'
    }
    Write-Host "SOURCE: $sourcePath"
    if ($oldCount -eq 1 -and $fixedCount -eq 0) {
        $backupPath = $sourcePath + '.pipe-constructor-' + [Guid]::NewGuid().ToString('N') + '.bak'
        [IO.File]::Copy($sourcePath, $backupPath, $false)
        [IO.File]::WriteAllText($sourcePath, $source.Replace($oldCall, $fixedCall), $encoding)
        Write-Host "BACKUP: $backupPath"
        Write-Host 'REPAIRED: removed DealerLogin only from the PipeServer constructor.'
    } elseif ($oldCount -eq 0 -and $fixedCount -eq 1) {
        Write-Host 'ALREADY FIXED: the cached source uses the two-argument PipeServer constructor.'
    } else {
        throw 'Unexpected constructor contents or multiple matches. No changes made.'
    }
    $verified = [IO.File]::ReadAllText($sourcePath, $encoding)
    if ($verified.Contains($oldCall) -or -not $verified.Contains($fixedCall) -or -not $verified.Contains($dealerCall)) {
        throw 'Source verification failed. Stop before rebuilding and inspect the backup.'
    }
    Write-Host $fixedCall
    if ($Rebuild) {
        Write-Host "BUILD: $buildScript"
        # A separate process inherits the VS environment and exposes exit 1
        # from build-windows.ps1 as an unambiguous native exit status.
        $powershell = Join-Path $PSHOME 'powershell.exe'
        & $powershell -NoLogo -NoProfile -ExecutionPolicy Bypass -File $buildScript -Executor -SdkRoot $SdkRoot -BuildDirectory $buildRoot
        $buildExitCode = $LASTEXITCODE
        if ($buildExitCode -ne 0) {
            throw "Build failed with exit code $buildExitCode. See $(Join-Path $buildRoot 'build.log')"
        }
    } else {
        Write-Host 'Source repair complete. Use -Rebuild in the x64 VS developer shell to build and test.'
    }
} catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
