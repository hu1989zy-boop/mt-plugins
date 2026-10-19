# 接口与能力证据 — 2026-10-14

> **当前结论入口：[2026-10-15 Manager-only Evidence](04-manager-only-evidence-2026-10-15.md)。** 已按 MT5 Manager → MT4 Manager 完成本轮静态复核，运行仍 NOT RUN。下文“当前仅修订计划”等为上轮适用性说明；本次阶段、数值契约和结论以04为准。原始事实与历史说明保留。

> **2026-10-15 当前适用性说明：历史静态证据保留，运行验证仍未完成。** 依据 [V1 MONEY MARKOUT REMOVED](../architecture-design/13-architecture-review-decisions.md#v1-money-markout-removed)，Money / Native Profit / FX / Profit Currency 及其专用 Account / Group Context 要求已 **SUPERSEDED FOR V1**。以下旧事实不删除，但涉及 Money 的验证门槛和业务结论不再适用。当前仅修订计划，按 [Manager 优先验证计划](02-minimal-verification-protocol.md) 执行后续评审；[影响分析](../architecture-design/14-money-removal-impact-analysis.md)解释保留和失效边界。本文不是运行 PASS，也不批准 Architecture Revision。

本文记录技术事实和缺口，不改写业务规则。规则以13 Decision Register为准；12旧证据文档保留原状，本文是独立新增证据。

## 证据级别与本轮实际动作

- DOCUMENTED：一手官方接口或产品文档承诺，限对应API / 版本 / 数据路径。
- STATIC：本地SDK声明、字段、示例、哈希和二进制格式的只读检查。
- RUNTIME VERIFIED：必须有目标环境、输入、返回码、数据及可重复结果；**本轮无此级别证据**。
- UNRESOLVED：接口契约或实际环境证据不足。不能当作“返回空”“没有数据”“已经不支持”。

本轮读取本机SDK与官方文档，检查了声明和PE架构；未编译或加载SDK、运行样例、连接Server、发起交易、修改配置。没有用合成Tick测试冒充Broker能力。取证材料仅在MarkoutAnalyzer目录内。商业SDK与真实账户数据未上传。

本地版本：MT4 Manager ProgramVersion 400 / Build 1470；MT5 Manager API 6182。这是头文件版本，**不是已经连接的Server Build**。主机为macOS 15.7.4 / arm64，命令检查未找到wine / wine64 / pwsh / dotnet。Windows PE包括x86 / x64 / ARM64，ARM64 PE也不是macOS动态库。本轮不安装兼容层或选择技术栈。[静态检查](../../temp/technical-evidence/local-static-inspection-2026-10-14.json)。

## E01 — MT4 Terminal历史报价

| 路径 | 一手证据及可确认事实 | 对本项目的结论 |
|---|---|---|
| SymbolInfoTick | [MQL4文档](https://docs.mql4.com/marketinformation/symbolinfotick)定义为当前价格和最后更新时间 | 当前双边Quote能力不证明历史双边能力 |
| Timeseries / CopyTickVolume | [MQL4时间序列](https://docs.mql4.com/series)列的是Bar / OHLC及Bar Tick Volume | Tick Volume不是历史Tick序列 |
| HST / 标准Tester | [官方Tester说明](https://www.metatrader4.com/en/trading-platform/help/autotrading/tester/tester_parameters)说明Bar历史、Bid价格历史、模拟Ask与建模Tick | 已检查路径不满足真实历史Bid/Ask；不得使用Tester输出或Spread重建作为替代 |
| History Center Download | [官方说明](https://www.metatrader4.com/en/trading-platform/help/service/history_center)提示下载数据可能不同于Broker历史 | 下载成功不证明Same Server；不能用该路径补齐 |

结论限定为已检查的标准路径不合格。其他官方扩展如存在，需要新的具体接口与数据来源证据；不将“未找到”写成普遍不存在，也不安装第三方采集器。开始实时记录Tick只能积累未来数据，不能补证此前历史；本轮未采用。

## E02 — MT4 Manager历史报价

本地[MT4ManagerAPI.h](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt4-manager-sdk/MT4ManagerAPI.h:858)的TickRecord含ctm（__time32_t）、bid / ask（double）、datafeed、flags；[TickRequest](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt4-manager-sdk/MT4ManagerAPI.h:872)含完整symbol字段、from / to及flags；[TicksRequest声明](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt4-manager-sdk/MT4ManagerAPI.h:1710)返回记录数组及数量。

| 维度 | 已确认 | 仍需证据 |
|---|---|---|
| Bid / Ask | 两个double字段 | 真实历史双边值是否实际返回；是否包含源报价或处理后报价 |
| Timestamp | __time32_t秒级字段，不能伪造毫秒 | 时区、历史Offset、端点包含关系、实际时间含义 |
| Ordering | 公开结构无Sequence / Tick ID | 同秒不同有效报价的真实先后；数组顺序契约，不能只验证重复调用稳定 |
| Coverage / Depth | 请求具备from / to | 实服保留范围、权限、部分返回/截断/空数组的区分、可完整检索边界 |
| Quote Flags | RAW=1、NORMAL=2、ALL=3 | 名称不能证明NORMAL等于Terminal报价；RAW与NORMAL是否可混用没有获证 |
| Retention配置线索 | ConCommon.keepticks及ConSymbol.logging字段存在 | keepticks单位、历史配置、真实覆盖不能由字段名称确定 |
| Server Identity | 接口可连接并取得部分Server配置 | 网络端点/名称与稳定真实Trading Server身份的绑定 |

本地包本轮未发现TicksRequest调用示例或专门方法文档；这一搜索范围仅限已存在的MT4 Manager SDK目录。不能以普通返回计数证明没有遗漏，也不能通过自行给数组下标赋Sequence来填补顺序证据。

同Server仍可能有报价差异：SDK ManagerAPISample / PageDealer.cpp示例按目标Group的spread_diff调整当前Bid/Ask。这只是当前样例行为，**不证明历史RAW / NORMAL语义**，也不授权Markout Analyzer自行调整历史Bid/Ask。[样例](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt4-manager-sdk/Examples/ManagerAPISample/PageDealer.cpp:254)。

当前定位：候选接口已确认；正式Provider资格未证明。真实历史深度不能给出虚构天数。若取得合格Terminal路径，同条件比较仍为必需；当前不能拿模拟Terminal数据充当对照。

Retention线索见[keepticks](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt4-manager-sdk/MT4ManagerAPI.h:102)、[logging](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt4-manager-sdk/MT4ManagerAPI.h:538)。TickRequest的symbol[12]还要求验证编码与容量；若无法无损传递真实完整Symbol，应明确能力不满足，不截断或去后缀。

## E03 — MT5历史Quote与完整性

> **2026-10-15 来源评审范围更新：** MT5 Manager 现已作为正式技术候选进入 Manager-only 可行性评审；下文“既定 Terminal”是历史阶段表述，不能再解释为 Terminal 必选。接口事实继续保留，候选许可不等于实际路径获证或启用。Manager 秒级查询端点与 Tick 毫秒时间需分别验证；不得舍弃有效 TimeMsc。

Terminal [CopyTicksRange](https://www.mql5.com/en/docs/series/copyticksrange)有历史MqlTick、毫秒区间和旧到新的排序契约，取数区间包含两个端点。超时或缓冲不足可能仍返回部分记录，故正数返回不等于Coverage完整。Quote取数端点契约不改变Candidate的[From,To)规则。

[CopyTicks](https://www.mql5.com/en/docs/series/copyticks)说明Provider为未变化字段提供当时最后已知值，并可同步Tick数据库。接口提供的完整报价状态不同于应用修复缺失数据；应用仍对返回Tick执行已确认Validity。终端可能继续后台同步，不意味着Markout Analyzer可以自动重开或修补已结束Version。

[MqlTick](https://www.mql5.com/en/docs/constants/structures/mqltick)包含time_msc、Bid / Ask / flags，无独立Sequence。文档的旧→新契约是有效证据来源，不能因为缺少Sequence就全盘否定该接口；但它在选定版本/路径下对同毫秒不同报价如何定义先后及分页边界仍需验证。不得以数组稳定本身替代契约。[官方Python样例](https://www.mql5.com/en/docs/python_metatrader5/mt5copyticksrange_py)存在同毫秒不同Bid，毫秒不是唯一Tick ID。[官方AlgoBook](https://www.mql5.com/en/book/applications/timeseries/timeseries_ticks_mqltick)亦讨论同毫秒续取遗漏风险；不得简单用最后时间+1ms跳过边界。

本地MT5 Manager也声明TickHistoryRequest / TickHistoryRequestRaw，记录含Bid、Ask、datetime_msc及flags。此发现用于技术交叉核查，**不自动批准MT5 Manager替代既定Terminal Quote Provider**。

- [TickHistoryRequest](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtmanagerapi_tickhistoryrequest.htm)：返回由历史Server按Symbol Filter接受的数据；要求相应Symbol pumping，禁止从IMT*Sink事件处理器调用。
- [TickHistoryRequestRaw](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtmanagerapi_tickhistoryrequestraw.htm)：涉及原始与处理后流；原始存储未开启时只返回已接受流；from / to注明Server时区秒值。调用Raw名称不保证实服保存了Raw。
- [MTTickShort](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/mttickshort.htm)：数据源可填写时间，Server亦有自动填充规则。这是SDK接收/构造语义，不能授权本应用为缺失Event TimeMsc补.000；Q12保持。

所有接口均未在目标Server执行。真实Symbol须排除本地Custom / 导入数据；完整Symbol名称不能截断、去后缀或替换。[Symbol属性](https://www.mql5.com/en/docs/constants/environment_state/marketinfoconstants)。

## E04 — 时间语义与Server Identity

[MT5 Python connector](https://www.mql5.com/en/docs/python_metatrader5/mt5copyticksrange_py)说明其时间按UTC处理；该说明不能不加验证地外推到所有MQL / Manager数据路径。本地Manager文档[TimeServerRequest](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtmanagerapi_timeserverrequest.htm)说明平台配置、数据库及日志默认使用平台交易时间，除非另有说明；MTTickShort及Raw查询另注明交易Server时区。需要按实际路径建立映射，不能因都是epoch数值就直接等同。

| 接口/来源 | 已确认的用途 | 不能证明什么 |
|---|---|---|
| Manager TimeServerRequest | 直接请求当前Server毫秒时间 | 历史日期Offset或DST切换时间线 |
| Manager TimeServer | 本机当前时间按Server时区计算 | 独立Server时钟真值、历史映射 |
| Manager TimeGet / TimeZone / DaylightState | 当前配置；DaylightState文档说明与Server Windows时区规则有关 | 历史配置未变、过去规则必然与当前相同 |
| Terminal TimeCurrent | 最后已知报价对应Server时间 | 无报价期间已推进到真实当前时刻 |
| Terminal TimeTradeServer / TimeGMT | 受本机时钟或本地时间规则影响 | 目标Server历史Offset的权威来源 |

来源：[TimeServer](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtmanagerapi_timeserver.htm)、[TimeZone](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtcontime_timezone.htm)、[DaylightState](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtcontime_daylightstate.htm)、[TimeCurrent](https://www.mql5.com/en/docs/dateandtime/timecurrent)、[TimeTradeServer](https://www.mql5.com/en/docs/dateandtime/timetradeserver)、[TimeGMT](https://www.mql5.com/en/docs/dateandtime/timegmt)。

建议证据绑定Platform / Provider / Server / API版本 / 历史期间；保存原始时间与精度、映射规则与依据。DST重叠或缺失时段若无法唯一确定，保留TIME_MAPPING未解决语义，不猜Offset、不归NO_QUOTE。当前Offset一致的样例只证明该样例，不证明整个历史期间。

[MT4 AccountServer](https://docs.mql4.com/account/accountserver)、[MT5 ACCOUNT_SERVER](https://www.mql5.com/en/docs/constants/environment_state/accountinformation)及[Manager NetworkServer](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtmanagerapi_networkserver.htm)返回Server名称，未承诺全局唯一且永久稳定。本地[IMTConServer::Id](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtconserver_id.htm)为配置Server ID；并非已证明跨接入节点/迁移稳定的逻辑Trading Server身份。尤其IMTAdminAPI的NetServer方法不能当成普通Manager已获权限的方法。需在目标拓扑用权威标识或可验证组合绑定两条路径，禁止仅凭Profile Name、地址、Login、相同价格或用户确认。

## E05 — Native Profit与目标Account / Group

> **SUPERSEDED FOR V1 — V1 MONEY MARKOUT REMOVED。** 本节 Native Profit / Money / FX 接口事实仅作历史留档；不再验证或将其作为 Eligibility / Q01 / Manager-only 阻塞项。Q03、Q09 的旧适用结论已失效。仅下文独立的 Symbol / Point 必要适用性证据仍可能相关，不保留完整 Profit Context。

| 路径 | 已确认合同 | 当前限制 |
|---|---|---|
| MT5 Terminal OrderCalcProfit | 当前账户、当前市场环境，按输入方向/量/价格估算，返回账户币种 | 没有任意目标Login参数；Quote Terminal账户不能自动替代被分析Login；具体内部FX时点/参数依实际证据记Current / Historical / Unknown |
| MT5 Manager TradeProfit / Ext | 指定Group及Symbol；返回Group入金币种，profit_rate输出转换率；原生换汇使用该Group当前市场价格 | 没有直接Login参数；必须证明目标Login在Analysis Time的实际Group及所有相关上下文适用，非等价Group替代 |
| MT4 Terminal OrderProfit | 读取选中订单现有Profit | 不是给定历史成交价/未来报价的任意Profit计算接口 |
| MT4 Manager TradeCalcProfit | 本地头文件有TradeRecord*参数声明 | 无足够方法契约证明输入输出、目标Login/Group、FX、只修改本地对象或可能实际影响；本轮未调用 |

来源：[OrderCalcProfit](https://www.mql5.com/en/docs/trading/ordercalcprofit)、[TradeProfit](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtmanagerapi_tradeprofit.htm)、[TradeProfitExt](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtmanagerapi_tradeprofitext.htm)、[OrderProfit](https://docs.mql4.com/trading/orderprofit)、[MT4声明](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt4-manager-sdk/MT4ManagerAPI.h)。

> **SUPERSEDED — 下段为历史 Money 业务解释。** 当前 V1 不再执行 Q09 Money Eligibility 或验证 Native Profit 上下文。

MT5原生当前换汇符合Q03允许范围，不能描述为历史FX精确重建。指定实际Group不等于使用等价账户；仍需证明该方法涵盖目标Login所有影响Profit的实际上下文。不能只凭几个样例金额一致就通过Q13。若无法可靠证明，执行Q09，保留已得结果但不进入正式Summary。

> **SUPERSEDED FOR V1 — 下段 Profit 方法保留静态事实，不继续纳入验证计划。**

MT5 TradeProfit契约定位为按条件计算的辅助方法，没有创建或修改订单/成交的参数；这支持将其作为候选计算接口，但本轮没有运行证据证明实际DLL效果。MT4 TradeCalcProfit在[1722行](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt4-manager-sdk/MT4ManagerAPI.h:1722)；TradeRecord.conv_rates的历史转换字段也不能证明该方法实际按哪个时点转换。该方法的非持久化语义尚未明确，不能直接在真实记录上试调用。

> **保留的有限适用范围：** 下列 Symbol / Group 接口事实只用于确认实际 Point / Quote Interpretation / Diagnostics 所需字段的适用性。并非要求所有 Point 必然 Group-specific，更非要求继续收集 Profit Context；已证明公共的字段可以复用。保留字段以实际 V1 用途为限，不为 Future Money 收集整套规格。

MT5 [SymbolRequest](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtmanagerapi_symbolrequest.htm) / [SymbolGet](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtmanagerapi_symbolget.htm)有两类重载：不传Group返回默认交易设置，传Group返回该Group适用设置。不能用基础Symbol快照冒充已解析的目标Group上下文。建议由目标Login真实Group获取获证配置，记录Source / Acquisition Time；运行中变更的一致性策略仍需技术验证。快照仅证明取得的配置，不能宣称等于SDK所有内部参数。

实际Login→Group读取契约见[UserRequest](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtmanagerapi_userrequest.htm)和[UserGroup](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtmanagerapi_usergroup.htm)；后者依赖PUMP_MODE_USERS，缓存未就绪不得当作已证实。这里只读取所需字段，不能整体序列化User / Group对象，避免其密码、Secret及无关个人资料进入证据。

> **SUPERSEDED FOR V1 — 下述 Money 缺口已不再需要填补。** 用户已通过[AR-02](../architecture-design/13-architecture-review-decisions.md#ar-02)明确撤销USD-only；旧限制标记SUPERSEDED BY V1 MONEY MARKOUT REMOVAL。Account Currency仅可作原始Metadata，不进入公式/Eligibility，不恢复FX或Profit Currency逻辑。下述缺少USD Money样本只是历史事实，不是当前V1阻塞。

当前没有真实USD账户调用结果，没有获证的MT4 Native Money正式路径。不得自行实现FX、改用其他账户或硬编码规格填补这一缺口。

## E06 — MT4 Partial Close的真实Event Volume

[ConGroup.close_reopen](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt4-manager-sdk/MT4ManagerAPI.h:328)注释明确：非零模式可全平原Position并重开剩余Position。这至少证明不能假定所有Broker的部分平仓都以同一历史行形态出现。

TradeRecord有volume，但单个字段不能证明它在部分平仓链中就是某个真实OPEN / CLOSE Event的实际量。当前缺少目标Group模式及真实历史链证据，不能仅凭重开残余记录就认定其代表新的真实客户成交；须依据实际执行与历史链证据确定Event语义及成交量。不能用原始Order Lots、Ticket Comment模式或金额反推补齐，本证据也不预先排除所有重开记录的业务资格。

最小验证应读取已有已完成的普通、单次和多次Partial Close样本，关联真实执行证据、历史行、剩余记录与对应Group模式，证明每个Event的来源及Volume；不为测试发起真实平仓。历史记录不完整时，先判定Candidate Membership / Population是否可证明，不能一律降为已知Candidate的INVALID_VOLUME。

## E07 — Numeric Scaling / Sentinel / Precision

> **部分 SUPERSEDED FOR V1：** 下表 TradeProfit / Ext 行及 VolumeExt 行中的 Profit 路径要求仅作历史事实，不再取证；Deal Volume / VolumeExt 本身继续影响实际成交量与 Volume Weighted Points，必须保真。Tick Volume 仍不等于 Event Volume；并非所有已有字段都应进入当前最小快照。

| 字段/路径 | 已确认 | 边界 |
|---|---|---|
| MT5 Deal Volume | 1 raw unit = 1/10000 lot | 使用实际Deal，不用Order / Position量 |
| MT5 Deal VolumeExt | 1 raw unit = 1/100000000 lot | 不能先缩到4位再算；选用的Profit路径须匹配精度 |
| MT5 TradeProfit / Ext | 输入分别4位 / 8位缩放 | 不能混传raw编码；Terminal的业务lots与Manager整数编码不同 |
| MT5 Tick volume | 数据源与CalcMode相关，volume_ext另有固定精度 | 不是被分析Deal Volume，不可把所有名为Volume的字段统一按一种单位 |
| MT4 TradeRecord.volume | int字段只注释volume；Group lot_step及交易请求/summary样例直接支持1 lot = 100的SDK惯例 | 不能把请求/summary证据冒充本字段完整契约；TradeRecord实际历史行缩放及Partial Close事件量仍须核对 |
| MT5 Group Symbol defaults | 对应*Default()返回值可表示继承，不是有效基础Symbol数值 | 有限数值也可能不是业务参数；需依据字段语义解析，不能直接用于计算 |

来源：[Deal Volume](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtdeal_volume.htm)、[VolumeExt](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtdeal_volumeext.htm)、TradeProfit / Ext及MTTickShort见上文；[Group Symbol文档](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtcongroupsymbol.htm:754)。

继承标记是具体获证的特殊语义，不能推广为“所有最大值/零值都是Sentinel”。读取文档或对应无参getter与调用配置Default() / setter不是同一动作；本轮未调用或修改配置。实际字段需要逐项保留Raw / Normalized / Unit / Scaling来源，Missing、Parse Error、NaN、Infinity、Unit Unknown与真实0保持区分。

Point需取目标上下文中有效Point字段，不能从Digits推导代替。SDK使用double不等于已决定应用Numeric Type；Decimal精度、溢出、舍入及结果有限性属于后续Technical Design，不能只凭静态类型认定所有计算安全。

MT4 [ConGroupSec.lot_step](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt4-manager-sdk/MT4ManagerAPI.h:255)及[请求量显示样例](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt4-manager-sdk/Examples/ManagerAPISample/PageDealer.cpp:272)是百分之一lot关联证据。TradeRecord.state标为reserved，即使另有TS_CLOSED_PART枚举，也不能假定历史数据总能靠state判定部分平仓。MT4价格double不应套用Bar RateInfo整数价格编码；头文件的自定义时间宏也不应直接套到__time32_t字段。

另一个单位反例：[MQL4 Symbol Properties](https://docs.mql4.com/constants/environment_state/marketinfoconstants)的MODE_TICKSIZE单位为points，而SYMBOL_TRADE_TICK_SIZE为最小价格变动；同名Tick Size不能无验证混用。MT5 [VolumeFromVolumeExt](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Include/Classes/MT5APIMath.h:283)采用整数除法，缩到标准精度可能丢掉实际扩展成交量；这不是业务批准的截断操作。

## 当前取证停止条件

> **以下为 2026-10-14 历史停止条件，其中 Money 要求已 SUPERSEDED。** 当前门槛改为两平台 Manager Historical Quote、时间 / 排序 / Coverage / Server、正式 Point、真实 Event Volume 与必要数值契约；详见 [2026-10-15 计划](02-minimal-verification-protocol.md)。不得继续把 Money 当 Q01 阻塞项，也不得把计划修订视为运行验证完成。

以上均不是运行PASS。进入Q01前仍需满足[最小验证清单](02-minimal-verification-protocol.md)中的实际历史报价、Ordering、Coverage、Server Identity和Time Mapping条件，并说明其余Money / Volume风险如何在冻结规则内被可靠识别与隔离。没有环境/样本/契约时，状态保持UNRESOLVED；不创建新的业务规则来掩盖缺口。
