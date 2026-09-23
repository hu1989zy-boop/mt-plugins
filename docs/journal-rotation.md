# 0.4.4：账本达到容量上限后漏跟平仓的修复

> **Windows 构建更新**：0.4.6 已在用户本机测试发现检查点替换失败，改用 [0.4.7 修复包与构建步骤](windows-checkpoint-fix.md)；下文旧版本命令仅供历史参考。

> 历史版本说明。0.4.5 已改为清理已结束记录和替换运行检查点，不再自动积累历史归档，并新增新仓容量保护。当前行为与升级步骤见 [运行账本与容量保护](live-state-journal.md)。

## 已确认的现场原因

用户提供的 2026-09-17 日志显示：

- 11:43:47.253：请求 935、936 已确认平仓。
- 11:43:48.232：`HALTED reason=worker_exception: journal size limit reached; archive/recovery required`。
- 管理页显示 HALTED、在管 18、未决请求 2。账户图中源 50001 已无仓位，50002 / 50003 各有 9 笔目标持仓。

0.4.3 每次持久化追加完整状态快照，单文件上限为 256 MiB；没有轮换。超过上限后账本写入失败，执行器停止后续交易，包括平仓。该故障与 32 个跨账号在途名额无关。管理页的“未决请求”也包含 Prepared，不能仅凭该数字认定请求已经发送到服务器。

## 修复行为

默认在下一次写入会超过 **32 MiB** 时轮换活动账本：

1. 持续持有 `<JournalPath>.lock` 独占写入租约，完整复制旧段到 `<JournalPath>.archive`，以新文件名保存并同步刷盘。
2. 将最后一次成功提交的完整快照写入临时检查点并同步刷盘。持仓映射、冻结开仓规则、配置、请求编号、成交证据、已处理源平仓和暂停/停止状态均保留。
3. 在同一文件系统替换活动文件，随后追加本次状态。只有 Save 完成后才允许提交对应交易请求。
4. 运行时读取和下次启动只加载活动文件；旧段用于审计，不在每次刷新时回放。日志增加 `JOURNAL checkpoint rotated`。

Windows 使用 `MoveFileExW` 的 `MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH`，源和目标文件都保留打开句柄并允许共享删除，稳定租约覆盖替换全过程；参见 [Microsoft API 文档](https://learn.microsoft.com/zh-cn/windows/win32/api/winbase/nf-winbase-movefileexw)。POSIX 使用同卷 rename 和目录 fsync。

32 MiB 是轮换触发值，完整检查点加下一帧可略超过阈值。持仓规则不变，跨账号在途上限仍为 32。

### 失败和兼容边界

- 复制、刷盘、替换失败均使 Save 失败，停止进一步交易。保留旧文件、归档和临时文件用于核对。
- 不自动从旧归档或 `.tmp` 回退，避免遗漏已提交请求。活动文件损坏、截断，或已有归档但活动文件缺失/为空时拒绝启动。
- 未决请求在重启后仍进入人工核对，不自动补发。修复不会自动解除历史 HALTED 或追平停机期间的源交易。
- 继续读取原 `TCXJ0001` 和 payload schema 1 / 2 / 3；轮换后的首帧为 `TCXJ0002` 检查点，后续帧仍为 `TCXJ0001`，payload schema 仍为 3。**轮换后必须使用 0.4.4 或更新版本的执行器和账本工具，不能直接降级到 0.4.3。**
- 本次不删除归档，不压缩全量历史。活动文件不会因累计追加到 256 MiB 而停止，但归档总磁盘占用仍会增长；磁盘写满仍会停止交易。单快照 16 MiB、1 万映射、10 万请求等已有限制保留。不能据此宣称长期容量或延迟问题已全部解决。

## 当前 18 笔残留仓位的处理

本修复预防同一容量故障再次发生，不会自动补平现场残留仓位。

1. 暂停源端 EA / 测试发送，保留服务器日志和管理页证据。不要清空或编辑原账本。
2. 由操作人员核对源仓、映射目标票号和实际成交，再手动处理确认属于本次测试的残留目标仓位；不要依据“在管 18”直接批量关闭其他交易。
3. 停用插件后完整备份原账本和配置。用同版本 `TradeCopierJournal` 离线检查最后持久化状态；它只输出证据，不修改映射、不解除 HALTED。实际仓位已经变化时，原账本重启可能进入 `source_changed_while_stopped` 或未决请求核对，这是保留的恢复保护。
4. 完成逐笔核对前，不要换空账本或强行解除停止。若后续决定从全平的 Demo 测试基线重新开始，应先确认相关源/目标均无持仓、无服务器未决交易，保留原账本并另外指定新 JournalPath。已有管理配置以旧账本为准，必须先导出/核实配置，不能假定 ConfigPath 文件含最新修改。

## 构建与部署

源码包：`out/mt5-trade-copier-0.4.4-journal-rotation-source.zip`。不包含 MT5 SDK、运行账本或本地账号配置。

在 Windows x64 Native Tools Command Prompt 中执行；下面 SDK 路径是示例，请换成包含 `Include\MT5APIServer.h` 的实际 Server API 6182 SDK 根目录：

```cmd
cd /d D:\work\mt-plugins
scripts\build-windows.cmd -Executor -SdkRoot "D:\SDK\MT5ServerAPI" -BuildDirectory build-windows-0.4.4
```

一起更新 `TradeCopierExecutor.dll`、`TradeCopierManager.exe` 和 `TradeCopierJournal.exe`。不要覆盖/删除原账本；新版本加载合法旧账本后会在写入时按需归档。首次加载接近 256 MiB 的旧账本仍需完整校验，不能当成日常启动耗时。

启动日志应包含 `journal rotation=32MiB` 和 `(0.4.4)`；信息页显示 0.4.4，并直接展示停止原因。

## 验证范围

自动化测试覆盖活动账本替换后的独占锁、字节一致的旧段归档、提交中请求及冻结规则保留、重启不重发、孤立临时文件、活动文件损坏/截断/丢失、归档路径失败，以及两跟单账号各 9 仓的多轮开平仓。

另运行默认 32 MiB 阈值的 30 轮连续文件测试，每轮 9 个源仓、两个目标账号，累计 1080 个开平请求，累计写入跨越旧 256 MiB 边界。结果见构建验证记录。

构建后可执行 `build-windows-0.4.4\Release\tradecopier_execution_tests.exe --journal-soak` 重跑该模拟；它使用临时账本和模拟交易端口，不连接 MT5。归档和临时文件会在测试结束时清理。

本地测试和交叉编译不代替 Windows NTFS 上的 MSVC 构建、实际文件替换/断电故障和 Demo 成交验证。部署后先验证小量开平，再进行连续交易及轮换验收。
