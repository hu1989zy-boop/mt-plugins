# Manager-only Technical Evidence — 2026-10-15

**Manager-only Architecture = UNRESOLVED。Q01 = NOT READY FOR DECISION。**

本次依次完成 STEP 1 MT5 Manager 静态取证、STEP 2 MT4 Manager 静态取证；MT4 Manager 历史 Quote 的充分性为 INSUFFICIENT EVIDENCE 后，才复核 MT4 Terminal 标准路径。没有连接 Server、编译或加载 SDK、执行交易样例或 Probe。两个平台的运行验证均为 **NOT RUN**。本报告结束后 STOP，不修改 Architecture / Calculation Specification，不进入 Implementation，不提前提出 Q01。

本报告取代旧材料中的当前能力结论；[01历史事实](01-capability-findings.md)仍保留为来源记录。[02计划](02-minimal-verification-protocol.md)承接未完成的运行验证。[03旧指南](03-windows-evidence-setup.md)仍为 WITHDRAWN / DO NOT EXECUTE。

## 证据范围与结论含义

- 能力结论仅用 SUPPORTED / NOT SUPPORTED / INSUFFICIENT EVIDENCE / NOT RUN。SUPPORTED 必须注明支持的是“已文档化接口契约”还是“目标环境行为”；两者不能混同。
- 本地 MT5 Manager header API version = 6182；MT4 Manager header ProgramVersion = 400、Build = 1470。不是已经验证的 Server Build。
- 本机 Darwin 24.6.0 / arm64；SDK DLL 为 Windows PE。未找到 wine / wine64 / pwsh / dotnet / x86_64-w64-mingw32-g++ 可执行入口。Windows ARM64 DLL 也不能直接作为 macOS 动态库运行。
- **Runtime = NOT RUN — BLOCKED BY：当前任务没有可访问的 Windows SDK 执行入口，也没有已提供的目标 Server 只读取证连接与范围或实服返回证据。** 用户拥有 Windows 机器不等于本会话能够在其上执行。未搜索凭据、读取密码文件或探测其他项目配置；未测试网络通断，因此不声称网络连接失败。
- 本次只读检查本地官方 headers / HTML / samples / wrapper，以及官方公共文档；没有把接口名、重复返回稳定或社区文章当作完整能力证明。SDK、Terminal、其他项目及 architecture-design 下全部文档保持不变。
- 当前 V1 仅 Price / Points / 辅助 Mid。Money、Profit、FX、USD-only、Money 专用 Account / Group Context、跨 Symbol Points Ranking 不属于本次验证或阻塞项。

## 1. MT5 Manager Evidence

下表“契约”是 SDK 声明和本地官方文档，“目标范围”是能否在本项目指定 Server / Symbol / 历史期间执行冻结规则。所有目标环境运行项均 NOT RUN。

