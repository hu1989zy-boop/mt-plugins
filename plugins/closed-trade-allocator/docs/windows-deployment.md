# Windows 构建与 Demo 部署

本插件在 Trade Server 内运行，通过 Server API 把源账号完整平仓历史按概率分配到同服目标账号。它不安装到交易终端，不是 EA，也不使用跟单插件的配置。源码已实现；目标 Windows 构建、实际插件加载和历史/资金效果需要按 [验证记录](implementation-validation.md) 分别验收。

## 构建目录与工具链

保留 `plugins/closed-trade-allocator/shared/` 和所选 `mt4/` 或 `mt5/` 目录的相邻关系。公共核心不是 SDK，单独复制平台目录无法完成构建。SDK 使用本地授权文件，不随插件源码发布。

```text
mt-plugins/
├── plugins/closed-trade-allocator/
│   ├── shared/
│   ├── mt4/
│   └── mt5/
└── vendor/
    ├── mt4-sdk/include/MT4ServerAPI.h
    └── mt5-sdk/Include/MT5APIServer.h
```

准备 Visual Studio C++ 工具链及 CMake 3.21 或更高版本。MT4 使用 **Win32 / x86**；MT5 使用 **x64**。两者使用独立构建目录，不能共用 CMake 缓存。服务器 build 和 SDK 兼容性以实际 Trade Server 为准，不能用终端或 Administrator 客户端版本替代。

在仓库根的 Windows 开发者命令行执行：

```cmd
plugins\closed-trade-allocator\mt4\build-windows.cmd -SdkRoot "D:\SDK\mt4-sdk"
plugins\closed-trade-allocator\mt5\scripts\build-windows.cmd -SdkRoot "D:\SDK\mt5-sdk"
```

脚本配置 CMake、构建 Release 并运行本地测试，不复制 DLL 到服务器，不自动启用写入。默认产物分别位于：

| 平台 | DLL |
| --- | --- |
| MT4 | `plugins\closed-trade-allocator\mt4\build-windows\Release\MT4ClosedTradeAllocator.dll` |
| MT5 | `plugins\closed-trade-allocator\mt5\build-windows\Release\ClosedTradeAllocatorMT5.dll` |

手动构建等价命令：

```cmd
cmake -S plugins/closed-trade-allocator/mt4 -B build/closedalloc-mt4 -A Win32 -DCLOSEDALLOC_MT4_BUILD_SERVER=ON -DMT4_SERVER_SDK_DIR="D:/SDK/mt4-sdk" -DBUILD_TESTING=ON
cmake --build build/closedalloc-mt4 --config Release --parallel
ctest --test-dir build/closedalloc-mt4 -C Release --output-on-failure

cmake -S plugins/closed-trade-allocator/mt5 -B build/closedalloc-mt5 -A x64 -DCLOSEDALLOC_MT5_BUILD_SERVER=ON -DMT5_SDK_ROOT="D:/SDK/mt5-sdk" -DBUILD_TESTING=ON
cmake --build build/closedalloc-mt5 --config Release --parallel
ctest --test-dir build/closedalloc-mt5 -C Release --output-on-failure
```

MT4 强制使用 MSVC Win32 和未经改动的 API 5 头文件；当前 DLL 定义 `_USE_32BIT_TIME_T`。MT5 要求未经修改的 API 6182 头文件。正式部署应使用对应服务器支持的 Windows / MSVC 工具链完成编译和导出检查。

## 接收账号与运行目录

每个实例配置一条 A → B 关系，源和目标账号必须不同、同服同币种。B 应为专用分配账号，禁止普通交易、持仓、挂单及与分配并发的出入金或历史修改。MT4 强制 B 只读；MT5 要求只读或禁用交易权限，且 A、B 均使用 hedging 组。

MT4 当前在同一物理服务器上只允许一个本插件实例。MT5 同一服务器上的同一 B 账号只能由一个实例持有。账本另有排他文件锁；这些限制用于避免不同关系同时改变 B 的余额而破坏资金核对。

