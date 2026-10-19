# MT5 0.7.3：压测优化与成交计时

本版对应 2026-10-13 EA 压测：50001 每笔 0.02 手，最新一轮约 20 笔/秒，共 100 笔；50002 的 100 次开仓和 100 次平仓均已确认，但出现偶发确认延迟尖峰。

## 改动

1. 账本内部直接保存上一份 payload，减少每次 delta/replay 的全量字符串复制；在既有语义允许时复用已编码状态用于转换/压缩校验；已确定必须发布 checkpoint 时不再先计算无用的 delta。保留全量校验、CRC、写前意图持久化及 `FlushFileBuffers`，文件格式不变。
2. 每个读取轮次建立历史索引：intent→订单、order→成交、position→成交。多个请求复用索引，减少重复遍历；重复行、空行、错身份、错数量和异常证据仍按原规则处理。扩大历史查询窗口时完整重建，下一轮清空，不能跨轮复用过期结果。
3. MT5 worker 已先执行 `ConfirmPending` 时，如果期间没有新回调，且确认完成后 admission 不超过 10ms，则单次复用该确认轮次。新回调、超时或没有有效前置确认时重新核对；随后到达的回调仍能唤醒下一轮。10ms 指确认结束至 Tick 的间隔，不代表整份历史快照年龄。默认 `Tick()` 和 MT4 仍执行原先的新核对。
4. 增加轻量累计计时，按 5 秒输出性能摘要；确认日志增加经过完整成交证据核对的实际目标 Deal 时间。

交易行为维持现有设置：同服 USD 对冲、多源多跟单、三种手数、正反向、新仓跟随、比例平仓、手动全平后终止映射、无挂单/SL/TP 复制。只读跟单仍使用 `DealerLogin=2016`，客户权限和管理账号授权检查保留。不调高并发限额，不重试旧失败单。

## Windows 编译与更新

已确认现场 SDK 路径是 `D:\SDK\mt5-sdk`。原工作目录是 `D:\work\mt-plugins\plugins\trader-copier\mt5`，其历史解压曾产生重复嵌套；为避免再编译到旧副本，本次完整源码包解压到独立目录 `D:\work\TradeCopier-MT5-0.7.3`。该目录应直接包含 `scripts`、`plugins`、`CMakeLists.txt` 和 `README-WINDOWS.txt`。

在 **x64 Native Tools Command Prompt** 中执行：

```bat
cd /d "D:\work\TradeCopier-MT5-0.7.3"
scripts\build-windows.cmd -Executor -SdkRoot "D:\SDK\mt5-sdk" -BuildDirectory "D:\MT5Build\TradeCopier-0.7.3"
```

看到 `Build and tests passed` 后，产物在：

```text
D:\MT5Build\TradeCopier-0.7.3\package-executor
```

在测试服务器按已有流程停止 EA、关闭管理界面，停用并卸载旧插件，再备份原配置及停止写入后的完整账本/伴随文件，替换 `TradeCopierExecutor.dll`、`TradeCopierManager.exe`、`TradeCopierJournal.exe` 并重新加载。测试开始前确认原测试仓位已经处理完毕。

保留原配置路径、Journal 路径、`ExpectedServerId`、`ApiAppId`、`DealerLogin=2016` 和管理参数；不要用示例配置覆盖现场配置，不要删除账本。schema 7、delta frame 4 和管理协议 2 保持不变。

启动日志应包含：

```text
indexed history; single-pass confirmation reuse; journal phase metrics; target deal timestamps (0.7.3)
```

`build-manifest.json` 应为 `source_version=0.7.3`、`build_script_revision=26`。该源码包没有预编译 Windows DLL/EXE，SDK 也不在包中。

## 下一轮压力测试

先用一笔 0.02 手开仓确认 50002 正常跟随，并验证部分平仓和全平。随后测试：

