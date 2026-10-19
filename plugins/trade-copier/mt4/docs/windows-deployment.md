# MT4 同服跟单：Windows 构建与 Demo 部署

本轮交付 MT4 Server API 跟单源码及独立 Windows 管理器，接口基准为 `ServerApiVersion=5`。它复用 `trade-copier/shared/core` 的手数计算、配置、动态订阅、冻结开仓规则、账本与管理协议，并由 MT4 专属适配器查询和执行交易。MT4 / MT5 跨服务器路由、Web / Java 管理端不在本轮范围。

本版为 0.2.0 Dealer 执行测试源码，尚未完成本版 Windows 编译和 MT4 Demo 交易验收。此前 0.1.0 现场加载记录不能替代新版本验收。部署前核对 Trade Server 名称及版本，并通过插件加载日志和受控交易确认兼容性。新增流程及验证边界见 [Dealer 执行说明](dealer-execution-0.2.0.md)。

## 1. 构建

向 Windows 复制源码时，必须同时复制同一版本的 **`mt4/` 和 `shared/` 完整目录**，并保持同级关系。仅复制 `mt4/` 会报 `include could not find .../shared/cmake/Core.cmake`，随后出现 `Unknown CMake command "tradecopier_add_core"`。这是公共源码缺失，尚未进入 C++ 编译。推荐保留以下路径：

```text
mt-plugins/
├── plugins/trade-copier/
│   ├── mt4/
│   │   ├── CMakeLists.txt
│   │   └── scripts/build-windows.cmd
│   └── shared/
│       ├── cmake/Core.cmake
│       ├── core/
│       └── tests/
└── vendor/mt4-sdk/include/MT4ServerAPI.h
```

独立构建 MT4 不需要复制 MT5 平台目录。Server SDK 单独提供，可用 `-SdkRoot` 指向其实际位置；`shared/` 是本项目的公共源码，不能用 SDK 目录代替。如果解压或移动到新路径，使用新的 `-BuildDirectory`，避免复用记录旧路径的 CMake 缓存。

准备 Windows C++ 工具链、Visual Studio 2022 / 2026 开发者命令行和匹配生成器的 CMake。MT4 DLL 构建目标为 **Win32 / x86**，使用 `_USE_32BIT_TIME_T` 和静态 MSVC 运行库；不要沿用 MT5 的 x64 构建缓存。

从仓库根进入 MT4 目录，指定含 `include\MT4ServerAPI.h` 的 SDK 根目录：

```cmd
cd /d "D:\work\mt-plugins\plugins\trade-copier\mt4"
scripts\build-windows.cmd -SdkRoot "D:\SDK\mt4-sdk"
```

默认构建执行器、管理器和账本检查器；`-Executor` 是可选的兼容开关。SDK 默认路径为仓库根目录的 `vendor\mt4-sdk`，不可使用 `vendor\mt4-manager-sdk` 代替。脚本检查 `ServerApiVersion=5`，运行 CTest，并静态检查 Win32 产物及 DLL 导出。失败时保留 `build.log`，不会部署 DLL。

PowerShell 可直接执行 `./scripts/build-windows.ps1`。如果源目录、VS 版本或架构改变，指定全新构建目录：

```cmd
scripts\build-windows.cmd -SdkRoot "D:\SDK\mt4-sdk" -BuildDirectory build-windows-mt4-clean
```

仅编译管理器用 `-UiOnly`，无需 SDK；不得同时使用 `-UiOnly -Executor`。默认产物在 MT4 目录内的 `build-windows\package-executor`，只编译界面则为 `build-windows\package-ui`。

完整包包括：

| 文件 | 用途 |
| --- | --- |
| `MT4TradeCopierExecutor.dll` | MT4 服务端交易执行器，默认 `Armed=0` |
| `MT4TradeCopierManager.exe` | 本机关系配置和运行监控 |
| `MT4TradeCopierJournal.exe` | 停用插件后离线检查账本 |
| `copier.example.json` | MT4 配置示例，关系及总开关默认禁用 |
| `copier.symbol-routing.example.json` | 带品种过滤及后缀匹配的禁用配置示例 |
| `windows-deployment.md` / `editor-readme.md` | 部署与管理器操作说明 |
| `build-manifest.json` | 构建版本、SDK 头文件哈希、产物哈希和验证状态 |

构建仅在本地生成文件，不启动 MT4、不连接账户、不启用交易。清单中的 MT4 加载、真实执行和界面运行状态保持未验证；原生管道集成检查需要管理员权限，测试输出为跳过时不能视为管道权限已经验收。

## 2. 准备测试环境

使用独立 MT4 Trade Server 测试环境和受控测试账户。首版按 USD 账户配置，先用单源单跟随、同名品种和小手数验证。不得将 MT5 配置或账本复制给 MT4，也不要复用已有 MT5 的运行目录。

