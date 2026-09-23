# TradeCopierSdkProbe：只读 Server API 接入探针

> 0.6.0 已更新运行策略：按映射隔离异常，不自动全局终止；订阅可运行中加入，退出会关闭该关系仓位。新的行为及升级步骤以 [0.6.0 说明](../docs/live-subscriptions-0.6.0.md) 为准。

> **当前版本 0.5.0**：新增多品种掩码与自动后缀匹配，账本写入 schema 5，保留 10,000 条容量与检查点修复。当前构建、设置和兼容说明以 [0.5.0 多品种说明](../docs/symbol-routing.md) 为准；下文版本记录用于历史参考。

> **Windows 构建更新**：0.4.6 已在用户本机测试发现检查点替换失败，改用 [0.4.7 修复包与构建步骤](../docs/windows-checkpoint-fix.md)；下文旧版本命令仅供历史参考。

0.4.6 支持同时在管 10,000 条映射，并同步扩大快照及文件容量保护；需一起更新执行器、界面和账本工具，详见 [10,000 条容量说明](../docs/ten-thousand-capacity.md)。

0.4.5 在确认平仓后清理完整历史，默认 32 MiB 检查点替换，不再自动归档；容量接近阈值时暂停新仓并继续管理已有仓位。兼容旧账本、写入 schema 4，详见 [运行账本与容量保护](../docs/live-state-journal.md)。不改变只读探针用途。

0.4.3 增加分账号异步调度，最多 32 笔跨账号请求在途；同一账号一次一笔，保留先落盘、成交核对和重启不重发。部署与性能验证见 [分账号调度](../docs/account-pipeline.md)。

0.4.2 执行器允许暂停新仓后带仓新增 / 修改订阅；老映射持久化独立开仓规则，新仓使用新配置，无需先平旧仓。账本升级为 schema 3，需同步更新执行器、管理界面及账本检查器，见 [更新步骤](../docs/frozen-opening-rules.md)。本次不改变探针用途。

0.4.0 执行器新增默认关闭的 Integer `ManagementEnabled` 参数。设为 1 后，本机管理员界面可连接运行状态和配置应用通道；详见 [升级说明](../docs/management-ui.md)。探针仍只读。

这个目标生成独立的 `TradeCopierSdkProbe.dll`，用于 Windows 测试服务器验证 SDK 加载、配置读取和有限交易事件记录。**它不是完整跟单 DLL，不会下单、平仓、修改交易或账户，也不读取净值。** 不应把它作为业务跟单插件交付。

用户回传的 VS2026 构建截图已确认生成本 DLL，Demo 6182 日志进一步确认启停和普通成交回调正常；见 [探针验收记录](../docs/demo-probe-observations.md)。真实跟单使用新增的独立 `TradeCopierExecutor.dll`，见 [执行器教程](../docs/executor-demo-validation.md)。本探针继续保持只读。

## 构建前提

- Windows x64、Visual Studio 2022/v143 或 Visual Studio 2026/v145 C++ 工具链及 Windows SDK。VS2026 对应 CMake 4.2+；根构建脚本会检查生成器并固定当前 VS 安装实例。
- 用户提供的 SDK：`Include/MT5APIServer.h` 定义 `MTServerAPIVersion=6182`；安装包资源版本为 6191，这两个数字不是同一字段。
- 根项目通过 `MT5_SERVER_SDK_DIR` 指定 SDK 根目录，其下必须存在 `Include/MT5APIServer.h`；默认使用项目中的 `vendor/mt5-sdk`。

在 Windows 的项目根目录运行：

```powershell
cmake -S . -B build-probe -G "Visual Studio 17 2022" -A x64 -DTRADECOPIER_BUILD_SDK_PROBE=ON -DTRADECOPIER_BUILD_WINDOWS_UI=OFF
cmake --build build-probe --config Release --target TradeCopierSdkProbe
```

SDK 位于其他目录时，在第一条命令追加 `-DMT5_SERVER_SDK_DIR="C:/path/to/MetaTrader5SDK"`，使用实际路径。上述 Visual Studio 构建的输出为 `build-probe/server/Release/TradeCopierSdkProbe.dll`。只构建此目标不会打包 SDK 或部署 DLL；不能将整个 SDK 的 Libs 目录复制到服务器插件目录。

