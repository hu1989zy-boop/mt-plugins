# MT5 压测日志一键采集

独立工具，无需重新编译或替换执行器。适用于 Windows PowerShell 5.1。

已配置安装目录：`D:\MT5 Platform\Demo MainTrade`。
首次运行会检查 `D:\MT5 Platform\Demo MainTrade\logs` 中是否有 `YYYYMMDD.log` 或 `YYYYMMDD.zip`。目录不存在时会要求输入真实日志目录；找不到正确日志不会显示采集成功。

## 使用方法

将整个工具包解压到服务器的普通本地目录，例如 `D:\MT5Capture`。保留全部文件，不要只复制一个 CMD，也不要放进 Trade Server 的 logs 目录。

### A. 导出已经做完的测试

1. 双击 **Export-Logs.cmd**。若提示读取权限不足，以管理员身份运行。
2. 输入日志日期，例如 `2026-09-22`；直接回车使用当天日期。
3. 等待出现“请上传这个 ZIP”，上传该 ZIP 即可。

此模式收集完整日日志，并包含所选日期前后相邻日已存在的日志，以覆盖服务端日期与 Windows 日期的偏差。日志原文不按账号过滤或重编码。若 `C:\PerfLogs\MT5-Stress` 有相邻日期修改的 `.csv` 或 `.blg`，也会一起收集。它不会为已经结束的测试补造性能数据。

### B. 下一轮压测同时记录性能

1. 右键 **Start-Capture.cmd → 以管理员身份运行**。
2. 工具记录 30 秒基线，显示“现在可以开始 EA 压测”后再启动 EA。
3. 保持窗口打开。所有订单处理结束后，回到该窗口按 **回车**。
4. 工具继续记录 30 秒，停止自己的性能采集，然后复制日志、压缩并校验 ZIP。
5. 上传显示路径中的 ZIP。

默认每秒记录 CPU 各核心、系统排队、可用内存/提交量、分页读取、各磁盘读写响应时间/队列/吞吐量、各进程 CPU/内存/IO 和进程 ID。计数器名称按 Windows 当前语言解析。启动前会读取真实样本，计数器不可用时明确报错。

性能数据默认最多采集 **30 分钟、256 MB**。测试超时、文件上限或采样缺口会导致覆盖不全，必须查看包内提示。分段计时诊断仍需另行加入执行器；系统指标不能独立证明某个函数耗时。

### C. 窗口意外关闭

运行 **Stop-Capture.cmd**，它只尝试停止和清理本工具记录的唯一命名性能采集任务。系统任务另有 30 分钟的时间上限，即使窗口被强制关闭，也不会无限采集。

已采文件保留在 `captures`。该清理入口不会补齐或自动打包中断的测试；可保留该采集目录，并用 Export-Logs.cmd 补取当天原始服务端日志。

## 结果在哪里

工具目录中的 `captures`，每次使用不同目录和 ZIP 名称。例如：

```text
captures\MT5-20260923-143000-a1b2c3d4.zip
captures\MT5-20260923-143000-a1b2c3d4.zip.sha256.txt
captures\MT5-20260923-143000-a1b2c3d4\
```

ZIP 中主要内容：

| 文件 | 含义 |
| --- | --- |
| `server-logs\*.log / *.zip` | 原始日文件的只读副本 |
| `performance\*.csv` | 本次自动采集的性能数据（新压测模式） |
| `existing-performance\*` | 已存在的性能文件（导出模式，如有） |
| `machine.json` | Windows 时区、CPU/内存/磁盘信息、Trade Server 进程和执行器文件版本/哈希 |
| `session.json` | 开始、可以启动 EA、用户结束测试、停止采集等时间点 |
| `manifest.json` | 文件来源、实际捕获长度、SHA256、缺失项及采集边界 |
| `performance-coverage.json` | 样本数和可解析的首尾时间、采样间隙 |

`test_ready_utc` 表示工具提示可以开始 EA，并非 EA 的真实首单时间；逐笔分析以源成交日志为准。`machine.json` 中是操作系统时区，工具没有假定它等于交易服务器日志时区。

如果输出包含黄色提示，请仍保留完整包。未成功复制、文件过大、性能缺失都会记录，不能将“生成了 ZIP”理解为“日志时间范围已验证完整”。

## 原始日志与空间

- 只对源日志使用共享只读句柄，允许服务继续追加和轮转。复制时固定当前长度，保留原始字节，不编辑或截断源文件。
- 运行中的日志不是原子快照，末尾可能有半条记录或尚未刷新出的内容。包内记录复制期间文件是否增长，分析时需要检查实际覆盖。不要用采集副本覆盖服务端原日志。
- 默认单个输入文件最多 **1 GB**，全部输入最多 **4 GB**。超限明确记录并保留其余结果，不静默取尾部。
- 同时保留解压副本与 ZIP。复制前检查输出盘空间；建议至少有“待复制大小的两倍 + 512 MB”空闲空间。原始日文件复制和压缩均在压测结束、性能采集停止后执行。
- ZIP 写入独立临时文件，完成后重新读取每一项并校验 SHA256，校验通过才生成最终 `.zip`。

## 可调整设置

编辑 `collector-settings.json`：

- `server_root`：安装目录。
- `server_log_directory`：默认留空，使用安装目录的 `logs`。若日志位于其他位置，在这里填写完整路径。
- `output_directory`：默认留空，使用工具目录的 `captures`；可填写另一个空间充足的本地盘目录。
- `existing_performance_directory`：导出已有日志时顺带收集的性能目录；留空跳过。
- `maximum_capture_minutes`：性能采集最长时间，最大 120 分钟。
- `maximum_performance_mb`、`maximum_source_file_mb`、`maximum_total_source_mb`：性能文件与原始文件大小限额。单输入文件支持范围最多 1800 MB；超过此范围需另行处理。

JSON 中 Windows 反斜杠写成 `\\`。设置使用 UTF-8；中文 PowerShell 脚本带 BOM 以兼容 Windows PowerShell 5.1。

## 验证与适用边界

工具没有在你的 Windows 服务器上执行；本地验证使用 macOS 上的 PowerShell 7.4.6，覆盖语法、只读复制、日期选择和打包校验。Windows PowerShell 5.1、PDH、本地化计数器、logman/CIM 及真实服务端文件共享仍以服务器首次预检为准。

采集工具独立于跟单 DLL，不修改跟随关系或运行账本，不操作交易，也不停止或重启 Trade Server。运行 CMD 的执行策略参数仅对本次 PowerShell 进程生效。

## 依据

- 用户提供的官方 SDK `DailyServerReport.cpp::GetFileName` 使用服务程序目录 `logs\YYYYMMDD.log`；`LogFileReader.cpp::Open` 兼容同名 `.zip`。因此目录是候选默认值，启动时仍需检查。
- [Microsoft：logman create counter](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/logman-create-counter)
- [Microsoft：本地化计数器名称查询](https://learn.microsoft.com/en-us/windows/win32/api/pdh/nf-pdh-pdhlookupperfnamebyindexw)
- [Microsoft：文件共享访问](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)
