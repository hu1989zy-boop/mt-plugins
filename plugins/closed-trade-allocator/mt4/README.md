# MT4 Server API 平仓后分配插件

独立 DLL `MT4ClosedTradeAllocator.dll`，使用本地 MT4 Server API 5，Windows / MSVC / Win32。与跟单插件不共享模块、配置或运行账本。

实现直接 `OrdersAdd` 插入完整已平仓 `TradeRecord`：复制方向、品种、手数、开平仓时间和价格、SL、TP、利润、佣金、隔夜利息、带符号 taxes 及换汇元数据；Comment 始终为空。不发送市场开平仓请求，不通过余额调整模拟历史。

该 SDK 头文件没有证明 `OrdersAdd` 对直接已平仓记录的资金副作用。代码包含实际调用和强制核对，但本地构建/测试不等于服务器 Demo 验收。默认 `Armed=0`、`DirectClosedAddVerified=0`。在隔离 Demo 账号中，可明确设置两者为 1 发起验证；只在该服务器 build 的完整 Demo 验收通过后用于正式账号。若 SDK 没有正确结算余额，任务进入 `review`，本插件不会调用 `ClientsChangeBalance` 补差。

## 构建

安装 Visual Studio 2022 Desktop development with C++、Windows SDK、CMake 3.21+。本地提供未经修改的 `vendor/mt4-sdk/include/MT4ServerAPI.h`。

```powershell
.\plugins\closed-trade-allocator\mt4\build-windows.ps1
```

等价命令：

```powershell
cmake -S plugins/closed-trade-allocator/mt4 -B plugins/closed-trade-allocator/mt4/build-windows -G "Visual Studio 17 2022" -A Win32 -DCLOSEDALLOC_MT4_BUILD_SERVER=ON -DMT4_SERVER_SDK_DIR=C:/sdk/mt4-sdk
cmake --build plugins/closed-trade-allocator/mt4/build-windows --config Release --parallel
ctest --test-dir plugins/closed-trade-allocator/mt4/build-windows -C Release --output-on-failure
```

产物在 `build-windows/Release/MT4ClosedTradeAllocator.dll`。测试不能证明真实服务器 ABI、历史插入、资金及网关副作用；仍需 Demo 验收。macOS/Linux 可构建默认目标运行共享引擎和 MT4 纯模型测试，不产生可部署 DLL。

## MT4 Administrator 参数

| 参数 | 默认 | 含义 |
|---|---|---|
| `SourceLogin` / `TargetLogin` | 0 / 0 | 同服不同账号，正 32 位整数；均为 0 时插件保持未配置 |
| `ServerName` / `ServerBuild` | 空 / 0 | 精确核对当前服务器名称与 build |
| `JournalPath` | 空 | 现有目录中的独立账本路径；必须是固定本地盘绝对路径，不能是 UNC 网络盘 |
| `LossPercent` / `ProfitPercent` | 30 / 70 | 各自 0～100 的百分比，允许小数，不要求相加 100 |
| `Enabled` | 1 | 启用新筛选；改为 0 创建暂停区间，恢复时跳过该区间完成平仓的交易 |
| `Armed` | 0 | 允许提交目标历史；0 仍查询、作决定和核对已有任务 |
| `DirectClosedAddVerified` | 0 | 已明确允许在该 build 使用直接已平仓插入；Armed=1 必须同时为 1 |
| `EnabledSince` | 0 | 服务器交易时间整数秒；0 首次初始化时在账本固化启用边界，不补写边界及更早交易 |
| `SettleSeconds` | 30 | 平仓后等待且源快照连续不变的最少秒数，范围 5～3600 |
| `PollSeconds` | 5 | 历史补查间隔，范围 1～300 |
| `CurrencyDigits` | 2 | 已确认的账号币种精度，范围 0～8；本 SDK 分组无明确币种精度字段，部署时必须正确配置 |

账号配置后只有 `Enabled`、`Armed`、两项概率支持热修改。其它字段需卸载/重载插件；账本路由身份仍必须一致。配置更新由单个工作线程依次持久化，生效时间以工作线程处理时的服务器时间为准。不可通过换账本重新抽样或重新分配。

目标账号必须启用、只读并且无任何持仓、挂单或保证金敞口；源/目标币种必须相同。品种可用性、精度和手数限制需兼容。若分配后余额将小于零，保持入选待处理并记录原因。`taxes` 作为带符号费用加到净盈亏，需 Demo 核实所在 build 的符号口径。非零代理佣金 `commission_agent` 暂不写入，避免未经验证的第三方代理账号副作用。

## 读取、去重与核对

- 交易回调只唤醒工作线程；实际读取 `OrdersGetClosed` 权威历史及开放订单。停机后从同一账本恢复；每次扫描最多接受 100,000 条历史记录，超过即停止并报告，不截断漏单。
- 首版只接受 `TS_CLOSED_NORMAL`、BUY/SELL、一次性完整平仓。部分平仓状态、余单状态以及 `from #` / `to #` 链全部排除。Close By、已删除等其它状态也保守排除。手工删除/篡改平台部分平仓标记后无法还原真实交易结构，不能作为可分配来源。
- 历史在等待区间前后连续两次相同才观察；后续历史修订由共享账本识别为差异，不再次抽样。等待不是平台“费用永久最终确定”的证明。
- 目标保留 `TR_REASON_API`；`magic=0x43414c31`，`api_data[0]=0x43414c31`，`[1]/[2]` 为随机 64 位分配 tag，`[3]` 为源票号。来源账号、服务器、规则和写入时间保存在独立账本，不写 Comment。带本插件 namespace 的订单不能作为源信号。
- 写前持久化目标完整历史的 SHA-256、余额、信用额、净值、保证金、可用保证金。写后按 API tag 及返回票号查询，逐字段核对 SL/TP、价格、数量、费用、空 Comment 和来源时间。只有目标历史仅增加预期的一条、无持仓，且余额/净值/可用保证金各增加净盈亏一次时，才标记 `completed`。
- 返回丢失、接口报错但副作用不明、标记丢失、记录被修改、其它账号活动导致核对证据变化，均进入 `review`。核对只读，永不修复余额、删除目标记录或盲目重发。未决任务阻止后续分配，需先人工核对。
- 写前发现相同源票号已有本插件标記，即使随机 tag 不同也拒绝再写。进程级服务器互斥与账本文件锁避免多个实例并发分配。任何磁盘/账本错误使工作线程停止，不继续写入。

## 状态与 Demo

`JournalPath.status.tsv` 自动导出每笔任务、净盈亏、抽样和状态、目标票号及错误；不要编辑二进制账本。MT4 Server 日志标签为 `closed-allocator`。JournalPath 目录及日志仅对服务器服务账号和管理员开放，并备份保留。

Demo 至少逐笔覆盖盈利、亏损、扣费后变号、SL/TP、0%/100%、双 100%、重启、部分平仓排除、暂停恢复、源修订和回执丢失。先选专用只读目标且无其他管理程序改动；检查资金变化、报表、没有目标持仓、没有网关下单、API 标记保留。模拟“写入后账本保存前崩溃”时，只允许回查完成或待核对，不允许第二次 OrdersAdd。此版本未连接真实服务器，也未宣称通过上述 Demo 验收。
