# MT4 SDK 接收与接入依据（2026-09-29）

已检查用户提供的 `MetaTraderServerAPI.zip`、`ManagerAPI.zip` 及版本截图。本文记录 SDK 接收时的接口依据；后续已新增 MT4 0.1.0 同服跟单源码，可移植部分通过编译和模拟测试，Windows / Demo 验收仍待完成。当前实现与限制见 [MT4 README](../README.md)。

## 版本依据

| 对象 | 已确认信息 | 证据与边界 |
| --- | --- | --- |
| 截图中的程序 | MetaTrader 4 Administrator，4.00 build 1470，13 Mar 2026 | 这是 Administrator 客户端版本，不能替代实际 Trade Server 版本或位数确认 |
| Server API | `ServerApiVersion = 5` | `include/MT4ServerAPI.h:10`；API 接口版本不能与产品 build 1470 直接比较 |
| Manager API 头文件 | `ManAPIProgramVersion = 400`、`ManAPIProgramBuild = 1470` | `MT4ManagerAPI.h:13-15`，与截图中的产品 build 一致 |
| Manager API 动态库 | `mtmanapi.dll` 为 x86，`mtmanapi64.dll` 为 x64；两者固定文件版本均为 4.0.0.1470 | 静态读取 PE 头及版本资源，未加载 DLL |
| 实际 MT4 Trade Server | 尚未从运行中的服务端确认 | 后续只读探针可调用 `CServerInterface::Version()`、`CommonGet()`，读取 `ConCommon.server_version/server_build` |

Server 头文件版权年份为 2024，没有声明针对服务器 build 1470 的兼容矩阵。版本宏相符或 Manager DLL 版本匹配，都不等于 Server 插件已经通过加载和交易验证。

## 本机 SDK 目录

```text
vendor/
├── mt4-sdk/
│   ├── include/MT4ServerAPI.h
│   ├── include/DebugInfo.exe
│   ├── Examples/
│   └── extraction-manifest.json
└── mt4-manager-sdk/
    ├── MT4ManagerAPI.h
    ├── mtmanapi.dll
    ├── mtmanapi64.dll
    ├── Examples/
    └── extraction-manifest.json
```

Server 包剥离外层 `MetaTraderServerAPI/` 后解压，共 216 个文件；Manager 包共 131 个文件。读取 ZIP 时验证 CRC32，写入后逐文件验证 SHA256，之后再次全量核对，两包均完整。原始压缩包保留在仓库根目录，压缩包及解压目录均被 Git 忽略。没有运行 SDK 中的 EXE、加载 DLL 或连接服务器。

| 文件 | SHA256 |
| --- | --- |
| `MetaTraderServerAPI.zip` | `a65903acfa92113c406a9fe2355d608d77fe1378f0e33186a0a2db155caee1fd` |
| `ManagerAPI.zip` | `98f4b7edc563111bcf677a1ad0bf5c003b3f652baa23d67fbfbdee60249b5bf7` |
| `mt4-sdk/include/MT4ServerAPI.h` | `9790a923d95d8b63479981c60282929c2aab68991eb2aabe7d26a3663b87c7a6` |
| `mt4-manager-sdk/MT4ManagerAPI.h` | `8d5ecc24c4ff396f5d50f614e680619f238a8fe9efc0d7f47d46002c1a7fdd4a` |

完整本机清单见 [Server SDK 清单](../../../../vendor/mt4-sdk/extraction-manifest.json)与 [Manager SDK 清单](../../../../vendor/mt4-manager-sdk/extraction-manifest.json)。这些链接依赖本机 SDK，不随源码交付。

## Server API 接入点

以下行号均指本次收到的 `mt4-sdk/include/MT4ServerAPI.h`。

| 用途 | 已发现的接口 | 位置 |
| --- | --- | --- |
| 接口与服务器版本 | `Version()`、`CommonGet()`、`ConCommon.server_version/server_build` | 1095、1104、104–105 |
| 成交与订单变化 | `MtSrvTradesAdd`、`MtSrvTradesAddExt`、`MtSrvTradesAddRequest`、`MtSrvTradesUpdate`、`MtSrvTradesCloseBy` | 1364–1369 |
| 订单查询与核对 | `OrdersGet`、`OrdersGetOpen`、`OrdersGetClosed` | 1176–1179 |
| 交易请求入口 | `RequestsAdd` | 1165 |
| 高层订单操作 | `OrdersOpen`、`OrdersClose`、`OrdersCloseBy` | 1204–1207 |
| 净值及保证金 | `TradesMarginInfo`、`TradesMarginGet` | 1182、1256 |
| 品种、账户及报价 | `SymbolsGet`、`ClientsUserInfo`、`HistoryPricesGroup` | 1146、1156、1187–1188 |