| 项目 | 契约结论 | 证据与限制 | 目标范围结论 |
|---|---|---|---|
| Historical Bid / Ask | SUPPORTED | `TickHistoryRequest` 返回历史服务器按 Symbol Filter 接受的 Tick；`MTTickShort` 含独立 double Bid / Ask。Raw 方法另含 raw / processed 数据，未启用 raw storage 时退化为 accepted stream；不能凭 Raw 名称证明存在原始流。[M5-01][M5-02] | INSUFFICIENT EVIDENCE：真实目标数据、具体 accepted / raw 路径及历史完整性未验证 |
| Timestamp Precision | SUPPORTED | Tick 有 int64 秒和毫秒字段，Deal 暴露 int64 TimeMsc 字段；实际原始值与精度仍待验证。请求端点是秒，返回字段精度不因此降为秒。[M5-02][M5-03] | INSUFFICIENT EVIDENCE：底层数据源可能仅给秒并由服务器补 .000；字段容量不证明实服原始毫秒精度 |
| Ordering | INSUFFICIENT EVIDENCE | 已查 Manager 历史请求契约未承诺同毫秒真实先后；MTTickShort 无 Sequence / Tick ID。flags 和 reserved 不是获证的顺序字段。[M5-01][M5-02] | INSUFFICIENT EVIDENCE：必须取得顺序契约，或证明冲突集合及其影响可可靠隔离 |
| Coverage / Retention | INSUFFICIENT EVIDENCE | 文档有请求范围、返回码、数量；未据此证明目标历史起点、无缺口、端点和完整续取。TICK_COLLECTRAW 控制额外 raw 存储，不等于无限历史保留。[M5-01][M5-07] | INSUFFICIENT EVIDENCE：没有目标范围完整性或保留边界证据 |
| Server Identity | INSUFFICIENT EVIDENCE | Connect 面向接入节点，SDK 可重连/切换接入点；NetworkServer 返回名称。网络组件 ID 不能直接等同稳定的业务 Trading Server 身份。[M5-05] | INSUFFICIENT EVIDENCE：尚无目标拓扑的权威绑定 |
| Exact Symbol | SUPPORTED | 历史请求接受 Symbol 名称；Deal / Symbol API 提供实际名称。必须逐字保留后缀。MTTickShort 本身没有 Symbol，需绑定原请求及连接上下文。[M5-01][M5-02][M5-06] | NOT RUN：没有实际全名一致性、编码和返回绑定验证 |
| Point | SUPPORTED | 正式 `IMTConSymbol::Point()` 文档定义为价格的一点；存在基础 / Group-aware SymbolRequest 和 SymbolGet。[M5-06] | NOT RUN：实际返回、来源时间与必要目标上下文尚未核实 |
| Deal Volume | SUPPORTED | `IMTDeal::Volume()` = 1/10000 lot；`VolumeExt()` = 1/100000000 lot，均 uint64，属于实际 Deal。[M5-04] | NOT RUN：实服 Deal、扩展精度、字段有效性及完整候选集尚未验证 |
| Overall | INSUFFICIENT EVIDENCE | 已有明确的历史 Tick、正式 Point 和 Deal 数量接口；完整 Quote / Candidate Contract 未获证 | INSUFFICIENT EVIDENCE |

### 时间、范围与数值精度

`DealRequest` / `DealRequestPage` 查询端点以 Trade Server 时区的秒表达；`TickHistoryRequestRaw` 同样明确 Server 时区。普通 TickHistoryRequest 页仅写 epoch 秒，需要结合 MTTickShort 和实际数据路径验证，不直接把 epoch 数值解释为 UTC。[M5-01][M5-03]

MTTickShort 文档规定：Gateway 仅给秒时服务器可产生 .000 毫秒；只给毫秒时服务器可据此填秒；未给时间时可填服务器接收时间。这是上游构造契约，不允许 Analyzer 自行补造缺失时间。真实恰好发生在 .000 的 Tick 不能仅因此判无效；需要证明所选路径实际时间精度与语义。Deal 的有效 TimeMsc 也必须单独验证，不能借 Tick 字段存在推定 Deal 完整。[M5-02][M5-03]

秒级取数端点不自动否定 100ms / 500ms：可以在取数契约允许时扩大读取范围，再严格按原始有效毫秒筛选和匹配；不能改变 `[From, To)`、Target 或把 To 加偏移模拟包含边界。当前尚无实服端点、分页完整性及毫秒保真证据，故不能宣布任一窗口通过。

### Ordering 的准确边界

缺少 Sequence 不直接等于 NOT SUPPORTED。取得完整范围后，不同时间戳可用时间比较；若最大有效时间戳下多个 Bid / Ask 冲突，只有真实先后有充分证据才可选择其中一条。若能证明冲突的完整集合及影响范围，按 Q07 排除受影响 Events，其他独立获证 Events 可继续。不能用排序数组、价格大小、数组下标、重复调用稳定来创造先后，也不能跳过这些有效但顺序不明的 Tick 继续找更早报价。

如果遗漏的 Tick 或返回范围本身未知，就不能声称已可靠界定冲突影响；此时不能套用局部隔离。无需建立跨任务 Provider Registry，也不要求证明所有 Server / 所有历史永久有序。