本目标固定 MSVC x64、C++17，使用 SDK 示例同类的静态 MSVC 运行库。`/EHa` 与各边界的异常处理减少异常逃出 DLL 的可能，但不能把损坏指针、宿主错误或运行时兼容问题变成已验证安全。最终仍需 Windows 构建和测试服验证。

## 参数

| 参数 | Administrator 类型 | 默认值 | 行为 |
| --- | --- | --- | --- |
| `WatchLogin` | String | `0` | 只观察这一账号的 Buy/Sell 成交；0 完全关闭账户事件订阅。数字字符串支持无符号 64 位账号。 |
| `MaxEvents` | Integer | `100` | 本轮启用最多记录的成交回调日志行数，允许 0–10000；0 关闭账户事件订阅。 |

缺失参数使用安全默认值；非数字、负数、类型不符或越界使 Start 失败。只在 Start 读取配置，**运行中改变参数后必须停用该配置，再重新启用**；不会订阅配置更新来热加载。

配额是回调日志行数，不是唯一成交数。一次 Close By 最多消耗两行；相同成交也可能从不同回调重复记录。该设计用于观察真实事件覆盖与重复，不会提前去重而隐藏证据。启动及配置错误日志不计入 MaxEvents。

## 记录范围和日志

仅实现 `IMTTradeSink` 的四个事件入口：

- `OnTradeRequestProcess`：记录非空 deal 对应的成功请求成交。
- `OnTradeExecution`：记录非空 deal 对应的网关执行。
- `OnTradeRequestProcessCloseBy`：分别记录 deal 和 deal_by，允许其中任一为空。
- `OnTradeExecutionCloseBy`：记录交易执行 Close By 的 deal 和 deal_by，共用一个回调生命周期保护；允许 execution 或任一 deal 为空。两腿使用 `GatewayCloseBy.primary`/`GatewayCloseBy.opposite` 日志来源标签。

不记录空 deal、非 Buy/Sell 成交或其他账号。日志包含回调来源、成交/订单/持仓标识、entry/action、SDK 扩展数量原值、成交毫秒时间以及可获得的请求或执行 ID。不获取或保存回调对象、符号字符串、账户余额或净值；运行中的宿主 API 主接口仅用于受控日志。

启动日志通过 `IMTServerAPI::About` 取得实际 server version/build，同时记录 `MTServerCreate` 传入的 runtime Server API 和编译目标 API。`MTServerAbout` 本身没有服务器接口，仅返回正确的插件描述与默认参数。

四个入口不代表已证明覆盖所有交易路径。`OnTradeExecutionCloseBy` 的签名和 Close By 事件含义由提供的头文件确认，但该 SDK 的英文 CHM 没有它的独立说明；日志标签不代表已验证网关路由覆盖，其发生时点及与其他入口的重叠仍需实测。源码还存在其他事件；源事件时点、Close By、分批成交和不同执行路由仍需在测试服务器观察。日志中的 order/position 标识不能被当作已经核实的执行后完整仓位快照。

## 生命周期和并发

参数及每轮运行状态在 Start 中准备，配置 SDK 对象随即释放，最后才订阅。回调使用原子关闭位和活跃计数获取短期访问权，日志额度通过原子 CAS 限制；回调不获取本插件互斥锁，不等待，不保存任何借用对象指针。

按官方正文，服务器在调用 Stop 前已经取消所有事件订阅，Stop 时已不允许通过 IMTServerAPI 访问服务器。因此正常 Stop/Release 只关闭本地 gate、等待已放行回调退出并清理本地状态，**不调用 TradeUnsubscribe 或 LoggerOut**。Start 内订阅异常的回滚仍可调用退订。部分官方示例采用不同 Stop 写法，本探针遵循正文。

TradeUnsubscribe 文档没有明确承诺等待在途回调排空。原子 gate 不能防御宿主在实例销毁后仍投递回调；该实现依赖官方“停止投递 → Stop → 销毁”生命周期。重复启停、并发日志和卸载中的回调退出必须在测试服验证，不能凭静态审查宣称已无竞态。