为服务账号准备可读写、持久保存的本地目录，例如 `D:\ClosedTradeAllocator\mt4\` 和 `D:\ClosedTradeAllocator\mt5\`。两平台均要求 `JournalPath` 为固定本地磁盘上的绝对路径，拒绝相对路径、UNC 和网络盘。MT4 父目录须预先存在，MT5 可创建父目录。账本、TSV 报告和日志不能与跟单插件混用。

DLL 按 Trade Server 的插件部署流程安装到服务器插件目录，在 Administrator 中填写本插件参数；不要放到终端的 `MQL4\Experts` 或 `MQL5\Experts`。

### MT5 文件存在，但 Module 列表没有显示

`plugins` 下的子目录受支持。例如当前 Demo 使用的路径可为：

```text
D:\MT5 Platform\Demo MainTrade\plugins\ClosedTradeAllocator\ClosedTradeAllocatorMT5.dll
```

本地 SDK 的 `serverapi_interaction.htm` 说明：Trade Server 启动时扫描 `plugins`、`reports` 及其子目录，加载 DLL 并读取 `MTServerAbout`。因此首次放入新的 DLL 后，在该 Demo 服务可暂停时重启对应 Trade Server，再重新连接 Administrator、打开插件配置。仅重新打开 Administrator 不能替代服务器启动扫描。该环境此前的跟单 DLL 也在相同服务重启后被发现。

Module 右侧服务器应选择 DLL 所在的交易服务器。回到插件配置列表新增本插件配置，然后选择 `ClosedTradeAllocatorMT5.dll`；若当前打开的是 `EMIR CME Reporter` 等已有插件，先取消退出其编辑窗口，避免覆盖已有配置。初次配置保留 `Armed=0`。

若对应服务重启后仍不可见，检查该服务器启动时的 DLL/插件加载日志，再按实际错误检查 x64 架构、`MTServerAbout` / `MTServerCreate` 导出、API 兼容性和 VC++ 运行库依赖。文件存在本身不证明 DLL 加载成功；不能仅凭模块列表缺失认定其中某个原因。

### MT5 加载错误 126：找不到模块或其依赖

用户反馈 `APIDispatcher: plugin ClosedTradeAllocatorMT5.dll load error [The specified module could not be found. (126)]`。这说明服务器已尝试加载插件；即使插件 DLL 文件存在，依赖 DLL 缺失也会导致加载失败。126 本身不指出缺失哪个依赖，不能据此认定 API 版本不兼容。[Windows 错误码](https://learn.microsoft.com/en-us/windows/win32/debug/system-error-codes--0-499-)、[LoadLibraryEx 依赖说明](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexa)。

原构建未指定 MSVC runtime，默认使用 `/MD`，需要外部 VC++ 运行库 DLL。现在按本地 SDK `Examples/Server/ServerPlugin/ServerPlugin.vcxproj` 示例，将公共库、平台模型、测试和插件统一为 Release `/MT`、Debug `/MTd`。同时移除 MT5 `DllMain` 中的 `DisableThreadLibraryCalls`，保留静态 CRT 需要的线程通知。[CMake runtime 默认值](https://cmake.org/cmake/help/latest/prop_tgt/MSVC_RUNTIME_LIBRARY.html)、[线程通知要求](https://learn.microsoft.com/en-us/windows/win32/api/libloaderapi/nf-libloaderapi-disablethreadlibrarycalls)。

当前 MT5 独立构建需同步以下四个文件（相对于 `plugins/closed-trade-allocator/`），保留此前 `MAXULONG` 的修正：

- `shared/CMakeLists.txt`
- `mt5/CMakeLists.txt`
- `mt5/src/plugin.cpp`
- `mt5/scripts/build-windows.ps1`

如果构建 MT4 或插件聚合工程，还需同步 `mt4/CMakeLists.txt`，以保持与公共库的 runtime 一致。

在 Windows PowerShell 执行（此前指定了 `-SdkRoot` 时继续带上该参数）：

```powershell
cd D:\work\mt-plugins\plugins\closed-trade-allocator\mt5\scripts
.\build-windows.cmd -BuildDir ..\build-windows-static
```

本次切换运行库使用新目录 `build-windows-static`，避免继承原缓存和产物；如果该目录已有其他配置的构建，换一个尚未使用的目录名。脚本会以 `--clean-first` 全量重编公共库、模型测试和 DLL。构建和测试通过后，在 Demo 可暂停时停止对应 Trade Server，把这次新目录中的 `mt5\build-windows-static\Release\ClosedTradeAllocatorMT5.dll` 覆盖到上述插件目录，再启动服务并检查日志/Module 列表。

若出现 `msvcprt.lib` 的 `LNK2005`、`MSVCRT` 的 `LNK4098`，或 `closedalloc_core.lib(core.obj)` 导入 CRT 符号的 `LNK4217`，说明链接混入了动态和静态 CRT。MT5 构建入口现在再次统一设置公共库及测试的 runtime，并打印目标名和实际源码目录。不能只把插件 DLL 改为 `/MT`，也不要用 `/NODEFAULTLIB` 隐藏冲突；所有相关对象必须按相同 runtime 重新生成。错误截图不能单独区分旧对象、源码同步不完整或外部编译参数覆盖。[微软 LNK2005 说明](https://learn.microsoft.com/en-us/cpp/error-messages/tool-errors/linker-tools-error-lnk2005)。

若新目录仍出现该冲突，保留完整构建输出，并核对新目录内 `ClosedTradeAllocatorMT5.vcxproj` 和 `shared\closedalloc_core.vcxproj` 的 Release 配置是否均为 `<RuntimeLibrary>MultiThreaded</RuntimeLibrary>`；同时检查是否有外部 `CL` / `_CL_` 环境变量、CMake flags 或 `.props` 注入 `/MD`。

若仍有 126，在 Visual Studio x64 Native Tools 命令提示符检查实际部署文件的导入依赖，并保留输出用于诊断：

```cmd
dumpbin /dependents "D:\MT5 Platform\Demo MainTrade\plugins\ClosedTradeAllocator\ClosedTradeAllocatorMT5.dll"
```

静态 CRT 版本不应再直接导入 `VCRUNTIME140*.dll`、`MSVCP140*.dll` 或 `ucrtbase.dll`，但仍需要 Windows 系统 DLL。依赖列表只用于核对，最终以目标服务器加载结果为准。

## 通用参数

两平台统一使用百分数：`LossPercent=30` 表示 30%，`ProfitPercent=70` 表示 70%。输入 `0.30` 表示 0.30%，不能当作 30%。两项均可独立配置为 0 到 100，不要求合计 100。

| 参数 | MT4 默认 | MT5 默认 | 含义 |
| --- | --- | --- | --- |
| `SourceLogin` | `0` | `0` | 源 A，配置实际非零账号 |
| `TargetLogin` | `0` | `0` | 目标 B，配置实际非零账号 |
| `LossPercent` | `30` | `30` | 亏损单入选百分比 |
| `ProfitPercent` | `70` | `70` | 盈利单入选百分比 |
| `Enabled` | `1` | `1` | 业务启用；0 表示持久化暂停区间 |
| `Armed` | `0` | `0` | 实际写入闸门；0 时可筛选与回查，不提交新写入 |
| `PollSeconds` | `5` | `30` | MT4 历史轮询；MT5 未写入/读失败重试间隔（实际最低 5 秒） |
| `SettleSeconds` | `30` | `5` | 平仓后/源快照稳定等待时间；MT5 两段等待并行 |
| `RescanSeconds` | 不适用 | `300` | MT5 增量补扫间隔，至少 60 秒；旧配置缺省自动用 300 |
| `JournalPath` | 空，必填 | `C:\ClosedTradeAllocator\` | 独立持久账本路径 |

首次完成配置时保持 `Armed=0`，确认源历史准入、目标账号条件及 TSV 内容。源账号是 0 或目标账号是 0 时，不能执行分配。MT5 配置不完整时会拒绝启动。

`Armed=0` 期间形成的已入选决定会保留，打开闸门后可能写入 B。若需要跳过某个时间段，使用 `Enabled=0`；恢复时暂停区间内已平仓交易仍然跳过。已提交但未确认的任务在暂停/未开闸门时继续只读回查。

## MT4 专属参数与写入方式

| 参数 | 默认 | 要求 |
| --- | --- | --- |
| `ServerName` | 空 | 精确填写实际 Trade Server 名称 |
| `ServerBuild` | `0` | 精确填写实际 Trade Server build |
| `EnabledSince` | `0` | 0 时首次启动固化服务器时间；已有账本不可改启用边界 |
| `CurrencyDigits` | `2` | 账户币种金额精度，须与实际账户匹配 |
| `DirectClosedAddVerified` | `0` | 明确允许测试/使用直接已平仓 `OrdersAdd` 路径；`Armed=1` 要求该参数为 1 |

MT4 使用 `OrdersAdd` 写入完整已平仓记录，复制 SL/TP，清空 Comment，并写入非备注 API 来源标记。不会发送市场交易请求或调用余额补记接口。存在代理佣金等不支持的费用情形会拒绝提交；可能形成负余额的分配也不提交。

此 SDK 路径的资金副作用必须由实际服务器证明。首次受控 Demo 写入需在测试环境显式设置 `DirectClosedAddVerified=1` 与 `Armed=1`；该配置只是准许走测试路径，不代表插件已自动确认服务器行为。只有回查历史字段、目标余额/净值/可用保证金及无活动持仓全部通过，任务才算完成。历史已写入但资金效果不符时进入 `review`，不补余额也不重复写历史。

## MT5 历史与余额路径

MT5 从实际服务器读取 `server_id` 绑定账本，不提供额外 `ServerName` / `ServerBuild` 参数。首次启动固化当前服务器时间；默认不补录更早平仓。成交/历史事件只传标识到后台，由账号 + PositionID 定向查询完整生命周期。源快照至少两次相同并持续不变达到 `SettleSeconds` 才纳入，提交前再次核对完整源成交集合。启动和丢事件时分段补查；平时每 `RescanSeconds` 按游标及重叠窗口补扫。限速、退避和实机负载验收边界见 [MT5 说明](../mt5/README.md#事件处理与负载边界)。

每个支持的源 PositionID 必须恰有一次开仓成交和一次等量完整平仓成交，以及可核对的开仓/平仓历史订单。部分平仓、加仓、多次成交、反转、Close By 和 netting 账号排除。两条目标历史订单和两条目标成交复制对应 SL/TP，Comment 均为空，关联到新目标 PositionID。

依据 SDK 语义，`DealAdd` 只修改成交历史，不直接改变账户余额。适配器先核对 B 的原有历史与账户余额一致，并记录全目标历史校验摘要；完整历史写入及字段关联核对通过后，持久化资金操作意图，再调用一次 `TradeBalanceCheck(...,1)` 将余额校正到已验证历史，随后只读核对余额、信用额、净值、可用保证金和无持仓状态。它不会追加一笔重复计算盈亏的余额成交。

`TradeBalanceCheck(...,1)` 检查并校正整个账号，因此 B 必须专用，不能与其他交易或人工历史修改并发。若历史只写入一部分、原有记录改变、余额校正结果不明，任务停留 `review`，回查不会再次调用资金校正或补写缺少记录。此路径的真实服务器表现仍需 Demo 验证。

## 状态查询与参数修改

插件持续导出当前任务列表，字段包括源标识、平仓时间、净盈亏、概率、抽样值、入选标识、状态、API 标记、目标标识和原因：

| 平台 | 报告路径 |
| --- | --- |
| MT4 | `<JournalPath>.status.tsv` |
| MT5 | `<JournalPath>.tsv` |

TSV 可用文本编辑器或表格工具只读查看；它是派生报告，不能代替账本做恢复。运行日志中检查启动失败、历史读取失败和停止原因。首版没有独立图形管理器，也不提供修改账本状态的管理命令。

摘要变化时，日志分别输出盈利/亏损候选数、入选数和实际入选率，以及完成、待写、待核对和跳过总数。候选数为零时入选率显示 `n/a`。

MT5 事件版本还输出实际 `Enabled/Armed` 及一条待写/待核对原因，避免只看到 `selected` 增长却不知道阻止条件。`ClosedTradeAllocator load:` 每分钟报告源查询次数、积压、溢出和处理耗时。TSV 仅在内容变化时写文件。

### 从轮询版升级到 MT5 事件版

本次同时修改公共核心并新增源事件/调度文件，建议整体同步 `plugins/closed-trade-allocator/shared/` 和 `plugins/closed-trade-allocator/mt5/` 源码目录，保留目录结构及本地 SDK。不要只覆盖 `plugin.cpp`。脚本仍使用静态 CRT 和全量重编；原 `.journal` 和 `.tsv` 保留，账本格式、冻结决定及来源标记不变。

随附 `closed-trade-allocator-event-source.zip` 是源码更新包，内部从 `plugins/closed-trade-allocator/` 开始；解压到 Windows 仓库根目录（例如 `D:\work\mt-plugins`）并覆盖同名源码。包内不含 SDK、运行账本或 DLL；不要解压到交易服务器的插件运行目录。

```powershell
cd D:\work\mt-plugins\plugins\closed-trade-allocator\mt5\scripts
.\build-windows.cmd -BuildDir ..\build-windows-events
```

成功产物是 `mt5\build-windows-events\Release\ClosedTradeAllocatorMT5.dll`。按 Demo 维护流程替换 DLL；建议参数 `PollSeconds=30`、`SettleSeconds=5`、`RescanSeconds=300`，原账号/概率/账本身份沿用。旧配置没有 `RescanSeconds` 也会自动使用 300，已有 `SettleSeconds` 不会自动变为 5。正常情况下不再等待 `PollSeconds` 才发现新平仓；待写积压仍按每次 1 笔、成功至少间隔 5 秒消化。

验证阶段先比较插件关闭、启用但 `Armed=0`、受控 `Armed=1` 三种状态下相同 Demo 交易负载的成交延迟分位数、CPU、磁盘延迟、网关/请求队列错误率，再按目标正常客户的既定延迟标准验收。确认无越界回调工作、溢出可补查、慢查询会退避，及金额/SL/TP/空备注与旧版一致后，才能宣称该环境达到负载要求。尚未完成目标 Windows 压力验证，不能宣称“不影响正常客户”。

`skipped` 为永久跳过；`selected` 为已入选待写；`writing` 为已持久化写入意图；`completed` 为历史与资金核对完成；`review` 为需要保留证据核对。只要同一关系仍有已入选的未决写入，插件先核对它，不继续提交其他入选订单。

运行中可修改概率、`Enabled` 和 `Armed`；原决定不重抽。源/目标身份、账本路径或启用边界不能替换成新值后继续使用旧账本。MT4 的服务器身份、精度、轮询/稳定间隔及接口验证参数变更需按宿主流程停止并重载，不能绕过持久化身份约束。

## 故障与更换版本

停用/卸载插件后备份完整运行目录，再替换 DLL，并继续使用原账本和原关系身份。不得删除账本、回滚旧备份或新建账本“重试”同一分配；插件的非备注来源检查也会阻止已分配源交易在新标签下再次写入。

账本损坏、截断、占用、持久化失败或源历史修订时，保留账本、报告与服务器日志进行核对。`review` 不等于未写入，尤其不能因为没有成功回执就重发。实际目标资金与记录完成验证前，不应将本地测试通过写成 Demo 验收通过。

0.1.0 账本保留全部抽样与写入进度，不自动删除去重记录；上限为 512 MiB、500,000 个源交易任务及单帧 8 MiB，达到上限会停止新增写入。源/目标历史查询也有平台数量上限，超限报错而不截断结果。当前没有生产吞吐或长期容量验收结论。
