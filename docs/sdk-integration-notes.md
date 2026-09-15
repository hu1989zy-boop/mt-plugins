# 已提供 SDK 的核对结果

用户已确认按实际 SDK 版本开发。本文件记录 2026-09-11 的接入依据，不把 SDK 安装器、Server Build 与 Server API 版本混为一谈。

## 文件与版本

| 项目 | 实际结果 |
| --- | --- |
| 用户提供文件 | 项目根目录 `MetaTrader5SDK.exe` |
| 安装器 SHA256 | `1f55a75781bb65b9515e879df5bec6c8dbe9bc927fa61826cfa289df2ef3ade7` |
| 安装器资源版本 | `5.0.0.6191` |
| 安装包格式 | Inno Setup 6.7.0 |
| Server API 编译版本 | `MTServerAPIVersion = 6182` |
| Server API 头文件日期 | `5 Sep 2026` |
| 官方 ServerPlugin 示例工具集 | Visual Studio v143、Windows SDK 10.0、Release `/MT` |
| 当前 DLL 构建目标 | Windows x64，Server API 6182 |
| 用户测试环境 | 用户拥有独立 Windows 测试服务器，部署与加载由用户按教程执行 |

SDK 已静态提取到 `vendor/mt5-sdk`，没有执行安装器。1003 个原始文件均与安装包内保存的 SHA256 一致；其中包含 82 个头文件、9 个 Server API 示例项目及 3 个 CHM。英文 CHM 另外展开为 6924 个文档与资源文件。逐文件记录见 [提取清单](../vendor/mt5-sdk/extraction-manifest.json)。

这些哈希用于核对提取完整性，不替代发行方数字签名验证。安装器、提取后的 SDK 与本机运行状态均被 Git 忽略，也不加入本项目的源码 ZIP。

版本证据在 [MT5APIServer.h](../vendor/mt5-sdk/Include/MT5APIServer.h)。`MTServerCreate` 收到的 `apiversion` 是宿主支持的 API 版本。官方说明插件编译所用 API 必须被服务器支持，否则拒载并记日志；没有找到一条可以代替实际加载验证的通用兼容保证。工程不改写 SDK 宏来伪装兼容旧服务端。

## 已核对的接入点

| 接入点 | 决定与依据 |
| --- | --- |
| DLL 入口 | 使用真实 `MTServerAbout(MTPluginInfo&)` 和 `MTServerCreate(uint32_t, IMTServerPlugin**)`；按 `IMTServerPlugin` 实现 `Release/Start/Stop` |
| 实际成交观察 | 检查 `IMTTradeSink` 的请求处理、网关执行及 Close By 回调；空指针合法，不能无条件解引用 |
| 成交去重 | 正式业务按成交标识及各自源持仓统一去重；探针只保留回调来源，不把每条日志当作一笔新交易 |
| 交易提交 | `TradeRequest` 成功只表示入队，SDK 分配 Request ID，必须等待成交/拒绝结果；当前探针完全不提交请求 |
| 生命周期 | `Start` 完成后开始接收事件；`Stop` 调用前宿主自动退订，`Stop` 内不再调用 `IMTServerAPI` |
| 线程模型 | 事件可能并发，不保证顺序；不把回调传入的数据对象指针保存到回调外，不在回调内做跨账户查询、等待或交易状态写入 |
| 插件配置 | `PluginCurrent` 可读取本插件配置；独立 EXE 不能直接使用此对象作为远程入口 |

`OnDealPerform` 的方法文档限定其用于 `DealPerform*` 执行的交易；不能把它当作所有客户成交的唯一监听入口。`OnDealAdd` 只说明新增数据库记录，也不能单独证明新仓。官方样例中的修改订单逻辑不作为本产品的交易执行实现。

`OnTradeRequestProcess` 文档明确 `order` 是执行前的当前状态；没有据此推断 `position` 的前后语义。正式数量账本需要结合持仓、成交历史与独立测试事件顺序核对。Close By 专用回调携带两笔成交，处理时需分别归属源持仓。

