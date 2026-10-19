# 已提供 SDK 的核对结果

> **0.4.0 更新（2026-09-16）**：本机运行状态、暂停/恢复新仓、空闲时配置应用及只读请求核对已加入源码；升级使用原账本，见 [管理界面说明](management-ui.md)。下文 0.3.0 的 UI/迁移限制和截图结论属于对应版本，不代表 0.4.0 已在 Demo 验收。新版账本不支持直接降级到旧 DLL。

用户已确认按实际 SDK 版本开发。本文件在原接入依据上补充 2026-09-15 的执行接口核对，不把 SDK 安装器、Server Build 与 Server API 版本混为一谈。

## 文件与版本

| 项目 | 实际结果 |
| --- | --- |
| 用户提供文件 | 仓库根目录 `MetaTrader5SDK.exe`（原接入时的位置） |
| 安装器 SHA256 | `1f55a75781bb65b9515e879df5bec6c8dbe9bc927fa61826cfa289df2ef3ade7` |
| 安装器资源版本 | `5.0.0.6191` |
| 安装包格式 | Inno Setup 6.7.0 |
| Server API 编译版本 | `MTServerAPIVersion = 6182` |
| Server API 头文件日期 | `5 Sep 2026` |
| 官方 ServerPlugin 示例工具集 | Visual Studio v143、Windows SDK 10.0、Release `/MT` |
| 当前 DLL 构建目标 | Windows x64，Server API 6182 |
| 用户测试环境 | 用户拥有独立 Windows 测试服务器，部署与加载由用户按教程执行 |

SDK 已完整静态提取到本机仓库根目录的 `vendor/mt5-sdk`（相对当前插件目录为 `../../../vendor/mt5-sdk`），没有执行安装器。当前提取清单记录全部 1,003 个文件与安装包内 SHA256 一致，`verified_files` 为 1,003 项、`withheld_files` 为 0 项、`extraction_complete` 为 true，包含此前需要指令变换的二进制。配套二进制不参与本项目 Server API DLL 的头文件构建。英文 CHM 已展开到 `Docs/html`。本机实际结果以 [提取清单](../../../../vendor/mt5-sdk/extraction-manifest.json) 为准。

这些哈希用于核对提取完整性，不替代发行方数字签名验证。安装器、提取后的 SDK 与本机运行状态均被 Git 忽略，也不加入本项目的源码 ZIP。

版本证据在 [MT5APIServer.h](../../../../vendor/mt5-sdk/Include/MT5APIServer.h)。`MTServerCreate` 收到的 `apiversion` 是宿主支持的 API 版本。官方说明插件编译所用 API 必须被服务器支持，否则拒载并记日志；没有找到一条可以代替实际加载验证的通用兼容保证。工程不改写 SDK 宏来伪装兼容旧服务端。

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

执行器工作线程现已接入净值查询，按 `IMTAccount::CurrencyDigits()` 和组币种精度转换为共同十进制单位。净值值、共同精度和两次查询时间随请求意图落盘。当前采样及提交新鲜度上限为 5 秒；无效数据或超时停止本次执行，不回退缓存或改用重启时净值。

## 0.3.0 执行接口核对

独立执行器使用下列实际接口：

1. `TradeRequestCreate` / `TradeRequest` + `TA_MARKET`，目标平仓必须填精确 `Position` 和反向买卖类型。当前仅支持 Market Execution，选 IOC 或 FOK，不使用数据库修改接口或跳过保证金检查标记。
2. `IMTRequest::ApiDataSet` 文档明确：标记按 request → order → deal 传递。执行器使用应用 ID 下的字段 1–3 存储程序标记、持久化意图 ID、服务器 ID；不依赖备注、`SourceLogin` 或请求入队回执作为成交凭据。
3. `HistoryGet(ticket)` 读取已结束订单；`DealGet` 获取并归并实际成交；订单实际成交量、成交总量及精确持仓剩余量必须相符。分笔开仓只生成一次跟单额度。
4. `PositionGetByTicket` 读取精确持仓；缺失的目标 ticket 还需由普通开平仓历史证实已全平，避免把展期换票误认成人工全平。展期、拆分、迁移及 Close By 均不在本轮支持范围。
5. 6182 `VolumeExt` / `VolumeInitialExt` / `VolumeCurrentExt` 的单位为 1/100000000 手，与本项目 Units 一一对应。`100000000` 明确为 1 手。
6. `UserGet`、`GroupGet` 核对账户所属交易服务器、Hedging/USD；`SymbolGet(name, group, symbol)` 核对组内实际品种配置与合约一致性。
7. 回调只复制数据到有界队列；单线程执行查询、持久化和请求核对。Stop 关闭每次 API 调用的准入，再等待已准入调用/工作线程结束，不从 Stop 发起日志或退订调用。