0.2.0 使用 `RequestsAdd` 提交 `TT_ORDER_MK_OPEN` / `TT_ORDER_MK_CLOSE`，仅对本实例获得的受理请求 ID 调用 `RequestsLock` 和指定 Dealer 的 `RequestsConfirm`，查询实际订单并核对请求证据；没有使用 `OrdersAdd` / `OrdersUpdate` 修改数据库，也没有用高层 `OrdersOpen` / `OrdersClose` 替代正常请求。保证金、风控、报价、路由、返回值与成交回调语义仍须 Demo 验收；回调本身不构成独立成交证明。

插件生命周期及导出可参考 `Examples/ServerPlugin/ServerPlugin.cpp` 和 `.def`；启动时核对 Server API 版本。查询返回的订单数组需按头文件约定使用 `HEAP_FREE`（1172 行），不能直接用 C++ `delete[]`。

## 构建与示例的限制

- Server 示例 `Examples/ServerPlugin/ServerPlugin.vcproj` 使用 Win32、x86 链接目标及 `_USE_32BIT_TIME_T`。MT4 应建立独立的 Windows/MSVC 构建，以所附 x86 示例为初始基线，并确认宿主位数；不能沿用 MT5 的 x64 编译配置，也不能根据 Manager API 有 x64 DLL 就推断 Server 插件支持 x64。
- 头文件含 `__stdcall` 虚函数、32 位时间及局部 `pack(1)` 结构体。保留原始 SDK，不修改类型、对齐或版本宏来绕过编译错误。
- 部分 Server 示例已过时：`Examples/TradeCollector/TradeCollector.cpp:265` 使用 `RequestInfo.internal_id`，`CoverageBase.cpp:159` 使用 `TradeRecord.internal_id`，当前头文件均没有对应字段。因此不能把整个示例集视为与当前头文件一致、可以直接编译的现成实现。
- 示例中有工作线程及回调重入保护，但没有完整的逐接口线程安全说明。不能据此推断所有 SDK 方法都可在任意线程调用；回调数据的复制、队列、接口调用时机和停止流程需分别验证。
- 两包没有完整的 API 专用 CHM/PDF/HTML 文档。Server 包中的 HTML 是 WebRegistration 示例页面。头文件与示例未明确的行为，应通过补充接口文档及 Demo 验证确认。

## Manager API 的作用

Manager 包包含 C++ 头文件、两种位数的 DLL 及示例；根目录和 `Examples/` 内重复的头文件、DLL 内容一致。头文件中的工厂通过 `LoadLibrary` / `GetProcAddress` 加载库，因此包中没有 `.lib` 不代表缺少必需文件。未提供 Java 绑定。

Manager API 可用于独立管理服务或另一种外部跟单架构。本项目已选择 Server API 插件执行跟单；Manager API 不是加载 Server 插件或实现 Web + Java 管理界面的必需依赖。Java 管理端可以通过单独的服务协议与原生 MT4 / MT5 组件通信，需要 Manager API 特有功能时再增加适配。

Manager 示例中的 `TT_BR_ORDER_OPEN` / `TT_BR_ORDER_CLOSE` 和写死的价格只是示范；不能直接作为生产跟单的成交、风控和报价处理实现。

## 接入路线与当前进度

1. 已实现默认停用的加载入口、实际服务器身份检查及回调复制队列；待 Windows 编译与服务器加载验证。
2. 已实现 MT4 独立订单模型、手数转换和票号链验证；模拟测试覆盖部分平仓、人工减仓、请求核对及重启，待真实回调顺序验证。
3. 已提取 `shared/`，MT4 / MT5 分别编译配置与状态机，使用独立账本身份、管道及二进制。该共享目录目前不包含跨服务器交易协议。
4. 后续通过独立路由组件连接两端，实现 MT5→MT4 和 MT4→MT5，包含平台及服务器身份、来源追踪、去重、断线恢复与防循环；每条订阅仍是明确的单向关系。

已完成同服适配、共享业务核心、管理器及模拟测试；跨服务器路由仍待实现。当前 macOS 环境的静态检查不能代替 Windows MSVC、DLL 加载或 Demo 交易验收。原 MT5 0.6.2 SDK 实现和构建入口保持独立，聚合构建默认包含两平台的可移植目标。

## 2026-10-14 Dealer 执行补充

0.2.0 依据 Server SDK 的 AdvancedDealerHelper 锁定/确认示例及 TradeCollector 市场请求确认示例接入指定 Dealer。`DealerLogin=0` 是未配置占位，Armed 必须填写真实 MT4 管理账号。RequestsLock 为 BOOL 风格；ManagersGet/RequestsFree 的目标服务器返回约定和只读、保证金拒单仍需 Demo 验证。未将 Manager API TradeTransaction 的 TT_BR 请求直接套用到 Server RequestsAdd；未更改账户只读标志或数据库。详细边界见 [0.2.0 Dealer 说明](dealer-execution-0.2.0.md)。
