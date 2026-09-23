#requires -Version 5.1
[CmdletBinding()]
param(
    [ValidateSet('Capture', 'Export', 'Stop', 'Library')]
    [string]$Mode = 'Capture',
    [string]$SettingsPath = (Join-Path $PSScriptRoot 'collector-settings.json'),
    [string]$Date = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$script:ToolId = 'mt5-stress-capture-v1'
$script:Warnings = New-Object 'System.Collections.Generic.List[string]'
$script:ActiveRunDirectory = $null

function Save-Json($Value, [string]$Path) {
    $text = ConvertTo-Json -InputObject $Value -Depth 14
    [IO.File]::WriteAllText($Path, $text, (New-Object Text.UTF8Encoding($false)))
}

function Add-Warning([string]$Message) {
    $script:Warnings.Add($Message)
    Write-Host ('提示: ' + $Message) -ForegroundColor Yellow
}

function Get-FullPath([string]$Path) {
    return [IO.Path]::GetFullPath($Path)
}

function Test-UnderDirectory([string]$Path, [string]$Directory) {
    $root = (Get-FullPath $Directory).TrimEnd([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar)
    $full = Get-FullPath $Path
    return $full.Equals($root, [StringComparison]::OrdinalIgnoreCase) -or
        $full.StartsWith($root + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)
}

function Assert-NoReparseAncestor([string]$Path) {
    $current = Get-FullPath $Path
    while (-not [string]::IsNullOrEmpty($current)) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw ('路径包含链接或重解析点，请使用真实目录: ' + $current)
            }
        }
        $parent = [IO.Directory]::GetParent($current)
        if ($null -eq $parent) { break }
        $current = $parent.FullName
    }
}

function Read-Settings {
    $settings = Get-Content -LiteralPath $SettingsPath -Raw -Encoding UTF8 | ConvertFrom-Json
    # Recovery must not depend on valid capture duration or quota settings.
    if ($Mode -eq 'Stop') { return $settings }
    foreach ($field in @('baseline_seconds', 'tail_seconds', 'maximum_capture_minutes',
        'maximum_performance_mb', 'maximum_source_file_mb', 'maximum_total_source_mb')) {
        if ($null -eq $settings.PSObject.Properties[$field]) { throw ('配置缺少 ' + $field) }
        $value = [int]$settings.$field
        if ($value -lt 1) { throw ('配置必须大于零: ' + $field) }
    }
    if ($settings.maximum_capture_minutes -gt 120 -or $settings.baseline_seconds -gt 300 -or
        $settings.tail_seconds -gt 300 -or $settings.maximum_performance_mb -gt 1024 -or
        $settings.maximum_source_file_mb -gt 1800 -or $settings.maximum_total_source_mb -gt 16384) {
        throw '配置超出本工具支持的范围，请保留默认值。'
    }
    if (($settings.baseline_seconds + $settings.tail_seconds) -ge ($settings.maximum_capture_minutes * 60)) {
        throw '性能采集时长必须大于基线和收尾时间之和。'
    }
    return $settings
}

function Get-OutputRoot($Settings) {
    if ([string]::IsNullOrWhiteSpace($Settings.output_directory)) {
        return Join-Path $PSScriptRoot 'captures'
    }
    if (-not [IO.Path]::IsPathRooted($Settings.output_directory)) { throw '输出目录必须是完整路径。' }
    return Get-FullPath $Settings.output_directory
}

function Get-LogDirectory($Settings) {
    $path = $Settings.server_log_directory
    if ([string]::IsNullOrWhiteSpace($path)) { $path = Join-Path $Settings.server_root 'logs' }
    if (-not (Test-Path -LiteralPath $path -PathType Container)) {
        Write-Host ('没有找到默认日志目录: ' + $path) -ForegroundColor Yellow
        $path = (Read-Host '请输入 Trade Server 原始日志目录完整路径').Trim().Trim('"')
    }
    if ([string]::IsNullOrWhiteSpace($path) -or -not [IO.Path]::IsPathRooted($path) -or
        -not (Test-Path -LiteralPath $path -PathType Container)) { throw '日志目录不存在，未开始采集。' }
    Assert-NoReparseAncestor $path
    $path = (Get-Item -LiteralPath $path).FullName
    $files = @(Get-ChildItem -LiteralPath $path -File -Force | Where-Object {
        $_.Name -match '^\d{8}\.(log|zip)$'
    })
    if ($files.Count -eq 0) {
        throw ('目录没有发现 YYYYMMDD.log 或 YYYYMMDD.zip，请核对 Trade Server 日志目录: ' + $path)
    }
    return $path
}