## 测试服务器部署顺序

1. 在独立 Windows 测试环境完成 Release x64 构建，确认导出 `MTServerAbout` 与 `MTServerCreate`，并记录 DLL 的 SHA256。
2. 将**这个 DLL**放到目标 MT5 交易服务器安装目录的 `plugins` 或其子目录，而不是客户终端目录。Access Server 不支持插件。先保持测试配置禁用。
3. 在 Administrator 的插件配置列表选择 Add，填写独立配置名称，选择对应测试服务器和 `TradeCopierSdkProbe` 模块，确认两个参数的类型和值。
4. 首次用 `WatchLogin=0`、`MaxEvents=100` 启用，只检查 About/API 日志。模块已可见时，勾选 Enable 并保存即启动，无需为启用已识别模块而重启服务器。
5. 验证加载成功后，停用配置，设定一个受控测试账户 `WatchLogin`，保持小的 MaxEvents，再启用。由测试人员在该账户执行测试交易，观察日志；探针自身不发出测试交易。
6. 验证空/不匹配账号、限额、开平仓、部分成交、重复启停与卸载，核对日志中的实际 API 和 Build。Close By 当前暂缓验收。不要仅凭“DLL 已出现在列表”认定事件功能已验证。
7. 更新 DLL 前停用此模块的**全部配置实例**，确认已卸载后再替换。每个配置都会创建独立实例；只停用一个配置不保证整个 DLL 已卸载。

官方保证服务器启动时扫描 `plugins`/`reports` 及子目录，但当前核对的文档未明确新增 DLL 的即时刷新命令。若新模块不出现在列表，应按测试服务器的管理流程安排发现/重启，不承诺复制后必定即时可见。

## API 兼容边界

`MTServerAbout.version_api` 使用真实 `MTServerAPIVersion`。`MTServerCreate` 将 `apiversion < 6182` 拒绝为 `MT_RET_ERR_NOTSUPPORTED`，并保持输出实例为空；不能修改 SDK 常量绕过检查。这个门槛拒绝较旧运行时，并不证明所有较新运行时都兼容。

用户已选择按提供的 SDK 6182 开发。先前报告的服务端 Build 6140 与实际 runtime API 是不同字段；必须核对测试服务器支持的 API。版本不满足时，本探针不降号加载，应准备匹配的测试服务器或 SDK。

## 本地 SDK 证据

- [MTServerCreate](../vendor/mt5-sdk/Docs/html/mtservercreate.htm)：runtime API 参数。
- [服务器交互](../vendor/mt5-sdk/Docs/html/serverapi_interaction.htm)：扫描目录、API 支持检查、实例生命周期。
- [配置插件](../vendor/mt5-sdk/Docs/html/serverapi_configure_plugin.htm)：Add、Module、Enable 与参数。
- [Stop](../vendor/mt5-sdk/Docs/html/imtserverplugin_stop.htm)：调用前自动退订。
- [线程约束](../vendor/mt5-sdk/Docs/html/serverapi_best_practice.htm)：并发回调、内部锁与 SDK 对象限制。
- [TradeUnsubscribe](../vendor/mt5-sdk/Docs/html/imtserverapi_tradeunsubscribe.htm)：退订返回值，未声明回调排空保证。
- [PluginCurrent](../vendor/mt5-sdk/Docs/html/imtserverapi_plugincurrent.htm)：读取本实例配置。
- [执行事件](../vendor/mt5-sdk/Docs/html/imttradesink_ontraderequestprocess.htm)、[网关执行](../vendor/mt5-sdk/Docs/html/imttradesink_ontradeexecution.htm)、[请求 Close By](../vendor/mt5-sdk/Docs/html/imttradesink_ontraderequestprocesscloseby.htm)：前三个观察入口的 CHM 说明。
- [MT5APIServer.h](../vendor/mt5-sdk/Include/MT5APIServer.h)：`OnTradeExecutionCloseBy` 的真实接口及 Close By 事件注释；当前英文 CHM 未提供这个入口的独立页面。

这些 SDK 文件保留在忽略目录中，不随项目提交。