通过 MT4 管理器创建配置草稿，填写实际源和目标账号，选择倍率、净值比例或固定手数。在计划开始测试前保持配置总开关和关系禁用。将配置及账本目录放在 MT4 服务账号可读写的位置，例如：

```text
D:\MT4Copier\config\copier.json
D:\MT4Copier\runtime\executor.journal
```

账本及其 sidecar 文件共同保存执行状态。运行期间不得删除、覆盖或用旧备份回滚它们；更换程序前停用模块并备份完整运行目录。

## 3. 插件配置与首次加载

按 MT4 Trade Server 的插件部署流程，将 DLL 放入该交易服务器使用的插件目录，通过 Administrator 配置独立实例。不要放入交易终端的 `MQL4\Experts`。所有路径使用测试服务器上的实际路径；插件模块发现及重载方式以该服务器环境为准。

| 参数 | 初次设置 | 含义 |
| --- | --- | --- |
| `Armed` | `0` | 实际交易总闸门，首次加载保持关闭 |
| `ExpectedServerName` | 实际 Trade Server 名称，必填 | 核对连接到的交易服务身份 |
| `ExpectedServerBuild` | `1470`，须先与实际 Trade Server 核对 | 目标服务版本检查 |
| `ServerKey` | `1` | 本机实例标识，与管理器服务器标识一致 |
| `ApiAppId` | `3401` | 本插件应用标识，与管理器一致 |
| `DealerLogin` | `0` 占位；启用前改为真实 MT4 Dealer 账号 | 所有跟单请求的锁定及确认身份，须有 Deal 权限及目标组/品种组权限；不填写密码 |
| `ConfigPath` | 配置 JSON 完整路径 | 初始关系配置 |
| `JournalPath` | 独立账本完整路径 | 请求进度与归属恢复 |
| `ManagementEnabled` | `0`；准备管理测试时改为 `1` | 本机管理员管道，默认关闭 |
| `AllowedSymbol` | `*` 或本次测试范围 | 源品种过滤范围 |

这些是插件启动参数。运行且 `Armed=1` 时，身份、路径或其他启动参数变更会被拒绝，保留当前实例继续管理旧仓；须明确设为 `Armed=0` 停用后再修改。配置回调会按停用/启动边界应用更改，若宿主要求重新加载则按其部署流程操作。启用失败会记录原因并尝试恢复之前的停用配置。管理器下发订阅使用独立运行时协议，无需重启。

首次配置分两次保存：先保持 `Armed=0`，填好实际名称、Build、路径、`ServerKey`、`ApiAppId`、真实 `DealerLogin`、品种范围及 `ManagementEnabled=1`，保存并重新打开参数页确认值已保留；再单独将 `Armed` 改为 `1` 保存。当前参数回调只有启动成功才接受整份新配置，失败会回退到上一次接受的配置。若首次填写所有参数时直接设为 `Armed=1`，一旦启动失败，便可能整份恢复到初始空路径和默认值。先保存停用配置可保留所填参数，后续启用失败只回到这份停用配置。

先确认名称、Build、API、配置和账本路径匹配，并检查首次加载及停止日志。`ExpectedServerName` 必须逐字使用本插件日志 `server=` 的值，而非终端显示名称或安装目录名。排查时清空日志过滤，或搜索 `MT4 copier startup failed`；仅搜索 `MT4TradeCopierExecutor` 会漏掉当前启动失败消息。身份检查或账本加载失败时，先解决日志中的原因，不能以清空账本或修改 SDK 版本宏绕过检查。

## 4. 管理连接与交易验收

`Armed=0` 时不启动管理监听，即使 `ManagementEnabled=1` 也仅可离线编辑 JSON。确认试验范围后，先通过管理器离线编辑并保存 JSON，启用配置总开关及本次测试关系；按上一节保存并核对完整的停用参数后，再单独将 `Armed` 设为 `1`，按宿主流程应用配置或重新加载。

在同一台电脑以管理员身份运行 `MT4TradeCopierManager.exe`，填写对应 `ServerKey` 和 `ApiAppId`，连接并读取生效配置。等待执行器完成启动基线和日志确认后，才在源账户产生新的测试订单。启用前、暂停期间和停机期间的旧仓不应自动补跟。处理时源仓已发生平仓则不追补该开仓，包括开仓事件排队期间已经发生部分平仓的情况；这与共享 MT5 引擎的开仓准入规则一致。

逐项核对源订单、目标订单、实际数量、价格、时间、请求结果及账本记录：

1. 一次开仓、全部平仓；目标人工全平后不补开。
2. 源部分平仓，核对 MT4 剩余票号衔接和目标实际剩余量；目标人工部分平仓后的继续减仓。
3. 三种手数模式、最小手数和步长、净值比例快照。
4. 运行中加入关系只跟新仓；修改规则后旧仓仍按冻结规则管理。
5. 暂停新仓仍管理旧仓；删除或停用订阅并应用，会退出并关闭该关系所属仓位。
6. 多源多目标及品种映射，核对互不串单；品种候选冲突不得下单。
7. 重启恢复、交易拒绝、查询异常和存储故障；不确定请求应先核对，不能自动重复提交。
8. 管理界面关闭/重开、断开/重连、错误实例标识，以及同时运行 MT4 / MT5 管理器时的管道隔离。