### 必要上下文

MT5 官方配置有 Group spread 调整，说明“同 Server 的任意报价路径完全等价”不能直接成立。[M5-06] 本次需要说明选定历史路径返回何种真实 Bid / Ask；**不新增历史 Group spread 重建、账户专属历史客户端报价还原或 Profit Context 要求**，也不自行对返回报价应用当前 spread 公式。Point / Quote 确有上下文影响时才记录必要适用性字段；已证明 Server / Symbol 公共的字段可复用。

## 2. MT4 Manager Evidence

| 项目 | 契约结论 | 证据与限制 | 目标范围结论 |
|---|---|---|---|
| Historical Bid / Ask | INSUFFICIENT EVIDENCE | 有 `TicksRequest(TickRequest*, int*)` 和 TickRecord 的 double bid / ask；本地包未找到足以解释 RAW / NORMAL / ALL、错误和部分返回的专门契约或调用样例。[M4-01] | INSUFFICIENT EVIDENCE：字段不证明真实历史双边来源和完整性 |
| Timestamp Precision | SUPPORTED | TickRecord.ctm、TradeRecord.open_time / close_time、TickRequest.from / to 是 `__time32_t` 秒级表示，满足 MT4 不伪造毫秒的表示要求。[M4-01][M4-03] | INSUFFICIENT EVIDENCE：历史时区、端点、实际事件时间语义未获证 |
| Ordering | INSUFFICIENT EVIDENCE | TickRecord 无获证 Sequence / Tick ID；datafeed 是 feed index，flags 是类别，不能充当同秒顺序。[M4-01] | INSUFFICIENT EVIDENCE：需真实顺序或 Q07 所需可靠隔离依据 |
| Coverage / Retention | INSUFFICIENT EVIDENCE | ConCommon.keepticks 注释仅为 ticks keep period，未给单位；Symbol logging 也不是实际历史覆盖证明。[M4-02] | INSUFFICIENT EVIDENCE：历史边界、缺口、截断、完成状态与权限未获证 |
| Server Identity | INSUFFICIENT EVIDENCE | Connect / ManagerCommon 提供连接和部分配置，但名称、地址不能单独证明稳定逻辑 Server。[M4-02] | INSUFFICIENT EVIDENCE：需目标实际 Server 身份绑定 |
| Exact Symbol | SUPPORTED | TickRequest、TradeRecord、SymbolInfo 有 `char symbol[12]` 字段；TickRecord 本身没有 Symbol，必须绑定原请求完整名称及连接上下文。来源字段 source 是另一概念，不能替代真实 Symbol。[M4-01][M4-02][M4-03] | NOT RUN：固定容量和编码必须无损容纳完整名称；不截断、不去后缀 |
| Point | SUPPORTED | `SymbolInfoGet` 的正式 SymbolInfo.point 注释说明价格点单位；官方样例实际读取并使用它。ConSymbol.point 标 internal 不意味着不存在正式只读 Point 路径。[M4-02] | NOT RUN：目标 Symbol / Server 返回及必要初始化、上下文尚未验证 |
| OPEN Event Volume | INSUFFICIENT EVIDENCE | TradeRecord 只有单个 int volume；缺少 partial-close 后原始 OPEN 实际量与机械重开记录的充分契约。[M4-03] | NOT RUN |
| CLOSE / Partial Close Actual Volume | INSUFFICIENT EVIDENCE | 有 close_reopen 配置和 TS_CLOSED_PART 名称，但 TradeRecord.state 标 reserved；未证明逐次真实平仓量、残余单链及单位。[M4-03] | NOT RUN |
| Overall | INSUFFICIENT EVIDENCE | 历史 Tick 候选接口、秒级表示与正式 Point 已发现；完整 Quote / Event Contract 未获证 | INSUFFICIENT EVIDENCE |

### OPEN / CLOSE / Partial Close 独立缺口