## 已确认的净值采样时点

这份 SDK 没有查到能直接取得“源成交时刻，源与所有跟单账户同一时点净值”的接口保证：

- `UserAccountGet(login, account)` 查询当前状态，没有历史时刻参数或多账户原子快照参数。
- `IMTAccount::Equity()` 返回净值，但对象没有通用状态更新时间或快照版本字段。`SOTime` 是 Margin Call / Stop Out 时间，不能借用作净值时间戳。
- 普通 `OnTradeRequestProcess` 不提供账户快照；`OnDealPerform` 的单账户执行后状态不能覆盖所有普通交易和全部跟单账户。
- `IMTAccountSink` 只有 Margin Call / Stop Out 事件，文档标记为 Manager API，不是 Server API 的完整净值更新流。

文档存在一处限制表述差异：`UserAccountGet` 方法页禁止在交易回调/Hook 调用；最佳实践列出“仅查询原始请求所属账户”的例外。这个例外不能扩展到任意跟单账户，也不能据此保证双方在同一成交时刻的净值。

用户已明确接受以下定义：**收到源开仓成交后，由独立工作线程在提交跟单前查询双方当前净值并冻结，记录源成交时间及双方查询时间。** 两次查询不宣称是原子快照，不用源成交时间替代各自查询时间。之后净值变化不重算该源交易既有跟单额度；新源交易重新取样。配置中的 `equity_snapshot` 为 `before_follower_submission`。

具体账户净值获取失败、无效或超出执行允许时效时，停止该次净值模式开仓并记录原因，不能回退到任意历史缓存或恢复时净值。真实工作线程采样尚未接通；界面公式只表达已确认的业务规则。

## 尚未实现的正式执行功能

只读探针用于验证接入条件，不是跟单业务的替代交付。以下内容仍未完成：

1. 真实 `TradeRequest` 开平仓、精确目标 Hedging 仓位关联、请求确认与部分成交账本。
2. 每条关系的启用快照屏障、分批开仓归并与可靠成交去重。
3. 状态落盘、请求结果未知时核对、重启恢复及单执行者约束。
4. 跟单提交前的净值采样、账户与品种实际限制、交易路径和桥接条件验证。
5. 独立 Windows 界面与执行 DLL 的配置应用、确认及状态管理通道。

业务核心和配置解析已在 macOS 测试；用户 VS2026 截图也确认 EXE/DLL 构建及两组测试通过。界面试用与测试服加载仍待验证，详见 [Windows 构建记录](windows-build-validation.md)。不能将单元测试或构建成功描述为 Live 验证通过。

## 本地 SDK 文档索引

- [MTServerCreate](../vendor/mt5-sdk/Docs/html/mtservercreate.htm)、[生命周期与版本](../vendor/mt5-sdk/Docs/html/serverapi_interaction.htm)、[Stop](../vendor/mt5-sdk/Docs/html/imtserverplugin_stop.htm)
- [请求处理](../vendor/mt5-sdk/Docs/html/imttradesink_ontraderequestprocess.htm)、[网关执行](../vendor/mt5-sdk/Docs/html/imttradesink_ontradeexecution.htm)、[Close By](../vendor/mt5-sdk/Docs/html/imttradesink_ontraderequestprocesscloseby.htm)
- [TradeRequest](../vendor/mt5-sdk/Docs/html/imtserverapi_traderequest.htm)、[DealPerform 事件](../vendor/mt5-sdk/Docs/html/imtdealsink_ondealperform.htm)
- [UserAccountGet](../vendor/mt5-sdk/Docs/html/imtserverapi_useraccountget.htm)、[线程与账户查询限制](../vendor/mt5-sdk/Docs/html/serverapi_best_practice.htm)

不含 SDK 的源码包中这些本地链接不可用；可改用安装后 CHM 的同名章节。
