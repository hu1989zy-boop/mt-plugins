# 最小技术验证计划 — MT5 Manager → MT4 Manager

日期：2026-10-15。阶段：**Technical Evidence Phase；Runtime NOT RUN**。已按严格顺序完成两平台静态取证，结果见[04当前报告](04-manager-only-evidence-2026-10-15.md)。以下是剩余运行验证的范围，不是已执行或通过记录；本轮没有编译/加载 SDK、查询 Server 或运行 Terminal。

权威依据：[V1 MONEY MARKOUT REMOVED](../architecture-design/13-architecture-review-decisions.md#v1-money-markout-removed)、[AR-01](../architecture-design/13-architecture-review-decisions.md#ar-01)与[AR-02](../architecture-design/13-architecture-review-decisions.md#ar-02)。AR-01 / AR-02 已 DECIDED / CLOSED；其他已确认且未被取代的 Candidate、Quote、Point、Volume、数值有效性和发布边界继续适用；参见[影响分析](../architecture-design/14-money-removal-impact-analysis.md)。

## 计划边界

V1 不验证 Native / Terminal / Specification-based Profit，不计算 Money 或 FX，不要求 Profit Currency、Contract Size / Tick Value Money 路径，不建立 Money fallback。AR-02 已明确撤销 USD-only；旧限制标 **SUPERSEDED BY V1 MONEY MARKOUT REMOVAL**，Q08 为 **NO LONGER APPLICABLE TO V1**。不因账户币种为 EUR、GBP、JPY、AUD 或其他币种拒绝分析。Account Currency 仅为可选原始 Account Metadata，不参与公式或 Eligibility，也不作为 Points 技术计算前提；不新增 FX / Profit Currency Logic。

AR-01 已确认以 Symbol Overview / Symbol List 取代跨 Symbol Ranking：各完整 Symbol 独立展示 Candidate、Eligible、Analysis Rate、Volume、Avg Points、VW Avg Points 及关键窗口；允许名称、Event Count、Volume、Analysis Rate 排序，不用 Points 建立跨品种高风险暗示、Normalized Points 或合成排名指标。本计划继续验证真实 Point 与实际成交量，不为已取消的 Money Ranking 或拟议 VW Points Ranking 增加取证任务。

Manager-only 是两平台分别通过取证后可提交的首选 Architecture Revision Proposal，尚未成立或启用。Terminal 仅为条件 Quote 候选 / 对照路径；Profile / Preflight 删除 Terminal 的修改尚未批准。Q01 继续等待充分证据，不提前要求用户选择。

后续最小验证仅限已有获授权环境的只读接口；不发订单、不平仓、不修改账户、Group、报价历史或配置，也不为构造 Partial Close 样本交易。不运行 SDK GUI 交易样例。所有新材料应置于 MarkoutAnalyzer/temp/technical-evidence，摘要置于 docs/technical-evidence；不修改其他项目、Terminal 或 SDK。

输出采用字段白名单，不整份 dump User / Group / Account 配置；不保存密码、Token、Secret 或无关个人信息。只保留必要边界样本、冲突 Tick、元数据与摘要，不建立长期完整 Historical Tick Stream。若后续选用 Terminal，应另说明其正常历史缓存副作用及授权边界。

## 当前缺少的材料

| 材料 | 用途 | 当前状态 |
|---|---|---|
| Windows 实际项目路径、系统 / SDK 架构、版本及只读执行入口 | 对应原生 SDK 运行验证 | 用户有 Windows 机器；运行证据未取得，无已交付取证 EXE |
| 两个平台的真实 Server 身份、完整 Symbol、短历史期间和目标实际 Login | 绑定 Trade / Quote / Point 证据范围 | 待取得；不通过聊天索取凭据 |
| MT4 TicksRequest 对应版本详细契约 | RAW / NORMAL、时间、排序、部分返回与权限语义 | 现有静态材料不足 |
| MT5 TickHistoryRequest / Raw 对应版本与 Server 行为材料 | accepted / raw 数据路径、毫秒保真、排序与覆盖 | 有本地文档，目标环境待验证 |
| 历史 Offset / DST / Server 变更依据 | 确定性历史时间映射 | 待取得 |
| Analysis-Time 正式 Point 与必要 Metadata 适用性材料 | 目标 Symbol / Server 的 Points 计算 | 需核实数据源、单位及真正影响结果的上下文 |
| 已有 MT4 普通 / 一次 / 多次 Partial Close 执行及历史链 | 真实 Event 与实际成交量 | 待取得，不为测试新交易 |

[旧 Windows 指南](03-windows-evidence-setup.md)已撤回，禁止执行；其原内容保留，不作为有效部署步骤。当前不要求安装工具链、Terminal 实例或 Profit 测试工具。

## 后续最小验证与否定条件

严格执行顺序：

1. **STEP 1 MT5 Manager**：V01、V05、V03、V04、V07、V08、V12，以及 MT5 Deal / Candidate 完整性。先完成其静态和可执行范围内的验证与缺口判定。
2. **STEP 2 MT4 Manager**：V01、V02、V03、V04、V07、V08、V11、V12，包括 OPEN / CLOSE / Partial Close 真实量。
3. **条件 Terminal**：只有 MT4 Manager Historical Quote 不满足契约或证据不足，才进入 V06 的深入核查；若 Manager 完整满足，不为比较扩大 Terminal 范围。MT5 Terminal 也不预设为必要候选。

V 编号只作旧计划追踪，不是执行顺序。V09 / V10 在 V1 取证计划中停用（NO LONGER APPLICABLE TO V1）。每项能力结论限 SUPPORTED / NOT SUPPORTED / INSUFFICIENT EVIDENCE / NOT RUN；SUPPORTED 必须注明契约或实服范围。不能把字段存在或 count > 0 当作完整验证通过。

| ID | 后续最小观察范围 | 通过所需证据 | 不能视为通过的情况 |
|---|---|---|---|
| V01 环境与真实 Server | 每个平台记录 SDK / Server 版本、只读权限、实际路径及权威身份依据 | Trade / Quote / Point 均绑定同一真实 Trading Server；区分逻辑 Server 与接入节点 | 只有 Profile Name、相同 Login、Endpoint、Server Name 或相同价格 |
| V02 MT4 Manager 历史 Quote | TicksRequest 的 RAW / NORMAL / ALL、datafeed、完整 Symbol 容量、时间端点、错误及部分返回 | 真实历史 Bid / Ask 来源和秒级时间明确；能解释每条记录属于何种历史数据路径 | 猜测 NORMAL = Terminal；ALL 混合路径无解释；截断 Symbol / 去后缀；自行合成 Ask |
| V05 MT5 Manager 历史 Quote | TickHistoryRequest / Raw、accepted stream、返回 datetime_msc / Bid / Ask / flags、请求范围与错误码 | 数据路径语义、返回精度、真实双边值及完整性可证明 | Raw 名称即证明存有 Raw；只得到数据即断言完整；秒级请求意味着可丢弃毫秒 |
| V03 Coverage / Retention / Availability | 已发生短区间的输入、返回码、数量、首末时间、同步状态及历史边界；必要整段 / 分段重叠核对 | 有接口契约及环境证据区分完整、截断、Retention 缺失、Loading 与确实无 Quote | count > 0 即完整；最早返回 Tick 即 Retention 起点；空数据即 NO_QUOTE 或“可能同步” |
| V04 Ordering | MT4 同秒 / MT5 同毫秒不同有效报价；排序契约、适用版本、数据路径、Ordering Metadata 和边界续取 | 真实顺序可确定，或影响能可靠隔离；取得稳定官方契约时确认其适用性 | 仅数组稳定、价格大小、数据库顺序、自己编号；跳过顺序不明有效 Tick |
| V07 时间映射 | Trade / Quote / 查询端点分别保存原始时间和语义；验证分析历史期间 Offset / DST | 同一真实时刻映射唯一；MT4 SECOND、MT5 有效 TimeMsc 保留 | 用当前 Offset 推历史；Windows Local Time 或价格走势猜偏移；DST 无法消歧 |
| V08 Point / 必要 Metadata | Manager 返回的 Analysis-Time 正式 Point、完整 Symbol、来源、采集时间、适用 Server 与必要上下文 | Point Present / Parseable / Finite / > 0 / Unit Known，并证明适用于目标分析上下文；只保存实际参与计算、报价解释和诊断的规格 | 应用从 Digits 自行填补 Point；默认值 / 相近 Symbol / 其他 Server 替代；继承 marker 当实值；保留全套未来 Money 规格 |
| V11 MT4 Partial Close / Candidate | 已有普通全平、一次 / 多次部分平仓及最终全平样本，历史链与实际执行证据 | 实际 OPEN / CLOSE Event、实际 Volume、机械性残余重开与真实事件关系均可追溯；Candidate 完整性可证明 | 用原始 Order Lots、Comment 规则、reserved state 或 Profit 反推；直接把重开都当新开仓 |
| V12 Numeric / Precision | 真正参与 Price / Point / Points / Mid / Volume 权重的字段，Raw、Normalized、Unit、Scaling、Sentinel；计算结果阶段状态 | 保留精确实际成交量；真实零值与缺失 / 非有限 / 未计算分开；输入失败与计算失败分开 | VolumeExt 先截成 4 位、UI 舍入使正量变 0、Missing → 0、借字段名猜单位或 Sentinel |
| V06 条件 Terminal 与 MT4 双来源比较 | MT4 Manager 不满足契约或证据不足才深入；Manager 满足时不为比较扩大 Terminal 研究。若已有且确需评审的合格双来源，再比较同 Server / Symbol / 期间的 Bid、Ask、Timestamp、ordering / coverage、depth | 各路径分别达标，差异可解释并记录；已有低成本事实可保留，不额外开发 Terminal Probe 仅为比较 | 用 HST / Tester / 模拟 Ask 当真实对照；同 Server 即等价；为补数据无提示切换 Provider |
| V09 MT5 Native Profit | **NO LONGER APPLICABLE TO V1** | 被 V1 MONEY MARKOUT REMOVED 取代；不执行 | 不再属于 Provider、Eligibility 或 Implementation 阻塞项 |
| V10 MT4 Native Profit | **NO LONGER APPLICABLE TO V1** | 被 V1 MONEY MARKOUT REMOVED 取代；不执行 | 不再要求 TradeCalcProfit 契约、USD 结果或 FX 验证 |

MT5 Manager 请求端点以秒表达、返回 Tick 可有毫秒时间，两者必须分别验证。必要的取数范围扩展不改变业务 [From, To) 或真实 Target；匹配始终使用有效原始精度，禁止以最后时间 + 1ms 跳过同毫秒记录。MT5 Terminal CopyTicks / CopyTicksRange 的已有文档可作为条件候选证据，不预设必须部署或执行。

整段 / 分段一致只是辅助证据，不独自证明真实排序、无遗漏或 Server 身份。覆盖必须包含必要的 Target 之前历史，不能为了有限样本方便给业务加入最大 Quote Age / 固定回溯上限。Loading 状态只是在进入有界同步等待时的必要证据；能够直接证明历史完整与查询完成的接口不因未提供 Loading 接口而失格。

## Point 与上下文的最小保留原则

- 计划保留：正式 Point、完整 Symbol、Specification Source / Acquisition Time、Platform / Server / Profile、单位与必要原始值、技术证据引用，以及仍实际用于 Quote Interpretation / Diagnostics 的字段。Digits 是否纳入取决于实际用途。
- SDK 正式 Point getter 的内部实现可能由 Digits 计算；读取该正式字段不同于应用自行替代 Missing / Invalid Point。
- MT5 SymbolRequest / SymbolGet 有基础与 Group-aware 重载；MT4 / MT5 有 Group spread 相关字段。须确认实际返回 Quote / Point 的适用语义，不能仅因 Money 消失就假定所有 Group 信息无关，也不能因此保留完整 Profit Context。
- Group / Account 仅按必要字段取证。若 Point / Quote 字段经证明是 Server / Symbol 公共数据，可安全复用；若确有上下文影响，保留适用性证据。不得使用其他 Login / Group 猜测填补，也不得自行改写 Bid / Ask。
- 不再要求 Contract Size、Tick Value、Profit Currency、Profit Calculation Mode、Conversion Rate 或全套 Native Profit 内部参数快照。若某字段另有 V1 必需用途，应先说明该用途，而非为未来 Money 储备。

## 每项证据的最小记录

Evidence ID、日期、执行环境 / SDK / Server 版本、Platform、真实 Server 身份依据、Provider / 数据路径、完整 Symbol、适用的目标 Login 与必要 Group 关系、请求历史范围、原始时间语义 / 精度、返回码、必要 Raw / Normalized / Unit / Scaling 字段、Point 来源与采集时间、结果摘要、冲突 / 边界样本、适用范围与未决事项。不要直接复制完整 SDK 对象。

同一 Version 的 Provider、Source、Time Mapping、Point Snapshot 和实际计算结果须可追溯；不以当前环境回写旧 Version。Re-analyze 创建新 Version。脱敏材料的可追溯性、顺序和精度不能因处理而丢失。

## 冻结异常边界继续适用

- Candidate Population / Range Membership 未确定：Q12 Version-level publication blocked，不能制造完整 Candidate Total / Analysis Rate。
- 已确定 Candidate 的 Required Windows 必须全部满足有效 Quote、Price、Point、Points 等现行 Eligibility；不再要求 Money。部分结果保留，失败不填 0。
- Ordering 可靠隔离时按 Q07 Event 排除；整体影响不明时当前 Run FAILED；不建跨任务 Provider Blocklist。
- Retention 明确的局部缺失与未知 Coverage 区分；未来 Target 不等待并保持独立 Not Ready。
- 已发生历史仅在获证 Loading 时有限等待；局部 Sync Timeout 的其余 Events 需独立获证才可发布，影响不明则整体 Data Not Ready。
- Numeric 输入与执行阶段分离；所有合法零结果保留；异常局部隔离必须有可靠边界。
- 不自动换 Provider / Server / Symbol / Platform；不能取得合格 MT4 来源则 MT4 不可用，不以插值、K Line、当前 Quote 或 Money 替代。

## Manager-only 与 Q01 提交门槛

MT4、MT5 分别取得以下证据：完整真实 Trade / Deal 候选、Historical Bid / Ask、时间精度及历史映射、Ordering、Coverage / Retention / Availability、Exact Symbol、真实 Server Identity、正式 Point、实际 Volume 与必要 Numeric / Metadata 契约。一个平台通过不代表另一个通过，接口存在不代表实际 Server 已验证。

若两平台均满足，可提交 Manager-only 首选 Proposal；Connection Profile 和 Preflight 去除 Terminal 仍属于待批准 Architecture Revision，当前不得自行落地。若某平台尚缺证据，只报告平台、路径、影响与仍待证明的具体门槛。

Q01 是当前唯一 User Decision Required，仍由用户在充分 MT4 Provider 证据后最终确认；AR-01 / AR-02 不重新提问。按最新指示，报告 MT5 Manager Evidence、MT4 Manager Evidence、Terminal Dependency、Manager-only Architecture、Blocking Evidence Gaps、Q01 Recommendation、条件 Architecture Impact。Money / FX / Profit Context、USD-only 账户限制及跨 Symbol Points Ranking 不进入 Q01 通过门槛。本轮静态取证已完成，**Runtime NOT RUN，完整能力仍 INSUFFICIENT EVIDENCE；Q01 = NOT READY FOR DECISION。报告后 STOP，不进入 Implementation、不修改正文。**
