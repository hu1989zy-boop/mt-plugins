# MT4 动态杠杆

> 2026-10-14：账户杠杆写入已停用，当前正式 DLL 会拒绝启动旧执行流程。新需求是新仓使用新杠杆、旧仓保持原杠杆。以下是旧版构建与运行说明，其中部署启用和账户杠杆变化的步骤已失效。当前状态见 [新仓保证金实现](../docs/new-position-leverage.md)。

独立 MT4 **Server API 插件 DLL**，以交易账号为单位修改 `UserRecord.leverage`。须由券商在 MT4 Trade Server 侧加载；MT4 终端 EA 不能修改账户杠杆。

使用仓库原始 `vendor/mt4-sdk/include/MT4ServerAPI.h`（Server API 5），Windows MSVC **Win32 / x86** 编译：

```powershell
.\plugins\dynamic-leverage\mt4\build-windows.ps1
# 指定本地 SDK 时：
.\plugins\dynamic-leverage\mt4\build-windows.ps1 -SdkRoot C:\SDK\mt4-sdk
```

产物为 `mt4/build-windows/Release/MT4DynamicLeverage.dll`。部署至服务器插件目录后，在 MT4 Administrator 插件配置中设置：

| 参数 | 默认值 | 说明 |
| --- | --- | --- |
| `Enabled` | `0` | 设置为 `1` 后开始调节 |
| `RulesFile` | 空 | 管理端生成的 UTF-8 JSON 的绝对路径，如 `C:\DynamicLeverage\runtime\mt4.rules.json` |
| `Accounts` | 空 | 兼容旧版：逗号分隔账号；与非空 `RulesFile` 互斥 |
| `Tiers` | `0:1000;2:500;5:200` | 仅用于未设置 `RulesFile` 的旧版模式；严格大于阈值时生效 |
| `PollIntervalMs` | `1000` | 漏失交易事件的周期补查间隔 |

v0.2.0 使用本机管理端维护多条组别/账户规则，DLL 只读 JSON，不提供网络服务。`Enabled=1` 与 JSON 的 `enabled=true` 必须同时满足；任一关闭即停止新写入。JSON 的 `poll_interval_ms` 为文件模式实际轮询间隔，SDK `PollIntervalMs` 仅用于旧版模式。

组别选择器支持 `*`、`ABC*`、`*ABC*`、`!ABC`，逗号分隔，大小写敏感；排除条件优先否决，与排列位置无关，仅排除项隐含包含其他全部组别。账户选择器为逗号分隔的账号。组别与账户规则统一按列表从上到下，第一条已启用且命中的规则生效；规则内仍按每个账户的独立总持仓计算。

原生 `PluginCfg` 字段限制：参数名最多 31 字符、参数值最多 127 字符，因此 `Accounts` 和 `Tiers` 整个列表也受 127 字符限制，`RulesFile` 路径也最多 127 字节；超长配置会被拒绝。JSON 内规则列表不受原生参数字段长度限制。MT4 账号必须位于 `1..2147483647`（`INT_MAX`），不得重复。

恰好 2 手保持 1000，2.01 手降至 500；恰好 5 手保持 500，5.01 手降至 200。减仓时自动恢复对应档位，清仓恢复 1000。各品种买卖持仓按手数相加，对冲仓位不抵消；排除挂单、历史、余额和信用记录。MT4 原生 `TradeRecord.volume` 的 100 单位等于 1 手，转换为共享层每手 100,000,000 单位后整数比较。

启动和每个轮询周期重新读取 JSON；存在组别规则时使用 `ClientsAllUsers` 获取账号及组别，再由共享匹配器筛选，SDK 数组以 `HEAP_FREE` 释放。仅账户规则无需全服枚举。交易、用户新增/更新/恢复事件只合并账号并触发该账号核对，避免每笔交易造成全服扫描；新开户和迁组也由周期发现补查。扫描边界收到的账号事件与本次候选名单合并，不会因全量扫描而丢弃。交易新增覆盖 `MtSrvTradesAdd`、`MtSrvTradesAddExt`、`MtSrvTradesAddRequest` 三种 SDK 回调。所有读写集中于一个 worker。

配置文件丢失、JSON 错误或平台不符时暂停写入，不回退旧规则，修复后自动恢复。首次加载、有效配置改变或故障恢复时记录 `MT4 configuration loaded`，包含开关、规则数、候选账户数和轮询间隔；配置未变时不重复输出。写入前重读文件并核对完整配置签名，原生参数版本变化或停止信号也会拒绝旧批次写入。已进入 SDK 的写入可能先完成；停用、移出规则后保留当时杠杆，重新启用时重新计算。

读取完整 `OrdersGetOpen` 数组并校验账号、ticket、状态和体积；可识别的读取异常会跳过该账号并重试，不当作零持仓。该 API 无独立错误码：代码将 `total` 初始化为 `-1`，拒绝未回填数量以及数量非零但数组为空的结果，接受 `null + total=0` 为空持仓。`ClientsAllUsers` 同样用 `-1` 数量哨兵防止把未完成查询当作空列表。必须在目标 Demo 服务器确认这两种 API 的读取失败不会同样返回 `null + total=0`；仅凭该 SDK 签名无法区分这种失败与真实空账户。

写入前重新读取完整 `UserRecord`，检查原杠杆、组别未变及配置版本，只赋值 `leverage`，保留其他字段。Server API 不提供账户更新的原子 compare-and-swap；最后一次检查与写入之间的并发管理员修改、迁组或文件替换仍存在极小窗口，需在 Demo 验证。实际保证金效果受品种、组配置及服务器规则影响。

可移植测试：

```sh
cmake -S plugins/dynamic-leverage/mt4 -B /tmp/dynamic-leverage-mt4 -DBUILD_TESTING=ON
cmake --build /tmp/dynamic-leverage-mt4
ctest --test-dir /tmp/dynamic-leverage-mt4 --output-on-failure
```

测试覆盖原生手数转换、严格阈值、买卖相加、历史/挂单过滤、部分平仓余单、还原订单、账号隔离、损坏记录拒绝，以及写入前组别/杠杆变更保护。`mt4_dynamic_leverage_worker_tests` 直接运行 DLL 使用的生产 worker（替换底层 SDK 端口），验证线程唤醒/停机、账户事件、周期发现、JSON 热重载及优先级、错误恢复、扫描边界事件不丢失，以及写入前停用/文件替换拒绝旧批次。

Windows server build 另运行 DLL 加载与全部导出检查，检测依赖缺失和导出错误；该检查不连接交易服务器。macOS/Linux 运行共享规则、可移植适配模型及生产 worker 测试，**不代表 DLL 已经通过 Windows 构建或真实 MT4 Server 验证**。真实部署还需执行上层 `docs/demo-validation.md` 中的场景。