ConGroup.close_reopen 的注释明确涉及部分平仓时完整关闭原单、重开剩余部分，因此不能默认“一条 TradeRecord 永远同时代表原始开仓量和本次平仓量”。TradeRecord 有 open/close time、price、单个 volume、ticket、comment；当前材料没有足够证据把这些字段恢复成完整且无重复的真实 OPEN / CLOSE 事件。[M4-03]

官方 PageDealer 中 `/100.0` 显示的是 RequestInfo.trade.volume，即 **TradeTransInfo 的请求量**；不能移植成 TradeRecord 历史实际 Event Volume 的已证单位，更不能把 Order Request Volume 当实际成交量。[M4-03] 不从 Profit、Commission、Comment 文本、相邻 Ticket、reserved state 推测链路或数量。

TradesUserHistory 有 from/to 参数，但已查声明未说明按开仓时间、平仓时间还是其他时间筛选、端点如何包含。还需证明区间内 OPEN 但尚未 CLOSE、跨区间 CLOSE、一次及多次 partial-close 后的 Candidate 完整性；最终归一化仍严格 `[From, To)`。这些是既有业务规则的技术证明，不增加新事件类型或业务决定。

验证应使用已有普通全平、一次/多次 Partial Close、最终全平的执行证据及 Manager 原始返回。**不为制造样本发单、平仓、改 Group，也不把剩余数量重开记录自动当作新增真实 OPEN。**

## 3. Required Windows 与 Quote Selection

| Platform | Required Windows | 表示能力 | 完整规则能力结论 | Runtime |
|---|---|---|---|---|
| MT5 Manager | 100ms、500ms | SUPPORTED：int64 毫秒字段 | INSUFFICIENT EVIDENCE：实际源精度、时间语义、覆盖与冲突处理未获证 | NOT RUN |
| MT5 Manager | 1s、3s、5s、10s、30s、60s、180s | SUPPORTED：毫秒字段可表示这些目标 | INSUFFICIENT EVIDENCE：没有可绕过覆盖/排序/时间缺口的已通过窗口 | NOT RUN |
| MT4 Manager | 1s、3s、5s、10s、30s、60s、180s | SUPPORTED：秒级表示 | INSUFFICIENT EVIDENCE：历史双边路径、时间、覆盖与冲突处理未获证 | NOT RUN |

`latest valid Tick where tick_time <= Target Time` 的端到端结果：两平台均 **INSUFFICIENT EVIDENCE**。本次没有生成虚构的 PASS、Markout Result 或合成 Tick 实验来代替 Server 能力。

有效性仍要求 Present / Parseable / Finite / 已知语义，Bid > 0、Ask > 0、Ask >= Bid；跳过无效 Tick 后找更早有效 Tick。无最大 Quote Age、无业务最大回溯时限、不修复/插值、不换 Provider / Server / Symbol。只取得有限样本不能证明所有更早数据不存在；样本查询边界不能变成新的业务回溯上限。

## 4. Server Time / UTC Mapping

| 路径 | 已确认事实 | 当前结论 / 尚缺证据 |
|---|---|---|
| MT5 Trade / Tick | Deal 查询、Raw Tick 查询和 MTTickShort 文档涉及 Trade Server 时区；TimeMsc / datetime_msc 保留毫秒。[M5-01][M5-02][M5-03] | INSUFFICIENT EVIDENCE：针对实际历史路径证明表示同一时刻；不能仅因都来自 Manager 就省略核实 |
| MT5 TimeServerRequest | 直接请求当前服务器毫秒时间；平台默认使用 Trading Time，例外须按对应接口说明。[M5-08] | SUPPORTED：当前时钟请求契约；不证明过去 Offset / DST |
| MT5 TimeServer | 根据客户端本地时钟和服务器时区计算。[M5-08] | NOT SUPPORTED：作为独立服务器时钟或历史映射真值的证明 |
| MT5 TimeZone / DaylightState | 当前 GMT 偏移和 DST 设置；DaylightState 文档关联服务器 Windows 时区切换规则。[M5-08] | INSUFFICIENT EVIDENCE：缺实际历史配置及切换适用性，不能拿当前状态推过去 |
| MT4 Event / Tick / ServerTime | 秒级类型；ConCommon 有 timezone / timezone_real / daylightcorrection。[M4-01][M4-02][M4-03] | INSUFFICIENT EVIDENCE：字段/宏不构成实际历史时间映射契约 |