| 参数 | 建议值 |
| --- | --- |
| 信号源 | 50001 |
| 每笔手数 | 0.02 |
| 每秒目标下单数 | 20 |
| 持仓时间 | 10 秒 |
| 方向 | Buy / Sell 交替 |
| 成功开仓上限 | 1200（约 60 秒） |

原先 100 笔约 5 秒便停止开仓，平仓尚未开始。本轮 1200 笔可覆盖持续开仓、持续平仓及两者重叠。测试结束后等待全部目标仓位核对并保留至少 10 秒的末尾日志，让最后一段性能汇总输出。

保留 `TradeCopierExecutor` 全部日志，从新版本启动到压测结束；不要只筛 ERROR。提供：实际跟单关系、实际源开平仓数量、是否有剩余仓位，以及对应时段 CPU/磁盘响应时间。上一轮 50007 的保证金不足拒单和另一关系的 `skip_below_minimum` 应单独统计；它们不会因性能优化自动变成成功成交。

## 新日志的读法

有活动时每 5 秒最多输出以下三行，不逐请求输出详细阶段计时：

| 记录 | 内容 |
| --- | --- |
| `PERF` | 当前映射/意图数、账本大小、确认轮数/复用次数、确认耗时、确认保存耗时、并发限额遇到次数 |
| `PERF_JOURNAL` | 提交次数、失败、checkpoint、写入/刷盘次数，编码/校验/delta与帧构造/写入/刷盘/checkpoint/状态更新耗时 |
| `PERF_HISTORY` | 读取轮数、Deal/Order 历史查询次数、缓存命中、建立索引的行数、候选行数、核对次数和时间 |

没有活动时不输出空摘要。`interval_ms` 是本次区间长度；普通计数/耗时是相对上一采样区间的增量；`_us` 单位为微秒；`_max_us_total` 为当前实例启动以来单次操作峰值，不是当前区间峰值。

`PERF_JOURNAL` 中 checkpoint 耗时包含其写入、刷盘和发布，不能再与 write/flush 相加。journal 各阶段最大值也可能来自不同提交，不能将各最大值相加作为一次提交时长。`PERF.confirm_us` 包含整个确认流程，其内已包含 `confirm_save_us`；历史核对时间同样是子项。

`account_limit_visits` 是调度遍历遇到账号额度的次数，不是独立请求数；`global_limit_hits` 是轮次入口已满次数，不代表完整排队时长。并发仍为每账号 8、全局 512。

目标确认日志示例：

```text
CONFIRMED intent=123 target_position=456 volume_ext=1000000 target_first_deal_ms=... target_last_deal_ms=...
```

通过相同 `intent` 关联原有 `PREPARED source_event_ms=...`，可计算：

- 第一笔真实成交延迟：`target_first_deal_ms - source_event_ms`。
- 完整成交延迟：`target_last_deal_ms - source_event_ms`。
- 插件确认附加耗时：CONFIRMED 日志时间减 `target_last_deal_ms`，须先按同一服务器时间基准转换。

时间字段仅在成交时间完整有效时提供，未知时省略，不能填 0 后参与统计。不要把 `QUEUED` 当实际成交，也不要把 CONFIRMED 行的输出时间当实际 Deal 时间。原有存储降级模式下，单看 CONFIRMED 也不能证明磁盘已成功提交；同时检查存储异常和 `PERF_JOURNAL failed`。

本轮新增字段仅诊断，不写入账本，不参与成交决策；没有目标价格字段，因此不能仅用本日志计算滑点。

## 验证边界

本地便携测试和真实本地文件账本模拟用于确认代码行为、恢复兼容与不重复请求。历史索引测试覆盖大量历史行与原全扫描候选集等价，保留重复/空行/异常证据。Windows API 路径、DLL 加载和真实交易服务器延迟，需要现场编译与压测；不将 macOS fsync 或模拟成交的耗时当作 Windows 性能承诺。完整验证项目及结果随源码包的 `VALIDATION_STATUS.json` 提供。