function Get-DayNames([datetime]$Start, [datetime]$End, [int]$Padding = 1) {
    $day = $Start.Date.AddDays(-$Padding)
    $last = $End.Date.AddDays($Padding)
    if ($last -lt $day -or ($last - $day).TotalDays -gt 7) { throw '本工具一次最多收集 8 个日期。' }
    $names = New-Object 'System.Collections.Generic.List[string]'
    while ($day -le $last) { $names.Add($day.ToString('yyyyMMdd')); $day = $day.AddDays(1) }
    return $names.ToArray()
}

function Get-ServerLogSelection([string]$Directory, [string[]]$Days) {
    $selected = New-Object 'System.Collections.Generic.List[object]'
    foreach ($file in @(Get-ChildItem -LiteralPath $Directory -File -Force)) {
        if ($file.Name -match '^(\d{8})\.(log|zip)$' -and $Days -contains $Matches[1]) {
            $selected.Add([pscustomobject]@{ path = $file.FullName; relative = ('server-logs/' + $file.Name); category = 'server_log' })
        }
    }
    return @($selected.ToArray() | Sort-Object relative)
}

# Source files are opened for read only. Capture the current prefix without waiting
# for a growing log to stop. Never decode, truncate, rewrite, or rename a source.
function Copy-SharedSnapshot([string]$Source, [string]$Destination, [long]$MaximumBytes) {
    Assert-NoReparseAncestor $Source
    $before = Get-Item -LiteralPath $Source -Force
    $result = [ordered]@{
        source = $before.FullName; status = 'pending'; snapshot_utc = [DateTime]::UtcNow.ToString('o')
        source_length_before_open = [long]$before.Length; source_last_write_utc = $before.LastWriteTimeUtc.ToString('o')
        captured_length = 0L; source_length_after_copy = $null; grew_during_copy = $false
        retained_bytes = 0L; atomic_snapshot = $false; sha256 = $null; error = $null
    }
    $inputStream = $null; $outputStream = $null; $partial = $Destination + '.partial'
    $createdPartial = $false; $publishedCopy = $false
    try {
        $share = [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete
        $inputStream = New-Object IO.FileStream($Source, [IO.FileMode]::Open, [IO.FileAccess]::Read, $share)
        $length = $inputStream.Length
        $result.captured_length = $length
        if ($length -gt $MaximumBytes) { $result.status = 'skipped_size_limit'; return [pscustomobject]$result }
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Destination))
        $outputStream = New-Object IO.FileStream($partial, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        $createdPartial = $true
        $buffer = New-Object byte[] (1024 * 1024)
        $remaining = $length
        while ($remaining -gt 0) {
            $read = $inputStream.Read($buffer, 0, [int][Math]::Min($buffer.Length, $remaining))
            if ($read -le 0) { throw '源文件在复制期间缩短；副本不完整。' }
            $outputStream.Write($buffer, 0, $read)
            $remaining -= $read
        }
        $outputStream.Flush(); $outputStream.Dispose(); $outputStream = $null
        $result.source_length_after_copy = $inputStream.Length
        if ($result.source_length_after_copy -lt $length) { throw '源文件在复制期间被截短。' }
        $result.grew_during_copy = $result.source_length_after_copy -gt $length
        $inputStream.Dispose(); $inputStream = $null
        [IO.File]::Move($partial, $Destination)
        $publishedCopy = $true
        $result.retained_bytes = $length
        $result.sha256 = (Get-FileHash -LiteralPath $Destination -Algorithm SHA256).Hash.ToLowerInvariant()
        $result.status = 'copied_prefix'
    } catch {
        $result.status = 'copy_failed'; $result.error = $_.Exception.Message
    } finally {
        if ($null -ne $outputStream) { $outputStream.Dispose() }
        if ($null -ne $inputStream) { $inputStream.Dispose() }
        if ($result.status -ne 'copied_prefix' -and $createdPartial -and -not $publishedCopy) {
            try { if (Test-Path -LiteralPath $partial) { [IO.File]::Delete($partial) } }
            catch {
                $result.error += ('；本次临时副本清理失败: ' + $_.Exception.Message)
                if (Test-Path -LiteralPath $partial) { $result.retained_bytes = (Get-Item -LiteralPath $partial).Length }
            }
        }
    }
    return [pscustomobject]$result
}