账户净值通过 SDK 示例所用的 `TradesMarginInfo(UserInfo + ConGroup)` 读取。附带示例未说明该函数的整数返回值约定，当前实现先将输出初始化为 `NaN`，再核验账户身份、数值是否有限及范围；Demo 验收仍须确认实际返回约定和 USD 两位金额精度。负净值允许作为账户状态读取；没有正净值时，拒绝按净值比例计算的新仓。

订单查询在最多三次 `closed + open` 连续采样中，要求相邻两份关键字段一致。这会减少读取期间交易变化的影响，但不构成服务器原子快照；未取得稳定结果时不会据此继续下单：源仓查询及请求确认可等待至超时，目标仓位规划查询失败则隔离对应映射，不会把缺失历史视为零仓位。Windows 原生 SDK 桥接目前仅完成静态接口审查，尚未通过 Windows MSVC Win32 实际编译和上述 Demo 验收。

挂单不直接复制，源挂单成交形成新市价仓后才按规则处理；目标不复制 SL/TP 数值。Close By、展期、复杂订单修订等特殊路径必须单独确认适配器支持和实测证据，不能因普通开平仓通过就宣称覆盖。

发现目标仓位与映射不符或成交结果不确定时，保留交易日志和完整账本，按实际状态核对处理。此构建和部署文档不承诺生产吞吐量或跨服延迟。

## 5. 旧 0.1.0 的请求诊断记录（2026-09-30）

以下保留旧版本的排错依据。0.2.0 已新增指定 Dealer 锁定和确认，诊断修订为 2；当前操作以 [0.2.0 说明](dealer-execution-0.2.0.md) 为准。

`QUEUED intent=N request=R` 只表示当前适配器获得了可接受的提交返回值和非零服务器请求号，不表示成交。`execution_confirmation_timeout` 表示在等待窗口内没有取得足够确认；管理器的“未决请求”也包含待人工核对记录，不能据此判断请求当前是否仍在服务器队列。

当时 0.1.0 源码的诊断修订号为 1，Windows 构建清单含 `request_diagnostics_revision=1`，新 DLL 的启动日志含 `request_diagnostics=1`。需用当前完整 `mt4/` 与 `shared/` 源码重新构建，旧 DLL 不会出现新增日志。此修订只补证据，未改变交易路由或自动确认请求：

| 日志 | 记录内容与边界 |
| --- | --- |
| `MT4_REQUEST_SUBMIT` | SDK 原始返回码、request、账户组、品种、原生 volume、请求类型/标记、实际传入的 isdemo、账户组 execution 与品种 exemode |
| `MT4_REQUEST_APPLY` | SDK Apply 回调时的 request/status/manager/isdemo；含当前受管源、跟随账户的买卖请求，可对照正常终端请求 |
| `MT4_REQUEST_RESTORE` | SDK Restore 回调时的原始字段；该回调没有 isdemo 参数，日志用 `-1` 表示未知 |
| `ISOLATED_MAPPING` | intent、request、source_position、目标账户及 evidence_reason |
| `MT4_REQUEST_DIAGNOSTICS_DROPPED` | 有界诊断队列或格式化/日志失败导致观测缺失，不能据回调缺席推断请求状态 |

没有匹配订单或执行回调时，`evidence_reason=mt4_no_matching_order_or_execution_callback`；已有匹配订单但缺精确回调时仍为 `mt4_awaiting_exact_request_callback`。Apply/Restore 只记录经过该回调时的现场，不是完整请求状态轮询，也不会被当作成交或最终拒绝证据。诊断走独立的 256 项队列，每个工作循环最多处理 32 项，溢出不会占用交易事件队列或改变交易状态。文本字段转义成单行。

账户组中的证券组 `execution` 与品种 `exemode` 是两项不同设置；SDK 枚举分别为 `MANUAL=0/AUTO=1/ACTIVITY=2` 和品种执行模式。当前提交传入 `RequestsAdd(...,FALSE,...)`；SDK 附带资料不足以证明应按服务器名称、账户组名称或 `ManagersIsDemo` 返回值更改该参数。因此旧 0.1.0 诊断修订保持原参数，不调用 Dealer 领取、确认或重置接口，也不猜测无示例的 Server `RequestsGet` 游标语义。

现场核对顺序：先按旧请求号检查已有服务器日志和实际持仓/历史，核对目标账户组对应证券组的 Execution。需要新增取证时，再按宿主的安全停用/替换流程加载诊断 DLL，保留配置及完整账本。暂停新仓不会取消此前已提交的服务器请求；旧不确定请求未核对前，不以清空账本、重启或重复下单尝试“修复”。旧请求进入待核对后，新增 DLL 也不能恢复其过去未记录的回调现场。