允许获证的恒等映射；若 Trade、Quote 和查询边界确实使用同一 Server 时间语义，不强迫额外绕经 UTC。只有实际需要 UTC 转换、历史 Offset/DST 消歧时才取相应证据。MT4 的 STDTIME / OURTIME 自定义编码宏不应未经说明套到所有 __time32_t 字段。原始值、原始语义、精度、映射和适用历史期均需可追溯；未解决时间不改叫 NO_QUOTE。

## 5. Numeric Evidence

下表记录实际 V1 所需输入。C++ 类型有确定表示，并不保证每条 SDK 返回值有业务意义。SDK 返回码、对象/数组是否可取得和字段本身缺失必须分开；没有证据时，不能把 0、最大整数、负数或空对象自行解释成某种 Sentinel。

| Platform / Field | Raw Type | Scaling / Semantic | Precision | Sentinel / Missing evidence |
|---|---|---|---|---|
| MT5 Deal.Price | double | 实际 Deal 成交价，无 lot scaling [M5-03] | 原始浮点值；Digits 不授权舍入后算 Markout | 字段专用 Sentinel 未获证；接口失败不生成 Price=0 |
| MT5 Tick.bid / ask | double | 独立 Bid / Ask 价格 [M5-02] | 返回原始价格；不由 Last / spread 补另一侧 | flags 是变化/流标志；未证明字段零值代表 Missing；零价按已冻结业务无效 |
| MT5 Symbol.Point | double | 正式一点的价格大小；文档定义为 1/10^Digits [M5-06] | 读取正式 getter，禁止 Analyzer 从 Digits 补缺 | 专用 Sentinel 未获证；Group 默认继承 marker 不能自动套到 Point 值 |
| MT5 Deal.Volume | uint64 | raw / 10000 lots [M5-04] | 4 位小数；整数缩放 | 未获证专用缺失值；0 不自动等于 Missing |
| MT5 Deal.VolumeExt | uint64 | raw / 100000000 lots [M5-04] | 8 位小数；保持实际量，不先截成 Volume | 同上；需要核实当前路径实际精度，不能假设转换无损 |
| MT5 Deal.Time / TimeMsc | int64 / int64 | 秒 / 毫秒；Deal 时间与 Server 时间语义结合 [M5-03] | 正式 MT5 Candidate / Markout 用有效原始 TimeMsc | setter 可联动填充不是对缺失实际成交时间的修复许可；实际失效编码尚未获证 |
| MT5 Tick.datetime / datetime_msc | int64 / int64 | 秒 / 毫秒；Server Trading Time [M5-02] | 源仅给秒可能自动补 .000；不等于真实毫秒精度获证 | Gateway 输入0触发服务器补时是特定构造语义，不泛化为历史查询 Missing 标记 |
| MT4 Trade.open_price / close_price | double / double | OPEN / CLOSE 各用自身真实成交价 [M4-03] | 原始值；与 RateInfo Bar 的整数价格编码区分 | 历史字段专用 Sentinel 未获证；0 不等于无记录 |
| MT4 TickRecord.bid / ask | double / double | Bid / Ask 字段；具体 RAW / NORMAL 历史数据路径仍缺契约 [M4-01] | 原始 double；禁止用 spread 重建 | 专用 Sentinel / 缺失表示未获证 |
| MT4 SymbolInfo.point | double | 正式 Symbol Point，注释为价格点大小 [M4-02] | 返回正式值；不自行回退 Digits | 专用 Sentinel 未获证；SymbolInfoGet 失败单独记录 |
| MT4 TradeRecord.volume | int | **Scaling = INSUFFICIENT EVIDENCE**；字段仅注释 volume [M4-03] | 实际 OPEN / CLOSE、部分平仓单位和量未获证 | 不借请求量 /100 或 gw_volume 补；专用 Sentinel 未获证 |
| MT4 open_time / close_time / Tick.ctm | __time32_t | 秒级表示；实际历史语义仍缺证据 [M4-01][M4-03] | SECOND；无毫秒，保持原精度 | 不把所有0时间解释相同状态；Windows 类型范围不等于任意历史均受支持 |

