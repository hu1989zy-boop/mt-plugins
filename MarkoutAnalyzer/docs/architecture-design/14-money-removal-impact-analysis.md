# V1 Money Markout Removal — Decision Impact Analysis

日期：2026-10-15。阶段：Architecture Review。状态：影响分析与评审记录已更新；Architecture / Calculation Specification 正文尚未统一 Revision；未进入 Implementation。

权威新决定：[V1 MONEY MARKOUT REMOVED](13-architecture-review-decisions.md#v1-money-markout-removed)。来源：用户本轮[完整25节请求](/Users/lukeh/.codex/attachments/ef752ec3-1aec-4fa5-a6f3-4024cd3d4ed6/已粘贴的文本.txt)。该决定优先于旧 Money 规则；旧记录保留，不表示它们仍是当前 V1 要求。

后续范围决定已确认：[AR-01 — Symbol Overview / Symbol List](13-architecture-review-decisions.md#ar-01)与[AR-02 — 撤销 USD-only 账户限制](13-architecture-review-decisions.md#ar-02)均为 **DECIDED / CLOSED**。以下当前口径已同步；仅 Q01 最终 MT4 Quote Provider 继续等待 Technical Evidence。

## 1. Superseded Decisions

| 旧决定 | 当前适用性 | 失效范围与保留边界 |
|---|---|---|
| Q03 | SUPERSEDED FOR V1 | 原生 Profit、内部 FX、USD Money 输出及专属证据全部退出 V1；历史版本不可变的通用原则仍由 Q15 等保持 |
| Q09 | SUPERSEDED — MONEY MARKOUT REMOVED FROM V1 | Money 成功不再是 Event 资格；统一 Eligible Population、保留已取得事实由下文新契约明确承接 |
| Q08 / 旧 USD-only 范围 | SUPERSEDED BY V1 MONEY MARKOUT REMOVAL；NO LONGER APPLICABLE TO V1 | AR-02 已撤销仅 USD 账户限制；不因账户币种为非 USD 而拒绝或跳过。账户币种仅为可选原始 Metadata，不参与公式或 Eligibility；其他账户身份、数据可靠性与局部隔离规则保留 |
| Q01 | PARTIALLY SUPERSEDED | 原“不预设优先级”调整为优先验证 Manager-only、获证后提出首选方案；候选许可不等于最终 Provider 批准；同源、无自动 fallback、必要双来源比较仍有效 |
| Q05 第三、四项 | PARTIALLY SUPERSEDED | 仅去掉 Money 计算/汇总分支；真实 Trade Price > 0、实际 Event Volume > 0、Partial Close 取量及加权规则不变 |
| Q05 第五项 | PARTIALLY SUPERSEDED | Point 失败后 Money 是否执行/如何记录等分支不再适用；Point 不可用仍失去正式资格，非 Money 已得事实保留，不允许猜 Point |
| Q05 第六项 | PARTIALLY SUPERSEDED | Money 的零值、Trace、金额阈值示例不再是 V1 路径；统一 Numeric Validity、真实零、单位、错误阶段、隔离原则保留 |
| Q13 第一项 | PARTIALLY SUPERSEDED | 原第3/4/14/15/17条 Money 专属内容失效；第6条不再强制 Money-only 规格，第9/13条去 Money 分支。Analysis-Time Point、来源/时间/Server、逐版快照、禁猜、不建历史规格库保留 |
| Q13 第二项 | PARTIALLY SUPERSEDED | 原第8/11/12/13条 Native Profit 上下文分支不再适用；第2/6/7/9/10/16条仅保留 Point / Quote 实际需要的适用性和上下文，不建立完整 Profit Account/Group Context |
| Q07、Q22 | PARTIALLY SUPERSEDED | 去 Money 聚合目标及成功条件；Ordering、未来目标、Retention、同步等待与局部/整体发布边界保持 |
| Q11 | PARTIALLY SUPERSEDED — 引用范围 | Money Failure 相关计数引用退出；三项 B/A/A 的同 Scope 分母分子、零分母、原因去重与重叠计数保持 CLOSED |
| Q15 | PARTIALLY SUPERSEDED | 只去掉为 Money 保存/验证的计算环境；Platform + 真实 Server + Login Set + [From,To) 固定、新 Version 完整重跑、旧 Version 不变 |
| Q17 第一、二项 | PARTIALLY SUPERSEDED | 所有 Money 字段、Money Trace 与补算分支退出；两种显式证据导出、只导出已有事实、无重查重算、状态不变继续有效。第三项无长期 Test History 完整保留 |
| Q19 | PARTIALLY SUPERSEDED — 路径引用 | Profit Calculation 路径不再存在；重启恢复 PENDING 不自动执行，用户 Start 后重新 Preflight 保留 |

Q02、Q04、Q06、Q12、Q16的业务核心不重新开放。旧段落中的 Money 交叉引用由新决定覆盖，不生成新的待答问题。

### 原基线 Dxx 的影响（只登记，不改正文）

| 基线 | 后续统一 Revision 应处理的部分 |
|---|---|
| D02 / D03 / D36 | Terminal 配对连接、实例、凭据及 Preflight 仅在 Manager-only 获证并采纳后删除；当前为条件提案 |
| D18 | Point 仍是正式必需值；其来源可评估 Manager，不能继续把 Terminal-only 写成既定不可变条件 |
| D19 | Money / Profit / FX 全部退出 V1；AR-02 已明确撤销“仅 USD 账户”，该限制标 SUPERSEDED BY V1 MONEY MARKOUT REMOVAL，不因账户币种拒绝分析 |
| D20 | 为 Money 计算排除 Profit/Commission/Swap 等的公式规则不再适用；原始交易字段如确有诊断用途可保留，不新增费用分析或金额指标 |
| D21 / D37 / D41 / D46 | Version 证据和历史冻结保留；规格快照、结果及导出删除 Money-only 字段和上下文 |
| D22 | 删除 Total / Avg Money，保留逐窗 Event Count、Volume、Avg Points、VW Avg Points |
| D23 | 删除 Account 跨 Symbol Money 聚合；仅数量、Rate、Symbol Overview / Symbol List、数据质量，不新增跨 Symbol Points 合计 |
| D24 | 旧 Total Money Ranking 与曾拟议的跨 Symbol VW Points Ranking 均不再适用；AR-01 已决定采用 Symbol Overview / Symbol List，支持名称、Event Count、Volume、Analysis Rate 排序，不构造新的跨品种评分 |
| D25 | Symbol VW Points Curve / Event Points Curve 保留，不存在 Money Curve |
| D27 / D28 | Money 列、Money 排序及多账户金额展示删除；每 Login 独立，禁止跨 Login / Symbol 综合分 |
| D29 / D30 / D40 / D41 | Trace、Single Test 和三类文件输出去 Money；既有诊断定位、显式导出及完整状态表达保留 |

旧 Architecture 文档中相应内容是待修订基线，不能与当前决定混读成两套同时有效的规则。

## 2. Closed Open Questions

- **Q03 / Q09：NO LONGER APPLICABLE TO V1**。历史决定分别标 SUPERSEDED，不删除。
- **Q13 / Q24 的 Profit、Currency Conversion、Profit Currency、Money-only Specification 和 Account/Group-profit 子项：NO LONGER APPLICABLE TO V1**。无需再取得这些证据来解除 V1 阻塞。
- **Q05 第五项的 Money 执行顺序 / Money-only Diagnostics、Q25 金额精度、Q17 Money 导出字段子项：NO LONGER APPLICABLE TO V1**。非 Money 业务继续闭合，不重复询问。
- **Q23 不整体关闭**：Terminal 历史报价及自有缓存仅条件适用；Manager-only 尚未获证，不能先删除这条潜在数据路径。
- **Q08：NO LONGER APPLICABLE TO V1**。AR-02 已撤销 USD-only；旧非 USD 账户不支持/跳过要求标 SUPERSEDED BY V1 MONEY MARKOUT REMOVAL。其他账户身份、候选完整性及局部隔离语义继续有效，不因撤销币种限制而放宽。
- **AR-01 / AR-02：DECIDED / CLOSED**。不再跨 Symbol Points Ranking，账户币种不再是准入门槛；两项均从 User Decision Queue 移除，不重复提问。

当前完整状态与计数见 [09](09-open-questions.md)。已关闭规则、Technical Review 和 UI/UX Design 不变成新问卷。

## 3. Remaining V1 Metrics 与当前 Eligibility Contract

### 输出、统计与公式

| 层级 | 当前 V1 内容 |
|---|---|
| Event / Window | Price Markout、Points Markout；Mid Markout 为辅助详情/诊断信息；Quote / Target / Age / 状态与 Trace |
| Symbol Summary（每个 Required Window） | Eligible Event Count、这些 Eligible Events 的实际 Volume 合计、Avg Points、Volume Weighted Avg Points |
| Account | Candidate、Eligible、Analysis Rate、Symbol Overview / Symbol List、数据质量；不汇总跨 Symbol Points |
| Symbol Overview / Symbol List | 每个完整 Symbol 独立展示 Candidate Events、Eligible Events、Analysis Rate、Volume、Avg Points、VW Avg Points及各关键 Markout Window；支持按 Symbol Name、Event Count、Volume、Analysis Rate 排序，不作为跨 Symbol Points 或风险排名 |
| Multi-account Overview | 每 Login 独立的 Candidate、Eligible、Rate、必要基本状态；不合并 Login 总体 |
| Curve | Symbol：VW Avg Points；Event：该 Event Points；没有 Money Curve |
| Export / Single Trade Test | 保留原格式、隔离及已有事实规则，删除 Money 与 Money Trace，不生成替代金额或评分 |

沿用既有公式，不新增指标：

- BUY Price Markout = Selected Bid − Trade Price；SELL = Trade Price − Selected Ask。
- Points = Price Markout / 本 Version 的有效 Point。正值/负值继续表示客户有利/不利；不是自动风险结论。
- Avg Points = 当前 Scope 的 Eligible Points 合计 / Eligible Event Count。
- VW Avg Points = Σ(Eligible Event Points × 该 Event 实际 Volume) / Σ(Eligible Event Volume)。
- Mid = (同一 Selected Tick Bid + Ask) / 2；Mid Markout 使用既有方向公式，仅为辅助价格差，不改称 Mid Points。
- Trace：Trade Price → Matched Quote → Price Markout → Symbol Point → Points Markout；Mid 为辅助信息。
- 正式 Scope = Account + Symbol（进入 Symbol 层时）+ Side AND Entry/Event Type。Summary、Rate、Curve使用同一筛选和同一 Eligible 集合；普通搜索、排序、Diagnostics Filter 不改变正式统计 Scope。
- Symbol List 的 Points 与 Volume 指标继续按该 Symbol 当前 Scope 的 Eligible Events 表达，Candidate / Eligible / Rate 各保留既有含义。进入某个 Symbol 后，继续查看其自身 Points Summary、Points Curve 和 Event Detail；不得把高 Points 默认或暗示为跨品种高风险。
- Account Currency 可作为原始 Account Metadata 保存、展示；不进入 Price / Points / Mid 公式或 Eligibility，不执行 FX / Profit Currency 逻辑，不恢复 Money。

### Event Eligibility — DERIVED FROM EXISTING DECISIONS + 新决定

以下是在当前 Version 具备发布条件前提下的 Event 资格，必须同时成立：

1. **Candidate Membership 已确定**：真实市场成交事件，归属目标 Platform / 真实 Server / Login，按精确 [From,To) 入围。MT5 使用原始有效 TimeMsc；MT4 按其 SECOND 时间契约。MT5 同 Order 多 Deal 不合并、INOUT 不拆；MT4 OPEN / CLOSE 分别判断，沿用开/平方向。非市场交易不进入 Candidate。
2. **Required Event Fields 有效**：完整 Symbol、正确 Side / Entry / Event 语义、真实成交价 > 0、真实该 Event 成交量 > 0、有效时间与平台精度。MT4 Partial Close 不以原始总 Lots 代替真实 Event Volume；MT5 使用该 Deal 自身成交量。
3. **所用数值有效**：实际计算需要的输入 Present、Parseable、Finite、Unit / Semantic Known，Scaling 经证据确认。Missing、Unavailable、Parse Error、NaN、±Infinity、Unknown Unit 与真实 0 分开；不默认填零、猜测、修复价格或以其它数据替代。
4. **全部 Required Windows 报价可证明完整**：MT5 100ms、500ms、1s、3s、5s、10s、30s、60s、180s；MT4 后七窗。Target = Event Time + Window，可越过 To；MT4 不支持的两窗不是失败。无需强制 T0。
5. **Quote Selection 可靠**：同 Platform / Profile / Provider / Server / 完整 Symbol，历史覆盖、时间映射、顺序能证明 Quote = latest valid Tick with tick_time ≤ Target。允许跳过无效 Raw Tick向更早方向查找；无最大回溯或 Quote Age 阈值；有序性不明的有效冲突 Tick不能跳过或任意选取。禁止跨源补齐、插值、模拟、K线或未来 Tick 替代。
6. **选中 Quote 有效**：Bid > 0、Ask > 0、Ask ≥ Bid，所有所用数值有限且语义明确；零 Spread 允许，不设最大 Spread。保留同一 Tick 自身 Timestamp / Bid / Ask，Quote Age 用该 Tick 时间计算。
7. **每个 Required Window 的 Price Markout 合法且有限**：按真实 Side 和真实 Trade Price 计算；合法零值与负值允许。
8. **Point 有效**：Analysis-Time 正式规格中取得，适用于真实 Server / 完整 Symbol / 必要目标上下文；Present、Parseable、Finite、>0、Unit Known。禁止从 Digits 自行推导来替代 Missing Point，禁止手填、默认或其它 Server / Symbol / Account 上下文替代。
9. **每个 Required Window 的 Points Markout 合法且有限**：实际 Price / 实际 Point；Overflow / Non-finite Result是计算阶段失败，不反向伪装输入错误。任一 Required Window 失败则整个 Event 不入正式 Points 集合。

**明确不加入的条件：**Money 成功、Profit API 可用、FX、Contract Size、Tick Value、Profit Currency、Account Currency，以及仅为 Profit 需要的账户计算上下文。AR-02 已撤销 USD-only，不因 EUR、GBP、JPY、AUD 或其他账户币种拒绝分析；币种作为可选原始 Metadata，不构成资格门槛。原平台、真实 Manager Event、有效 Quote、完整 Symbol、Point、实际 Event Volume 及其他 Eligibility 规则不变。Mid 是辅助信息，不因其辅助定位增设新 Summary Eligibility 门槛；错误必须真实记录，若显示整体计算路径不可靠仍按可靠性边界停止。Digits 或可选诊断字段缺失不自动成为新的通用 Event 失败条件。

已证公共 Server/Symbol Point 可安全复用；不能凭删除 Money 就假设所有必要字段不受 Group 影响。Point / Quote 实际依赖的上下文须先取证，保留最小字段即可。

### 数量、失败与正式发布边界保持

- 已知 Candidate 被排除仍在分母；所有正式 Points 指标与 Curve 只用统一 Eligible 集合。Discarded按Event去重，Reason Count允许重叠，同Reason内去重。
- 当前正式 Scope Candidate = 0 → Candidate 0 / Eligible 0 / Rate “— / No Candidates”；Candidate > 0、Eligible = 0 → 有效 0%，同时 No Eligible Results，不生成伪零 Markout。总体未知不能冒充空集合。
- Q12 成员/总体不确定仍阻止 Version 正式 Summary / 正式 Rate；已知成员但时间精度不合格才是 Event 级失败。不能靠筛选绕过发布阻断。
- Q22 任一 Candidate 的 Required Target 尚未发生：整体 Horizon Not Reached，不能丢掉该 Event 后发布，也不能等待未来 Target。
- Q07 Ordering 整体不可靠或影响无法定位：当前 Run FAILED，不发布；不建立 Provider 长期治理系统。
- 已证 Retention Missing、局部 Ordering、Sync Timeout 或数值问题，仅在影响能可靠定位、其它 Events 独立可靠时隔离。无法隔离则按对应整体停止/未就绪语义，不能一律归普通 FAILED。
- 同步只在有 Loading / Sync 明确证据时有限等待、可取消；不因空返回猜同步。取消/失败/未就绪等受限版本不能通过删除 Money 取得正式发布资格。
- 已取得的非 Money 结果和必要证据保留；不为补全原因执行已禁止的额外计算，不长期保存完整 Tick Stream。
- 旧 Version 不自动重算/补丁；Re-analyze 新 Version 使用同一固定 Task Scope，重取本次输入/Point/环境并保存。Single Test 不建长期 History。

## 4. Remaining Terminal Dependencies

**已取消：**所有 Terminal Profit Calculation / Money-only 上下文依赖。

**仍为条件候选：**真实历史 Bid/Ask、其时间/顺序/覆盖证明，以及 Manager 无法可靠提供时所需 Point / Quote 元数据来源。尚未证明这些能力必须由 Terminal 提供，也尚未证明可以全部移除 Terminal。

Manager-only 需要 MT4、MT5 分别通过；一方通过不代表另一方通过。若两平台均满足，才将 Manager-only 提为首选 Architecture Revision Proposal。Terminal Connection、实例/缓存、凭据、Running / Connected / Server Preflight 的删除仍是条件提案，当前不改架构正文或配置。

MT4 两种来源都具备合格历史能力时仍须同 Server / Symbol / 区间对照；不因同 Server 假定等价，不自动 fallback。不要求先证明所有 Terminal 路径失败才能开始优先验证 Manager。

## 5. Manager-Only Feasibility Requirements

1. MT4 Manager / MT5 Manager 各自提供可证明完整的真实成交候选；MT4 Partial Close 实际事件与成交量可靠。
2. Manager 提供同 Server、精确完整 Symbol 的真实历史 Bid/Ask；RAW / NORMAL / Server-filtered 数据语义明确，不伪造 Ask。
3. Timestamp 精度、单位、历史时间语义可验证；MT5 保留原始毫秒，秒级查询参数不意味着可截断返回 Tick / Deal 时间。
4. Ordering 足以证明 latest valid Tick；稳定数组顺序本身不算真实时间顺序证据。
5. Coverage / Retention / 查询完成与可用性可证明；只有进入有界同步等待时才要求经过验证的 Loading / Sync 状态，不从空返回推断同步。直接返回完整历史的接口不因没有 Loading 接口而失格。覆盖必要的 Target 前范围，不暗加最大 Quote Age；Provider变更不混入同 Version。
6. Trade / Quote / Point 均绑定同一真实 Trading Server；Profile Name / Login数字 / Endpoint相同不足以证明身份。
7. 官方 Point 能可靠取得并通过 Q05 Numeric Contract；最小元数据保留来源、获取时间、Symbol、实际值、单位、Server/Profile及必要适用性。Digits仅在实际需要时保留。
8. SDK Raw Encoding / Scaling / Sentinel / Precision 已验证；保留实际 Event Volume 精度，用于权重和 Volume 合计。
9. 每 Version 可追溯实际 Provider、查询范围、时间映射、Point、数据/规则/环境版本及异常证据；遵守原发布与历史冻结边界。

本地函数/字段声明不等于 Runtime Verified；本轮没有加载 DLL、发起真实查询或执行任何验证。

## 6. Remaining User Decisions — 仅 Q01 等待证据

| 编号 | 优先级与状态 | 真正剩余的决定 |
|---|---|---|
| Q01 | P0 / WAITING FOR TECHNICAL EVIDENCE | 经更新证据后最终确认 MT4 Quote Provider；Manager-only 是条件首选，不是已批准来源 |
| AR-01 | DECIDED / CLOSED | 不做跨 Symbol Points Ranking，采用 Symbol Overview / Symbol List；只允许已确认的事实列示与排序，不新增评分 |
| AR-02 | DECIDED / CLOSED | 已撤销 USD-only；Account Currency 仅可选原始 Metadata，不参与公式、Eligibility 或 FX |

AR-01、AR-02均已有明确用户决定，不再列为待答事项；权威记录见[AR-01](13-architecture-review-decisions.md#ar-01)与[AR-02](13-architecture-review-decisions.md#ar-02)。原 Q08 以非 USD 为由拒绝/跳过的前提已失效，其他身份和隔离规则不变。当前唯一 User Decision Required 为 Q01，须等待 Technical Evidence 完成后再由用户最终确认；现在停止业务提问。

### AR-01 — 已决定的依据：跨 Symbol Points 不构成共同尺度

合成反例（不是某个真实 Server 的运行结果）：

| Symbol | Event Volume | Price Markout | Point | VW Avg Points |
|---|---:|---:|---:|---:|
| A | 1 lot | +0.10 | 0.01 | +10 |
| B | 1 lot | +0.10 | 0.001 | +100 |

同一价格变化仅因 Point 尺度不同就相差10倍；Volume加权不能消除单位差异。不同Symbol的Lot也不是共同经济权重。因此不能将这种排序解释为跨品种风险、收益或经济影响大小。

依据：既有 [Points公式](03-markout-calculation-spec.md)；本地 [MT4 ConSymbol.point](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt4-manager-sdk/MT4ManagerAPI.h:559) 与 [MT5 Point文档](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtconsymbol_point.htm:74)。这是公式与字段定义推论，不声称已完成Server测试；不能从Digits推导来替代缺失的正式Point。

**已确认 — AR-01 DECIDED：**原 Symbol Ranking 改为 Symbol Overview / Symbol List。每个完整 Symbol 独立展示 Candidate Events、Eligible Events、Analysis Rate、Volume、Avg Points、VW Avg Points 和各关键 Markout Window；允许按 Symbol Name、Event Count、Volume、Analysis Rate 排序。Points 只表达该 Symbol 内的事实，不默认或暗示高 Points 代表跨品种高风险；不提供跨 Symbol Points Ranking。进入 Symbol 后继续其自身 Points Summary、Curve 与 Event Detail。

旧 Total Money 排名和曾拟议的跨 Symbol VW Points 排名均不再适用；不引入 Cross-symbol Points Score、Normalized Points、Risk Score、Synthetic Ranking Metric，也不引入金额或其他替代综合分。上述反例保留为已决定依据，不再作为待表决问题。

### AR-02 — 已撤销 USD-only 账户范围

**已确认 — AR-02 DECIDED：**V1 不再仅限 USD 账户，也不因 EUR、GBP、JPY、AUD 或其他账户币种拒绝分析。所有旧 USD-only 限制标 **SUPERSEDED BY V1 MONEY MARKOUT REMOVAL**，Q08 的非 USD 不支持/跳过问题为 **NO LONGER APPLICABLE TO V1**。Account Currency 可作为原始 Metadata 保存或展示，不参与公式或 Eligibility，不新增 FX、Profit Currency Logic 或 Money。

其他平台范围、真实 Manager 记录、Quote Contract、完整 Symbol、Point、真实 Event Volume、统一 Eligible 集合及版本发布/历史不可变边界全部保持。

## 7. Remaining Technical Evidence 与本轮验证

下一轮取证优先级：MT4 Manager Historical Bid/Ask → MT5 Manager Historical Bid/Ask → Timestamp Precision → Ordering → Coverage/Retention → Server Time → Exact Symbol → Point → MT4 Partial Close Actual Event Volume。Server Identity、候选读取完整性、Numeric Scaling / Sentinel / Precision 作为这些路径的交叉门槛保留。

Native Profit、Currency Conversion、Profit Currency、Money-only Account/Group Context 全部不再阻塞 V1。接口声明/静态文档与实际运行通过必须分层；现有 Windows 环境仅由用户确认存在，尚无本轮 Runtime Evidence。

当前计划见 [Technical Evidence README](../technical-evidence/README.md) 和 [最小验证计划](../technical-evidence/02-minimal-verification-protocol.md)。旧 Windows 准备指南因误嵌入文本及旧 Money 步骤停用，保留现有内容供追溯；它不是当前可执行指南。

本轮仅同步已决定的 AR-01 / AR-02 及评审/取证计划；未修改 Architecture / Calculation Specification 正文，未写应用代码、UI、正式 SQLite Schema，未修改 SDK / Terminal / 其它项目。下一阶段仍为 Technical Evidence Completion；Q01 唯一待证据，当前停止业务提问，不将本次文档同步当作运行取证已完成。