新 DLL 的 Windows 编译、三组测试，以及 Demo 的 Armed=0 加载现已由用户截图确认；加载日志为 server-id=1、Build/API/compiled-api=6182。50001 → 50002 的 1:1 跟单已收到多笔目标开平仓 CONFIRMED 与源/目标历史；按当前实现，这些确认日志在带 API 标记的订单/成交及目标余量核对后输出。挂单成交后跟单、SL/TP 同步平仓、目标人工减仓后的比例平仓按回传证据通过；人工全平已出现 MANUAL_CLOSED，图示区间未见补仓。2026-09-16 的正常停用/启用测试已确认重新 READY、图示无重复开仓、恢复后按原目标 ticket 继续平仓。

同日 0.5 倍批次确认目标 19225721 开 0.02 手、两次各平 0.01 手（该批次 intent 1～3）；固定手数批次确认目标 19226103 开/平 0.02 手及目标 19226115 开 0.02 手、两次各平 0.01 手（该批次 intent 1～5）。两批均符合指定步骤，源明细未独立展示。随后净值比例批次确认目标 19226311 开 0.05 手、依次平 0.02/0.03 手（该批次 intent 1～3）。后补账本检查器输出确认源持仓 19226310、冻结净值 7895.90/10268.40 USD，按指定源开仓量 0.04 手及系数 1.00 复算向下取整为 0.05 手；三笔 Confirmed、映射 Closed 且双方剩余量为 0，本轮净值比例场景通过。

多关系测试已确认 50001 同时跟到 50002/50003：50002 的目标 19227449 开 0.04 手、两次各平 0.02；50003 的目标 19227450 开 0.02 手、两次各平 0.01。该批次六笔均 CONFIRMED，两个规则各自的关闭请求保持正确目标 ticket，符合独立手数与单源多目标步骤。后续 50001/50003 同时跟到 50002 批次也有六笔 CONFIRMED：A=19227926 开 0.04、两次各平 0.02；B=19227945 开 0.02，在 A 全平后两次各平 0.01。同账户两个规则未混用关闭票号，B 的后续成交确认支持其未被 A 的关闭请求误平。

启用前已存在源仓不补跟也已实测：50001 的 19228200 在 11:47:46.578 开 sell 0.02，插件于 11:48:00.609 READY，源在 11:49:01.563 全平。目标 50002 空仓截图及截至 11:49:14.298 无新增跟单请求的日志与源成交明细共同支持本轮通过。后补源历史还确认之前多轮的源初始数量；双源同目标的 A 源实际为卖出，B 源方向未直接展示，保留票号隔离结论，不将原“双买入”步骤视为实际方向证据。

未成交挂单撤销场景也已核对：50001 的 Sell Limit 19228495 于 11:57:53.165 创建、11:58:20.471 撤销，状态 CANCELED，初始量与剩余量均为 0.02 手，未产生成交。结合用户明确确认 50002 没有持仓或订单，本轮通过。目标状态依据用户确认，未回传该区间执行器专属日志；源日志的 request confirmed 不计为目标成交确认。

仍待完成：逐笔成交归档、人工全平后源关闭、其余数量取整边界、固定手数的源分笔成交归并、更复杂关系的隔离、异常恢复等验收，以及人工恢复流程、配置迁移和界面实时应用通道。现阶段不自动重发未知/部分成交，不回放停机期间交易；详见 [执行器验收说明](executor-demo-validation.md)。旧探针日志不能替代新执行器的实测。

## 本地 SDK 文档索引

- [MTServerCreate](../../../../vendor/mt5-sdk/Docs/html/mtservercreate.htm)、[生命周期与版本](../../../../vendor/mt5-sdk/Docs/html/serverapi_interaction.htm)、[Stop](../../../../vendor/mt5-sdk/Docs/html/imtserverplugin_stop.htm)
- [请求处理](../../../../vendor/mt5-sdk/Docs/html/imttradesink_ontraderequestprocess.htm)、[网关执行](../../../../vendor/mt5-sdk/Docs/html/imttradesink_ontradeexecution.htm)、[Close By](../../../../vendor/mt5-sdk/Docs/html/imttradesink_ontraderequestprocesscloseby.htm)
- [TradeRequest](../../../../vendor/mt5-sdk/Docs/html/imtserverapi_traderequest.htm)、[DealPerform 事件](../../../../vendor/mt5-sdk/Docs/html/imtdealsink_ondealperform.htm)
- [请求 API 标记传递](../../../../vendor/mt5-sdk/Docs/html/imtrequest_apidataset.htm)、[订单实际未成交量精度](../../../../vendor/mt5-sdk/Docs/html/imtorder_volumecurrentext.htm)、[持仓 ticket 变化](../../../../vendor/mt5-sdk/Docs/html/imtposition_position.htm)
- [UserAccountGet](../../../../vendor/mt5-sdk/Docs/html/imtserverapi_useraccountget.htm)、[线程与账户查询限制](../../../../vendor/mt5-sdk/Docs/html/serverapi_best_practice.htm)

不含 SDK 的源码包中这些本地链接不可用；可改用安装后 CHM 的同名章节。