function Copy-SelectedFiles([object[]]$Selection, [string]$RunDirectory, $Settings) {
    $records = New-Object 'System.Collections.Generic.List[object]'
    $remaining = [long]$Settings.maximum_total_source_mb * 1MB
    foreach ($entry in $Selection) {
        Write-Host ('收集: ' + $entry.path)
        $dest = Join-Path $RunDirectory $entry.relative
        $limit = [Math]::Min(([long]$Settings.maximum_source_file_mb * 1MB), $remaining)
        try {
            $result = Copy-SharedSnapshot $entry.path $dest $limit
        } catch {
            $result = [pscustomobject]@{ source = $entry.path; status = 'copy_failed'; captured_length = 0L; retained_bytes = 0L; error = $_.Exception.Message }
        }
        $result | Add-Member -NotePropertyName relative -NotePropertyValue $entry.relative
        $result | Add-Member -NotePropertyName category -NotePropertyValue $entry.category
        $records.Add($result)
        $remaining = [Math]::Max(0L, $remaining - $result.retained_bytes)
        if ($result.status -ne 'copied_prefix') { Add-Warning ('未完整收集 ' + $entry.path + '；状态 ' + $result.status) }
    }
    return $records.ToArray()
}

function Write-Package([string]$Directory) {
    Add-Type -AssemblyName System.IO.Compression
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = $Directory + '.zip'
    $partialZip = $zip + '.partial'
    $stream = $null; $archive = $null
    $files = @(Get-ChildItem -LiteralPath $Directory -Recurse -File -Force | Where-Object { $_.Extension -ne '.partial' })
    try {
        $stream = New-Object IO.FileStream($partialZip, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        $archive = New-Object IO.Compression.ZipArchive($stream, [IO.Compression.ZipArchiveMode]::Create, $true)
        foreach ($file in $files) {
            Assert-NoReparseAncestor $file.FullName
            $relative = $file.FullName.Substring($Directory.TrimEnd('\', '/').Length + 1).Replace('\', '/')
            [void][IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $relative, [IO.Compression.CompressionLevel]::Optimal)
        }
        $archive.Dispose(); $archive = $null
        $stream.Dispose(); $stream = $null
        # Reopen and read each entry, checking bytes against its uncompressed copy.
        $verify = [IO.Compression.ZipFile]::OpenRead($partialZip)
        try {
            if ($verify.Entries.Count -ne $files.Count) { throw 'ZIP 文件数量校验失败。' }
            foreach ($entry in $verify.Entries) {
                $source = Join-Path $Directory $entry.FullName
                $reader = $entry.Open(); $sha = [Security.Cryptography.SHA256]::Create()
                try { $actual = [BitConverter]::ToString($sha.ComputeHash($reader)).Replace('-', '') }
                finally { $reader.Dispose(); $sha.Dispose() }
                if ($actual -ne (Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash) { throw ('ZIP 校验失败: ' + $entry.FullName) }
            }
        } finally { $verify.Dispose() }
        [IO.File]::Move($partialZip, $zip)
        $hash = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
        [IO.File]::WriteAllText(($zip + '.sha256.txt'), ($hash + '  ' + [IO.Path]::GetFileName($zip) + [Environment]::NewLine))
        return $zip
    } finally {
        if ($null -ne $archive) { $archive.Dispose() }
        if ($null -ne $stream) { $stream.Dispose() }
    }
}

function Invoke-Logman([string[]]$Arguments, [switch]$AllowFailure) {
    $executable = "$env:SystemRoot\System32\logman.exe"
    if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) { throw '找不到 Windows logman.exe。' }
    $previousPreference = $ErrorActionPreference
    try {
        # Windows PowerShell 5.1 may turn native stderr into terminating errors
        # under Stop. Capture it first, then apply our explicit exit-code policy.
        $ErrorActionPreference = 'Continue'
        $text = (& $executable @Arguments 2>&1 | Out-String)
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    if ($code -ne 0 -and -not $AllowFailure) { throw ('logman 返回 ' + $code + ': ' + $text.Trim()) }
    return [pscustomobject]@{ exit_code = $code; text = $text.Trim() }
}

function Get-LocalizedCounterPaths {
    if (-not ('Mt5Capture.PdhNames' -as [type])) {
        Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
using System.Text;
namespace Mt5Capture {
    public static class PdhNames {
        [DllImport("pdh.dll", CharSet = CharSet.Unicode)]
        private static extern uint PdhLookupPerfNameByIndexW(string machine, uint index, StringBuilder name, ref uint size);
        public static string Lookup(uint index) {
            uint size = 0;
            uint status = PdhLookupPerfNameByIndexW(null, index, null, ref size);
            if (size == 0 || (status != 0 && status != 0x800007D2))
                throw new InvalidOperationException("PDH name size: " + status.ToString("X"));
            StringBuilder name = new StringBuilder((int)size);
            status = PdhLookupPerfNameByIndexW(null, index, name, ref size);
            if (status != 0) throw new InvalidOperationException("PDH name: " + status.ToString("X"));
            return name.ToString();
        }
    }
}
'@
    }
    $english = (Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Perflib\009' -Name Counter).Counter
    $indices = @{}
    for ($n = 0; $n -lt ($english.Count - 1); $n += 2) { $indices[$english[$n + 1]] = [uint32]$english[$n] }
    $groups = @(
        @{ object = 'Processor Information'; instances = '(*)'; counters = @('% Processor Time') },
        @{ object = 'System'; instances = ''; counters = @('Processor Queue Length') },
        @{ object = 'Memory'; instances = ''; counters = @('Available MBytes', '% Committed Bytes In Use', 'Page Reads/sec') },
        @{ object = 'PhysicalDisk'; instances = '(*)'; counters = @('Avg. Disk sec/Read', 'Avg. Disk sec/Write', 'Current Disk Queue Length', 'Disk Read Bytes/sec', 'Disk Write Bytes/sec') },
        @{ object = 'Process'; instances = '(*)'; counters = @('ID Process', '% Processor Time', 'Private Bytes', 'Working Set', 'IO Read Bytes/sec', 'IO Write Bytes/sec') }
    )
    $paths = New-Object 'System.Collections.Generic.List[object]'
    foreach ($group in $groups) {
        if (-not $indices.ContainsKey($group.object)) { throw ('缺少计数器分类: ' + $group.object) }
        $object = [Mt5Capture.PdhNames]::Lookup($indices[$group.object])
        foreach ($counter in $group.counters) {
            if (-not $indices.ContainsKey($counter)) { throw ('缺少计数器: ' + $counter) }
            $name = [Mt5Capture.PdhNames]::Lookup($indices[$counter])
            $paths.Add([pscustomobject]@{
                english = ('\{0}{1}\{2}' -f $group.object, $group.instances, $counter)
                local = ('\{0}{1}\{2}' -f $object, $group.instances, $name)
            })
        }
    }
    return $paths.ToArray()
}

function Start-Performance($Session, $Settings, [string]$RunDirectory) {
    $paths = @(Get-LocalizedCounterPaths)
    Save-Json $paths (Join-Path $RunDirectory 'counter-names.json')
    $counterFile = Join-Path $RunDirectory 'counters.txt'
    $paths.local | Out-File -LiteralPath $counterFile -Encoding Unicode
    # Real samples, not just registering paths. Transient process churn may emit
    # nonterminating errors, so record them and require samples for each category.
    $sampleErrors = @()
    $sample = @(Get-Counter -Counter $paths.local -SampleInterval 1 -MaxSamples 2 -ErrorAction SilentlyContinue -ErrorVariable sampleErrors)
    $valid = @($sample | ForEach-Object { $_.CounterSamples } | Where-Object { $_.Status -eq 0 -or $_.Status -eq 1 })
    Save-Json ([ordered]@{ valid_samples = $valid.Count; errors = @($sampleErrors | ForEach-Object { $_.ToString() }) }) (Join-Path $RunDirectory 'performance-preflight.json')
    foreach ($path in $paths.local) {
        # Wildcard paths expand into instance names in sampled output.
        $pattern = '*' + $path
        if (@($valid | Where-Object { $_.Path -like $pattern }).Count -eq 0) { throw ('计数器未取得有效样本: ' + $path) }
    }
    $perf = Join-Path $RunDirectory 'performance'
    [void][IO.Directory]::CreateDirectory($perf)
    $limit = [TimeSpan]::FromMinutes($Settings.maximum_capture_minutes).ToString('hh\:mm\:ss')
    $create = Invoke-Logman -Arguments @('create', 'counter', $Session.collector_name, '-cf', $counterFile,
        '-si', '00:00:01', '-f', 'csv', '-max', [string]$Settings.maximum_performance_mb,
        '-rf', $limit, '-o', (Join-Path $perf 'counters.csv'), '-v', 'nnnnnn')
    $Session.collector_created = $true
    Save-Json $Session (Join-Path $RunDirectory 'session.json')
    $start = Invoke-Logman -Arguments @('start', $Session.collector_name)
    $Session.performance_started_utc = [DateTime]::UtcNow.ToString('o')
    $Session.performance_status = 'started'
    Save-Json ([ordered]@{ create = $create; start = $start }) (Join-Path $RunDirectory 'performance-start.json')
    Save-Json $Session (Join-Path $RunDirectory 'session.json')
}

function Get-CollectorState([string]$Name) {
    $collector = $null
    try {
        $collector = New-Object -ComObject Pla.DataCollectorSet
        $collector.Query($Name, $null)
        $status = [int]$collector.Status
        if ($status -eq 0) { return [pscustomobject]@{ state = 'stopped'; value = $status; error = $null } }
        return [pscustomobject]@{ state = 'not_stopped'; value = $status; error = $null }
    } catch {
        $exception = $_.Exception
        while ($null -ne $exception) {
            if ($exception.HResult.ToString('X8') -eq '80300002') {
                return [pscustomobject]@{ state = 'missing'; value = $null; error = $null }
            }
            $exception = $exception.InnerException
        }
        return [pscustomobject]@{ state = 'unknown'; value = $null; error = $_.Exception.Message }
    } finally {
        if ($null -ne $collector) { [void][Runtime.InteropServices.Marshal]::FinalReleaseComObject($collector) }
    }
}

function Stop-OwnCollector($Session, [string]$RunDirectory) {
    if ($Session.tool_id -ne $script:ToolId -or $Session.collector_name -notmatch '^MT5Diag-\d{8}-\d{6}-[a-f0-9]{8}$') {
        throw '采集任务身份不匹配，未停止任何任务。'
    }
    if (-not $Session.collector_created) { return }
    $query = Invoke-Logman -Arguments @('query', $Session.collector_name) -AllowFailure
    $stop = Invoke-Logman -Arguments @('stop', $Session.collector_name) -AllowFailure
    $delete = Invoke-Logman -Arguments @('delete', $Session.collector_name) -AllowFailure
    $state = Get-CollectorState $Session.collector_name
    Save-Json ([ordered]@{ utc = [DateTime]::UtcNow.ToString('o'); query = $query; stop = $stop; delete = $delete; final_state = $state }) (Join-Path $RunDirectory 'performance-stop.json')
    # Query failure alone never means the task is absent. An explicit successful
    # stop/delete is also evidence, unless a fresh COM query says still active.
    $safe = $state.state -in @('missing', 'stopped') -or
        ($state.state -eq 'unknown' -and ($stop.exit_code -eq 0 -or $delete.exit_code -eq 0))
    if ($safe) {
        $Session.performance_stopped_utc = [DateTime]::UtcNow.ToString('o')
        if ($state.state -eq 'missing' -or $delete.exit_code -eq 0) { $Session.collector_created = $false }
        else { Add-Warning ('性能采集已停止，但任务配置尚未删除，可运行 Stop-Capture.cmd 清理: ' + $Session.collector_name) }
    } else {
        $Session.performance_status = 'cleanup_failed_or_unknown'
        Add-Warning ('无法确认性能采集已停止，本次不打包活动文件。请运行 Stop-Capture.cmd，任务名: ' + $Session.collector_name)
    }
    Save-Json $Session (Join-Path $RunDirectory 'session.json')
    if (-not $safe) { throw '性能采集停止状态未确认，已保留采集目录。' }
}

function Save-MachineInformation([string]$RunDirectory, $Settings) {
    $info = [ordered]@{
        tool_id = $script:ToolId; powershell = $PSVersionTable.PSVersion.ToString()
        machine = $env:COMPUTERNAME; time_zone = [TimeZoneInfo]::Local.Id
        utc = [DateTime]::UtcNow.ToString('o'); local = [DateTimeOffset]::Now.ToString('o')
        server_root = $Settings.server_root; errors = @()
    }
    try {
        $info.os = @(Get-CimInstance Win32_OperatingSystem | Select-Object Caption, Version, OSArchitecture, TotalVisibleMemorySize)
        $info.cpu = @(Get-CimInstance Win32_Processor | Select-Object Name, NumberOfCores, NumberOfLogicalProcessors)
        $info.disks = @(Get-CimInstance Win32_LogicalDisk -Filter 'DriveType=3' | Select-Object DeviceID, FileSystem, Size, FreeSpace)
        $info.server_processes = @(Get-CimInstance Win32_Process | Where-Object {
            $_.ExecutablePath -and (Test-UnderDirectory $_.ExecutablePath $Settings.server_root)
        } | Select-Object Name, ProcessId, ExecutablePath)
    } catch { $info.errors += $_.Exception.Message }
    try {
        $pluginRoot = Join-Path $Settings.server_root 'plugins'
        $info.executor_files = @()
        if (Test-Path -LiteralPath $pluginRoot -PathType Container) {
            $info.executor_files = @(Get-ChildItem -LiteralPath $pluginRoot -Filter 'TradeCopierExecutor.dll' -Recurse -File | ForEach-Object {
                [pscustomobject]@{ path = $_.FullName; version = $_.VersionInfo.FileVersion; length = $_.Length;
                    sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
            })
        }
    } catch { $info.errors += $_.Exception.Message }
    Save-Json $info (Join-Path $RunDirectory 'machine.json')
}

function Wait-Countdown([int]$Seconds, [string]$Message) {
    for ($left = $Seconds; $left -gt 0; $left--) {
        Write-Progress -Activity $Message -Status ($left.ToString() + ' 秒') -PercentComplete (100 * ($Seconds - $left) / $Seconds)
        Start-Sleep -Seconds 1
    }
    Write-Progress -Activity $Message -Completed
}

function Read-PerformanceCoverage([string]$Directory, [datetime]$ExpectedEndUtc) {
    $summary = [ordered]@{ data_rows = 0; parsed_timestamps = 0; first_utc = $null; last_utc = $null;
        largest_sample_gap_seconds = 0.0; tail_gap_seconds = $null; status = 'missing_output'; files = @() }
    $first = $null; $last = $null; $previous = $null
    foreach ($file in @(Get-ChildItem -LiteralPath $Directory -Filter '*.csv' -File | Sort-Object Name)) {
        $summary.files += $file.Name
        $reader = New-Object IO.StreamReader($file.FullName, $true)
        try {
            [void]$reader.ReadLine() # PDH header contains counter names, not a sample.
            while ($null -ne ($line = $reader.ReadLine())) {
                if ([string]::IsNullOrWhiteSpace($line)) { continue }
                $summary.data_rows++
                if ($line -notmatch '^"([^"]+)"[,]') { continue }
                $stamp = $Matches[1]; $parsed = [DateTime]::MinValue
                $ok = [DateTime]::TryParse($stamp, [Globalization.CultureInfo]::CurrentCulture,
                    [Globalization.DateTimeStyles]::AllowWhiteSpaces, [ref]$parsed)
                if (-not $ok) {
                    $ok = [DateTime]::TryParse($stamp, [Globalization.CultureInfo]::GetCultureInfo('en-US'),
                        [Globalization.DateTimeStyles]::AllowWhiteSpaces, [ref]$parsed)
                }
                if (-not $ok) { continue }
                $utc = [TimeZoneInfo]::ConvertTimeToUtc([DateTime]::SpecifyKind($parsed, [DateTimeKind]::Unspecified), [TimeZoneInfo]::Local)
                $summary.parsed_timestamps++
                if ($null -eq $first -or $utc -lt $first) { $first = $utc }
                if ($null -eq $last -or $utc -gt $last) { $last = $utc }
                if ($null -ne $previous) {
                    $summary.largest_sample_gap_seconds = [Math]::Max($summary.largest_sample_gap_seconds, ($utc - $previous).TotalSeconds)
                }
                $previous = $utc
            }
        } finally { $reader.Dispose() }
    }
    if ($null -ne $first) { $summary.first_utc = $first.ToString('o') }
    if ($null -ne $last) {
        $summary.last_utc = $last.ToString('o')
        $summary.tail_gap_seconds = [Math]::Max(0.0, ($ExpectedEndUtc.ToUniversalTime() - $last).TotalSeconds)
    }
    if ($summary.data_rows -ge 2) {
        $summary.status = 'recorded_requires_analysis'
        if ($summary.parsed_timestamps -ne $summary.data_rows) { $summary.status = 'timestamp_coverage_unverified' }
        elseif ($summary.tail_gap_seconds -gt 5 -or $summary.largest_sample_gap_seconds -gt 5) { $summary.status = 'sample_gaps_detected' }
    }
    return [pscustomobject]$summary
}

function Export-ExistingPerformance([string]$Directory, [datetime]$Day) {
    $selection = New-Object 'System.Collections.Generic.List[object]'
    if ([string]::IsNullOrWhiteSpace($Directory) -or -not (Test-Path -LiteralPath $Directory -PathType Container)) { return @() }
    Assert-NoReparseAncestor $Directory
    foreach ($file in @(Get-ChildItem -LiteralPath $Directory -Recurse -File -Force)) {
        if ($file.Extension -notin @('.csv', '.blg') -or $file.LastWriteTime -lt $Day.Date.AddDays(-1) -or
            $file.LastWriteTime -ge $Day.Date.AddDays(2)) { continue }
        Assert-NoReparseAncestor $file.FullName
        $relative = $file.FullName.Substring($Directory.TrimEnd('\', '/').Length + 1).Replace('\', '/')
        $selection.Add([pscustomobject]@{ path = $file.FullName; relative = ('existing-performance/' + $relative); category = 'existing_performance' })
    }
    return $selection.ToArray()
}

function Main {
    if ($env:OS -ne 'Windows_NT') { throw '请在 Windows Trade Server 所在电脑运行此工具。' }
    $settings = Read-Settings
    $outputRoot = Get-OutputRoot $settings
    Assert-NoReparseAncestor $outputRoot
    if ($Mode -eq 'Stop') {
        if (Test-Path -LiteralPath $outputRoot -PathType Container) {
            foreach ($dir in @(Get-ChildItem -LiteralPath $outputRoot -Directory)) {
                $state = Join-Path $dir.FullName 'session.json'
                if (Test-Path -LiteralPath $state) {
                    try {
                        $session = Get-Content -LiteralPath $state -Raw -Encoding UTF8 | ConvertFrom-Json
                        if ($session.tool_id -eq $script:ToolId -and $session.collector_created) {
                            Stop-OwnCollector $session $dir.FullName
                            Write-Host ('已处理本工具采集任务: ' + $session.collector_name)
                        }
                    } catch { Add-Warning $_.Exception.Message }
                }
            }
        }
        Write-Host '停止操作结束。已有文件保留在 captures 中；交易服务继续运行。'
        return
    }
    $logDirectory = Get-LogDirectory $settings
    if (Test-UnderDirectory $outputRoot $logDirectory) { throw '输出目录不能放在服务端日志目录内。' }
    $day = [DateTime]::Now.Date
    if ($Mode -eq 'Export') {
        $requested = $Date
        if ([string]::IsNullOrWhiteSpace($requested)) {
            $requested = Read-Host ('输入要导出的日志日期 YYYY-MM-DD，直接回车为 ' + $day.ToString('yyyy-MM-dd'))
        }
        if (-not [string]::IsNullOrWhiteSpace($requested)) {
            $day = [DateTime]::ParseExact($requested, 'yyyy-MM-dd', [Globalization.CultureInfo]::InvariantCulture)
        }
    }
    $id = [DateTime]::Now.ToString('yyyyMMdd-HHmmss') + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
    $runDirectory = Join-Path $outputRoot ('MT5-' + $id)
    if (Test-Path -LiteralPath $runDirectory) { throw '本次输出目录已存在，请重新运行。' }
    [void][IO.Directory]::CreateDirectory($runDirectory)
    $script:ActiveRunDirectory = $runDirectory
    $session = [ordered]@{
        tool_id = $script:ToolId; mode = $Mode; collector_name = ('MT5Diag-' + $id)
        collector_created = $false; created_utc = [DateTime]::UtcNow.ToString('o')
        created_local = [DateTimeOffset]::Now.ToString('o'); timezone = [TimeZoneInfo]::Local.Id
        log_directory = $logDirectory; server_root = $settings.server_root
        performance_started_utc = $null; performance_stopped_utc = $null; performance_status = 'not_requested'
        performance_maximum_minutes = $settings.maximum_capture_minutes
        test_ready_utc = $null; test_finished_utc = $null; capture_end_utc = $null
        export_requested_date = $day.ToString('yyyy-MM-dd'); finished = $false
    }
    Save-Json $session (Join-Path $runDirectory 'session.json')
    Save-MachineInformation $runDirectory $settings
    $startLocal = [DateTime]::Now
    Write-Host ('服务端日志: ' + $logDirectory)
    Write-Host ('本次采集目录: ' + $runDirectory)
    try {
        if ($Mode -eq 'Capture') {
            try {
                $captureDrive = New-Object IO.DriveInfo([IO.Path]::GetPathRoot((Get-FullPath $runDirectory)))
                if ($captureDrive.AvailableFreeSpace -lt ([long]$settings.maximum_performance_mb * 1MB + 512MB)) {
                    throw '输出盘空闲空间不足以容纳性能采集上限和 512 MB 余量，请更换 output_directory。'
                }
                Start-Performance $session $settings $runDirectory
            } catch {
                $session.performance_status = 'failed'
                Add-Warning ('性能采集未启动: ' + $_.Exception.Message)
                throw '请以管理员身份运行 Start-Capture.cmd。若计数器仍不可用，可先用 Export-Logs.cmd 收集已有交易日志。'
            }
            Write-Host ('性能采集已经开始。先记录 ' + $settings.baseline_seconds + ' 秒的空闲基线。')
            Wait-Countdown $settings.baseline_seconds '记录测试前基线'
            $session.test_ready_utc = [DateTime]::UtcNow.ToString('o')
            Save-Json $session (Join-Path $runDirectory 'session.json')
            Write-Host ''
            Write-Host '现在可以开始 EA 压测。请保持本窗口打开。' -ForegroundColor Green
            Write-Host ('性能采集最长 ' + $settings.maximum_capture_minutes + ' 分钟；到时自动停止记录。')
            [void](Read-Host '全部订单处理结束后，回到此窗口按回车，开始收尾并打包')
            $session.test_finished_utc = [DateTime]::UtcNow.ToString('o')
            Wait-Countdown $settings.tail_seconds '记录测试后收尾'
        }
        $session.capture_end_utc = [DateTime]::UtcNow.ToString('o')
    } finally {
        Stop-OwnCollector $session $runDirectory
        Save-Json $session (Join-Path $runDirectory 'session.json')
    }
    if ($Mode -eq 'Capture') {
        $started = [DateTime]::Parse($session.performance_started_utc).ToUniversalTime()
        if (([DateTime]::UtcNow - $started).TotalMinutes -ge $settings.maximum_capture_minutes) {
            Add-Warning '本次超出性能采集时长上限，末段性能数据可能缺失。'
        }
        $coverage = Read-PerformanceCoverage (Join-Path $runDirectory 'performance') ([DateTime]::Parse($session.capture_end_utc))
        Save-Json $coverage (Join-Path $runDirectory 'performance-coverage.json')
        $session.performance_status = $coverage.status
        if ($coverage.status -ne 'recorded_requires_analysis') {
            Add-Warning ('性能数据覆盖需要检查: ' + $coverage.status + '；详见 performance-coverage.json。')
        }
        # Include adjacent daily logs: trade-server time may differ from OS time.
        $days = @(Get-DayNames $startLocal ([DateTime]::Now) 1)
    } else { $days = @(Get-DayNames $day $day 1) }
    $selection = @(Get-ServerLogSelection $logDirectory $days)
    if ($selection.Count -eq 0) { Add-Warning '目标日期及相邻日期没有日志，不能认定本次导出覆盖了测试。' }
    if ($Mode -eq 'Export') {
        $selection += @(Export-ExistingPerformance $settings.existing_performance_directory $day)
    }
    $selectedBytes = 0L
    foreach ($entry in $selection) { $selectedBytes += (Get-Item -LiteralPath $entry.path).Length }
    $drive = New-Object IO.DriveInfo([IO.Path]::GetPathRoot((Get-FullPath $runDirectory)))
    $copyBudget = [Math]::Min($selectedBytes, ([long]$settings.maximum_total_source_mb * 1MB))
    $alreadyCollected = 0L
    foreach ($file in @(Get-ChildItem -LiteralPath $runDirectory -Recurse -File -Force)) { $alreadyCollected += $file.Length }
    if ($drive.AvailableFreeSpace -lt (2 * $copyBudget + $alreadyCollected + 512MB)) { throw '输出盘空间不足以保留日志副本、性能数据和ZIP，请更换配置中的 output_directory。' }
    $records = @(Copy-SelectedFiles $selection $runDirectory $settings)
    $session.finished = $true
    Save-Json $session (Join-Path $runDirectory 'session.json')
    $manifest = [ordered]@{
        tool_id = $script:ToolId; session = $session; selected_dates = $days
        selection_policy = 'Complete daily .log/.zip files for requested local date range plus one adjacent day each side; no account filtering or re-encoding.'
        snapshot_policy = 'Read-only shared fixed-length prefix. Not an atomic server snapshot; last record may be partial and buffered server writes may be absent.'
        server_time_zone = 'not_verified; machine.json records OS timezone only'
        records = $records; warnings = $script:Warnings.ToArray()
        all_selected_files_copied = ($records.Count -gt 0 -and @($records | Where-Object { $_.status -ne 'copied_prefix' }).Count -eq 0)
        trade_log_time_coverage = 'requires_analysis'; performance_coverage = 'requires_analysis'
    }
    Save-Json $manifest (Join-Path $runDirectory 'manifest.json')
    Write-Host '日志收集结束，正在压缩并校验 ZIP。'
    $zip = Write-Package $runDirectory
    if ($script:Warnings.Count -gt 0) { Write-Host '导出包含提示或缺失项，请连同 manifest.json 一起分析。' -ForegroundColor Yellow }
    Write-Host ''
    Write-Host ('请上传这个 ZIP: ' + $zip) -ForegroundColor Green
    Write-Host '原始日志与采集目录均已保留。'
}

if ($Mode -ne 'Library') {
    try { Main }
    catch {
        if ($script:ActiveRunDirectory -and (Test-Path -LiteralPath $script:ActiveRunDirectory -PathType Container)) {
            try { Save-Json ([ordered]@{ utc = [DateTime]::UtcNow.ToString('o'); error = $_.Exception.Message;
                warnings = $script:Warnings.ToArray() }) (Join-Path $script:ActiveRunDirectory 'capture-error.json') } catch { }
        }
        Write-Host ('采集未完成: ' + $_.Exception.Message) -ForegroundColor Red
        Write-Host '已采集的文件保留在输出目录。意外关窗后可运行 Stop-Capture.cmd 清理本工具的性能采集任务。'
        exit 1
    }
}