所有输入都继续执行已确认的 Numeric Contract。Bid / Ask / Trade Price / Volume / Point 必须 >0；合法计算得到的 Price / Points / Mid / Quote Age 可为0。NaN、Infinity、Missing、Unavailable、Parse Error、Unit Unknown、Not Executed、Calculation Failure 不能变成0。计算 Overflow / Non-finite 与输入无效分开，尚未进行实际计算阶段验证。

“字段专用 Sentinel 未获证”不等于强制每个字段都必须存在 Sentinel 文档：充分的接口返回码、字段存在性、值域和有效性契约也可提供证明。若运行观察到未解释特殊编码，先记录并核实，不猜含义。不得为了取证修改 SDK 字段或构造交易。

## 6. Terminal Dependency

**MT5 Terminal Required = UNRESOLVED。MT4 Terminal Required = UNRESOLVED。** Manager 未完全获证不等于 Terminal 已证必须，也不等于 Terminal 足以补上缺口。

MT4 Manager Historical Quote 的 INSUFFICIENT EVIDENCE 已触发用户允许的条件分支。本次只复核下列官方标准路径，没有开发 Terminal Probe：

| MT4 Terminal 路径 | 对真实历史双边 Contract 的结论 | 官方依据 |
|---|---|---|
| SymbolInfoTick | NOT SUPPORTED：它返回当前价和最后更新时间，不是历史区间查询 | [MQL4 SymbolInfoTick](https://docs.mql4.com/marketinformation/symbolinfotick) |
| Timeseries / CopyTickVolume | NOT SUPPORTED：所查接口是 Bar / OHLC / 每根 Bar 的 Tick 数量，不是逐 Tick 历史 Bid/Ask | [MQL4 Timeseries](https://docs.mql4.com/series) |
| 标准 HST / Strategy Tester | NOT SUPPORTED：Bar 历史、建模价格及模拟 Ask 不满足本项目真实 Bid/Ask | [MT4 Tester 官方说明](https://www.metatrader4.com/en/trading-platform/help/autotrading/tester/tester_parameters) |
| History Center Download | NOT SUPPORTED：所查 Bar 下载路径不能提供本项目要求的同 Broker 历史双边 Tick | [History Center 官方说明](https://www.metatrader4.com/en/trading-platform/help/service/history_center) |
| 其他尚未提供具体接口的官方扩展路径 | INSUFFICIENT EVIDENCE | 未发现可据以判定合格的接口/数据路径；不是对所有可能扩展作“不存在”的断言 |

MT5 Terminal 仅沿用[01中的既有官方 CopyTicks / CopyTicksRange 事实](01-capability-findings.md#e03--mt5历史quote与完整性)作条件候选记录；本次未扩大其研究、运行或开发 Probe，其文档排序不能迁移为 Manager API 的承诺。

若后续 Manager 完整满足契约，依最新指示不为比较扩大 Terminal 研究。只有已经存在且确需比较的合格 MT4 双来源，才沿用同 Server / Symbol / 期间 Quote Comparison；当前没有这样的双来源证据，不能用 Tester 模拟输出制造比较通过。

## 7. Manager-only / Q01 / Blocking Evidence Gaps

**Manager-only Architecture = UNRESOLVED。Q01 = NOT READY FOR DECISION。**

当前只推荐技术验证顺序：先补 MT5 Manager 的实服和契约缺口，再补 MT4 Manager；不提交最终 Provider 选择。MT4 Terminal 标准路径的否定证据不能替代 MT4 Manager 自身的正向证明。

| 阻塞项 | 平台及影响 | 可关闭缺口的最小证据 |
|---|---|---|
| B01 Historical Quote 数据路径及完整性 | 两平台；MT4 还缺方法级真实 Bid/Ask / RAW/NORMAL 语义 | 适用 SDK / Server 的官方契约或可重复只读证据，绑定具体历史路径；可证明请求完成、覆盖/Retention、端点/分段无遗漏。成功码/返回若干 Tick 单独不充分 |
| B02 Ordering 与同时间戳冲突 | 两平台；影响 latest valid Tick | 适用排序契约/Metadata；或完整冲突组和其可靠 Event 影响边界。禁止仅依赖数组顺序 |
| B03 时间语义及源精度 | 两平台；MT5 特别影响100/500ms；MT4 保持秒 | Trade、Quote、请求端点的同一时刻证明，必要历史 Offset/DST 依据；MT5 有效原始 TimeMsc 和实际 Tick 毫秒来源 |
| B04 同真实 Server / 完整 Symbol / 正式 Point | 两平台；影响任务身份、来源与 Points | 真实 Server 稳定身份策略和目标连接验证；全名无损往返；Analysis-Time 正式 Point 返回、单位及必要目标上下文。没有新历史 Group 重建要求 |
| B05 真实 Event / Candidate 完整性及 Volume | MT4 尤其 Partial Close；MT5 Deal 历史端点/分页和有效 TimeMsc | 既有成交与历史链的权威对应；OPEN/CLOSE 实际量；字段单位/Scaling、范围查询完整性。MT5 扩展实际量精度不截断 |
| B06 必要 Numeric 字段解释 | 两平台；仅限参与 V1 计算的字段 | 返回码、原始字段、单位/精度、已发生特殊编码的正式解释；未知输入不进入正式计算，可靠局部异常按已有边界隔离 |

B01–B06 是当前完整能力尚不能获证的技术缺口，不表示每个不合格 Event 都必须让 Task 失败；Q07/Q12/Q22 的局部隔离与整体发布边界继续适用。没有新增覆盖率门槛、数值阈值、Money、FX、币种限制或 Ranking 需求。

### 运行验证还需要的条件

1. 用户已有 Windows 机器上的实际 `mt-plugins/MarkoutAnalyzer` 路径、可执行入口和匹配 SDK 的系统/进程架构。当前没有交付或运行验证 EXE，旧03指南不可执行。
2. 在 Windows 本地配置的获授权只读 Manager 连接、可达 Server、所需历史/交易/Symbol 读取权限。密码留本地，不提交聊天或证据文件；不运行带交易操作的 SDK GUI 样例。
3. 真实 Server 身份证明、完整 Symbol、已有目标 Login 和短历史区间；包含必要同时间戳冲突、边界和 MT4 Partial Close 已有样本，不为了样本产生新交易。
4. 对应 SDK / Server 的查询与数据路径契约、必要历史时间规则和已有执行证据。样本结果只证明其绑定范围，不外推全历史。

先满足这些运行条件再作最小只读 Probe；不因当前不能运行而安装兼容层、修改 SDK / Terminal 或创建正式工程。本次完整静态核查并不等于 Technical Evidence 的实服部分已完成。

## 8. Architecture Impact — 仅条件提案

如果两个 Manager 分别充分满足全部冻结规则，并经用户完成 Q01 / 后续 Revision 确认，可提出删除：Terminal Connection、Terminal 凭据/路径/实例配置、Terminal Preflight、Terminal Quote Provider、Manager–Terminal Server Matching、Terminal 历史同步与缓存专用流程。

仍须保留：Manager Connection / Preflight、真实 Server Identity、Exact Symbol、统一 Quote Provider 契约、Ordering / Coverage、历史时间语义、正式 Point、真实 Event Volume、Candidate/Eligibility、诊断、Version 来源和结果冻结。**不能因为删除 Terminal Matching 而删除真实 Server 验证，也不能因 Manager-only 绕过 Q01。** 本次没有落地这些架构修改。

## 来源索引

以下本地路径相对现有 `mt-plugins`。行号为本轮读取位置；[静态 manifest](../../temp/technical-evidence/manager-only-static-evidence-2026-10-15.json)记录版本、源文件哈希和运行状态，不包含凭据或完整历史流。

- **M5-01**：`vendor/mt5-sdk/Include/MT5APIManager.h:310,513`；`Docs/html/imtmanagerapi_tickhistoryrequest.htm:88–106`、`imtmanagerapi_tickhistoryrequestraw.htm:88–105`。普通 Manager；Admin 界面从 header:803 开始，不借用其权限。
- **M5-02**：`vendor/mt5-sdk/Include/Bases/MT5APITick.h:50–75`；`Docs/html/mttickshort.htm:113–121,129–136,171–177,190+`。
- **M5-03**：`vendor/mt5-sdk/Include/Bases/MT5APIDeal.h:134–143,200`；`Docs/html/imtdeal_price.htm:74`、`imtdeal_time.htm:74,111`、`imtdeal_timemsc.htm:74,111`、`imtmanagerapi_dealrequest.htm:133–141`、`imtmanagerapi_dealrequestpage.htm:125–137`。
- **M5-04**：`vendor/mt5-sdk/Include/Bases/MT5APIDeal.h:143,232`；`Docs/html/imtdeal_volume.htm:74–76`、`imtdeal_volumeext.htm:74–76`、`features.htm:50–61`；`Include/Classes/MT5APIMath.h:283` 的 `VolumeFromVolumeExt` 整数除法转换需避免损失实际量精度。
- **M5-05**：`vendor/mt5-sdk/Docs/html/imtmanagerapi_connect.htm:96,109–115`、`imtmanagerapi_networkserver.htm:79`、`imtconserver_id.htm:74`。`Include/MT5APIManager.h:858,875` 的 CommonGet / NetServerGet 在 Admin 界面，不当作已获普通 Manager 权限。
- **M5-06**：`vendor/mt5-sdk/Include/Config/MT5APIConfigSymbol.h:621–624`；`Docs/html/imtconsymbol_point.htm:74`、`imtconsymbol_symbol.htm:74,114`、`imtmanagerapi_symbolrequest.htm:89,138–139`、`imtmanagerapi_symbolget.htm:144`、`imtcongroupsymbol_spreaddiff.htm:76`、`imtcongroupsymbol_spreaddiffbalance.htm:76–84`。名称 setter 的截断说明不授权本应用截断或调用 setter。
- **M5-07**：`vendor/mt5-sdk/Docs/html/imtconsymbol_enum.htm:662–666` 的 TICK_COLLECTRAW。
- **M5-08**：`vendor/mt5-sdk/Docs/html/imtmanagerapi_timeserverrequest.htm:79–84`、`imtmanagerapi_timeserver.htm:76`、`imtcontime_timezone.htm:74`、`imtcontime_daylightstate.htm:74–80`。
- **M4-01**：`vendor/mt4-manager-sdk/MT4ManagerAPI.h:858–879,1689–1710`。核查 MT4 包 headers、Delphi wrapper、C++ samples，未发现完整 TicksRequest 方法契约或调用样例；“未发现”限定这个已查材料范围。
- **M4-02**：同 header `:48–60,96–102,512–559,1265–1280,1617–1620,1729`；`vendor/mt4-manager-sdk/Examples/ManagerAPISample/PageDealer.cpp:234,254–264`。
- **M4-03**：同 header `:328,1015–1057,1108,1196,1645–1647`；同 PageDealer.cpp `:272`。close_reopen、TradeRecord、TradeTransInfo、RequestInfo 分别核查，不混用请求量与实际量。

本次结论：文档与静态审查完成；Windows / Server 运行验证仍 NOT RUN；Manager-only 和 Terminal 依赖均 UNRESOLVED；Q01 保持等待证据。没有新增用户业务问题。
