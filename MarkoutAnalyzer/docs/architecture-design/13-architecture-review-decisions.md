# Architecture Review — Decision Log

阶段：Architecture Review业务问题收束 → Technical Evidence Completion。更新日期：2026-10-15。AR-01 / AR-02已DECIDED并CLOSED；当前唯一User Decision Required为Q01，等待技术证据。本轮记录决定并同步状态，未执行新的运行验证，未进入Implementation。

**当前有效决定：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)及其后续[AR-01](#ar-01) / [AR-02](#ar-02)。** V1不做跨Symbol Points Ranking，改为Symbol Overview / Symbol List；USD-only已明确撤销，Account Currency不参与公式或Eligibility。所有旧USD-only业务限制统一标记 **SUPERSEDED BY V1 MONEY MARKOUT REMOVAL**，包括D19、Q03范围说明、旧Q08及历史归并中的非USD跳过规则。历史原文保留，仅作决策追踪。

Architecture / Calculation Specification正文仍为待统一Revision的基线，不因本轮闭题自动改写；其中旧Money、Ranking、USD-only规则不能继续作为当前V1约束。其余已确认范围、数值、数据源、发布及历史冻结规则不变。

## 评审队列 — AR-01 / AR-02 闭合后当前状态

| 编号 | 当前状态 | 内容 |
|---|---|---|
| Q01 / P0 | USER DECISION REQUIRED — WAITING FOR TECHNICAL EVIDENCE | 最终MT4 Quote Provider；先完成证据和推荐，不提前提问或启用Provider |
| AR-01 / P1 | DECIDED / CLOSED | Symbol Overview / Symbol List，独立展示各Symbol统计，不做跨Symbol Points Ranking或合成分 |
| AR-02 / P0 | DECIDED / CLOSED | 撤销USD-only；币种只可作原始Metadata，公式/Eligibility不使用币种、不换汇 |

原Q01–Q25互斥计数：User Decision 1（Q01）；NO LONGER APPLICABLE TO V1 3（Q03/Q08/Q09）；Derived / Closed 8（Q05/Q11/Q12/Q13/Q15/Q16/Q17/Q19）；Technical Review 12（Q02/Q04/Q07/Q10/Q14/Q18/Q20/Q21/Q22/Q23/Q24/Q25）；UI/UX 1（Q06），共25题。新增AR-01/AR-02各有唯一DECIDED记录，另计2项CLOSED。当前剩余用户决定总数 **1**，没有新的待答业务问题。

Q08关闭的是“因为账户不是USD而跳过”规则，不是其他无效账户/事件的可靠隔离原则。Q13/24的Profit、FX及Money-only上下文等既有失效分支继续不适用；Point、真实Volume、Quote与可复现要求保持。Q23仍是条件Technical Review，不能提前认定所有Terminal依赖已消失。

**评审流程：**停止逐项业务提问；下一阶段Technical Evidence Completion。先核对[当前计划](../technical-evidence/README.md)，优先MT4 / MT5 Manager历史Bid/Ask，并完成时间精度、Ordering、Coverage/Retention、Server Time、Exact Symbol、Point、MT4 Partial Close真实量与交叉身份/数值证据。Money / FX / Profit Currency及USD-only不再是阻塞项。证据完成后先报告各来源结果、推荐与风险，再由用户决定Q01。

**提问前核验规则：**先查本Register最新有效决定及[09](09-open-questions.md)，已DECIDED / CLOSED / DERIVED内容不重问；旧OPEN / 待澄清 / 七类报告STOP只代表当轮历史。技术事实不能由用户投票替代。下一阶段不改业务规则、不改Architecture / Calculation Specification正文、不选UI技术栈、不开发正式接入/UI/SQLite，不在Q01确认前进入Implementation。

## Q03 — DECIDED：允许官方原生 Profit Calculation 内部换汇

<!-- AR-01-02 applicability -->
> **AR-02后续适用性：SUPERSEDED BY V1 MONEY MARKOUT REMOVAL。** 本节及后续历史记录中“仅USD / 不支持非USD”范围已由[AR-02](#ar-02)明确撤销；账户币种不是V1资格或计算前提，不能据旧原文拒绝非USD账户。
<!-- /AR-01-02 applicability -->

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**SUPERSEDED FOR V1：Q03的Native Profit / Currency Conversion / USD Money决定全部不再适用；原文保留作历史记录，不再要求其技术取证。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P0。
- Status：**DECIDED — 业务定义已确认**。
- User Choice：**B，以下列九项补充定义为完整决定**。
- Decision Source：用户在 Architecture Review 中对 Q03 的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**，尚未统一修改基线文档。
- Technical Verification：未由该决定自动通过；实际 SDK/Terminal 行为仍须据实记录。

### 已确认业务定义

1. **“不做 Currency Conversion”的准确含义**：Markout Analyzer V1 不自行实现 Currency Conversion Engine。不自行获取历史 FX Conversion Rate、不维护 Currency Conversion Pair、不推导 Conversion Path、不计算跨币种转换。
2. **允许官方内部转换**：允许 MT4/MT5 Terminal 或官方 SDK 的原生 Profit Calculation，在内部执行其自身必要的 Currency Conversion。
3. **Money Markout USD 的允许来源**：只要官方 Profit Calculation 能够针对 USD Trading Account 返回 Money Result，该结果允许作为 V1 Money Markout USD。
4. **区分金额结果与历史汇率精确还原**：如果官方 Profit Calculation 内部使用 Current Market FX Rate，结果可以展示为 Money Markout USD；但不得描述为“使用 Markout 时点历史 FX Rate 精确还原的 USD Profit”。
5. **不自行补做历史 FX 修正**：V1 不为修正上述时点差异而开发 Historical FX Conversion。
6. **Technical Evidence 要求**：若能从 SDK/Terminal 验证内部转换的实际行为，应记录是否发生 Currency Conversion、所用 Conversion Source，以及使用 Current Rate / Historical Rate / Unknown。不能验证的内容保持 Unknown，不能从返回 USD 这一事实推断内部汇率来源。该要求待统一修订时纳入 Technical Evidence；本次决定本身不是新的接口验证结果。
7. **历史版本冻结输出**：Historical Version 保存最终计算出的 Money Markout USD，不因以后汇率变化自动重算旧版本。
8. **重新分析独立成版**：Re-analyze 创建新 Version。SDK 后续若返回不同 Money Result，不覆盖旧 Version。
9. **Money Eligibility 不在本项决定**：某 Symbol 的官方 Profit Calculation 无法可靠返回 USD Result 时，该 Event 的 Money Markout 资格留给后续 Q09 单独确认。

### 决定范围与仍未决定事项

- Q03此前“禁止所有换汇，包括原生内部换汇”的建议未被采用，不能再作为当前业务要求。
- 当前仍限定 USD Trading Account；本决定不授权应用自行换汇，也不扩大为非USD账户支持。
- 本决定不保证所有 Symbol/所有 SDK 都能返回可靠 USD 结果，不替代 Q09 的 Event/指标/聚合资格决定。
- Q13 的规格快照时点、被分析账户与 Terminal/group 条件是否一致仍开放。
- 不在 Q03 另行选择公式 fallback、技术栈、Terminal 实例或数据来源，不改 Price/Points、报价完整性及既有版本隔离规则。
- 新版本 Money 与旧版不同可由原生环境变化造成；本决定不新增版本 Diff/Tolerance 功能。

### 待统一 Revision 的影响清单（本轮未修改）

| 文档 | 之后需同步的内容 |
|---|---|
| 01 Product Scope / D19 | 将“不做换汇”明确为“不自建转换引擎”，允许官方原生内部转换 |
| 02 Architecture / Money Boundary | 更新原生路径允许边界，不再以存在内部转换为自动不适用条件 |
| 03 Calculation Specification | 更新Money语义、内部转换证据与历史FX还原描述限制；保留Q09/Q13 |
| 04 Logical Model / 05 State Model | 在后续决定完整后统一落实Money证据与资格边界；本项不新增状态或Schema |
| 06 UX / 07 Wireframes / 08 Acceptance | 使用Money Markout USD称谓，禁止未经证明的历史FX精确还原声明；调整依赖Q03的条目 |
| 10 Assumptions / 11 Stack Options / 12 Technical Evidence | 区分允许的原生当前环境结果、尚未证明的实际能力和可核查转换信息 |
| README / 09 Questions | 统一修订时更新汇总及闭环状态；09已做最小状态标记以指向本决定 |

## Q01 — DECIDED：允许评审同 Server 的 MT4 Manager Quote Provider 候选

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**PARTIALLY SUPERSEDED：原第3条不预设优先级已调整为优先验证Manager-only、获证后提出首选方案；候选许可不等于最终批准。真实同源Bid/Ask、时间/覆盖/排序、必要双来源比较、无自动fallback及逐版来源不变。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P0。
- Status：**DECIDED — 候选来源及准确性边界已确认**。
- User Choice：**B，以下列十项补充边界为完整决定**。
- Decision Source：用户在 Architecture Review 中对 Q01 的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**，尚未统一修改基线文档。
- Technical Verification：**PENDING**；未认定任一候选已满足要求，未选定实际 Provider 或来源优先级。本轮未执行接口调用或 Quote Comparison Test。

### 已确认业务边界

1. **允许的候选范围**：允许与交易数据属于同一 MT4 Server 的官方 Manager API，作为 Historical Quote Source 的备选技术方案进入评审。
2. **授权限于能力验证**：选择 B 仅表示允许进行能力验证，不代表已经批准 Manager API 替代 MT4 Terminal。
3. **来源优先级未定**：不预设 Terminal 一定优先，也不预设 Manager API 一定优先；最终依据技术验证结果决定。
4. **任何来源必须满足同一准确性要求**：与交易数据属于同一 MT4 Server；完整 Symbol 精确匹配；能够取得真实历史 Bid/Ask；时间语义明确；历史覆盖范围可验证。不允许插值、模拟报价、MT5 报价替代、其他 Server 报价替代或相近 Symbol 替代。
5. **双来源能力均成立时必须比较**：若 Terminal 与 Manager API 都具备历史报价能力，必须进行同一 Server、同一 Symbol、同一时间段的 Quote Comparison Test，至少比较 Bid、Ask、Timestamp、Tick sequence / coverage 和 Historical depth。不能因为连接同一 Server 就默认两者完全等价。
6. **Quote Provider 抽象**：统一 Architecture Revision 时，应将 Historical Quote Source 抽象为 Quote Provider。MT4 候选可包括 MT4 Terminal Quote Provider 与 MT4 Manager Quote Provider；具体启用哪个必须经过 Technical Evidence 验证后决定。本轮只记录此要求，不修改架构正文或创建实现。
7. **Version 记录实际来源**：一个 Analysis Version 必须记录实际使用的 Quote Provider，例如 `Quote Source = MT4 Terminal` 或 `Quote Source = MT4 Manager API`，保证历史结果可追溯。
8. **禁止无提示混用**：同一个 Analysis Version 禁止无提示地混用多个 Quote Provider；不得在短窗口使用 Terminal、较长窗口缺数据时自动改用 Manager API。用户所举跨窗口 fallback 是禁止行为的示例，不改变 MT4 的既有时间精度和支持窗口规则。
9. **缺报价不能自动换源补齐**：选定 Quote Provider 无法提供某 Event 所需完整报价时，按既定缺失报价规则处理；不得为了补齐 Event 自动切换到另一个 Provider。
10. **双来源均不满足时保持不可用**：若最终验证 MT4 Terminal 和 MT4 Manager API 都无法满足真实历史 Bid/Ask 与时间要求，MT4 Markout 功能应保持不可用，不降低数据准确性标准。

### 决定范围与仍未决定事项

- Q01 的业务边界已决定；Terminal/Manager 的实际能力、比较结果、最终 Provider 选择与来源优先级仍待技术证据，不将 DECIDED 写成技术验证通过。
- 此前仅接受 MT4 Terminal 的评审基线已由本决定开放同 Server 官方 MT4 Manager 候选；没有开放第三方、跨 Server、跨平台、近似 Symbol 或模拟报价来源。
- 本决定不授权自动 fallback，也不因第 8 项使用“无提示”一词就认定提示后混用已获批准。
- MT4 仍为 SECOND 精度，100ms/500ms 仍不支持；不因跨窗口 fallback 示例而新增支持窗口。
- 既定报价完整性规则继续适用：任一支持窗口 NO_QUOTE/INVALID_QUOTE，整 Event 不进入 Summary，保留诊断。访问失败、查询未完成或覆盖未证实不能伪装为 NO_QUOTE；具体有效性、覆盖及失败边界仍按待决问题逐项确认。
- 本决定不替代 Q02 的时间语义确认、Q07 的顺序/覆盖确认或其它未决事项，不扩大本轮为接入、编码、技术栈选择或数据库实现。

### 待统一 Revision 的影响清单（本轮未修改）

| 文档 | 之后需同步的内容 |
|---|---|
| 01 Product Scope / 02 Architecture | MT4 候选来源边界、Quote Provider 抽象、无预定优先级与能力门槛 |
| 03 Calculation / 05 State Model | 固定实际来源、禁止自动跨 Provider 补齐、保持报价完整性与环境失败区分 |
| 04 Logical Model / 06 UX / 07 Wireframes | Analysis Version 记录并可追溯实际 Quote Provider；本轮不改 Schema 或界面 |
| 08 Acceptance / 12 Technical Evidence | 双来源具备能力时的同条件 Quote Comparison Test，以及双方均不达标时 MT4 不可用 |
| 10 Assumptions / 11 Stack Options / README / 09 Questions | 同步候选与实际启用的区别、剩余验证事项；09 本轮仅标记决定状态 |

## Q02 — DECIDED — A：允许经过验证的确定性时间映射

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**CURRENT APPLICABILITY — Money相关分支PARTIALLY SUPERSEDED：本节历史Money计算/指标/字段/资格/证据引用已不适用于V1；其余已确认的时间、匹配、统计Scope、隔离/发布、导出、重跑与历史冻结规则保持。具体范围见14影响矩阵，不能因删除Money扩大其它业务范围。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P0。
- Status：**DECIDED — A**。
- User Choice：**A，以下列十四项业务和数据完整性边界为完整决定**。
- Decision Source：用户在 Architecture Review 中对 Q02 的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**，尚未统一修改基线文档。
- Technical Verification：**PENDING**；本决定不证明任何实际接口的时间语义或某个历史期间的映射已验证，本轮未连接 Manager/Terminal 或实施时间转换。

### 已确认业务和数据完整性边界

1. **允许确定性映射**：允许经过技术证据验证的确定性时间映射，例如 UTC ↔ Server Time。只要能够证明两者表示同一个真实时刻，可以在内部统一时间基准后进行 Quote Matching。
2. **“不自动时区校正”的准确定义**：禁止猜测、推断或根据当前环境自动补偿未知时间差；不禁止经过验证的确定性时间表示转换。
3. **保持原始精度**：MT4 的 SECOND 转换后仍为 SECOND，禁止因为转换而产生伪造毫秒；MT5 的 TimeMsc 必须保留毫秒精度。
4. **保存原始时间及追溯信息**：至少在 Analysis Version / Technical Metadata 中能够追溯 Original Time、Original Time Semantics、Normalized Server Time、Time Mapping Rule / Evidence、Quote Provider 和 Server Identity。
5. **映射适用于实际历史日期**：时间映射必须针对实际分析的历史日期有效，禁止使用当前 UTC Offset 推断历史日期的 Server Time。
6. **历史 DST / Offset 变更须有证据**：若 Server 存在 DST 或 Historical Offset Change，必须能够证明分析期间对应的历史规则。历史上存在 UTC+2 / UTC+3 切换时，不能简单写死 `Server Time = UTC + 2`；这些数值是规则示例，不代表已确认任何实际 Server 的时区。
7. **绑定平台、连接及历史期间**：Time Mapping 应绑定 Platform、Connection Profile / Server、Historical Period；不得建立一个全局 Offset 并应用到所有 MT4/MT5 Server。
8. **禁止用价格走势反推时间**：不得通过比较 Trade 与 Tick 的价格走势反向猜测偏移。时间关系必须来自 API / SDK Semantics、Server Configuration、Official Documentation 或可重复验证的技术证据。
9. **无法唯一确定时停止受影响计算**：某段历史时间无法唯一确定时，不得继续使用猜测后的时间计算 Markout；受影响数据必须明确标记为时间映射失败，不得误归类为 NO_QUOTE。
10. **设计独立失败原因**：Architecture / State Model 应为时间映射无法确定的情况设计独立原因，例如 `TIME_MAPPING_UNRESOLVED`；最终命名由 Technical Design 统一。本轮记录此要求，不修改状态模型正文。
11. **匹配业务规则不变**：完成确定性时间归一化后，仍使用 `Quote(T) = latest valid Tick where tick_time <= target_time`。时间转换不能改变该规则。
12. **冻结旧版本的实际映射及结果**：历史 Analysis Version 必须保存当时实际使用的 Time Mapping 信息。之后 Server Offset、Terminal 设置或程序规则变化，不得自动改变旧 Version 的结果。
13. **重新分析独立成版**：Re-analyze 可以使用经过更新验证的 Time Mapping Rule，但必须生成新的 Version，不覆盖旧 Version。
14. **预检验证时间条件**：Preflight / Technical Validation 应验证 Trade Time Source、Quote Time Source、Time Semantics、Server Identity 和 Mapping Availability；不能只验证 Manager 与 Terminal“连接成功”。

### 决定范围与仍未决定事项

- Q02 允许的是经过证实、保持同一真实时刻的表示转换，不是允许猜测时差、价格对齐或依赖 Windows 本地时间匹配。
- 内部可统一时间基准；业务层的 Server Time 要求及已确认的原始时间精度保持不变。本决定不选择内部存储类型、技术栈或数据库结构。
- 具体接口时间契约、历史期间规则及映射唯一性仍需技术证据；不能把连接成功、当前 offset 或显示格式一致视作历史时间映射已通过验证。
- 时间映射失败必须有独立原因且不得继续受影响计算；该原因最终名称、在 Task / Account / Event / Window 各层的具体影响及其它数据是否继续，留后续失败边界与 Technical Design 统一，不在本项擅自指定。
- 第 11 项重申 latest valid 匹配规则，不修改 Quote Validity、同时间 Tick 顺序、覆盖证明或其它未决业务口径；Q04/Q05/Q07 的相关细节继续逐项评审，不把 Q02 扩展为它们已全部决定。
- Q01 的实际 Quote Provider 选择及优先级仍未决定；Q03 的 Money 定义与旧 Version 冻结要求不变。

### 待统一 Revision 的影响清单（本轮未修改）

| 文档 | 之后需同步的内容 |
|---|---|
| 01 Product Scope / 02 Architecture | 明确“不自动时区校正”的含义、确定性历史映射边界与 Preflight 验证要求 |
| 03 Calculation Specification | 保持来源精度，按经证实时间关系归一化，并维持 latest valid Tick 匹配规则 |
| 04 Logical Model / 05 State Model | Version 保存实际时间映射证据，设计独立时间映射失败原因；本轮不创建 Schema 或新增状态实现 |
| 06 UX / 07 Wireframes / 08 Acceptance | 时间映射失败明确展示、与 NO_QUOTE 区分；历史时区变化与旧 Version 冻结验收 |
| 10 Assumptions / 11 Stack Options / 12 Technical Evidence / README / 09 Questions | 同步已决定边界与仍待验证事实；09 本轮仅标记决定状态 |

## Q05（第一小项）— DECIDED — A：Bid / Ask 必须均大于零

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**PARTIALLY SUPERSEDED — 仅Money引用：本项Bid/Ask、Trade Price、真实Event Volume、Point及Numeric Validity等非Money规则继续有效；Money计算、汇总、状态、示例与资格引用不再适用。Q05六项业务保持CLOSED，不重复提问。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P0。
- Status：**DECIDED — A，本小项决定保留**；Q05六项现均已决定，整体为**CLOSED**，见第六小项后的闭合检查。
- User Choice：**A，以下列九项补充边界为完整决定**。
- Decision Source：用户在 Architecture Review 中对 Q05 第一小项的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**，尚未统一修改基线文档。
- Implementation：未开始；本轮仅记录决定，不实现校验器、诊断界面或 Quote Matching。

### 已确认业务边界

1. **统一正价必要条件**：V1 统一要求 `Bid > 0 AND Ask > 0`，才具备成为 Valid Quote 的资格。满足本条件不等于已经满足所有 Quote Validity 条件。
2. **任一侧非正即拒绝该 Tick**：Bid = 0、Ask = 0、Bid < 0、Ask < 0，出现任意一种情况，该 Tick 不能作为有效 Markout Quote。
3. **V1 不建立例外**：不建立 Negative Price Exception、Zero Price Exception、Symbol-specific Exception、Server-specific Exception 或 Historical-period Exception。
4. **业务范围与理由**：Markout Analyzer 当前是公司内部 MT4/MT5 风控小工具，服务现有 Broker Trading Symbols；不为理论上的特殊市场场景增加复杂 Quote Validity 配置体系。
5. **未来需求单独评审**：若未来确有业务需要支持合法 0 / Negative Quote 的交易品种，作为 Future Requirement 单独评审；V1 不预留复杂 Exception Framework。
6. **禁止修补以通过校验**：不得修改价格、取绝对值、替换为上一条价格、使用另一侧价格、插值或自动切换 Quote Provider，以让本来不合格的 Tick 通过校验。
7. **保留原始诊断证据**：Tick 因 Bid/Ask <= 0 被拒绝时，应保留原始 Bid/Ask 用于 Diagnostics，不修改原始数据。
8. **Window 最终结果留给 Q04**：该 Window 最终是 INVALID_QUOTE，还是因继续向前寻找其它有效 Tick 而成功匹配，不在本小项决定；按 Q04 / Quote Matching Validity 相关问题单独确认。
9. **本小项仅冻结正价条件**：只冻结“Bid / Ask 必须 > 0”；不同时决定 Ask >= Bid、Spread、Quote Age、Point、Trade Price 或 Volume。

### 决定范围与仍未决定事项

- 之前关于零价、负价按品种证据开放例外的 Codex 建议未被采用；不能作为 V1 要求或预留扩展框架继续设计。
- 拒绝一条 Tick 不等于已决定整个 Window 或 Event 的最终状态；Q04 的回溯及失败分类继续开放。
- “不得替换为上一条价格”禁止修补当前不合格 Tick；本小项不据此自动批准或禁止 Q04 中选择另一条完整真实 Tick 的匹配行为。
- Ask/Bid关系已由第二小项、数值有限性及缺失值已由第六小项确认；本项正价条件仍是必要条件，不能单独替代完整有效性契约。
- 不将 Bid/Ask 正价规则自动推广至 Trade Price、Point、Volume；不新增 Spread 或 Quote Age 阈值。
- 保留原始 Bid/Ask 的要求不自动扩展为保存完整 Tick Stream 或新建诊断存储架构；具体证据记录方式留统一 Revision。

### 待统一 Revision 的影响清单（本轮未修改）

| 文档 | 之后需同步的内容 |
|---|---|
| 01 Product Scope / 03 Calculation Specification | Bid/Ask 严格正价作为统一必要条件，V1 不建立零价/负价例外体系 |
| 02 Architecture / 10 Assumptions | 移除将特殊价格例外体系作为 V1 需求的假设，不为未批准场景预留复杂框架 |
| 04 Logical Model / 05 State Model / 06 UX / 07 Wireframes | 原始非正 Bid/Ask 的诊断证据；Tick 被拒绝与 Window 最终状态的关系仍待 Q04 |
| 08 Acceptance / README / 09 Questions | 覆盖任一侧为零或负数及禁止修补；第一小项原仅确认正价，当前六项业务决定齐备、Q05已关闭 |

## Q05（第二小项）— DECIDED — A：Ask >= Bid，允许零点差、拒绝倒挂

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**PARTIALLY SUPERSEDED — 仅Money引用：本项Bid/Ask、Trade Price、真实Event Volume、Point及Numeric Validity等非Money规则继续有效；Money计算、汇总、状态、示例与资格引用不再适用。Q05六项业务保持CLOSED，不重复提问。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P0。
- Status：**DECIDED — A，本小项决定保留**；Q05六项现均已决定，整体为**CLOSED**，见第六小项后的闭合检查。
- User Choice：**A，以下列十项补充边界为完整决定**。
- Decision Source：用户在 Architecture Review 中对 Q05 第二小项的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**，尚未统一修改基线文档。
- Implementation：未开始；本轮仅记录决定，不实现 Quote Validity、Quote Matching 或 Diagnostics。

### 已确认业务边界

1. **统一价格关系**：V1 Valid Quote 的价格关系必须满足 `Bid > 0 AND Ask > 0 AND Ask >= Bid`。这是本项价格关系校验，不等于完整 Quote Validity 定义已经全部决定。
2. **允许零点差**：Ask = Bid 允许；两侧仍须为正数。Zero Spread 本身不构成 INVALID_QUOTE。
3. **拒绝倒挂 Tick**：Ask < Bid 时，该 Tick 不具备 Valid Quote 资格；Crossed / Negative Spread Quote 不参与正常 Markout Quote Matching。
4. **V1 不建立倒挂例外**：不建立 Crossed Quote Exception、Symbol-specific Exception、Server-specific Exception 或 Historical-period Exception。
5. **禁止修复倒挂以通过校验**：不得交换 Bid/Ask、修改 Bid、修改 Ask、使用 Mid 替代、使用另一侧价格、插值或自动切换 Quote Provider，以修复 Ask < Bid。
6. **保留原始诊断证据**：被拒绝 Tick 的原始 Bid、Ask、Timestamp 必须保留用于 Diagnostics，不修改原始数据。
7. **不设置 Spread 门槛**：本题不设置 Maximum Spread、Minimum Positive Spread 或 Spread Deviation Threshold。只要 `Bid > 0 AND Ask > 0 AND Ask >= Bid`，就通过本项价格关系校验。
8. **不主观排除巨大正 Spread**：本题不为极大正 Spread 额外设置阈值；V1 不因 Spread“看起来异常大”而主观丢弃报价。
9. **回溯仍由 Q04 决定**：当前最近 Tick 无效后是否继续向历史方向寻找上一条有效 Tick，本题仍不决定，继续由 Q04 单独确认。
10. **Quote Age 规则不变**：Quote Age 只记录、不设置 STALE_QUOTE Threshold 的既有规则不变。

### 决定范围与仍未决定事项

- 本项接续 Q05 第一小项的正价必要条件，新增 Ask >= Bid 的价格关系约束；零点差不是零报价，不放开任何 Bid/Ask <= 0 的 Tick。
- Ask > Bid、Ask = Bid 的 Tick 仅通过本项价格关系校验；数值有限性、缺失字段、时间/来源/覆盖等其它条件仍须按相应已确认或后续获批规则判断。
- 不按极大正 Spread 主观丢弃，不代表已批准 NaN、Infinity、不可表示数值或算术溢出继续参与计算；这些技术有效性与失败边界不能由本项推导。
- 倒挂 Tick 不合格，不自动等于当前 Window 最终 INVALID_QUOTE、Event DISCARDED 或整个 Task 失败。Q04 的回溯与最终原因分类仍 OPEN。
- 保留被拒绝 Tick 的原始 Bid/Ask/Timestamp 不自动扩展为保存完整 Tick Stream；具体诊断证据记录方式留统一 Revision。
- 本决定不推广到 Trade Price、Point 或 Volume，不选择实现方式、技术栈或数据库结构。

### 待统一 Revision 的影响清单（本轮未修改）

| 文档 | 之后需同步的内容 |
|---|---|
| 01 Product Scope / 03 Calculation Specification | Ask >= Bid、正价零点差允许、倒挂拒绝；不设置 Spread 门槛或主观异常筛除 |
| 02 Architecture / 10 Assumptions | 不建立倒挂及按 Symbol / Server / 历史期间的例外体系，不设计价格修补路径 |
| 04 Logical Model / 05 State Model / 06 UX / 07 Wireframes | 保留被拒绝 Tick 原始 Bid/Ask/Timestamp；Window 最终状态仍待 Q04，Quote Age 规则不变 |
| 08 Acceptance / README / 09 Questions | 覆盖正价零点差、倒挂、极大正 Spread 与禁止修补；09 本轮仅更新子项状态 |

## Q04 — DECIDED — A：跳过无效 Raw Tick，匹配 latest valid Tick

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**CURRENT APPLICABILITY — Money相关分支PARTIALLY SUPERSEDED：本节历史Money计算/指标/字段/资格/证据引用已不适用于V1；其余已确认的时间、匹配、统计Scope、隔离/发布、导出、重跑与历史冻结规则保持。具体范围见14影响矩阵，不能因删除Money扩大其它业务范围。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P0。
- Status：**DECIDED — A，回溯匹配规则已确认**；本决定明确保留的后续错误分类仍 OPEN。
- User Choice：**A，以下列十四项补充边界为完整决定**。
- Decision Source：用户在 Architecture Review 中对 Q04 的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**，尚未统一修改基线文档。
- Implementation：未开始；本轮仅记录决定，未实现 Matcher、查询、诊断存储或测试程序。

### 已确认业务边界

1. **正式匹配定义保持不变**：`Quote(T) = latest valid Tick where tick_time <= Target Time`。
2. **latest 修饰 valid Tick**：寻找最新有效 Tick，不是直接采用 Raw latest Tick。
3. **跳过无效候选并继续回溯**：若 Target Time 之前最新 Raw Tick 不满足 Valid Quote 条件，跳过它，继续沿历史方向查找更早 Tick，直到找到 latest valid Tick。
4. **应用全部已确认有效性条件**：当前基础条件为 `Bid > 0 AND Ask > 0 AND Ask >= Bid`；后续确认的其它 Quote Validity 条件同样用于 valid Tick 判断。
5. **严格保持同源边界**：回溯必须限制在同一 Platform、Connection Profile、Quote Provider、Server、完整 Symbol。不得为了找到有效报价切换 Provider、Server、Symbol、去除 Symbol 后缀或使用其它平台报价。
6. **不得修补被跳过 Tick**：不得修改、修复、交换 Bid/Ask、插值、使用单侧价格或生成替代报价。
7. **使用选中 Tick 的完整原始记录**：匹配成功时，必须使用被选中 Valid Tick 自身的 Timestamp、Bid、Ask；Mid 按该 Tick 的 `(Bid + Ask) / 2` 计算。
8. **Age 根据选中 Tick 计算**：`Quote Age = Target Time - Selected Valid Tick Time`，不得使用被跳过 Invalid Tick 的时间。
9. **无最大回溯时间**：V1 不因 Quote Age 较大而自动停止寻找，不设置最大回溯时间；Quote Age 继续只记录，不设置 STALE_QUOTE Threshold。
10. **允许多个 Window 选中同一 Tick**：每个 Window 分别保存自己的 Target Time 和 Quote Age。
11. **诊断需说明是否跳过无效 Tick**：系统必须能够说明某 Window 在找到最终 Valid Tick 前是否跳过过 Invalid Tick；诊断记录至少包含被拒绝 Tick 的 Timestamp、Bid、Ask、Invalid Reason。保存全部被跳过 Tick 还是必要诊断记录，由 Technical Design 决定，但不得导致完整 Historical Tick Stream 被长期保存。
12. **全候选无效的状态仍未决定**：Target Time 之前存在 Tick，但回溯后没有任何 Tick 通过 Valid Quote 校验时，本题不决定最终 Window Status，留后续 Error Classification 确认。
13. **根本没有 Raw Tick 的分类仍未决定**：Target Time 之前根本没有 Raw Tick 时，本题不决定最终状态分类，留后续 NO_QUOTE / COVERAGE 相关问题。
14. **Event 报价完整性规则不变**：只有该平台所有 Required Windows 最终都成功匹配到 Valid Quote，Event 才具备进入 Markout Summary 的报价完整性条件。

### 决定范围与仍未决定事项

- 选择较早完整真实 Tick 不等于使用旧价修补较新的 Invalid Tick；不得混用不同 Tick 的 Timestamp/Bid/Ask，也不得保留无效候选的时间来计算 Age。
- 本决定没有新增回溯时长或 Age 限制，也没有证明任一 Provider 的历史深度、查询完整性或同时间 Tick 顺序。Q07/Q22 的覆盖、顺序与终止证据仍须确认；有限查询没有找到有效 Tick，不足以证明完整历史不存在有效 Tick。
- 时间映射失败、来源不一致、顺序不明、查询未完成或覆盖未证实，不属于可以按普通 Invalid Quote 跳过的候选；各自按对应已确认边界和后续分类处理。
- 第 14 项保持报价完整性作为进入 Summary 的必要条件；不能将“全部窗口报价有效”直接解释为 Money Eligibility、Point/Volume 或其它未决计算条件已通过。Q09 等问题继续 OPEN。
- 第 11 项明确将诊断记录保存量交由 Technical Design；保留必要拒绝证据，不据此扩展为长期保存完整历史 Tick Stream。
- Q04 的回溯匹配决定已关闭；第 12、13 项及相应错误分类、覆盖失败边界仍 OPEN，不将其默认为 INVALID_QUOTE、NO_QUOTE 或 Task 失败。

### 待统一 Revision 的影响清单（本轮未修改）

| 文档 | 之后需同步的内容 |
|---|---|
| 01 Product Scope / 03 Calculation Specification | latest valid 的回溯语义、同源边界、选中 Tick 自身 Timestamp/Bid/Ask/Mid/Age、无最大回溯时间 |
| 02 Architecture / 04 Logical Model | Matcher 与来源/时间/覆盖证据边界；Window 关联选中 Tick，并记录是否跳过及必要拒绝证据 |
| 05 State Model / 06 UX / 07 Wireframes | 区分候选被拒绝与匹配最终状态；保留诊断、Quote Age 与后续错误分类待决边界 |
| 08 Acceptance | 最新候选无效而较早有效、多 Window 复用同一有效 Tick、禁止跨源/修补、Age 来源与诊断边界 |
| 10 Assumptions / 12 Technical Evidence / README / 09 Questions | 同步已确认匹配规则与仍未验证的覆盖/顺序事实；09 本轮仅更新状态与决定引用 |

## 原保留事项 — 已纳入本轮 Consolidation

- Q04 后续 Error Classification：存在 Raw Tick 但全部不合格时的最终 Window Status；Q04 本次决定未涵盖。
- Q04 / Q07 / Q22 后续 NO_QUOTE / COVERAGE：根本不存在 Raw Tick、历史范围覆盖不足或查询未完成等情况的分类及失败边界；不能混为一类。
- Q05六个业务子项已全部决定，整体CLOSED，退出待问队列。Numeric Field不同不构成重问共同契约的理由。具体SDK映射/Scaling/数值类型/错误码等留Technical Design / Evidence；Q04残余分类及Version/Task可靠性的最终状态表达继续在各自评审项处理，不重新开放Q05。

## Q09 — DECIDED — A：正式 Summary 采用统一 Event Eligibility

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**SUPERSEDED — MONEY MARKOUT REMOVED FROM V1：旧Money资格决定整体失效；统一非Money Eligible集合和事实保留由新决定及14中的当前Eligibility Contract承接。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P0。
- Status：**DECIDED — A，Money Eligibility 已确认**；用户第15项原保留的Point / Volume / Trade Price业务资格及数值契约已由Q05六项决定闭合；具体技术字段、原因枚举和证据仍待相应设计/验证。
- User Choice：**A，以下列十五项补充边界为完整决定**。
- Decision Source：用户在 Architecture Review 中对 Q09 的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**，尚未统一修改基线文档。
- Implementation：未开始；本轮仅记录决定，未实现计算、状态、统计、导出、界面或数据存储。

### 已确认业务边界

1. **统一 Event Eligibility**：一个 Event 只有在所有 Required Markout Windows 都取得 Valid Quote、Points Markout 可以正常计算、所有 Required Windows 的 Money Markout USD 都可以可靠计算时，才具备进入正式 Summary 的资格。
2. **任一窗口 Money 失败即整 Event 排除**：任意 Required Window 的 Money Markout 无法可靠计算，整个 Event 不进入任何正式 Markout Summary。
3. **保留已取得的部分结果**：成功计算出的 Price Markout、Points Markout、Mid Markout、部分窗口已成功的 Money Markout 不删除，必须保留在 Deal / Event Detail、Diagnostics、Historical Version Data，用于核查。
4. **失败不能填零，可靠零值有效**：禁止用 0 USD 替换 Money Calculation Failure。正常、可靠计算得到的 0.00 USD 是有效 Money Result，与 Money Calculation Failed 不同。
5. **Points / Money Summary 使用相同集合**：必须使用同一组正式 Eligible Events，不允许 Points 使用 1000 Events、Money 使用 937 Events 这种不同样本的正式 Summary。
6. **Symbol Curve 使用相同集合**：Symbol Markout Curve 的 Volume Weighted Avg Points Markout 只使用正式 Eligible Events，确保 Points Curve、Points Summary、Money Summary 基于同一 Event Population。
7. **Account Summary 使用相同资格**：只聚合正式 Eligible Events。
8. **Multi-account Overview 使用相同资格**：只使用正式 Eligible Events 的 Money Markout。
9. **Analysis Rate 区分候选与完成分析**：区分 Total Candidate Events 与 Fully Analyzed / Summary Eligible Events。因 Money Calculation Failure 不能进入 Summary 的 Event 计入 Not Analyzed / Discarded，不计入 Analyzed。
10. **独立失败原因**：Money Calculation Failure 必须有独立、明确的 Discard / Diagnostic Reason，例如概念 `MONEY_CALCULATION_FAILED`；最终状态名称由 State/Error Model 统一。
11. **诊断必须定位原因与计算上下文**：Diagnostics 必须能看到失败的 Window、使用的 Money Calculation Path、Terminal / SDK 是否返回错误、Symbol Specification Snapshot、已取得的 Price / Points Markout；不得只显示 Event Discarded。
12. **Single Trade Test 保留可用信息**：即使 Money Calculation 失败，仍显示 Quote Matching、Price Markout、Points Markout、Mid Markout、Calculation Trace，并明确显示 `Money Markout = Calculation Failed`。不得因正式 Summary Eligibility 隐藏可用测试结果。
13. **导出保留部分结果与原因**：Discarded Event 中必须保留该 Event 的可用计算结果及 Money Failure Reason，供测试与人工复核。
14. **新版本可改变资格，旧版本不变**：后续 Re-analyze 若 Money Calculation 成功，新 Version 可以使该 Event 成为 Eligible；旧 Version 的状态保持不变。
15. **其它异常资格仍待决定**：本题不决定 Point 无效、Volume 无效、Trade Price 无效时的 Event Eligibility，继续由对应 Open Question 单独确认。

### 决定范围与仍未决定事项

- 正式汇总不开放 Points-only Event；Detail / Diagnostics / Historical Version Data / Discarded Event Export 中保留的部分结果不因此进入正式 Summary 或 Symbol Curve。
- 对应统计范围内，Points / Money / Symbol Curve 使用统一 Eligible Event 集合；不改变既有 Account / Symbol 分组规则，也不引入跨账户合并统计。
- 后续Q05第五小项已确认：因Point Failure未执行Money，不等于Money Calculation实际执行失败，不计Money Calculation Failure Count；此前已成功的Money结果保留成功事实，不改为失败、未计算或0。该执行边界不改变本项统一Summary Eligibility。
- Money 失败 Event 不计 Analyzed 已决定；Q11范围/筛选计数已由第一小项B确认，零候选Rate显示及原因区分已由第二小项A确认；多原因统计展示已由Q11第三小项A确认，Q11整体CLOSED。Q12 的候选归属与发布边界已另行 DECIDED — B，见本文件唯一 Q12 决定，不再列为待问事项。
- 本决定不扩大成“任何环境或技术故障都只丢弃 Event”。连接、时间映射、查询覆盖及 Task 级失败边界仍按对应决定或待决问题处理。
- Q03 已允许的原生内部换汇、Current FX、内部转换来源未能验证，本身不构成 Money 失败。正常返回的可靠 0 USD 有效；接口路径、输入条件与输出币种的可靠性仍需 Technical Evidence，不能以 Q09 的业务决定代替验证。
- Q13 的 Symbol Specification 时点及被分析账户/计算上下文一致性仍 OPEN；本项要求保存和展示 Snapshot，不证明它就是原生计算实际使用的规格。
- 不在本项新增通用 Money 公式、自动替代计算路径或系统级重试策略。
- Single Trade Test 显示可用结果已决定，但测试会话是否长期保存、FAILED/CANCELLED 版本的导出权限等仍按 Q17 单独确认。
- Q04的全候选无效/无Raw Tick后续错误分类仍OPEN；Q05输入资格及统一Numeric Validity业务契约已关闭，不因技术字段不同重复提问。

### 待统一 Revision 的影响清单（本轮未修改）

| 文档 | 之后需同步的内容 |
|---|---|
| 01 Product Scope / 03 Calculation Specification | 全 Required Windows 的报价、Points、Money 条件与统一 Event Eligibility；失败不可填零、可靠零值有效 |
| 02 Architecture / 04 Logical Model | 统一 Eligible 集合贯穿 Summary / Curve / Overview；部分结果、计算路径、规格快照及失败窗口证据随 Version 保留 |
| 05 State Model | Money 失败独立原因、Not Analyzed / Discarded 计数；不将其扩展为其它输入异常或 Task 失败的默认规则 |
| 06 UX / 07 Wireframes | Detail / Diagnostics / Single Trade Test 展示可用结果与失败细节；避免将失败金额显示为 0 |
| 08 Acceptance | 任一窗口 Money 失败排除整个 Event、统一样本集合、导出保留部分值、合法 0 USD、新版本可 Eligible 而旧版冻结 |
| 10 Assumptions / 12 Technical Evidence / README / 09 Questions | 同步 Money 资格与证据边界；09 本轮只标记决定及关联计数子项 |

## Q13（第一小项）— DECIDED — A：采用 Analysis-Time Symbol Specification

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**PARTIALLY SUPERSEDED：第3/4/14/15/17条Money专属内容、第6条Money-only强制规格及第9/13条Money分支不再适用。Analysis-Time Point、必要Digits/报价元数据、来源与获取时间、Server/Profile、逐版快照及禁猜原则继续有效。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P0。
- Status：**DECIDED — A，规格时点子项**；账户适用性决定后续记录于 Q13 第二小项，实际技术能力仍待验证。
- User Choice：**A，以下列十八项补充边界为完整决定**。
- Decision Source：用户在 Architecture Review 中对 Q13 第一小项的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**，尚未统一修改基线文档。
- Implementation / Technical Verification：本轮未取数、未连接 SDK/Terminal、未实现规格存储或计算；业务决定不代表任何实际计算路径已验证。

### 已确认业务边界

1. **采用分析时规格**：V1 使用 Analysis Time 获取并验证的 Symbol Specification，作为本次 Analysis Version 的计算规格。
2. **不要求成交时历史规格**：V1 不要求取得 Event Time Historical Symbol Specification。
3. **理论 Money 定义**：V1 Money Markout 是基于历史成交价格、历史 Markout Quote、Analysis-Time Symbol Specification 得到的理论 Money Markout USD；既有 Event Side / Volume 等计算输入规则不变。
4. **不得冒称历史规格精确还原**：不得将结果描述为“完全按照 Event 发生时历史 Symbol Specification 精确还原的 Money Markout”。
5. **Points 使用本版 Point**：Points Markout 使用本 Analysis Version 实际取得的 Symbol Point。
6. **保存规格快照**：每个 Analysis Version 必须保存实际使用的 Symbol Specification Snapshot，至少包括实际计算所需的 Symbol、Digits、Point、Contract Size、Tick Size、Tick Value、Profit Currency、Calculation Mode，以及实际 SDK/Terminal 可提供并参与计算的其它必要字段。原生内部参数与 Snapshot 的可证明关系按第 14 项准确描述。
7. **记录规格来源及身份**：Snapshot 必须记录 Specification Source、Specification Acquisition Time、Connection Profile、Server Identity。
8. **不同 Symbol 分别保存**：不同 Symbol 使用不同 Specification 时，必须分别保存各自快照。
9. **Re-analyze 重新取规格**：重新分析时取得当时实际适用的 Symbol Specification；规格变化后，新 Version（V2）允许产生与旧 Version（V1）不同的 Points / Money Markout。
10. **旧版不随新规格改变**：不得因为 V2 的规格变化而修改、覆盖或重算 V1。
11. **历史快照可查看**：Historical Version 必须能够查看当时实际使用的 Specification Snapshot，并按第 14 项明确其证据边界。
12. **必要规格不可得时禁止补猜**：Analysis Time 无法可靠取得计算所需的 Symbol Specification 时，不得猜测、使用硬编码默认值、复制相近 Symbol 的规格或从其它 Server 获取。
13. **按完整资格规则处理计算不可用**：上述情况下，受影响 Event 因 Points / Money 无法计算而失去 Summary Eligibility 的处理，按已经确认的完整 Event Eligibility 规则执行；不得生成不可靠的正式结果。
14. **原生参数证据边界**：若无法证明官方 Terminal / SDK Profit Calculation 内部实际使用的参数与保存 Snapshot 一致，Snapshot 只能表示“本次分析时获取到的 Symbol Specification”，不能宣称“这些参数就是 SDK 内部实际使用的全部参数”；Technical Evidence 必须明确这一点。
15. **记录实际计算路径**：Calculation Trace 记录实际 Money Calculation Path，例如 Native Terminal / SDK Profit Calculation，或 Specification-based Calculation，以解释结果来源。
16. **V1 不建设历史规格体系**：Future 可单独考虑 Historical Symbol Specification Support；V1 不建立 Historical Specification Database、Specification Timeline 或 Specification Version Reconstruction。
17. **Q03 的换汇边界不变**：允许官方 Profit Calculation 内部进行必要 Currency Conversion；Markout Analyzer 自身不实现 Historical FX Conversion。
18. **账户适用性另审**：Account / Group 与 Symbol Specification 是否必须严格对应，本题不决定，继续单独评审。

### 决定范围与仍未决定事项

- “Analysis-Time Specification”是计算口径；“采集到的快照”“应用明确使用的字段”和“官方原生内部采用的参数”需要按可得证据区分，不能仅凭有 Snapshot 就宣称原生内部所有参数已核实，或可按该快照离线精确复算。
- 第 14 项不把原生内部全量参数不可见自动判为 Money Calculation Failure；必要输入、币种、路径适用性与结果可靠性仍需相应技术证据，不能因此忽略第 12、13 项的必要规格缺失问题。
- 必要规格不可得而不能计算 Points / Money 时，遵守 Q09 的统一 Event Eligibility，保留已有结果与失败证据；不扩展成所有连接/服务器故障都只丢弃单个 Event。
- 本项不重新决定Point、Volume、Trade Price的校验与原因分类。相关业务资格、Point执行边界及统一数值契约后续已由Q05六项决定闭合；具体错误码与SDK能力证据不由业务决定自动通过。
- Calculation Path 示例用于实际路径留痕，不新增通用公式、自动 fallback 或未验证计算模式，也不替代实际 SDK 能力验证。
- Q13 第二小项的 Account / Group 适用性仍 OPEN；本决定不自动授权使用任意同 Server USD 账户的规格或计算环境代表目标账户。
- 按 Symbol 保存规格不意味着不同 Account / Group 可以无条件共用同名 Symbol 的快照；适用上下文以及是否可以共用仍需第二小项决定和后续技术证据。
- 本项只记录规格时点、证据和版本边界，不创建历史规格库、业务代码、数据库或架构实现。

### 待统一 Revision 的影响清单（本轮未修改）

| 文档 | 之后需同步的内容 |
|---|---|
| 01 Product Scope / 03 Calculation Specification | Analysis-Time Spec 的理论 Money / Points 口径，禁止历史规格精确还原声明与必要规格补猜 |
| 02 Architecture / 04 Logical Model | 按 Version / Symbol 保存规格及来源、获取时点、Profile、Server；不建设历史规格库或时间线 |
| 05 State Model / 06 UX / 07 Wireframes | 必要规格不可用时按统一资格处理、历史快照可查看、原生参数证据边界明确展示 |
| 08 Acceptance | Re-analyze 获取新规格可改变结果、旧版冻结；禁止默认值/近似 Symbol/跨 Server 补规格；记录实际计算路径 |
| 10 Assumptions / 12 Technical Evidence / README / 09 Questions | 区分实际采集字段与原生内部参数的可证明范围；09 仅标记本子项决定，账户适用性继续 OPEN |

## Q13（第二小项）— DECIDED — B：使用目标 Login 的实际计算上下文

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**PARTIALLY SUPERSEDED：原生Profit专用Account/Group Context及Money资格（尤其第8/11/12/13条）不再适用。只保留Point/Quote实际需要且经证据确认的目标上下文与字段；可证公共Server/Symbol数据可复用，不建立Profit上下文系统，不假设所有字段必然公共。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P0。
- Status：**DECIDED — B**。Q13 的两个业务子项均已决定：规格时点选 A，账户上下文选 B；**实际能力与证据验证仍 PENDING**。
- User Choice：**B，以下列十六项补充边界为完整决定**。
- Decision Source：用户在 Architecture Review 中对 Q13 第二小项的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**，尚未统一修改基线文档。
- Implementation：本轮仅记录决定，未连接或修改 Terminal、Manager、账户、Group、缓存或数据库。

### 已确认业务边界

1. **不接受等价账户替代**：V1 不使用“经过验证的等价账户 / Group”替代目标交易账户的实际计算上下文。
2. **明确适用目标 Login 的当前上下文**：对每个被分析 Login，Symbol Specification 与 Native Profit Calculation Context 必须明确适用于该目标 Login 及其 Analysis Time 实际所属 Group / Trading Context。
3. **相同属性不能授权替代**：不得仅因 Same Server、Same Symbol、Same Currency、Same Leverage、Same Account Type、Same Contract Size 或 Sample Calculation Result Same，就使用另一个 Login / Group 作为正式计算上下文。
4. **V1 不建设等价体系**：不建立 Account Equivalence Engine、Group Equivalence Engine、Specification Equivalence Rules 或 Cross-Group Validation Matrix。
5. **业务取舍**：作为内部小工具，V1 优先保证 Correctness、Traceability、Simple Calculation Boundary，不以最大化 Money Calculation Coverage 为目标。
6. **Event 上下文可追溯**：每个 Event 的计算上下文必须可追溯到 Target Login、Target Login Group / Trading Context、Connection Profile、Server、Symbol。
7. **保存分析时账户上下文**：Analysis Version 保存分析时取得的必要 Account / Group Context；具体字段由 Technical Design 根据 MT4/MT5 SDK 能力确定。
8. **多 Login 不默认共用计算环境**：同一个 Task 中多个 Login 即使交易同一个 Symbol，也不得默认共用某一个账户的 Profit Calculation Context。
9. **公共数据与账户相关参数分别处理**：经技术确认属于 Server / Symbol 级公共数据的 Symbol 基础市场数据，可以安全缓存复用；任何可能受到 Account / Group 影响的计算参数，必须按目标账户实际上下文处理。
10. **架构区分两类上下文**：统一 Revision 时区分 Server/Symbol-level Specification 与 Account/Group-dependent Calculation Context，不假设所有 Symbol Specification 字段全服务器统一。本轮只记录此架构要求。
11. **原生目标适用性不可证明则不得正式纳入**：若 Native Profit Calculation 无法明确针对目标 Login / Group 执行，且无法证明结果就是目标账户实际适用的计算结果，不得将其 Money Markout 作为正式 Summary Eligible Result。
12. **应用 Q09 统一资格规则**：上述情况下，Money 无法可靠计算，整个 Event 不进入正式 Summary；已取得的 Quote / Price / Points 等结果仍保留用于 Diagnostics，并继续遵守 Q09 的其它保留要求。
13. **禁止为覆盖率替换上下文**：不得为提高 Coverage 而自动选择另一个 Login、自动切换 Group、使用默认测试账户、管理员账户的计算环境或另一个 Terminal Account，生成目标账户的正式 Money Markout。
14. **等价上下文作为未来需求**：未来若业务证明某些 Server / Group 的计算环境完全统一，可在 Future Version 单独评审 Equivalent Calculation Context；V1 不实现。
15. **报价缓存不受账户替代禁令一并禁止**：允许 Quote Tick Cache 在同一 Profile / Server / Symbol 下安全复用；Quote Source 与 Account Profit Calculation Context 是不同概念。此许可继续受 Q01/Q04 已确认的 Platform / Quote Provider 等来源隔离边界约束，不授权混用来源。
16. **新版本重新取得目标上下文**：Re-analyze 必须重新取得目标 Login 在新 Analysis Version 中实际适用的 Account / Group Context；Group 或相关规格变化时，新 Version 可以产生不同结果，不修改旧 Version。

### 决定范围与仍未决定事项

- 上一轮推荐允许等价替代的选项 A 未被采用，不作为 V1 的设计要求或预留等价框架；不能以样本相同推导正式计算适用性。
- 使用目标账户实际适用的上下文，不等于本轮已证明任何 SDK/Terminal 接口具备该能力。实际接口契约、输入及上下文绑定仍需 Technical Evidence。
- 本决定不要求 SDK 揭示全部内部参数，也不撤销 Q13 第一小项关于“采集到的 Snapshot 不等于全部原生内部参数”的披露边界；但不能以内部不透明为由使用其它账户的环境替代目标上下文。
- “管理员账户的计算环境不能替代目标上下文”与 Manager 连接身份是不同问题；本决定未选择凭据、连接或 Terminal 实例方案，也未授权修改账户/Group、切换 Terminal 登录或新增实例。
- 公共数据可缓存的前提必须被技术确认，不能预先将所有 Specification 字段认定为公共；共享 Quote Cache 不代表共享 Profit Calculation Context。
- 账户上下文绑定 Analysis Time，不因采用历史成交数据而自动新增历史 Group 重建要求。旧 Version 保留当时上下文及结果，新版重新获取不改旧版。
- Q03 对官方原生内部必要换汇的许可不变；未知内部 FX 来源本身不能被自动归为目标账户上下文不适用。
- Q09 的 Event 资格与结果保留规则继续适用；本决定不将其它查询/连接/时间/任务故障一律归为单 Event 的 Money Failure。

### 待统一 Revision 的影响清单（本轮未修改）

| 文档 | 之后需同步的内容 |
|---|---|
| 01 Product Scope / 03 Calculation Specification | 目标 Login / Analysis-Time Group 的实际计算上下文；拒绝等价账户替代，不以覆盖率放宽边界 |
| 02 Architecture / 04 Logical Model | 区分公共 Server/Symbol 规格与 Account/Group 相关计算上下文；Event / Version 保存身份与必要上下文；安全缓存不跨来源边界 |
| 05 State Model / 06 UX / 07 Wireframes | 原生目标适用性不可证明时按 Q09 处理；保留可用结果和上下文证据，历史可追溯 |
| 08 Acceptance | 多 Login 同 Symbol 不默认共用账户环境、禁止替代上下文、公共缓存条件、Re-analyze 重新取上下文且旧版冻结 |
| 10 Assumptions / 12 Technical Evidence / README / 09 Questions | 不建设等价引擎/矩阵，区分业务决定与实际 SDK 能力；09 标记 Q13 两项决定并保留技术验证待办 |

## Q07（第一小项）— DECIDED — A：可靠界定影响时隔离受影响 Event

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**CURRENT APPLICABILITY — Money相关分支PARTIALLY SUPERSEDED：本节历史Money计算/指标/字段/资格/证据引用已不适用于V1；其余已确认的时间、匹配、统计Scope、隔离/发布、导出、重跑与历史冻结规则保持。具体范围见14影响矩阵，不能因删除Money扩大其它业务范围。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P0。
- Status：**DECIDED — A，仅局部 Ordering Ambiguity 子项**；Q07 整体仍为 **PARTIALLY DECIDED**。
- User Choice：**A，以下列二十项补充边界为完整决定**。
- Decision Source：用户在 Architecture Review 中对 Q07 第一小项的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**，尚未统一修改基线文档。
- Implementation / Technical Verification：本轮仅记录决定，不实现匹配、状态或统计，不连接 Provider，不把任何实际 Ordering Metadata 视为已验证。

### 已确认业务边界

1. **可可靠定位时局部隔离**：Quote Ordering Ambiguity 能够明确定位到具体 Event / Window 时，只隔离受影响 Event，不自动升级为整个 Task Failure。
2. **典型歧义场景**：同一 Target Time 前有多条 Timestamp 相同、Bid/Ask 不同且均满足 Valid Quote 条件的 Tick，Provider 没有足够信息证明其先后顺序，且该歧义实际影响窗口最终报价选择。
3. **禁止任取 Tick**：无法确定顺序时，不得任意选择其中一条 Tick。
4. **禁止猜测或绕过**：不得按返回数组顺序猜测、按 Bid/Ask 大小排序、按数据库读取顺序或内存顺序决定、随机选择、按对客户更有利或更不利选择、跳过这些有效但顺序不明的 Tick 去找更早 Tick，或切换 Quote Provider。
5. **可使用经验证的顺序证据**：Provider 的 Sequence、Tick ID、Monotonic Sequence 或其它 Ordering Metadata，只有在 Technical Evidence 已证明字段语义、能够证明顺序时，才可用于确定先后关系。
6. **稳定返回不等于真实时序**：API 返回顺序稳定，本身不足以证明真实 Tick Chronological Order。
7. **独立 Window 失败原因**：无法确定顺序时，受影响 Window 进入独立失败状态；概念可用 `QUOTE_ORDER_UNRESOLVED`，最终名称由 State / Error Model 统一。
8. **Required Window 失败影响整个 Event 资格**：任一 Required Window 出现上述 Ordering Failure，整个 Event 不具备 Summary Eligibility。
9. **所有正式聚合一致排除**：该 Event 不进入 Points Summary、Money Summary、Symbol Markout Curve、Account Summary、Multi-account Overview 聚合。
10. **保留原始事件与冲突证据**：不得删除该 Event；保留 Trade/Event Data、Target Time、冲突 Tick、Timestamp、Bid、Ask、Quote Provider、Failure Reason，用于 Diagnostics。
11. **不受影响 Event 继续**：能够证明不受该歧义影响的其它 Events 正常继续分析，满足完整 Eligibility 后进入 Summary。
12. **保留任务/版本质量计数**：Task / Version 继续生成 Total Candidate Events、Eligible Events、Discarded Events、Analysis Rate，让用户看到局部数据损失。
13. **按原因统计并支持下钻**：Diagnostics 可统计 QUOTE_ORDER_UNRESOLVED Event Count，并下钻至具体 Login、Deal/Ticket、Symbol、Window、Target Time、冲突 Tick。
14. **多窗口记录、Event 去重计数**：同组顺序不明 Tick 影响多个 Markout Window 时，记录所有受影响 Window；该 Event 仍只作为一个 Discarded Event 统计。
15. **跨账户复用不自动扩大为 Task 失败**：同组歧义 Tick 影响多个 Accounts 时，每个受影响 Event 独立判定；不因复用同一 Quote Data 就自动令整个 Task Failed。
16. **影响范围可靠是必要前提**：只有影响范围能够可靠界定，才可使用本题的局部隔离规则。
17. **影响不明/Provider 不可信另行升级**：影响范围无法确定，或有证据表明 Provider 整体 Ordering Semantics 不可靠时，不能套用局部隔离，必须升级为 Provider / Task Level Reliability 问题，由 Q07 后续小项决定。
18. **准确性不能为覆盖率让步**：不得为提高 Analysis Rate 降低 Ordering Correctness 要求。
19. **旧版保存真实失败事实**：Historical Version 保存当时实际发生的 Ordering Failure 及被排除的 Event。
20. **新证据仅影响新版本**：以后 Provider 能提供足够 Ordering Evidence 时，Re-analyze 的新 Version 可以使这些 Event 成为 Eligible；旧 Version 保持不变，且新版本仍须满足其它已确认 Eligibility 条件。

### 决定范围与仍未决定事项

- 顺序歧义必须实际影响某 Required Window 的最终选择；不是历史中任意位置出现重复时间戳就排除 Event，也不单凭重复时间戳认定 Provider 整体失败。
- 原始 Tick 可以同时满足价格有效性而存在顺序歧义；不能把它当作 Q04 可跳过的 Invalid Tick，也不能将此独立 Ordering Failure 混成 NO_QUOTE 或 Bid/Ask 无效。
- 可用的 Sequence / Tick ID 需要证明实际顺序语义与适用范围；字段存在、数值递增或少数调用稳定，本身不等于任何真实 Provider 已通过验证。本决定不伪造顺序或给原始 Tick 新增推测时序。
- 共享 Quote Data 时，需识别所有确受影响 Event / Window；不能仅隔离最先发现问题的一条 Event，也不能假定不同 Login 自动不受影响。
- 统计按 Event 去重的要求已确认；Q11筛选计数已由第一小项B确认，零候选Rate显示及原因区分已由第二小项A确认；多原因统计展示已由Q11第三小项A确认，Q11整体CLOSED。Q12 时间归属不明时的候选计数与发布边界已另行 DECIDED — B，见本文件唯一 Q12 决定。
- 必要冲突 Tick 与拒绝原因是诊断证据，不将其扩大为长期保存完整 Historical Tick Stream；旧 Version 的失败事实不因新证据被自动改写。
- 本小项不决定影响无法界定或 Provider 整体不可靠时的 Task 终态、Provider 禁用/恢复范围；Q07 下一小项必须单独确认。
- 历史覆盖证明、查询未完成、同时间同价等其它边界仍须相应技术证据或后续评审，不以本次业务选择宣称完整 Quote Provider 契约已闭合。

### 待统一 Revision 的影响清单（本轮未修改）

| 文档 | 之后需同步的内容 |
|---|---|
| 01 Product Scope / 03 Calculation Specification | 经证实的 Ordering Metadata、禁止猜测与绕过、影响可界定才局部隔离 |
| 02 Architecture / 04 Logical Model | 共享 Quote Data 的影响定位，Window 与冲突 Tick 证据、Event 身份和版本记录关联 |
| 05 State Model | 独立 Ordering Window Failure、整 Event 全部正式聚合排除、多窗只计一个 Discarded Event；整体可靠性失败仍待后续 |
| 06 UX / 07 Wireframes | 展示候选/Eligible/Discarded/Rate、Ordering Event Count，并下钻到 Login/Deal/Ticket/Symbol/Window/Target/冲突 Tick |
| 08 Acceptance | 局部与跨账户影响隔离、稳定数组顺序非证据、禁止更早 Tick 绕过、旧版失败冻结及新版可重新 Eligible |
| 10 Assumptions / 12 Technical Evidence / README / 09 Questions | 区分业务规则与实际 Ordering 证据，09 仅更新子项状态及关联计数项 |

## Q07（第二小项）— DECIDED — B：本次运行失败，后续任务按适用范围验证

- Priority：P0。
- Status：**DECIDED — B**。Q07 第一小项 A 与第二小项 B 均已决定；历史覆盖等剩余边界尚未关闭，Q07 整体仍为 **PARTIALLY DECIDED**。
- User Choice：**B，以下列二十项补充边界为完整决定**。
- Decision Source：用户在 Architecture Review 中对 Q07 第二小项的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**，尚未统一修改基线文档。
- Implementation / Technical Verification：本轮仅记录决定，未连接或验证实际 Provider，未实现状态、验证机制或证据缓存。

### 已确认业务边界

1. **无法界定影响或本次顺序语义不可靠时运行失败**：Quote Ordering 的影响范围无法可靠界定，或已有证据表明当前 Quote Provider 的 Ordering Semantics 对本次分析不可靠时，当前 Analysis Run 必须 FAILED。
2. **本次版本不发布正式聚合结果**：本次 Version 不发布任何正式 Account Summary、Symbol Summary、Symbol Markout Curve、Multi-account Overview Markout Result。
3. **保存失败证据与来源上下文**：必须保留 Failure Evidence、Quote Provider、Platform、Profile、Server、Symbol / Historical Range（如果能够定位）、Ordering Problem、Technical Diagnostics；不能把无法定位的范围伪装成已知。
4. **不建设跨任务 Provider 治理体系**：V1 不建立 Provider Blocklist、Provider Reliability Registry、Disabled Provider State、Manual Unblock Workflow 或 Reliability Approval Workflow。
5. **后续任务允许再次使用同一来源但须验证**：后续 Task 可以再次使用同一 Quote Provider，但必须重新通过该 Task 所需的 Preflight、Quote Ordering Validation 及其它必要 Technical Validation。
6. **连接成功不等于顺序可靠**：“重新验证”不能仅指 Connection Successful 或 Terminal / Manager Reconnected；连接成功不能证明 Ordering Reliability。
7. **历史失败证据持续保留**：已存在的历史 Failure Evidence 不得因新 Task 创建、应用重启或 Reconnect 被删除或改写。
8. **后续任务证据不足仍失败**：后续 Task 若无法取得足够证据证明本次分析范围内的 Quote Ordering 可靠，仍必须 FAILED。
9. **暂未观察到异常不证明恢复**：不得因上次失败而此次没有立即观察到异常，就自动认为 Provider 已恢复可靠。
10. **验证方法由实际能力决定**：Technical Design 根据实际 Provider 能力设计 Quote Ordering Validation；可依赖 Sequence、Tick ID、Documented Ordering Contract、可重复技术验证，但不能依赖未经证明的数组返回顺序。
11. **当前范围通过验证即可正常分析**：若本次验证能够证明当前任务涉及的 Server / Symbol / Historical Range Ordering 可靠，允许该 Task 正常分析，并继续满足其它已确认条件。
12. **证据范围匹配任务需要**：不要求证明 Provider 在所有 Server、所有 Symbol、所有历史期间永久可靠；Validation Scope 应与本次 Task 实际需要相匹配。
13. **允许复用仍然适用的确定性证据**：对于通过稳定官方接口契约证明的确定性 Ordering 能力，Technical Design 可以缓存/复用技术证据，不要求每次重新进行昂贵的数据实验；必须确认该 Evidence 仍适用于当前 Platform、Provider、Server、API/SDK Version、Historical Data Path。
14. **版本或关键路径变化须重新判断证据适用性**：Provider / SDK Version 或关键数据路径发生变化时，之前的 Ordering Evidence 不应自动视为继续有效。
15. **目标是每个版本的数据路径可证明正确**：V1 要求每个 Analysis Version 能够证明其使用的数据路径满足正确性要求，而不是建立长期 Provider Governance System。
16. **任务失败与局部丢弃分层**：当前 Task 因 Ordering Reliability Failed 时，Task Status = FAILED；这与几个 Event 因 QUOTE_ORDER_UNRESOLVED 被 Discarded 属于不同层级。
17. **局部隔离规则继续有效**：Q07 第一小项继续适用；影响范围能够可靠界定时，只 Discard 受影响 Events，Task 可以继续。
18. **本类问题升级为任务失败的条件**：只有影响范围无法可靠界定，或 Provider Ordering 对本次分析整体无法证明可靠时，才因该 Ordering 问题升级为 Task FAILED；不能借此把可可靠隔离的局部问题一律扩大为 Task Failure。
19. **禁止换源绕过失败**：不得自动切换 Provider、自动切换 Server、自动换 Symbol 或使用其它平台报价，绕过 Task Failure。
20. **重新分析独立成版并重新满足验证要求**：Re-analyze 创建新 Version，新 Version 必须重新满足当前有效的 Ordering Validation；旧 Version 的 Failed 状态和证据保持不变。

### 决定范围与仍未决定事项

- 前一轮选项 A 所提持续、跨任务的 Provider 不可信阻断标记未被采用；不能在 V1 另建封禁、人工解封或可靠性审批流程。
- “每个任务重新通过验证”不等于每次重新做全量实验，也不等于仅复用上次成功状态。允许复用的是经确认仍适用于本次数据路径与范围的技术证据；不能只因 SDK 名称相同、连接成功或未立即出现异常而视为可靠。
- 禁止跨任务治理体系，不禁止保存历史失败证据或缓存适用技术证据；两者不能混为 Provider Blocklist。证据组织方式与适用性验证方法留待 Technical Design。
- 本项将当前 Run 记为 FAILED，不以把全部 Event 标为 Discarded 的方式伪装成正常完成，也不发布该失败版本的部分正式 Summary。既有其它历史 Version 不被改写。
- 第一小项与第二小项共同界定局部/任务级 Ordering Failure；本决定不把任何重复 Timestamp 都视为任务失败，不降低 Quote Validity、Quote Matching 或 Event Eligibility 要求。
- Q07 历史覆盖证明、Q22 未来 Target / 历史保留 / 同步等待，以及其它尚未决定的错误分类继续开放。本项不自动把所有覆盖问题套用 Ordering Failure，也不宣称任何实际 Provider 的能力已经验证通过。

### 待统一 Revision 的影响清单（本轮未修改）

| 文档 | 之后需同步的内容 |
|---|---|
| 01 Product Scope / 02 Architecture | 本次数据路径可靠性边界；不建设长期 Provider 治理体系；按任务范围验证及适用证据复用 |
| 03 Calculation Specification / 05 State Model | 可界定影响时局部隔离；不可界定或本次整体不可靠时 Run FAILED，不发布本次正式聚合 |
| 04 Logical Model / 12 Technical Evidence | 保存失败事实、来源与可定位范围；证据适用 Platform/Provider/Server/API-SDK Version/Historical Data Path；旧版冻结 |
| 06 UX / 07 Wireframes | 明确任务失败、具体原因及 Diagnostics；不把重连显示为 Ordering Reliability 恢复，不引入解封/审批操作 |
| 08 Acceptance | 局部/任务级失败区分、失败版无正式结果、无换源绕过；新任务证据适用性验证，变化后不自动沿用证据 |
| 10 Assumptions / README / 09 Questions | 区分已决定业务边界与待验证技术能力；09 仅更新子项状态，历史覆盖问题继续 OPEN |

## Q22（第一小项）— DECIDED — A：Future Target 不等待，以独立未就绪语义结束

- Priority：P0。
- Status：**DECIDED — A，包含用户对状态语义的修正**；Q22 整体仍为 **PARTIALLY DECIDED**。
- User Choice：**A，以下列十八项补充边界为完整决定**。前一轮选项 A 所提“普通 FAILED”明确未被采用。
- Decision Source：用户在 Architecture Review 中对 Q22 第一小项的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**，未统一修改 Architecture、Calculation Specification 或 State Model 基线。
- Implementation / Technical Verification：仅记录业务决定与状态层级建议，未实现时间判断、状态、等待或重跑机制，未接入任何 Provider。

### 已确认业务边界

1. **V1 不等待 Future Target**：只要任意 Candidate Event 的 Required Target Time 尚未到达，当前 Analysis Run 不继续等待，以本项独立的时间条件未成熟语义结束。
2. **禁止提前计算或绕过不成熟事件**：不得提前使用当前最新 Quote，不得使用 Target Time 之前“现在能拿到的最后报价”代替未来报价，不得缩短 180s 等 Required Window，不得将 Future Target 记为 NO_QUOTE 或 INVALID_QUOTE，也不得跳过该 Event 后继续发布正式 Summary。
3. **重新分析创建新版本**：用户稍后可以 Re-analyze；创建新的 Version，不覆盖本次记录。
4. **不归入普通失败**：“Required Target Time 尚未到达”属于分析条件尚未成熟，不是 Manager Failure、Terminal Failure、Quote Failure、Calculation Failure 或 SQLite Failure，不直接归类为普通 FAILED。
5. **建立独立状态语义**：State Model 应设计独立语义，可考虑 NOT_READY、TARGET_NOT_REACHED 或 INCOMPLETE_TIME_HORIZON；具体最终命名由 State / Error Model Review 决定。
6. **与真正 FAILED 明确区分**：不能仅用普通 FAILED 掩盖此未就绪结果；历史记录和用户反馈必须能够区分两者。
7. **本次不发布正式结果**：本次 Version 不发布正式 Multi-account Overview Markout Result、Account Summary、Symbol Summary 或 Symbol Markout Curve。
8. **指出导致时间窗口未成熟的 Event**：至少显示 Login、Deal ID / Ticket、Event Time、Latest Required Window、Required Target Time，让用户知道具体哪个 Event 导致条件未成熟。
9. **多事件影响与任务最晚目标**：多个 Event 尚未成熟时，应能够显示 Affected Event Count，以及整个任务需要达到的 Latest Required Target Time。
10. **给出明确时间提示**：UI 可以提示“Analysis horizon not reached. Required through: <Server Time>”；最终文案由 UX Design 决定，不在本题固定全部界面文字。
11. **按已确认时间语义判断**：成熟判断必须使用已确认的统一时间语义，不得使用 Windows Local Time 猜测。
12. **已到时但历史数据未提供属于另一问题**：所有 Required Target Time 已到达，但 Quote Provider 尚未提供对应历史数据时，不属于本项 TARGET_NOT_REACHED；这是 Historical Quote Coverage / Synchronization 问题，后续单独确认。
13. **成熟历史范围不受本规则影响**：完全历史时间范围且所有 Required Targets 已到达时，不因本规则阻止分析；仍须满足其它已确认条件。
14. **不增加自动等待或重跑机制**：V1 不增加 Background Wait、Scheduled Resume、Automatic Re-run 或 Timer-based Retry。
15. **已结束版本不能再取消**：已经因 TARGET_NOT_REACHED 结束的 Version 不适用 Cancel；不将其视为仍在运行或等待中的版本。
16. **后续动作由用户主动发起**：用户稍后主动 Re-analyze 即可，不要求恢复本次 Version。
17. **Event Completeness 不变**：只有所有 Required Windows 真正成熟并成功计算，Event 才可能成为 Summary Eligible；本题不放宽 Q09 等完整资格要求。
18. **状态所属层级留技术建模评审**：Architecture Review 应判断该语义最适合 Task Status、Analysis Version Status 或 Precondition Result 中的哪一层；可以提出 Recommendation，最终建模与命名待 State / Error Model Review，不能与普通 FAILED 混为一谈。

### 状态层级建议（RECOMMENDATION，尚未作为最终状态模型采用）

建议以 **Analysis Version 的独立已结束状态/结果** 作为持久化事实，配合 **Precondition Result** 解释具体原因；Task 行显示对应 Version 的状态，不维护另一个可能矛盾的终态。

| 层级 | 建议职责 | 评审边界 |
|---|---|---|
| Analysis Version | 独立的已结束、未发布结果，例如 NOT_READY；与 FAILED、COMPLETED、CANCELLED 分开 | 记录本次运行因时间条件未成熟而结束，不是 PENDING/RUNNING、暂停或等待恢复；具体名称待 Review |
| Precondition Result | 原因可用 TARGET_NOT_REACHED，并关联受影响 Event、已验证时间依据、各 Required Target 与任务最晚 Target | 解释为什么该版本未就绪；仅有临时检查结果不足以替代可持久追溯的版本结果 |
| Task / History 展示 | 从所展示的 Version 读取状态；若列表展示最新版本，应明确为该版本的状态 | 避免 Task 与 Version 双重独立状态互相矛盾；不覆盖仍可读取的其它历史版本 |

- **为何优先 Version 层**：同一 Task 以后可能有成功的 V2，但 V1 因时间窗口未成熟结束的事实不能改变。独立的 Version 终态可同时支持保留记录、不发布、不取消、不自动恢复及 Re-analyze 新版。
- 即使真实时间后来超过 Latest Required Target Time，旧 Version 也不自动变成 Ready、RUNNING 或 COMPLETED；新运行必须创建新 Version 并按当时条件重新验证。
- 此处 NOT_READY 只是当前 Future Target 语义的候选名称，不顺带决定其它前置条件不满足、历史覆盖不足、时间映射失败或真正系统故障的终态。
- Affected Event Count 与任务 Latest Required Target Time 的最终值，应建立在候选集合读取完成的证据上。若读取尚未完成，只能标明已知范围，不能将局部计数或局部最晚 Target 冒充全任务最终值；这不替代用户要求提供完整影响信息，具体如何取得由 Technical Design 解决。
- 既有 D33 / 05 文档的五种状态基线需要在统一 Revision 中纳入此次独立语义要求；不能以保留原五状态为理由将其压回 FAILED。最终枚举、状态层级和转移关系仍待统一 State / Error Model Review，本轮不改基线文件。

### 决定范围与仍未决定事项

- 前一轮将选项 A 描述为“当前 Run FAILED”的技术建议已被用户明确修正并撤回；当前决定是独立的 Not Ready / Horizon Not Reached 语义，不能写成 FAILED 加一句提示。
- 判断对象是实际 Candidate Event 的 Required Targets，不仅凭输入 To 推定有未来 Event；使用可信时间依据，不猜当前 Server Offset，也不将无法证明时间关系的情形冒充 TARGET_NOT_REACHED。
- 判断未来目标和证明历史覆盖是不同事项。到时不保证历史同步完成、保留范围足够或返回完整；Q22 剩余 Coverage / Synchronization 问题及 Q07 覆盖证据仍 OPEN。
- 本项不把未成熟 Event 当作普通 Quote Failure / Event Discard 后继续发布剩余正式结果，也不自动缩短窗口、修改输入范围或换 Provider。
- 本项不改变 Q07 的 Ordering Reliability 失败层级；真正的本次整体 Ordering 不可靠仍按 Q07 第二小项处理，不归入 Future Target 未就绪。
- 实际读取候选事件、取得可信当前时间、证明候选集合完成度及形成计数的方法留 Technical Design；没有实际验证结果，不宣称任何 Provider 或时钟能力已通过。

### 待统一 Revision 的影响清单（本轮未修改）

| 文档 | 之后需同步的内容 |
|---|---|
| 01 Product Scope / D33 / 02 Architecture | Future Target 不等待、不自动重跑；独立的已结束未就绪语义，不能压入普通 FAILED |
| 03 Calculation Specification | 任一 Candidate Event Required Target 未到达时本版不发布；禁止提前使用现有 Quote、缩短窗口或跳过该 Event 发布 |
| 04 Logical Model / 05 State Model | Review Version 独立终态、前置条件原因及 Task 显示关系；保存未成熟事件、Affected Count、Latest Required Target；结束后无 Cancel/Resume |
| 06 UX / 07 Wireframes | 清楚显示未达到分析时间范围、具体事件与所需 Server Time；与系统失败区分，用户主动 Re-analyze |
| 08 Acceptance | 未来目标不归为 NO_QUOTE/INVALID_QUOTE/FAILED，无正式 Summary、无自动等待；旧版不因时间推进改变，新版独立验证 |
| 10 Assumptions / 12 Technical Evidence / README / 09 Questions | 区分时间成熟性与报价覆盖，记录可信时间依据；09 仅标记第一小项决定，状态建模建议与其它覆盖问题保持待评审 |

## Q22（第二小项）— DECIDED — B：可靠定位的历史保留不足仅排除受影响 Event

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**CURRENT APPLICABILITY — Money相关分支PARTIALLY SUPERSEDED：本节历史Money计算/指标/字段/资格/证据引用已不适用于V1；其余已确认的时间、匹配、统计Scope、隔离/发布、导出、重跑与历史冻结规则保持。具体范围见14影响矩阵，不能因删除Money扩大其它业务范围。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P0。
- Status：**DECIDED — B**。Q22 第一小项 A 与第二小项 B 均已决定；整体覆盖不明、同步/可用性等边界仍 OPEN，Q22 整体为 **PARTIALLY DECIDED**。
- User Choice：**B，以下列二十一项补充边界为完整决定**。
- Decision Source：用户在 Architecture Review 中对 Q22 第二小项的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**，未统一修改 Architecture、Calculation Specification 或 State Model。
- Implementation / Technical Verification：仅记录决定及相关问题状态，未调用接口、验证实际历史保留范围、选择 Provider 或实现任何代码。

### 已确认业务边界

1. **证据充分且影响可定位时只排除受影响 Event**：技术证据已确认某些 Event 所需 Historical Quote 超出 Quote Provider 可提供的历史保留范围，且受影响 Event 可以准确定位、其它 Event 的 Quote Coverage 可独立证明完整、Provider 本身仍然可靠时，只排除受影响 Event。
2. **受影响 Event 退出所有正式聚合**：不进入 Points Summary、Money Summary、Symbol Markout Curve、Account Summary 或 Multi-account Overview 聚合。
3. **可靠的其它 Event 正常继续**：其它能证明 Quote Coverage 完整的 Event 正常继续分析，并满足其它已确认 Eligibility 条件；不得仅因部分旧 Event 超出 Historical Retention 就令整个 Task / Version FAILED。
4. **Retention Missing 与 NO_QUOTE 分开**：“Provider 已经无法提供这段历史数据”不代表“当时市场没有 Quote”，二者必须区分。
5. **保留独立诊断语义**：可概念化为 HISTORICAL_COVERAGE_UNAVAILABLE 或 OUTSIDE_QUOTE_RETENTION；最终名称由 State / Error Model Review 决定。
6. **区分三种高层含义**：TARGET_NOT_REACHED 表示目标时间还没有发生；HISTORICAL_COVERAGE_UNAVAILABLE 表示目标时间已经发生，但 Provider 已无法提供所需历史范围；NO_QUOTE 表示“在已经证明覆盖充分的历史范围内，没有找到满足条件的 Raw / Valid Quote”。此处完整保留用户本轮的 Raw / Valid Quote 表述。
7. **禁止替代历史数据补齐 Event**：不得自动切换 Quote Provider、Server 或 Symbol，不得用 MT5 替代 MT4，不得使用当前 Quote、K Line、插值、模拟 Tick，或使用最早仍可取得的 Tick 代替目标时点报价。
8. **任一 Required Window 缺覆盖则整个 Event 不 Eligible**：任何 Required Window 超出可证明的 Historical Coverage，整个 Event 失去 Summary Eligibility。
9. **其它窗口成功不恢复部分正式资格**：即使该 Event 其它 Windows 的 Quote、Points、Money 已成功计算，该 Event 仍不进入正式 Summary。
10. **部分结果继续保留**：已取得结果不得删除；保留于 Deal / Event Detail、Diagnostics、Historical Version Data、Exported Discarded Data。
11. **诊断包含具体事件与覆盖证据**：至少包括 Login、Deal ID / Ticket、Symbol、Event Time、Missing Window、Target Time、Quote Provider、Confirmed Historical Coverage Boundary、Failure Reason。
12. **正确计入 Analysis Rate**：这类 Event 计入 Total Candidate Events，不计入 Fully Analyzed / Summary Eligible Events。
13. **Account 显示覆盖程度**：Account Summary 继续展示 Analyzed Events / Total Events 和 Analysis Rate，让风控人员知道正式结果仅覆盖部分成交。
14. **不输出低覆盖率风险结论**：不得自动判断“Analysis Rate 太低，所以结果不可信”；工具只展示事实。
15. **账户无 Eligible 时不伪造零 Markout**：某 Account 全部 Event 因 Historical Coverage 不足被排除时，显示 0 Eligible Events 并明确原因，不生成看起来正常的 0 Markout Summary。
16. **整个任务无 Eligible 时明确无有效结果**：如果整个 Task 所有 Accounts 都没有 Eligible Events，应进入明确的 No Valid / Eligible Results 结果状态；最终采用 Completed-with-no-results 或其它状态，由 State Model Review 提出建议，本题不强行定义为 FAILED。
17. **局部隔离前提必须成立**：Historical Coverage Boundary 能够可靠证明，且受影响范围能够准确定位，才可使用本题 B。
18. **覆盖不明或影响不明升级可靠性评审**：Historical Coverage 本身无法确定，或不能证明哪些 Event 受到影响时，不能使用本题局部隔离；应升级到 Quote Provider / Task Reliability 级别单独处理。
19. **同步未完成属于独立问题**：历史数据尚未同步完成、但理论上仍在 Provider 可提供范围内时，不属于 Retention Missing；属于 Synchronization / Availability，后续单独确认。
20. **覆盖或明确选择的来源变化只影响新版本**：未来 Provider 历史覆盖发生变化，或更换经过正式验证并明确选择的新 Provider 时，Re-analyze 的新 Version 可以重新分析这些 Event；旧 Version 保持原来的 Discard 状态。
21. **同一版本不自动混用来源**：本题不改变同一个 Analysis Version 禁止为了补数据自动混用 Quote Provider 的既有规则。

### 关联问题与状态建模建议（未作为最终模型采用）

- **Q11 关联决定**：Retention Discard 纳入 Total Candidate、不计 Eligible；Account 继续展示 Analyzed / Total 和 Analysis Rate；不把低 Analysis Rate 自动解释为不可信。账户真实 0 Eligible 可以显示计数，但不得把无结果伪装成正常 0 Points / Money Markout。零量准入与无Eligible时不得伪造零Markout已由Q05第四小项确认；筛选分子/分母已由Q11第一小项B确认，零候选Rate显示及不可显示原因区分已由第二小项A确认。
- **Q14 关联决定**：No Valid / Eligible Results 的明确结果语义已确认，不能只因没有 Eligible Event 就强行归类 FAILED；最终状态建模仍待 Review。原先将本题全部无结果情况完全留待后续业务选择的描述，现被用户第十五、十六项补充决定更新。
- **状态建议**：在任务输入/候选读取与处理已经完整结束、所有 Event 的处置均有可靠依据、必要记录保存完成的前提下，可采用“执行已完成 + 结果为 NO_ELIGIBLE_RESULTS”的两层表达；History 明确显示“无有效结果”，不能只显示普通完成而暗示已有正常 Markout Summary。此为供 State Model Review 的 Recommendation，未确定最终枚举、界面或存储结构。
- **未知不等于零**：上述无结果语义针对能够可靠判定的零 Eligible；不能把查询/同步未完成、覆盖整体不明或系统故障导致的未知集合伪装成已处理完的空结果。真正的系统/可靠性失败继续按相应规则处理。
- **Q04 分类衔接**：本轮已给出 NO_QUOTE 的高层口径，尤其确认覆盖充分是前提。原 Q04 对“没有 Raw Tick”与“有 Raw Tick 但全部无效”的具体 Window 原因分类，需在后续 Error Model Review 中与本轮 Raw / Valid Quote 表述统一；不擅自删掉 INVALID_QUOTE，也不将 Retention Missing 混入 NO_QUOTE。

### 决定范围与仍未决定事项

- 前一轮选项 A 所提“任一局部 Retention Missing 都让整个 Run FAILED”未被采用；不得忽略可靠影响定位及其它 Event 独立完整的证据。
- 本决定不证明任何实际 Provider 具有特定历史深度，也不通过业务选择确定接口覆盖边界。有限 lookback 空数组、连接成功或看起来连续的少量 Tick，都不能单独冒充完整覆盖证据。
- 覆盖验证应足以证明实际 latest valid Tick 选择：若所选 Tick 及其后至 Target 的相关记录完整，更早无关历史未保留不自动使该匹配失败。Q04 无最大回溯时间规则不变，不能另设人为历史截断或 Quote Age 阈值。
- 禁止用“最早仍可取得的 Tick”填补不可覆盖的目标；不禁止一条可取得 Tick 在自身时间不晚于 Target、有效性和必要覆盖/顺序均经证明时按正常匹配规则被选中。
- Q07 Ordering Reliability 与 Q22 Future Target 的层级规则继续适用；本题不把 Provider 整体不可信、未来目标未成熟或尚未同步归为可局部丢弃的 Retention Missing。
- 新版本可使用经正式验证且明确选择的新 Provider，不授权本轮选择或接入 Provider，也不授权在当前 Version 自动 fallback；具体 Re-analyze 输入/来源选择的操作边界仍需与 Q01、Q15 等在统一 Revision 时衔接。
- 本题确认版本结果、诊断和导出中的部分数据保留要求，不长期保存完整 Historical Tick Stream，不更改现有项目、SDK、Terminal 或 Skill。

### 待统一 Revision 的影响清单（本轮未修改）

| 文档 | 之后需同步的内容 |
|---|---|
| 01 Product Scope / 02 Architecture | Retention 可证明、局部影响可定位、其它数据完整且来源可靠才局部隔离；覆盖不明升级可靠性问题 |
| 03 Calculation Specification | 任一 Required Window 缺必要覆盖，整 Event 退出所有正式结果；保留成功部分，不用其它来源/近似数据补齐 |
| 04 Logical Model / 05 State Model | 保留事件、失败窗口、覆盖边界与来源证据；区分 Future Target、Retention Missing、NO_QUOTE 和同步问题；Review 无 Eligible 结果模型 |
| 06 UX / 07 Wireframes | Account 的 Analyzed/Total/Rate、0 Eligible 原因、全任务无有效结果；无伪零 Markout、无低覆盖率自动风险结论 |
| 08 Acceptance | 局部保留不足隔离、未知覆盖不套用局部规则、部分结果保留及导出、全部无 Eligible 明示、旧版 Discard 冻结 |
| 10 Assumptions / 12 Technical Evidence / README / 09 Questions | 技术证明覆盖范围而非推断；09 最小标记决定和 Q04/Q07/Q11/Q14 关联事项，剩余同步与整体可靠性仍 OPEN |

## Q22（第三小项）— DECIDED — A：已发生历史数据允许本次运行内有界同步等待

- Priority：P0。
- Status：**DECIDED — A**。本小项关于局部同步超限的保留点，现已由第四小项 **DECIDED — B** 确认；第15条按该后续决定限定如下。Q22 最终状态建模及其它未决证据/可靠性细节仍待 Review。
- User Choice：**A，以下列二十三项补充边界为完整决定**。
- Decision Source：用户在 Architecture Review 中对 Q22 第三小项的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**；本轮不修改架构、计算规范或状态模型基线。
- Implementation / Technical Verification：仅记录决定，未调用 Provider、等待数据、测试取消/重试能力、设置参数、创建通知或实施代码。

### 已确认业务边界

1. **许可仅限已经发生的历史数据**：V1 允许针对已经发生的 Historical Data，在当前 Analysis Run 内进行有限等待和只读重试。
2. **进入等待须具备经验证的 Provider 状态**：只有 Provider 能提供明确且经过验证的状态，证明 Target Time 已经发生、所需历史范围仍属于其可提供范围、Historical Data 当前处于 Loading / Synchronizing / Not Ready、后续存在完成同步的可能，才允许进入该流程。
3. **不得从空数据或稀疏数据猜测同步**：Query 返回空数据、Tick 数量较少或没找到目标 Quote，均不能作为自行推断“可能还在同步”的依据。
4. **不能证明同步状态则不适用本规则**：无法证明是 Synchronization / Loading 状态时，不进入本规则等待流程；按实际能够证明的 Coverage / Availability 状态处理。
5. **等待有界、可取消、可观察**：必须有上限、可取消、可观察，禁止无限等待。
6. **参数由 Technical Design 确定**：Maximum Wait Time、Retry Interval、Retry Count、Backoff Strategy 属于 Technical Design，当前不作为用户配置项；本轮不指定数值。
7. **UI 明确显示同步阶段**：应显示类似 Waiting for historical quote synchronization 的当前阶段，不能用 Markout Calculation 99% 掩盖等待，也不能让界面看起来卡死。
8. **等待期间允许 Cancel**：用户可在等待期间取消。
9. **取消按既有版本规则处理**：Cancel 后按既定 Cancelled Task / Version 规则处理，不自动继续。
10. **数据出现不代表可计算**：同步完成后，必须重新验证 Historical Coverage、Quote Ordering、Quote Validity、Server / Provider Context、Time Mapping；全部满足条件后才允许继续 Markout Calculation，不能因数据出现就跳过验证。
11. **等待期间禁止换源或替代数据**：不得自动切换 Quote Provider、Server、Symbol，不得使用其它平台报价、K Line、插值、模拟 Tick 或当前行情替代历史行情。
12. **不得借此等待未来目标**：本规则只适用于 Target Time 已发生；Required Target 尚未发生时仍按第一小项 NOT_READY / TARGET_NOT_REACHED 独立语义结束当前 Run，不进入同步等待。
13. **已知超出保留范围不等待**：已证明所需数据超出 Provider Retention Range 时，继续按第二小项 HISTORICAL_COVERAGE_UNAVAILABLE 语义处理，不进入等待流程。
14. **限内完成可继续原运行**：有限等待期限内同步完成且全部验证通过时，当前 Run 可以继续，不需要创建新 Version。
15. **同步超限且不能可靠隔离时整版不发布（按第四小项限定）**：如果同步超限且影响范围无法可靠隔离，当前 Version 以整体 Data Not Ready / Synchronization Not Ready 语义结束，不发布正式 Summary。不能解释为“任何局部同步超限都禁止整个 Version 发布结果”。影响可准确定位、其它 Event 完整正式资格均可独立证明且满足第四小项全部边界时，允许排除受影响 Event 后发布其它 Eligible Events 的结果。Provider 整体历史数据可靠性无法确定时同样禁止局部发布。此限定来自用户对第四小项的明确决定。
16. **超限不自动归为普通系统失败**：这种结束属于 Historical Data Availability / Synchronization 尚未满足分析前提，不应自动归类为普通系统 FAILED。
17. **状态名称与层级留 Review**：可考虑 DATA_NOT_READY、HISTORICAL_SYNC_TIMEOUT、QUOTE_DATA_NOT_READY；最终名称与所属层级留 State Model Review，本轮不冻结枚举。
18. **与其它原因明确区分**：必须与 TARGET_NOT_REACHED、HISTORICAL_COVERAGE_UNAVAILABLE、NO_QUOTE、INVALID_QUOTE、CONNECTION_FAILED 区分。
19. **超限后没有自动后续动作**：V1 不自动 Background Retry、Scheduled Retry、Automatic Re-analyze、Automatic New Version 或 Notification when ready。
20. **用户主动重新分析成新版**：用户稍后可主动 Re-analyze，创建新 Version。
21. **当前版本保存同步证据**：至少保存 Quote Provider、Requested Historical Range、Synchronization / Loading Evidence、Wait Start Time、Wait End Time、Retry Attempts、Final Availability Result，用于 Diagnostics 和测试。
22. **详细重试过程与普通界面分开**：Technical Log 可以保存更详细的重试过程；普通风控 UI 仅显示必要状态和最终原因。
23. **局部超限后的发布资格单独评审（后续已决定）**：本小项原先只决定是否允许有限等待，并将局部 Symbol / Event 同步超限后的发布边界留待单独 Review；该保留点现已由第四小项 B 及其二十三项边界确认，详见下文，不能继续视为尚未授权局部发布。

### 状态层级与证据建议（RECOMMENDATION，最终模型待 Review）

- 正在有界同步等待时，可作为当前 RUNNING Version 的明确内部阶段展示；不增加 PAUSED / Resume，也不创建另一任务或版本。能否即时中断底层 SDK 调用仍需能力证据，不能未经验证承诺即时终止；取消处理必须满足用户已确认的可取消要求。
- 按第四小项需因同步超限整体结束时，建议由 Analysis Version 保存独立的数据未就绪结束结果，并以 Synchronization / Availability 原因及本次等待证据解释；Task / History 显示对应 Version 的状态。不能把它包装成普通 FAILED 加一句说明，也不能与 Future Target 的原因混为一谈。可可靠局部隔离并有其它 Eligible 结果时，不因存在局部超限而将整个 Version 标为未就绪。
- “历史同步未完成”只描述经过验证的实际状态，不承诺以后一定成功；真正的连接/权限/时间映射/Ordering 等故障继续适用各自规则，不能统一改称 DATA_NOT_READY。
- 等待证据归属于当前 Version，后续同步完成或新 Version 成功不能改写旧 Version 的超限事实。起止时间的来源、语义及重试明细组织方式由 Technical Design 明确，不将诊断计时当作 Trade / Target / Quote 时间来源。
- 此建议不决定并发、线程、进程、任务工作槽、分块大小、SDK超时实现或具体等待参数；这些均未获得实施授权。

### 决定范围与仍未决定事项

- 同步等待的业务许可现已确认，但只限第二项列出的全部前提。不存在可验证 Loading / Synchronizing 状态时，不能通过猜测、延长等待或循环空查询制造该前提。
- 第十五项及第二十三项已由第四小项 B 明确限定：可靠局部隔离后可发布其它经证明合格的结果；无法可靠隔离或整体可靠性无法确定时禁止局部发布。原先“尚未批准局部发布”的记录不再作为当前要求。
- 第一小项对 Future Target 的不等待/不自动重跑规则与本小项不冲突：前者是目标尚未发生，后者是已经发生的数据暂在同步。后者不授权前者，也不新增版本结束后的定时或后台重试。
- 结束于 Synchronization / Availability 尚未满足，不等于候选事件已经全部处理且真实 Eligible 数为零；不得把未完成同步的集合伪装为 Q22 第二小项的 No Eligible Results。
- Provider 整体覆盖不明或影响无法界定，仍按第二小项升级 Quote Provider / Task Reliability；Q07 的 Ordering Reliability 规则保持有效，不由本项等待许可绕过。
- 局部/整体发布政策已由第四小项决定；全部 Candidate 因同步超限无 Eligible 时的最终 Version 状态、其它残余可靠性/Quote Classification 仍待 Review。本决定不证明任何实际 SDK 的同步状态、覆盖能力或取消契约已经通过验证。

### 待统一 Revision 的影响清单（本轮未修改）

| 文档 | 之后需同步的内容 |
|---|---|
| 01 Product Scope / 02 Architecture | 已发生数据且同步状态可证时的本 Run 有界等待/只读重试；参数技术设计，无用户配置、后台自动重跑或通知 |
| 03 Calculation Specification | 同步后重验 Coverage/Ordering/Validity/Server-Provider/Time Mapping；禁止猜测同步、替代数据及等待未来目标 |
| 04 Logical Model / 05 State Model | 保存同步证据、请求范围、起止时间、次数和最终可用性；Review 独立未就绪结束结果，局部发布边界按第四小项衔接 |
| 06 UX / 07 Wireframes | 明确等待同步阶段、可观察/可取消，不用99%计算进度伪装等待；普通UI与详细Technical Logs分开 |
| 08 Acceptance | 等待进入条件、限内原Version继续、超限无自动后续、取消不续跑、五类验证重做、旧版记录冻结 |
| 10 Assumptions / 12 Technical Evidence / README / 09 Questions | 验证真实同步信号和可取消契约；09仅标记第三小项决定与局部发布保留点，未验证事实不当作能力承诺 |

## Q22（第四小项）— DECIDED — B：同步超限可可靠隔离时发布其它 Eligible Events 的结果

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**CURRENT APPLICABILITY — Money相关分支PARTIALLY SUPERSEDED：本节历史Money计算/指标/字段/资格/证据引用已不适用于V1；其余已确认的时间、匹配、统计Scope、隔离/发布、导出、重跑与历史冻结规则保持。具体范围见14影响矩阵，不能因删除Money扩大其它业务范围。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P0。
- Status：**DECIDED — B**；Q22 第一至第四小项依次为 **A / B / A / B**。局部发布的业务边界已确认，最终状态模型与实际技术证据仍待 Review/验证。
- User Choice：**B，以下列二十三项补充边界为完整决定**。
- Decision Source：用户在 Architecture Review 中对 Q22 第四小项的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**；本轮仅在本评审记录中限定第三小项第15条并同步问题状态，未修改 Architecture 或 Calculation Specification 正文。
- Implementation / Technical Verification：未执行代码开发、Provider 接入、同步测试、技术栈选择或任何数据库操作。

### 已确认业务边界

1. **可隔离且其它 Event 完整合格时允许发布**：Historical Synchronization 达到当前 Run 等待上限后，若同步未完成的影响可准确定位到具体 Event，且其它 Event 的 Historical Coverage、Quote Ordering、Time Mapping、Quote Validity、Required Window Completeness、Points Calculation、Money Calculation 均可独立证明满足正式分析资格，则允许发布其它 Eligible Events 的正式结果。
2. **受影响整个 Event 退出全部聚合**：不进入 Points Summary、Money Summary、Symbol Markout Curve、Account Summary 聚合或 Multi-account Overview 聚合，不能保留某些窗口或指标的正式资格。
3. **保留事件与可用结果**：受影响 Event 不删除；保留 Trade / Event Data、已取得 Quote、已取得 Markout Result、Synchronization Evidence、Wait / Retry Information、Failure Reason，用于 Detail、Diagnostics、Historical Version、Discarded Export。
4. **使用独立诊断语义**：可用 HISTORICAL_SYNC_TIMEOUT 或 QUOTE_DATA_NOT_READY 等概念；最终名称由 State Model Review 决定。
5. **与其它缺失或无效原因区分**：HISTORICAL_SYNC_TIMEOUT 不等于 NO_QUOTE、INVALID_QUOTE、HISTORICAL_COVERAGE_UNAVAILABLE 或 TARGET_NOT_REACHED。
6. **明确限定第三小项第15条**：该条改按“同步超限且影响范围无法可靠隔离时，当前 Version 不发布正式 Summary”理解；不得再解释为任何局部同步超限都禁止整个 Version 发布。本轮已在评审记录中直接标注此限定，未统一修改架构正文。
7. **局部发布须披露样本与排除情况**：能够局部隔离时，Version 可以正常产生正式结果，但须显示 Total Candidate Events、Eligible Events、Sync Timeout Events、Other Discarded Events、Analysis Rate。
8. **使用统一 Analysis Rate**：以 Fully Analyzed / Summary Eligible Events ÷ Total Candidate Events 表达，不因局部同步超限另建不同口径。
9. **不得为提高 Rate 放宽边界**：不得延长到无限等待、自动切换 Provider、Server、Symbol，或使用其它平台报价、插值、模拟报价。
10. **返回数据不等于完整覆盖**：其它 Event 有返回数据不足以证明其 Historical Coverage 完整，必须通过已确认 Technical Validation 证明 Quote Selection 可靠。
11. **允许按未就绪历史范围定位共同依赖**：若同步状态发生在 Symbol 级，例如 XAUUSD 历史仍在同步，可隔离所有确实依赖该未就绪 Historical Range 的 Events，不要求逐 Event 重复等待。
12. **禁止按 Symbol 名称一刀切**：不能仅因 Symbol = XAUUSD 就丢弃该 Symbol 的全部 Event；只排除实际依赖未完成 Historical Range 的 Events。
13. **跨账户影响按 Event 隔离**：同步问题影响多个 Accounts 时，各受影响 Event 按相同规则隔离；不能仅因共享 Provider 就自动令整个 Task 无结果。
14. **账户全部受影响时明确零资格**：能证明某 Account 所有 Event 都受同步超限影响时，Eligible Events = 0；UI 明确显示 No Eligible Results / Historical Data Not Ready，不生成 0 Markout Summary。
15. **全任务因同步超限无 Eligible 不能伪装正常结果**：所有 Candidate Events 都因同步超限而无 Eligible Event 时，不能表现为正常 Completed Markout Result；最终 Version 状态留 State Model Review，不在本项自行确定枚举或自动归普通 FAILED。
16. **无法界定影响时整体未就绪结束**：同步影响范围无法可靠界定时，禁止局部发布，当前 Version 按 Data Not Ready / Synchronization Not Ready 的整体未就绪语义结束，不发布正式 Summary。
17. **整体历史可靠性不明同样不局部发布**：若有证据表明 Provider 整体 Historical Data 处于无法确定可靠性的状态，同样禁止局部发布。
18. **隔离层级原则保持一致**：与 Local Quote Ordering Ambiguity、Historical Retention Missing 一致，可可靠隔离则 Event-level exclusion，不能可靠隔离则 Version / Task-level stop；不同原因的具体状态语义仍按各自已确认规则区分。
19. **旧版本保存实际排除事件**：Historical Version 必须保存本次哪些 Events 因 Sync Timeout 被排除。
20. **同步后来完成只影响用户主动创建的新版本**：用户主动 Re-analyze 创建新 Version；同步完成后，原被排除 Event 可在满足完整资格的新 Version 成为 Eligible，旧 Version 不变。
21. **禁止自动后续修补**：V1 不自动 Background Retry、Scheduled Re-analysis、Auto Merge Result 或 Auto Patch Old Version。
22. **不回改已完成版本**：同步完成后，不得自动修改已经 Completed 的旧 Version。
23. **主页面披露部分样本，细节留 Diagnostics**：Account / Symbol Summary 应让风控人员知道当前结果基于部分 Candidate Events，不弹出大量技术信息；详细同步原因放在 Diagnostics。

### 关联记录与仍需 Review 的边界

- 前一轮推荐 A（所有局部同步超限均整版不发布）未被采用。第三小项第15条、第23条及相应“尚未批准局部发布”的文字已按本轮决定限定；不能继续用旧建议阻止满足本轮条件的正式结果。
- 第三小项的进入等待证据、有界/可取消/可观察、同步后重新验证、禁止自动后续/通知、保存 Version 证据等规则继续适用。本题不重新决定等待参数、并发或执行顺序。
- **Q11 关联**：统一样本与 Analysis Rate 公式、Sync Timeout / Other Discarded 等计数披露已确认；不得因多个失败窗口把同一 Event 重复计入事件总数。筛选分子/分母已由Q11第一小项B确认，Total=0时Rate具体表达及原因区分已由第二小项A确认；多原因统计展示已由Q11第三小项A确认，不重问去重及Rate数学规则。
- **Q14 关联**：账户真实 0 Eligible 不表示 0 Markout；全任务所有 Candidate 均因 Sync Timeout 不 Eligible 时，不能直接套用一般“正常 Completed 结果”。最终 Version 状态需显式处理此分支，保留用户的 No Eligible / Historical Data Not Ready 语义与诊断事实。
- 第17条禁止 Provider 整体历史可靠性不明时局部发布；第18条确认不能可靠隔离时停止，而不是把未知影响假装成少数 Event 缺数据。本轮不把所有 Provider 故障改称同步超限，Q07 对整体 Ordering 不可靠使用 FAILED 的既定规则不变。
- 能定位未同步的 Symbol / Historical Range，不等于已证明该 Symbol 全部 Events 受影响；必须根据实际 Quote Selection 所需范围建立依赖关系，不因批量查询或缓存共享范围较大而无条件扩散排除。
- 其它 Events 满足正式资格须经独立证据证明，不用“响应非空”“同一 Server”或“其它账户返回成功”替代覆盖/时序/规格/Points/Money 等要求。旧 Version 的诊断事实、计数与正式结果不会在同步完成后自动改变。
- 四个业务子项已决定；实际 SDK / Provider 能力、精确状态命名与层级、全任务 Sync Timeout 无结果终态以及其余未决 Quote Classification/可靠性细节，继续留 Technical / State Model Review。没有因记录这些决定而执行接入或实施。

### 待统一 Revision 的影响清单（本轮未修改）

| 文档 | 之后需同步的内容 |
|---|---|
| 01 Product Scope / 02 Architecture | 同步超限按实际依赖范围隔离；其它Events全资格独立证明后可发布，范围不明/整体可靠性不明停止 |
| 03 Calculation Specification | 同一Event退出所有正式指标/窗口；其它Event有返回数据不等于可靠Quote Selection；统一Eligible样本 |
| 04 Logical Model / 05 State Model | Version记录实际Sync Timeout排除集合和证据；限定第三小项第15条，Review全任务零Eligible同步分支及诊断层级 |
| 06 UX / 07 Wireframes | Account/Symbol显示部分样本与Candidate/Eligible/SyncTimeout/OtherDiscarded/Rate；零Eligible不伪零Markout，技术原因放Diagnostics |
| 08 Acceptance | Symbol级范围精确影响、跨Account隔离、禁止无限等待或换源、局部发布条件、禁止旧版自动回填/合并 |
| 10 Assumptions / 12 Technical Evidence / README / 09 Questions | 区分状态与能力证据，09标记第四小项及第三条款限定，Q11/Q14保留未决模型细节 |

## Q06（第一小项）— DECIDED — B：Analysis Time Range 统一采用半开区间

- Priority：P0。
- Status：**DECIDED — B，仅 Boundary Inclusion 子项**；Q06 输入精度、日期快捷等剩余小项仍 OPEN。
- User Choice：**B，以下列十八项补充边界为完整决定**。
- Decision Source：用户在 Architecture Review 中对 Q06 第一小项的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**；本轮仅记录决定和问题状态，不修改架构或计算规范正文。
- Implementation / Technical Verification：未实现查询、过滤、界面或导出，未调用 Manager / Quote Provider，也不宣称实际 API 端点契约已验证。

### 已确认业务边界

1. **统一采用半开区间**：Analysis Time Range = [From, To)，即 From <= Event Time < To。
2. **包含 From**：Event Time 恰好等于 From 时，纳入本次分析。
3. **排除 To**：Event Time 恰好等于 To 时，不纳入本次分析。
4. **单段示例**：From=10:00:00、To=11:00:00 时，10:00:00 的 Event Included，11:00:00 的 Event Excluded。
5. **下一段示例**：From=11:00:00、To=12:00:00 时，11:00:00 的 Event Included。
6. **相邻区间无边界重复归属**：[10:00,11:00) 与 [11:00,12:00) 不会因边界重叠重复包含同一 Event。
7. **由应用保证最终语义**：Markout Analyzer 自身保证区间规则，不能依赖 MT4 / MT5 Manager API 默认 Inclusive 或 Exclusive 查询语义。
8. **Inclusive End 响应必须最终过滤**：若 Manager API 只能使用 Inclusive End，Adapter / Normalization Layer 取得数据后必须按 Event Time < To 执行最终 Candidate Event Filtering，并满足完整区间规则。
9. **扩大读取不扩大 Candidate Set**：API 因获取数据需要扩大查询范围时，最终 Candidate Events 仍严格按 From <= Event Time < To 归一化。
10. **MT5 用有效原始毫秒时间比较**：使用原始有效 TimeMsc 进行边界比较，禁止先取整到秒再判断。
11. **MT4 保持秒精度**：使用 SECOND Precision Event Time，按已经确认的时间语义比较。
12. **禁止用偏移模拟 Inclusive End**：不得为模拟 Inclusive End 自动将 To 加 1ms、999ms、1s 或其它偏移。
13. **To 是精确排除边界**：To 为精确的 Exclusive Boundary，不自动解释为该秒或该日的末尾。
14. **Required Target 可超过 To**：Required Markout Target 不受 To 限制。例如具有毫秒精度的 Event Time=To-1ms 时，该 Event 属于 Candidate，其 +100ms、+500ms 等直至 +180s 的 Required Windows 即使全部在 To 后，也必须正常计算。本例不为 MT4 伪造毫秒精度。
15. **区分成交选择与报价获取**：To 只决定哪些 Trade Events 属于 Candidate Set，不决定 Historical Quote Retrieval 的最终结束时刻。
16. **报价范围由实际事件需求确定**：Quote Retrieval 必须根据实际 Candidate Events 所需的最大 Required Target Time 获取必要历史范围。
17. **界面与导出明确区间含义**：UI / Export Metadata 应明确表达 Time Range: [From, To)；最终易理解的用户文案由 UX Design 处理。
18. **本题不决定输入与显示精度**：仅决定 Boundary Inclusion；用户输入时间的显示精度、秒/毫秒输入方式、日期快捷选择仍由 Q06 后续小项确认。

### 决定范围与仍未决定事项

- 前一轮推荐 A（双端包含）未被采用，不再作为当前需求。From 包含、To 排除是统一业务规则，不由 SDK 行为或界面显示格式决定。
- Adapter 可以为完整读取使用更宽的 API 请求区间，但不能改写实际 Analysis From/To 或扩大最终 Candidate Set；对数据获取的扩读许可与禁止为模拟 Inclusive End 给 To 加偏移是不同层次。
- 原始 TimeMsc 和 MT4 SECOND 的来源精度必须保留；比较仍使用 Q02 允许的已验证时间语义/确定性映射，不猜时差，不舍入成交时间，不把无效 TimeMsc 回退为 Time。无效时间的范围归属及计数已由 Q12 单独决定为 B，不再等待重复确认。
- 相邻区间避免端点重复，不意味着任意重叠任务之间自动去重，也不新建跨任务合并/去重功能。各 Version 仍保留自己实际使用的范围和结果。
- 最大 Required Target 决定报价读取需求，不授权等待尚未发生的目标；Q22 的时间成熟、覆盖和同步规则继续适用，不能截断 Target 以让未成熟 Event 通过。
- 报价搜索不能因此简化为只读取 [From, 最大Target]：Q04 仍允许向更早历史寻找 latest valid Tick、无最大回溯时间，所需前序覆盖须由实际匹配需要和证据证明，不另设 Quote Age 阈值。
- MT4/MT5 SDK 的查询端点、分页与完整性等具体能力仍须 Technical Evidence；本轮未调用接口或验证过滤实现，也未决定输入控件、日期展开或技术栈。

### 待统一 Revision 的影响清单（本轮未修改）

| 文档 | 之后需同步的内容 |
|---|---|
| 01 Product Scope / 03 Calculation Specification | Candidate 范围统一 [From,To)，MT5有效TimeMsc/MT4SECOND，禁止隐式To偏移与截断Target |
| 02 Architecture / 04 Logical Model | Adapter/Normalization保证最终半开过滤；扩读不扩候选；保存实际区间与独立报价读取需求 |
| 05 State Model / 12 Technical Evidence | 无效时间继续独立处理；核验真实API边界与完整读取，Q22成熟性/可用性规则不变 |
| 06 UX / 07 Wireframes | From包含、To排除清晰可见，显示精度/日期快捷仍待后续，不把精确To解释为整秒末尾 |
| 08 Acceptance | From/To相等边界、相邻区间归属、Inclusive API响应后过滤、扩读精筛、To后RequiredWindows仍完整计算 |
| 10 Assumptions / README / 09 Questions | 记录B取代原A建议；区分业务区间与实际API能力，09仅关闭第一小项 |

## Q06（第二小项）— DECIDED — B：MT5 可选毫秒边界，MT4 严格秒级边界

- Priority：P0。
- Status：**DECIDED — B**。Q06 的 Boundary Inclusion 与输入精度两个计算相关业务子项均已决定；Date/Time Picker、快捷日期、列表默认显示及毫秒字段展开方式等留 UX Design。
- User Choice：**B，以下列十八项补充边界为完整决定**。
- Decision Source：用户在 Architecture Review 中对 Q06 第二小项的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**；本轮仅更新独立评审记录和问题状态，不修改架构、计算规范或UI正文。
- Implementation / Technical Verification：未创建控件或程序、实现存储/导出、调用Manager接口或改变任何实际环境。

### 已确认业务边界

1. **MT5 最多支持毫秒输入**：MT5 Analysis Time Range 的 From/To 支持 millisecond，最大输入精度为毫秒。
2. **MT5 秒输入精确解释为 .000**：用户可只输入到秒；From=10:00:00、To=11:00:00 必须明确解释为 From=10:00:00.000、To=11:00:00.000。
3. **支持同一秒内的精确范围**：例如 From=10:00:00.250、To=10:00:00.750，Candidate Filtering 为 10:00:00.250 <= TimeMsc < 10:00:00.750。
4. **原始 TimeMsc 不损失精度**：MT5 Candidate Filtering 始终使用原始有效 TimeMsc；禁止先截断、四舍五入或秒级归一化后再比较。
5. **MT4 仅 SECOND 输入**：MT4 From/To 只支持 SECOND Precision。
6. **拒绝 MT4 非零毫秒**：MT4 不接受非零毫秒输入，例如 10:00:00.250 不能静默变成 10:00:00。
7. **明确解释输入限制**：MT4 输入有非零毫秒时，UI 必须明确提示其仅支持秒级边界，例如“MT4 analysis supports second-level time boundaries only.”；具体文案由 UX Design 决定。
8. **禁止修整 MT4 输入后继续**：不得对 MT4 毫秒输入执行 Truncate、Round、Ceiling 或自动修改为 .000 后继续。
9. **统一控件只能接受 MT4 的零毫秒部分**：若统一时间控件技术上总携带毫秒字段，MT4 只有 millisecond=000 才视为有效输入。
10. **版本保存平台边界精度**：From/To 的平台精度必须保存到 Analysis Version Metadata。
11. **历史恢复不丢精度**：再次打开历史 Task/Version 时，必须恢复原始 From/To 精度；MT5 的 10:00:00.250 不能重新显示或保存成 10:00:00。
12. **导出保留非零毫秒**：Export Metadata 中，MT5 若存在非零毫秒，必须保留毫秒。
13. **仍使用所选 Profile 的 Server Time**：From/To 仍属于所选 Profile 的 Server Time；毫秒支持不改变 Time Zone / Time Mapping 规则。
14. **半开区间不变**：Analysis Range 始终为 [From,To)。
15. **Required Target 规则不变**：To 仅筛选 Candidate Event，不限制 Markout Quote Retrieval。
16. **毫秒输入为可选能力**：MT5 支持毫秒不意味着日常必须填写毫秒；UX 默认保持简单，毫秒作为可选精确输入能力。
17. **Single Trade Test 使用真实事件时间**：不受 From/To 输入粒度影响，继续直接使用 Manager 返回的真实 Event Time / TimeMsc。
18. **控件与默认显示留 UX Design**：本题不决定 Date Picker、Time Picker、快捷日期范围、列表默认显示多少位时间或 UI 是否默认展开毫秒字段。

### 决定范围与后续设计边界

- 前一轮选项 A（MT5也仅允许秒级边界）未被采用。MT5输入可以精确到毫秒，但不能将其扩大为微秒/更高精度支持；具体输入语法和校验交互留Technical Design/UX，不把超出支持精度的值静默改成另一分析边界。
- MT5省略毫秒定义的是用户指定的整秒边界，不是补造Trade Event精度；MT4统一控件携带.000只是有效秒边界的表示，不意味着MT4获得毫秒级Event或窗口能力。
- 从粘贴、历史恢复或平台切换得到非零毫秒的MT4输入，同样受拒绝且不能自动修整的规则约束；具体反馈方式不在本轮选择控件或技术栈。
- 版本持久化与重新打开的范围必须保持原值和平台精度，不能因列表默认显示简化而丢失边界或覆盖原始Metadata；最终哪些列表默认显示几位仍是UX决定，不影响本题的精度保留要求。
- Quote Retrieval继续根据实际Candidate的RequiredTargets及必要前序覆盖获取数据；MT5原始TimeMsc、MT4SECOND、Q02确定性映射和Q22成熟性/可用性规则均不变。Single Trade Test不得因新分析表单的输入精度改变真实Event时间。
- 毫秒边界支持不宣称Manager API支持毫秒查询参数；实际读取按获证接口能力覆盖获取，再按Q06第一小项精确过滤。接口契约、时间精度和历史读取完整性仍需Technical Evidence。
- Q06两项计算相关业务规则已确认，剩余日期快捷/显示方式与控件细节留UX Design；不因为进入后续评审而启动UI实现或技术栈选择。

### 待统一 Revision 的影响清单（本轮未修改）

| 文档 | 之后需同步的内容 |
|---|---|
| 01 Product Scope / 03 Calculation Specification | MT5可选毫秒、秒输入=.000、MT4严格秒；原始Event精度、半开区间和Target规则不变 |
| 02 Architecture / 04 Logical Model | 平台精度校验与Version Metadata；历史恢复保留原始边界精度，读取API能力与业务输入能力分离 |
| 05 State Model / 06 UX / 07 Wireframes | 非零毫秒MT4输入明确拒绝、不修整继续；可选精确输入与简洁日常操作，控件/显示细节另定 |
| 08 Acceptance | MT5毫秒范围、秒输入整秒边界、MT4非零毫秒拒绝/000接受、Metadata重开/导出保真、Single Trade Test不受影响 |
| 10 Assumptions / 12 Technical Evidence / README / 09 Questions | 业务规则与SDK事实分开，09关闭第二小项并保留UX与技术验证边界，未实施UI或存储 |

## Q12 — DECIDED — B：Candidate Membership 无法确定时阻止版本正式发布

- Priority：P0。
- Status：**DECIDED — B / CLOSED**。
- User Choice：**B，以下列二十项边界为完整决定**；这是用户已经完成的业务决定，不重新提问或评审。
- Decision Source：用户明确授权“确认补写 Q12 Decision”，并提供完整二十项最终边界。本节为唯一权威 Q12 Decision；此前下一项评审中的未决定提案已被本决定取代。
- Architecture Revision：**PENDING BATCH REVISION**；仅补写本决定并同步 Open Questions / Review Queue，不修改 Architecture / Calculation Specification 正文。
- Implementation / Technical Verification：未进入 Implementation；未执行 Manager / Terminal 接入、SDK 能力验证或数据库创建。

### 已确认业务边界

1. **严格半开区间**：Candidate Filtering 严格使用 `[From, To)`，即 `From <= Event Time < To`。
2. **MT5 使用有效原始 TimeMsc**：正式 Candidate Filtering 使用有效原始 TimeMsc。禁止回退秒级 Time、假设毫秒为 .000 或 .500、截断、四舍五入、根据 Deal ID 推测时间，或根据附近 Tick / 价格走势推测成交时间。
3. **候选总体完整性无法证明的触发条件**：某条 Manager 返回记录仍可能属于当前 Analysis Range，但因缺少业务所需的有效时间精度，无法确认是否满足 `[From, To)`，则 Candidate Population 无法证明完整。
4. **阻止本版本正式发布**：上述情况下，当前 Analysis Version 不发布正式 Multi-account Overview Markout Result、Account Summary、Symbol Summary、Symbol Markout Curve 或正式 Analysis Rate。
5. **独立的总体归属问题**：这不是 Event Markout Calculation Failure，而是 Candidate Population / Range Membership Unresolved。
6. **独立状态与诊断语义**：需要独立语义，概念上可使用 `RANGE_MEMBERSHIP_UNRESOLVED` 或 `CANDIDATE_SET_UNRESOLVED`；最终命名由 State Model Review 统一。
7. **保留原始异常记录**：Diagnostics 至少保留 Login、Deal ID、Order ID、Position ID（如存在）、Symbol、Side、Volume、Price、Raw Time、Raw TimeMsc / Invalid Value、Query Range、Failure Reason。
8. **禁止强行归类或删除**：该记录不能被静默删除、强行算作 Candidate，或强行算作 Non-Candidate。
9. **总体数量不能伪装成完整已知值**：Candidate Total 不能显示为一个已知完整数字。
10. **禁止用已知子集冒充总体分析率**：Analysis Rate 不能基于 Known Candidates 计算后，冒充完整总体 Analysis Rate。
11. **诊断计数分开展示**：Diagnostics 可以分别展示 Confirmed Candidate Events 与 Unresolved Membership Records，但不能把它们组合成一个伪精确的正式 Analysis Rate。
12. **单条不确定记录也会阻止发布**：即使只有一条记录存在 Membership Uncertainty，只要它仍可能属于本次 Analysis Range，当前 Version 就不能发布完整正式 Summary。
13. **与 Event-level Isolation 的边界**：Event-level Isolation 的前提是 Candidate Membership 已经确定。Q12 的上述场景不满足该前提，因此不与局部隔离原则冲突。
14. **可靠证明在范围外**：后续可靠证据确认记录在范围外时，它不是 Candidate，不再阻塞 Candidate Population。
15. **可靠证明在范围内**：后续可靠证据确认记录在范围内时，它成为 Candidate，再按正常 Event Eligibility 和 Markout Calculation 规则处理；成员归属确定不等于自动满足计算资格。
16. **重分析保留历史版本**：新的 Manager Data / SDK 能提供有效时间信息时，Re-analyze 创建新 Version，不修改旧 Version。
17. **MT4 合法秒精度不触发本问题**：MT4 本身只有 SECOND Precision；原始 Event Time 满足已确认 MT4 时间契约时，不因为没有毫秒触发 Q12。
18. **MT4 缺失必要秒精度的情况**：如果连业务所需 SECOND Precision Time 都无法可靠取得，同样属于 Candidate Membership 无法确定。
19. **已确定成员但 MT5 时间无效须另行分类**：Candidate Membership 本身已能够确定，但 MT5 Event 缺少有效 TimeMsc 时，不使用 `RANGE_MEMBERSHIP_UNRESOLVED`。这是 Candidate 已确定、Event Time Precision 不满足 MT5 Markout Calculation 要求，按 `INVALID_DEAL_TIME_MSC` 或 State Model 最终确定的对应 Event-level 原因处理。
20. **两个层级不得混淆**：A. Candidate Membership 无法确定 → Version-level publication blocked；B. Candidate Membership 已确定，但 Event 缺少有效 TimeMsc → Event-level calculation eligibility failure。State Model 必须区分两者。

### 决定范围与后续设计边界

- 本决定关闭 Q12 业务选择；最终状态名称、状态层级表达和技术验证方法留后续 State Model / Technical Review，不据此重新开放 A/B 选择，也不擅自指定普通 FAILED、Future Target 或同步未就绪状态。
- 第14、15项的可靠证据不包括第2项禁止的时间补造或推断。已经结束的旧 Version 不因后来取得证据自动修改；按第16项创建新 Version。
- Q06 的用户范围输入省略毫秒可解释为 .000，与给缺失 TimeMsc 的成交记录补造毫秒完全不同；本决定不改变 Q06 已确认的输入规则。
- 第19项的已确认 Candidate 仍按正常 Eligibility 处理；Q12 不定义新的 Trade Price / Point / Volume 资格。Q11筛选分子/分母后续已由第一小项B确认，零候选Rate具体表达及原因区分已由第二小项A确认；多原因统计展示已由Q11第三小项A确认，Q12的未知总体不得被当作已知空集合。

## Q05（第三小项）— DECIDED — A：正式 Markout Analysis 要求 Trade Price > 0

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**PARTIALLY SUPERSEDED — 仅Money引用：本项Bid/Ask、Trade Price、真实Event Volume、Point及Numeric Validity等非Money规则继续有效；Money计算、汇总、状态、示例与资格引用不再适用。Q05六项业务保持CLOSED，不重复提问。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P0。
- Status：**DECIDED — A，本小项决定保留**；Q05六项现均已决定，整体为**CLOSED**，见第六小项后的闭合检查。
- User Choice：**A，以下列十八项补充边界为完整决定**。
- Decision Source：用户在Architecture Review中对Q05第三小项的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**；本轮仅更新独立Decision Register、Open Questions与Review Queue，不修改Architecture / Calculation Specification正文。
- Implementation / Technical Verification：未进入Implementation，未创建程序、选择技术栈、接入Manager / Terminal或创建数据库；该决定不代表实际SDK能力已验证。

### 已确认业务边界

1. **统一正成交价要求**：V1正式Markout Analysis统一要求 `Trade Price > 0`。
2. **非正成交价无正式资格**：Trade Price = 0或Trade Price < 0的Event，不具备正式Markout Calculation / Summary Eligibility。
3. **不建立例外**：V1不建立Zero Trade Price Exception、Negative Trade Price Exception、Symbol-specific Exception、Server-specific Exception或Historical-period Exception。
4. **禁止修补或替代成交价**：不得为通过校验修改Trade Price、取绝对值、用Open / Close附近Quote替代、用Bid / Ask替代成交价、用Order Price替代Deal Price、插值或推算成交价格。
5. **MT5使用Deal自身价格**：使用该Deal自身真实成交Price。
6. **MT4分别校验两类Event**：OPEN Event使用OpenPrice，CLOSE Event使用ClosePrice；两者分别执行Trade Price Validity校验。
7. **整个Event退出全部正式聚合**：Trade Price <= 0时，整个Event不进入Points Summary、Money Summary、Symbol Markout Curve、Account Summary或Multi-account Overview聚合。
8. **原始Event不删除**：原始交易数据必须保留在Deal / Event Detail、Diagnostics、Historical Version Data和Discarded Export。
9. **独立Event-level原因**：使用独立、明确的原因，概念上例如 `INVALID_TRADE_PRICE`；最终名称由State / Error Model Review统一。
10. **已确定Candidate的局部资格失败**：Trade Price无效属于Event-level Eligibility Failure。Candidate Membership已经确定时，不因这一笔成交价无效而阻止其他可靠Events发布正式结果。
11. **计数保留在候选总体**：该Event计入Total Candidate Events，不计入Fully Analyzed / Summary Eligible Events。
12. **最少诊断字段**：Diagnostics至少保留Login、Deal ID / Ticket、Event Type、Symbol、Side、Volume、Event Time、Raw Trade Price、Failure Reason。
13. **Single Trade Test明确失败**：真实Manager Trade Price <= 0时，必须明确显示 `INVALID_TRADE_PRICE`，不继续生成看似正常的Markout Result。
14. **正价仅通过本项校验**：Trade Price > 0不代表Event已经Eligible；仍须满足Time Validity、Quote Completeness、Quote Validity、Points Calculation、Money Calculation及其他已经确认的Eligibility Rules。
15. **不增加价格阈值**：本题不设置Maximum Trade Price、除>0外的Minimum Positive Trade Price，或Trade Price Deviation Threshold。
16. **不按附近行情判断成交异常**：V1不比较Trade Price与附近Market Quote来判断成交价格是否异常；这种Execution Quality / Slippage分析已明确不属于V1。
17. **价差大不构成本项无效**：即使Trade Price与当时市场报价差异很大，只要Trade Price > 0，不因偏离程度在本项中判无效。
18. **特殊产品留未来评审**：未来若确需支持合法零价 / 负价交易产品，作为Future Requirement单独评审；V1不提前建立Exception Framework。

### 决定范围与仍未决定事项

- 第10、11项以Candidate Membership已确定为前提；归属无法确定时仍遵守Q12的Version-level publication blocked，不强行把未知记录算入Total Candidate。
- MT4 OPEN与CLOSE分别校验，不能仅因共用Ticket就把一端的无效成交价自动扩展成另一端无效；各Event仍须满足其余全部资格规则。
- 保留原始交易记录与诊断不构成继续生成正常Markout Result的许可；Single Trade Test同样遵守第13项，不能用替代价格生成结果。
- Q05后续六项现已全部闭合，整体CLOSED。具体技术原因枚举、字段映射与证据仍待Review；已确认的Bid/Ask、Trade Price、Event Volume、Point及统一Numeric Validity规则不得重复提问。
- 第14项的正价条件不是完整数值有效性定义；本题不新增规格恢复、Volume修复、数值默认值、其它计算路径或异常修补方案。

## Q05（第四小项）— DECIDED — A：正式 Markout Analysis 要求真实 Event Volume > 0

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**PARTIALLY SUPERSEDED — 仅Money引用：本项Bid/Ask、Trade Price、真实Event Volume、Point及Numeric Validity等非Money规则继续有效；Money计算、汇总、状态、示例与资格引用不再适用。Q05六项业务保持CLOSED，不重复提问。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P0。
- Status：**DECIDED — A，本小项决定保留**；Q05六项现均已决定，整体为**CLOSED**，见第六小项后的闭合检查。
- User Choice：**A，以下列二十二项补充边界为完整决定**。
- Decision Source：用户在Architecture Review中对Q05第四小项的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**；仅更新独立Decision Register、Open Questions与Review Queue，不修改Architecture / Calculation Specification正文。
- Implementation / Technical Verification：未进入Implementation，未选择技术栈、创建程序/数据库或接入Manager / Terminal；原始量编码、单位换算及MT4 Partial Close能力未由本决定自动验证通过。

### 已确认业务边界

1. **统一正成交量要求**：V1正式Markout Analysis统一要求 `Event Volume > 0`。
2. **非正成交量无正式资格**：Event Volume = 0或Event Volume < 0的Event，不具备正式Markout Calculation / Summary Eligibility。
3. **使用真实Event实际成交量**：Event Volume必须来自Manager API返回的该真实Deal / Event实际成交量；禁止用Order Request Volume、Position Volume或Original Order Volume替代。
4. **MT5逐Deal使用自己的量**：使用实际Deal Volume。同一Order产生多个Deal时，每个Deal使用自身实际Deal Volume，不合并。
5. **MT4按真实Event取量**：OPEN / CLOSE Event必须使用各自实际对应成交量；Partial Close不能简单用原始Order Lots代替实际平仓量。
6. **Partial Close取量必须有证据**：如何从Manager API / Order History可靠取得MT4 Partial Close真实成交量，属于Technical Evidence Requirement。当前SDK数据模型不能可靠确定实际Event Volume时，不允许猜测。
7. **禁止修补、替代或反推**：不得为通过Volume校验填默认0.01 lot、取绝对值、用Order Volume替代实际Deal Volume、使用Position Volume、从Profit或Commission反推Volume，或从附近交易推测Volume。
8. **不建立成交量例外**：V1不建立Zero Volume Exception、Negative Volume Exception、Symbol-specific Volume Exception或Server-specific Volume Exception。
9. **正量仅通过本项**：Volume > 0不代表Event已Summary Eligible；仍须满足其他全部Eligibility Rules。
10. **整个Event退出正式聚合**：Volume <= 0时，整个Event不进入Avg Points Summary、Volume Weighted Points Summary、Money Summary、Symbol Markout Curve、Account Summary或Multi-account Overview聚合。
11. **原始Event不删除**：保留在Deal / Event Detail、Diagnostics、Historical Version Data和Discarded Export。
12. **独立Event-level原因**：设计独立原因，概念上例如 `INVALID_EVENT_VOLUME`；最终名称由State / Error Model Review统一。
13. **已确定Candidate的局部资格失败**：Candidate Membership已经确定时，Volume无效属于Event-level Eligibility Failure，不阻止其他可靠Events发布结果。
14. **保留候选计数**：该Event计入Total Candidate Events，不计入Fully Analyzed / Summary Eligible Events。
15. **缺失与真实零量区分**：Missing / Unavailable Volume不能自动解释为Volume = 0；Diagnostics必须区分缺失/不可得与Actual Volume = 0。
16. **无效数值/未知单位不能正式计算**：Volume字段无法解析、NaN、Infinity、Missing或Unit Unknown时，不应进入正式计算。具体状态可在Q05后续“缺失 / 非有限数值”问题中统一定义，不重开这项准入边界。
17. **显示舍入不能改变真实量**：Volume内部计算必须避免因UI显示精度或四舍五入，把真实正成交量变成0。
18. **使用标准化后的真实精确量计算**：UI可按合理小数位显示Volume；Eligibility与Money Calculation必须使用标准化后的真实精确成交量。
19. **统一Eligible集合的加权公式**：Volume Weighted Avg Points只使用Summary Eligible Events，公式为 `Σ(Points Markout × Event Volume) / Σ(Event Volume)`。
20. **不建立非空Eligible集合的零总量规则**：所有Eligible Events均要求Volume > 0，因此正常Symbol Summary中只要存在Eligible Event，Σ(Event Volume)必然>0；不需为正式Summary建立“Eligible Events存在但总Volume=0”的特殊业务规则。
21. **空Eligible集合不伪造零结果**：没有任何Eligible Event时，不生成0 Points、0 USD或0 Volume Weighted Markout，按已确认的No Eligible Results语义展示。
22. **编码与单位转换留技术取证**：本题不决定Volume Unit Conversion、Manager API Raw Volume Encoding或MT4 / MT5 SDK Volume Scaling；必须通过Technical Evidence确认后在Adapter层标准化。

### 决定范围与后续评审边界

- 本决定取代此前“允许获证零量进入正式集合”的B提案，零量准入不再OPEN。Q11不再保留有Eligible Events但ΣVolume=0或无Eligible时显示伪零Markout的业务选项；筛选口径后续已由Q11第一小项B确认，Total Candidate=0时Rate的具体表达及原因区分已由第二小项A确认；多原因统计展示已由Q11第三小项A确认，Q11整体CLOSED。
- 第13、14项以Candidate Membership已确定为前提；不能以Volume失败替代Q12的总体成员归属不明处理，也不能把未定归属记录强行计入已知完整Candidate Total。
- 规范化必须忠实于经验证的实际成交量契约；第22项不授权取绝对值、补默认量或推算实际成交量。原始字段与标准化真实量、显示舍入值应区分，不能用后者反过来决定Eligibility。
- Q10仍须证明MT4 Partial Close等场景的真实Event量与身份/覆盖；本轮不选择接口、读取组合、量编码或换算倍率，不用业务批准代替证据。
- 后续Q05第五/第六小项已分别决定Point失败后的Native Money执行边界和统一Numeric Validity契约，整体CLOSED。具体原因名称与字段技术证据仍待Review，不重复开放Point不可用时的正式资格。

## Q05（第五小项）— DECIDED — B：Point失败后不主动执行尚未执行的Native Money

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**PARTIALLY SUPERSEDED：Money后续执行、结果、NOT_EXECUTED及诊断专用Money分支全部NO LONGER APPLICABLE TO V1。Point不可用仍失去正式资格；非Money已得事实、真实阶段状态、禁猜/手填Point、Single Test同一边界继续有效，Q05保持CLOSED。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P0。
- Status：**DECIDED — B，本小项决定保留**；Q05六项现均已决定，整体为**CLOSED**，见第六小项后的闭合检查。
- User Choice：**B，以下列二十项补充边界为完整决定**。
- Decision Source：用户在Architecture Review中对Q05第五小项的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**；本轮仅更新独立Decision Register、Open Questions及Review Queue，不修改Architecture / Calculation Specification正文。
- Implementation / Technical Verification：未进入Implementation，未执行SDK接入或计算流程实现；不以本决定宣称任何Native Money能力已经验证。

### 已确认业务边界

1. **Point不可用即无法完成Points资格**：Event的Point无法可靠取得时，Points Markout无法计算，该Event已确定失去正式Summary Eligibility。
2. **不再主动发起未执行的Native Money**：一旦因Point不可用已确定该Event不能成为Summary Eligible Event，V1不再主动发起该Event尚未执行的Native Money Calculation。
3. **本场景的执行原则**：Event已确定没有正式资格后，不为增加诊断信息继续执行不影响Eligibility结论的额外业务计算；适用范围由第19项限定。
4. **已取得资料全部保留**：在Point Failure确认前已成功取得的Trade/Event Data、Matched Quotes、Bid / Ask / Mid、Price Markout、已完成的Money Markout、Symbol Specification和Calculation Evidence全部保留，不删除或忽略。
5. **已成功Money仍可核查**：Point Failure确认前已成功完成的Money Result保留，可在Deal / Event Detail、Diagnostics、Single Trade Test、Historical Version Data和Discarded Export查看。
6. **不改写已成功金额的事实**：已成功Money Result不得因随后Point Failure被改写为Failed、Not Calculated或0 USD。
7. **未执行不标为计算失败**：Point Failure确认时Money尚未执行，其状态表达为Not Calculated / Skipped Due To Prior Eligibility Failure，而不是 `MONEY_CALCULATION_FAILED`。
8. **执行失败与未执行明确区分**：区分A. Money Calculation实际执行并失败；B. 因Point Failure已确定Event无正式资格，因此Money Calculation未执行。
9. **未执行不计Money失败数**：第8项B不得计入Money Calculation Failure Count。
10. **分别展示资格失败与计算状态**：Diagnostics必须能看到Primary Eligibility Failure为 `POINT_UNAVAILABLE / INVALID_POINT`，以及Money Calculation为 `NOT_EXECUTED`；具体状态名称由State / Error Model Review统一。
11. **退出所有正式聚合**：Event不进入Avg Points、Volume Weighted Points、Money Summary、Symbol Markout Curve、Account Summary或Multi-account Overview聚合。
12. **保留候选计数**：Event仍计入Total Candidate Events，不计入Fully Analyzed / Summary Eligible Events。
13. **Single Trade Test使用同一计算边界**：Point无法可靠取得时，明确展示Point = Unavailable、Points Markout = Not Calculated；Money已成功则展示实际结果，尚未执行则展示Not Calculated。不为Single Trade Test另建计算流程。
14. **Trace说明停止位置**：Calculation Trace必须说明计算停止的位置；例如Quote Matching = SUCCESS、Price Markout = SUCCESS、Point Resolution = FAILED、Points Markout = NOT CALCULATED、Money Calculation = NOT EXECUTED。示例不代表每个Event必然已完成这些前置步骤，Trace保留实际执行事实。
15. **不新增诊断模式或开关**：V1不建立Diagnostic-only Money Calculation Mode、Money-only Event Mode、Force Calculate Money或Ignore Point Failure等额外模式/用户开关。
16. **禁止手工补Point继续计算**：不允许用户手工提供Point使该Event继续计算。
17. **禁止替代Point或上下文**：不允许用Default Point、相近Symbol Point、其他Server Point或其他Account / Group Context补齐。
18. **诊断专用Money留未来评审**：未来若Money-only Diagnostic Calculation有明确调查价值，可作为Future Requirement单独评审；V1不做。
19. **不泛化停止规则**：本决定仅针对“Point已经不可用，而Money尚未执行”的执行顺序，不自动推广为所有Event Failure都必须立即停止所有后续处理。
20. **后续区分必要计算与可选补充**：State / Calculation Pipeline Design应明确Required Eligibility Calculation与Optional Diagnostic Enrichment；V1应尽量避免在正式资格已确定失败后继续不必要的Optional Enrichment。

### 决定范围与后续设计边界

- 本决定取代此前推荐的A方案，即使认为可取得独立可靠Money，也不能在Point Failure已确认后为增加诊断信息主动发起该Event尚未执行的Native Money。既有成功结果继续保留，不把Event资格与单项计算执行事实合并成同一个状态。
- 第12项以Candidate Membership已确定为前提；成员未知仍按Q12阻止版本正式发布，不能用Point Failure替代范围归属未定处理或制造确定Candidate Total。
- 本题不新增正在执行的Money调用的中止/回收策略，也不据此修改已结束Version；技术流程须忠实记录实际执行状态，不把进行中、未执行、成功、失败混淆。
- Point有限正值、缺失/不可解析/非有限输入及计算产物异常的统一业务契约已由第六小项闭合；Q05整体CLOSED。已有资格和本次执行边界不再提问，具体技术枚举与证据留后续工作。

## Q05（第六小项）— DECIDED — A：统一 Numeric Validity / Error Classification Contract

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**PARTIALLY SUPERSEDED — 仅Money引用：本项Bid/Ask、Trade Price、真实Event Volume、Point及Numeric Validity等非Money规则继续有效；Money计算、汇总、状态、示例与资格引用不再适用。Q05六项业务保持CLOSED，不重复提问。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P0。
- Status：**DECIDED — A**；Q05全部六个业务子项已完成，整体状态为**CLOSED**。
- User Choice：**A，以下列三十项补充边界为完整决定**。
- Decision Source：用户明确确认统一Numeric Validity Contract及三十项边界，并授权在业务子项齐备后关闭Q05。
- Architecture Revision：**PENDING BATCH REVISION**；仅更新独立Decision Register、Open Questions及Review Queue，不修改Architecture / Calculation Specification正文。
- Implementation / Technical Verification：未进入Implementation；业务闭合不等于SDK字段、单位、数值精度或Sentinel能力已经验证。

### 已确认业务边界

1. **Point完整资格**：Point必须来自已确认适用于目标账户 / Group / Symbol / Server的Analysis-Time Specification，并满足Present、Parseable、Finite、Point > 0、Unit / Semantic已确认，才能用于Points Markout。
2. **禁止补造或替代Point**：禁止填默认值、猜测、从Digits自行推导替代已要求的正式Point、使用相近Symbol/其他Server/其他Account或Group Context，以及将Missing / Invalid Point替换为0。
3. **实际路径必要输入的共同契约**：所有实际计算路径需要使用的Numeric Input，在参与对应计算前必须满足Present、Parseable、Finite、Unit / Semantic Known。
4. **非有限值明确排除**：Finite明确排除NaN、+Infinity、-Infinity。
5. **缺失与解析错误区分**：Missing / Null / Empty / Unavailable必须与Parse Error区分。
6. **异常与真实零值不互换**：MISSING、UNAVAILABLE、PARSE_ERROR、NaN、+Infinity、-Infinity、UNIT_UNKNOWN、ACTUAL_ZERO这些概念不得互相替代。
7. **有限性不等于统一正值要求**：Numeric Validity Contract不要求所有Numeric Value都>0；Zero / Negative是否允许，由各字段自己的业务语义决定。
8. **既定正值字段保持**：Bid > 0、Ask > 0、Trade Price > 0、Event Volume > 0、Point > 0继续适用。
9. **允许真实零结果**：Price Markout = 0、Points Markout = 0、Money Markout = 0 USD、Mid Markout = 0、Quote Age = 0，只要由有效输入和合法路径真实计算得到，就允许。
10. **真实零与未取得/未计算/失败区分**：正常计算得到的0必须与Missing、Not Calculated、Calculation Failed明确区分。
11. **禁止异常填零**：禁止Missing → 0、Parse Error → 0、NaN → 0、Infinity → 0、Unit Unknown → 0、Calculation Failure → 0。
12. **有数值但单位语义不明也不能计算**：Unit / Scaling / Semantic无法确认的字段不能参与正式计算。例如Raw Volume = 100，不能证明是100 lots、1.00 lots或其它scaling时，属于UNIT / SEMANTIC UNRESOLVED，不能直接使用100。
13. **Adapter按获证契约标准化**：Adapter / Normalization Layer将经过技术验证的Raw SDK Value + Scaling + Unit转换为统一业务值。
14. **标准化保留含义与证据**：Normalization不能改变业务含义；必须保留必要的Raw Value、Normalized Value、Unit / Scaling Evidence，供Diagnostics / Technical Evidence追溯。
15. **有效输入下的数值异常属于计算阶段**：所有输入均有效，但Calculation产生Overflow、NaN、+Infinity、-Infinity或其它Non-finite Result时，记录为Calculation-stage Failure。
16. **不倒推输入无效**：Calculation-stage Failure不得反向归因为INVALID_TRADE_PRICE、INVALID_QUOTE、INVALID_VOLUME或INVALID_POINT，除非相应输入本身确实违反规则。
17. **输入验证失败与执行失败分开**：Diagnostics必须区分Input Validation Failure与Calculation Execution Failure。
18. **Trace指出实际失败阶段**：例如Quote Matching SUCCESS、Price Markout SUCCESS、Point Resolution SUCCESS、Points Markout OVERFLOW；或Money Calculation NON_FINITE_RESULT。Trace须说明实际在哪个阶段失败，示例不替代实际执行事实。
19. **非有限结果不成功、不聚合**：非有限Calculation Result不得标记SUCCESS，不得进入Summary、Curve或Overview Aggregation。
20. **可可靠定位时局部隔离**：异常能可靠隔离到具体Event时，按已确认Event-level Isolation处理；受影响Event失去Summary Eligibility，其他可靠Events可以继续。
21. **整体可靠性问题升级处理**：异常影响范围不能可靠界定，或表明Calculation Engine / Numeric Handling对当前整个Version不可靠时，不能套用局部Event隔离，应升级Version / Task-level Reliability问题。
22. **既有规则不变**：本契约不改变Q05前五项、Q09、Q12、Q13、Q22等已确认业务规则。
23. **技术细节留技术设计/证据**：本决定仅建立统一Numeric Validity / Error Classification Contract；SDK Field Mapping、Raw Encoding、Volume Scaling、Decimal Precision、Numeric Type、Overflow Boundary、Error Code由Technical Design / Technical Evidence确定。
24. **可靠零金额仍有效**：正常可靠计算得到的0.00 USD继续为有效Money Result。
25. **可靠零Points仍有效**：正常计算得到的0 Points继续为有效Points Result。
26. **执行报价等于成交价时零价差正常**：Future executable quote恰好等于Trade Price，Price Markout = 0是正常结果。
27. **报价与目标同刻时零Age正常**：Quote Time = Target Time，Quote Age = 0是正常结果。
28. **不增加非零阈值或风险过滤**：不得因统一Numeric Validation增加Minimum Non-zero Markout、Minimum Money Amount、Minimum Points Threshold或Epsilon-based Risk Filter；V1不需要这些业务阈值。
29. **数值实现不得制造业务误分类**：浮点 / Decimal实现方式由Technical Design确定，但必须避免因Binary Floating Point Artifact把正常业务值错误分类为Non-zero、Invalid或Non-finite。
30. **Sentinel语义先取证再映射**：Technical Review发现Manager / Terminal SDK Numeric Field有特殊Sentinel Value时，必须先提供Technical Evidence，再映射为Missing / Unavailable / Invalid；禁止猜测Sentinel含义。

### 决定范围与后续技术工作

- 正常真实零结果与没有Eligible Events是不同场景：前者按本决定保留有效值，后者继续按No Eligible Results展示，不伪造0 Points / 0 USD / 0 VW Markout。
- 第20项局部隔离以影响范围确实可界定及Candidate Membership已确定为前提；第21项已确定需要整体可靠性升级，不以它重复询问是否允许继续使用不可靠路径。最终Version/Task终态与技术错误枚举由后续State Review统一，本题不擅自指定普通FAILED或Not Ready。
- 第五小项B继续适用：Point失败后未执行Money不主动补算，NOT_EXECUTED不计实际Money失败；已有成功Money不改写。统一数值契约不把这一执行顺序扩大为所有Event Failure的全流程停止规则。
- SDK字段映射、数值类型/精度、单位/Scaling、溢出界限、Sentinel证据、具体错误码仍需技术设计与验证；不因这些技术任务尚未完成而重开已决定业务规则，也不因按字段展开测试而重复提出同一数值契约。

### Q05 业务闭合检查

| 原问题覆盖范围 | 已决定依据 | 业务状态 |
|---|---|---|
| Bid / Ask零负值与禁止修补 | 第一小项A | DECIDED |
| Ask >= Bid、零点差、倒挂及不加Spread阈值 | 第二小项A | DECIDED |
| Trade Price资格、平台价格来源及局部失败边界 | 第三小项A | DECIDED |
| Event Volume资格、真实量来源、显示与计算精度及零集合边界 | 第四小项A | DECIDED |
| Point失败后Money未执行/已成功结果及诊断执行边界 | 第五小项B | DECIDED |
| Point有效性、Missing/Parse Error/非有限值、单位语义、合法零值、计算阶段失败、局部/整体升级及Sentinel证据 | 第六小项A | DECIDED |

**Q05 Overall Status: CLOSED — 六个业务子项均已完成决定。** 未发现Q05原范围内尚未决定的业务子项；退出待问队列。Q04最终报价原因分类、其它问题的状态模型及技术证据继续在各自评审项处理。此关闭不执行Architecture Revision，也不授权Implementation。

## Q11（第一小项）— DECIDED — B：Analysis Rate 跟随当前正式统计范围

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**CURRENT APPLICABILITY — Money相关分支PARTIALLY SUPERSEDED：本节历史Money计算/指标/字段/资格/证据引用已不适用于V1；其余已确认的时间、匹配、统计Scope、隔离/发布、导出、重跑与历史冻结规则保持。具体范围见14影响矩阵，不能因删除Money扩大其它业务范围。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P0 — 统计总体与计数正确性。
- Status：**DECIDED — B**；后续第二小项A与第三小项A均已决定，Q11整体**CLOSED**。
- Decision Source：用户明确选择B，并确认以下二十四项边界；不是重新分析或改写Event资格的授权。
- Architecture Revision：**PENDING BATCH REVISION**。本轮仅更新Decision Register、Open Questions及Review Queue，未修改Architecture / Calculation Specification正文，未进入Implementation。

### 已确认业务定义

1. **统一当前范围公式**：Analysis Rate必须与当前正式统计范围一致，始终定义为Eligible Events in Current Statistical Scope ÷ Candidate Events in Current Statistical Scope。
2. **无统计筛选时为总体**：没有Side / Entry / Event统计筛选时，显示总体Analysis Rate。例如Candidate=100、Eligible=80，Rate=80%。
3. **BUY统计范围**：切换BUY时，若Candidate BUY=20、Eligible BUY=10，则Rate=50%。
4. **SELL统计范围**：SELL采用与BUY相同规则，使用SELL自己的Candidate与Eligible。
5. **MT5 Entry范围**：ALL、IN、OUT、INOUT、OUT_BY为正式统计范围时，Candidate分母与Eligible分子必须同时应用同一Entry条件。
6. **MT4 Event范围**：ALL、OPEN、CLOSE同样按照当前Event统计范围计算Analysis Rate。
7. **禁止只筛分子**：不能只筛选Eligible分子，却继续使用总体Candidate分母。
8. **Discarded仍在分母**：已确认属于Candidate且满足当前统计筛选条件的Event，即使被Discarded，也必须计入当前Candidate分母；不得因排除资格而从分母移除。
9. **只改变视图**：统计筛选不改变Event Eligibility、Historical Version、原始Event数据或Discard Reason。
10. **明确标注Scope**：UI必须让用户知道Rate对应的范围，例如ALL — 80 / 100 — 80%、BUY — 10 / 20 — 50%；具体表现由UX Design决定。
11. **关联指标范围一致**：切换统计范围后，相关Summary Metrics、Analysis Rate、Markout Curve（页面支持该统计维度时）使用一致的Event Population。正式Metrics / Curve仍只用该Scope内Eligible Events，Rate使用同Scope内Eligible / Candidate，不能据此删除Discarded分母。
12. **不得暗中混用范围**：不能显示BUY Markout Summary却搭配ALL Analysis Rate而不明确说明两者范围不同；当前正式Rate仍遵守本决定的Scope规则。
13. **取消筛选恢复总体**：取消统计筛选后，恢复原总体Eligible / Candidate。
14. **动态View Metric**：Analysis Rate变化不属于Re-analyze，不创建新Version，不修改SQLite中Event的原始Eligibility。本条记录后续设计约束，不授权当前创建或修改SQLite。
15. **历史视图的基础数据**：Historical Version保存计算Candidate / Eligible所需的基础Event状态；Rate可以依据这些已保存数据，按当前正式统计Scope重新计算。
16. **仅限已定义维度**：只有已正式定义的统计维度可以影响Rate，目前包括Side、MT5 Entry、MT4 Event Type。
17. **普通表格操作不改变Rate**：Deal Detail中的Text Search、Deal ID Search及临时UI排序，不自动改变正式Analysis Rate。
18. **Symbol属于明确范围**：进入明确的Symbol Summary后，该Symbol就是正式Statistical Scope的一部分，Rate基于该Symbol自己的Eligible / Candidate。
19. **Account默认范围**：Account Summary默认Scope为当前Account全部Candidate Events。
20. **Symbol默认范围**：Symbol Summary默认Scope为当前Account + 当前Symbol。
21. **逐Login独立计数**：Multi-account Overview中每个Login的Rate独立基于自己的Candidate / Eligible Events，禁止跨Login合并为一个总体Analysis Rate。
22. **零分母不显示0%**：当前正式Scope的Candidate Count=0时，0/0没有统计意义，不能显示Rate=0%。具体采用N/A、—或No Candidates的显示规则留给Q11后续子项。
23. **不决定组合筛选**：本题不决定BUY + IN等多个统计筛选的组合规则；既有V1不做复杂BUY × Entry交叉统计的边界不变。Q16未因本题自动关闭或获得组合筛选授权。
24. **Diagnostics不是正式统计Scope**：Diagnostics Filter属于调查视图，不改变正式Summary Analysis Rate。

### 适用边界与评审状态同步

- Q11第一小项仅有本节一份权威DECIDED — B，原待选提案已被本决定替代，不再列为待问题。Q05整体CLOSED、Q12 DECIDED — B / CLOSED保持不变。
- 已确认Candidate且被Discarded的Event仍在同Scope分母；Q12规定的Candidate Population完整性未知不能伪装为已知0，也不能通过筛选已知记录绕过Version-level正式发布限制。
- 从Historical Version已保存的Event状态计算View Metric，不重算旧Money / Points、不修改旧Eligibility、不创建新Version。
- 第一小项第22条当时保留的零候选显示，后续已由第二小项A确认：— / No Candidates；该子项不再OPEN。全体Candidate未知与当前Scope已知无Candidate必须区分，具体状态名称及Task / Version终态留State Model Review。
- Q22第四小项所关联的多原因统计展示已由Q11第三小项A闭合；同原因内Event去重、跨原因允许重叠，不建立原因优先级。Q05第五小项NOT_EXECUTED不计Money Failure的规则保持。
- 仅同步评审记录与队列；其它Architecture文档的统一修订仍待后续授权阶段。

## Q11（第二小项）— DECIDED — A：No Candidates 的 Rate 显示与原因区分

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**CURRENT APPLICABILITY — Money相关分支PARTIALLY SUPERSEDED：本节历史Money计算/指标/字段/资格/证据引用已不适用于V1；其余已确认的时间、匹配、统计Scope、隔离/发布、导出、重跑与历史冻结规则保持。具体范围见14影响矩阵，不能因删除Money扩大其它业务范围。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P2 — Display / Presentation Contract。
- Status：**DECIDED — A**；后续第三小项A已决定，Q11整体**CLOSED**。
- Decision Source：用户明确选择A，并确认以下十九项边界。以下是完整决定，不以原提案替代补充边界。
- Architecture Revision：**PENDING BATCH REVISION**。仅更新评审记录与队列；未修改Architecture / Calculation Specification正文、未创建SQLite或进入Implementation。

### 已确认业务定义

1. **No Candidates显示的必要条件**：当且仅当当前正式Statistical Scope已经确认Candidate Events=0时，Analysis Rate显示“—”。不能把未知Candidate Count当作0套用此显示。
2. **明确无候选原因**：必须能够同时明确表达“No Candidates / 当前统计范围无候选事件”；具体UI文案由UX Design决定。
3. **如实显示零计数**：上述情形同时显示Candidate=0、Eligible=0。
4. **禁止伪百分比**：No Candidates情形禁止显示0%、100%或N/A%。
5. **破折号的业务含义**：“—”表示Analysis Rate mathematically not applicable because Candidate denominator=0，不表示0% Analysis Rate。
6. **区分其它不可显示情形**：必须与Candidate Population Unknown、Data Loading、Data Not Ready、Calculation Failed、Historical Sync Pending、Range Membership Unresolved区分。
7. **保留Q12边界**：Candidate Population无法确定时，禁止用Candidate=0、Eligible=0、Analysis Rate=—这组显示伪装成No Candidates。
8. **不能仅凭空值渲染**：内部状态不能只保存analysis_rate=null，再让UI无差别显示“—”；UI必须取得Rate不可显示的具体原因。
9. **保留原因语义**：概念上至少区分NO_CANDIDATES、CANDIDATE_SET_UNRESOLVED、DATA_NOT_READY、CALCULATION_NOT_AVAILABLE；最终状态名称由State / Error Model Review统一。
10. **正分母的有效零率**：Candidate>0、Eligible=0时，Analysis Rate=0%，不能显示“—”。例如Candidate=20、Eligible=0，Rate=0%；这是有效0%。
11. **正常部分比例**：Candidate=20、Eligible=10时，正常显示50%。
12. **正常完整比例**：Candidate=20、Eligible=20时，正常显示100%。
13. **适用全部正式范围**：本显示规则适用于Account、Symbol、Side、MT5 Entry、MT4 Event Type各正式Statistical Scope。
14. **筛选后已知空范围**：切换正式统计筛选后，新Scope的Candidate=0时，Rate相应显示“— / No Candidates”。
15. **取消筛选恢复实际比例**：取消筛选后，根据恢复后的Statistical Scope重新显示实际Analysis Rate。
16. **逐Login同样适用**：Multi-account Overview中某Login的Candidate=0时，该Login的Rate同样显示“—”，并能说明No Candidates。
17. **不能仅靠颜色区分**：“—”、0%、Failed、Not Ready必须有文字 / 状态语义，不能只使用颜色区分。
18. **导出保留原因**：No Candidates不得导出为Analysis Rate=0；应保存Candidate Count=0、Eligible Count=0，Analysis Rate为空 / Not Applicable，并保留对应原因。
19. **仅决定呈现契约**：本题不改变Candidate、Eligible及Analysis Rate既有业务计算规则，不因显示变化创建新Version或修改原Event资格。

### 适用边界与 Q11 闭合检查

- 第一小项B的统计Scope及分子/分母规则、第二小项A的显示规则都已决定，不再提问；原第二小项待选提案已替换，本节为唯一权威记录。
- 正分母下的有效0%仍以计数已确定、符合既有正式Rate发布边界为前提。Q12未知总体或其它已确认的Version级禁止正式发布情形，不能因部分Confirmed Candidate Count>0而绕过；这不把正常的0/正数改为未定义。
- 有效0% Analysis Rate不等于有效0 Markout。没有Eligible Results时，仍按Q05 / Q09 / Q22展示No Eligible Results，不伪造0 Points / Money / Curve。
- 状态名称、具体数据表示、UI文案及导出编码留后续State / Error Model与UX / Technical Design；本轮不选择数据库字段或实现方式。不可显示原因必须可追溯的业务要求已冻结，不因命名待定而重开。
- Q11业务/显示核查：统一Eligibility、Event去重、Volume边界、无Eligible结果、正式Scope分子分母、零候选与有效0%显示均已有决定；最后的多原因统计展示已由第三小项A闭合，Q11整体CLOSED。
- 原保留的多原因展示现采用第三小项A；Discarded总数仍去重，NOT_EXECUTED不计Money Failure，不为增加原因执行被禁止或跳过的额外计算。

## Q11（第三小项）— DECIDED — A：Reason Breakdown 独立计数、允许重叠

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**CURRENT APPLICABILITY — Money相关分支PARTIALLY SUPERSEDED：本节历史Money计算/指标/字段/资格/证据引用已不适用于V1；其余已确认的时间、匹配、统计Scope、隔离/发布、导出、重跑与历史冻结规则保持。具体范围见14影响矩阵，不能因删除Money扩大其它业务范围。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P2 — Diagnostics Presentation Contract。
- Status：**DECIDED — A**；Q11三个子项依次为B / A / A，整体**CLOSED**。
- Decision Source：用户明确选择A，并确认以下十条规则。
- Architecture Revision：**PENDING BATCH REVISION**。仅记录决定与Consolidation，不修改Architecture / Calculation Specification正文，不进入Implementation。

### 已确认业务定义

1. **Discarded总数按Event去重**：同一个Event无论记录多少原因，只计1个Discarded Event。
2. **按原因独立统计**：Diagnostics的Reason Breakdown按每个原因分别统计。
3. **多原因Event可出现在多项计数中**：同Event存在多个已记录原因时，可以同时出现在多个Reason Count中。
4. **原因计数允许重叠**：不要求Reason Count合计等于Discarded Events，不用原因计数之和代替去重Event总数。
5. **明确重叠提示**：UI必须明确提示“Reason counts may overlap / 原因计数可能重叠”。
6. **原因下钻为去重Event列表**：点击某个Reason，下钻到包含该原因的去重Event列表。
7. **同原因内不重复计数**：同一个Event在同一种Reason中只计一次，即使多个窗口记录该原因。
8. **V1不建设原因分类复杂机制**：不做Reason Combination Group、Primary/Secondary Reason Ranking、Reason Priority Engine或Mutually Exclusive Reason Classification。
9. **保留全部已记录原因**：Export / Diagnostics中仍保留Event的全部已记录原因，不因概览计数而删除或覆盖其它原因。
10. **不为增加原因执行额外计算**：不得为了补齐更多Reason，执行已被业务规则禁止或跳过的额外计算。

### Q11 闭合检查

| 范围 | 已决定依据 | 当前业务状态 |
|---|---|---|
| 正式统计Scope、Eligible / Candidate、筛选与历史View Metric | 第一小项B | DECIDED |
| 已知零候选、有效0%、未知/未就绪/失败区分、呈现与导出原因 | 第二小项A | DECIDED |
| Discarded去重、Reason独立重叠计数、原因下钻与禁止额外计算 | 第三小项A | DECIDED |
| 非空Eligible正总量、无Eligible不伪造零Markout、统一资格 | Q05 / Q09 / Q22 | CLOSED BY EXISTING DECISIONS |

**Q11 Overall Status: CLOSED。** 原范围已无尚待用户决定的业务或显示子项；具体控件、布局、Label交UI/UX Design，原因枚举及数据组织交Technical Review，不重新创建P2问卷。

- Q05第五小项中的Primary Eligibility Failure仅保留实际Point失败与Money未执行的阶段事实，不据此实现本次明确禁止的Primary/Secondary Reason Ranking或原因优先级引擎。
- Q07 / Q22要求的原因披露不取消；Reason Breakdown不能被当作互斥类别加总，也不将Other Discarded解释成其它原因次数之和。具体集合/字段表达须忠实于去重Event及已记录Reason，不能新增原因排名。
- NOT_EXECUTED不计Money Calculation Failure、Q12未知总体不冒充完整计数、旧Version不回填及无额外诊断计算等既定边界不变。
- 按用户新要求，本轮接续Remaining Questions Consolidation；不立即提出下一题，不继续逐项询问P2 UI / Display细节。

## Remaining Questions Consolidation — 2026-10-14

<!-- AR-01-02 applicability -->
> **AR-02后续适用性：SUPERSEDED BY V1 MONEY MARKOUT REMOVAL。** 下列旧Q08“非USD账户跳过”及其D19依据全部不再适用；本节计数仅历史。当前Q08关闭为NO LONGER APPLICABLE TO V1，其他身份/数据失败的隔离边界保持。
<!-- /AR-01-02 applicability -->

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**HISTORICAL SNAPSHOT：本节旧计数、Money资格、取证队列及来源无优先级说明不代表当前状态。Q03/Q09与Money相关分支已被取代；AR-02已DECIDED，Q08旧USD-only拒绝分支为SUPERSEDED BY V1 MONEY MARKOUT REMOVAL / NO LONGER APPLICABLE TO V1。当前队列见文件顶部与09，不依旧清单重新提问。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)及[AR-02](#ar-02)。
<!-- /V1-MONEY-REMOVED applicability -->

**历史归并快照：** 本节保留归并完成时的分类、计数及当时未决事项，不作为实时队列；后续Q16已DECIDED — B并CLOSED。当前状态以文件顶部队列、09及文末权威决定为准，禁止依据本节旧Q16待决措辞重复提问。

### 用户授权与本轮范围

用户已确认Q11第三小项A并要求归并剩余问题：直接可推导的标DERIVED，纯技术移Technical Review，纯UI / Display / Layout / Label交UI/UX Design；仅保留改变计算、Candidate/Eligible、统计含义、可复现、数据源、失败/发布或明显产品范围的决定。本轮完成归并后不马上继续提问。本记录不批准技术实施，不改Architecture / Calculation Specification正文。

### 计数方法与结果

以原Q01–Q25为互斥主分类，每题只计一次；复合题中仍有真实业务选择则保留User Decision，其余技术/UX工作不重复加总。Derived / Closed含已有业务闭合题及本轮Q11关闭，Q08单独标明为本轮推导。Q02保留Technical分类，因为实际历史时间语义/映射证据仍是主要剩余任务，不把业务A已确认误写成取证通过。

| 分类 | 数量 | 编号 |
|---|---:|---|
| P0 — User Decision Required | 2 | Q01、Q16 |
| P1 — User Decision Required | 3 | Q15、Q17、Q19 |
| P2 — User Decision Required | 0 | 无 |
| Derived / Closed by Existing Decisions | 7 | Q03、Q05、Q08、Q09、Q11、Q12、Q13 |
| Moved to Technical Review | 12 | Q02、Q04、Q07、Q10、Q14、Q18、Q20、Q21、Q22、Q23、Q24、Q25 |
| Moved to UI/UX Design | 1 | Q06 |

### 逐题归并依据

| 编号 | 当前主分类 | 已决定内容与剩余边界 |
|---|---|---|
| Q01 | P0 — USER DECISION REQUIRED | 仅剩能力验证及同条件比较完成后，确认实际启用的MT4 Quote Provider；候选许可不等于批准Manager替代Terminal。依据：Q01 B第2–5、7–10条；Q22第二项第20–21条。 |
| Q02 | TECHNICAL REVIEW | 确定性映射、历史期间有效、保持精度和证据留存已决定；剩余实际API时间契约、DST历史规则与可定位范围验证交技术评审。依据：Q02 A；Q06；Q12；Q22第四项隔离原则。 |
| Q03 | CLOSED BY EXISTING DECISIONS | CLOSED BY EXISTING DECISIONS：原生内部换汇许可、理论USD表达、历史金额冻结及原保留Money资格/规格问题均已有决定。依据：Q03 B；Q09 A；Q13 A/B。 |
| Q04 | TECHNICAL REVIEW | 回溯latest valid规则已决定；没有Raw Tick和Raw Tick全无效均无法满足完整Valid Quote资格，剩余原因分类交State/Error Model。依据：Q04 A第12–14条；Q05；Q09；Q22第二项第4–6条。 |
| Q05 | CLOSED BY EXISTING DECISIONS | CLOSED：六项A/A/A/A/B/A完整覆盖报价、成交价、量、Point、缺失/非有限值与执行失败边界。依据：Q05全部六项；Q09；Q12。 |
| Q06 | UI/UX DESIGN | 业务两项B/B已决定；仅剩日期/时间控件、快捷范围、默认显示位数与可选毫秒交互，移交UI/UX Design。依据：Q06两项B；Q11第二项A。 |
| Q07 | TECHNICAL REVIEW | 局部隔离及整体Ordering不可靠FAILED已决定；剩余是实际顺序/覆盖证据、查询完成度及证据适用性验证。依据：Q07 A/B；Q04；Q22四项；Q12。 |
| Q08 | DERIVED — NO USER DECISION REQUIRED | DERIVED — NO USER DECISION REQUIRED：已可靠确认非USD的Login标记不支持币种并跳过，其它独立可靠USD账户继续。依据：01 D04部分无效账户继续、D19仅USD、D28逐Login；Q07/Q22可靠隔离；Q09/Q12。 |
| Q09 | CLOSED BY EXISTING DECISIONS | CLOSED BY EXISTING DECISIONS：统一全窗Quote/Points/Money资格已决定，原保留的Point、Volume、Trade Price问题已由Q05闭合。依据：Q09 A；Q05六项；Q11三项。 |
| Q10 | TECHNICAL REVIEW | 真实Event量、身份及覆盖的要求已冻结；SDK/Order History实际数据契约与读取组合交Technical Evidence。依据：Q05第四项；Q06；Q12；01 D06/D08。 |
| Q11 | CLOSED BY EXISTING DECISIONS | CLOSED：第一B、第二A、第三A；统计Scope、零候选/有效0%、全部原因保留与独立重叠计数均已决定。依据：Q11 B/A/A；Q05；Q09；Q22。 |
| Q12 | CLOSED BY EXISTING DECISIONS | CLOSED：唯一DECIDED B保持；总体未知的Version发布阻断与已知Candidate的Event时间精度失败明确分层。依据：Q12 B；Q06；Q11第二项A。 |
| Q13 | CLOSED BY EXISTING DECISIONS | CLOSED：规格时点A、实际目标Login/Group上下文B均已决定，不存在等价账户/Group替代业务选项。依据：Q13 A/B；Q03；Q09；Q05。 |
| Q14 | TECHNICAL REVIEW | 零Candidate、零Eligible、总体未知、同步未就绪及可靠性失败的含义已决定；剩余状态名称、层级和矩阵属于技术建模。依据：Q05第六项；Q07 B；Q11第二项；Q12；Q22四项。 |
| Q15 | P1 — USER DECISION REQUIRED | 仍需决定同Task的新Version是否允许编辑Accounts/Time Range，以及Profile修订改变身份时的重分析边界。依据：01 D31/D46；Q02/Q03/Q13旧版冻结；Q01/Q22来源边界。 |
| Q16 | P0 — USER DECISION REQUIRED | 仍需决定Side与MT5 Entry / MT4 Event Type能否共同限定正式Summary/Curve的交集总体。依据：01 D26；Q11第一项第16、23条。 |
| Q17 | P1 — USER DECISION REQUIRED | 仍需决定FAILED/CANCELLED等无正式发布版本的额外文件导出，以及Single Trade Test的独立导出/长期保存范围。依据：01 D30/D35/D40/D41；Q09第3/12/13条；Q05；Q22保留要求。 |
| Q18 | TECHNICAL REVIEW | 按既有历史快照、活动Run绑定不可漂移及整Task删除的明确范围设计引用和文件所有权，不新增删除范围。依据：01 D21/D35/D39/D46；Q02/Q13版本上下文；原需求§93/98。 |
| Q19 | P1 — USER DECISION REQUIRED | 仅保留从未开始执行的PENDING任务在应用重启后是否自动启动这一产品行为选择。依据：01 D31/D32/D33/D35；Q22自动重跑禁令的适用范围。 |
| Q20 | TECHNICAL REVIEW | 秘密不入明文存储/日志/导出/诊断/Crash的业务边界已确认，保护API及实际SDK日志能力交技术。依据：01 D36；09原Q20。 |
| Q21 | TECHNICAL REVIEW | 在指定Version、完整字段、账户隔离及既定XLSX/CSV格式不变的前提下，容量分片、命名和包装交技术设计。依据：01 D40/D41及导出编排PROPOSED标记；Q11导出语义。 |
| Q22 | TECHNICAL REVIEW | 四项A/B/A/B已决定等待、局部/整体发布及不自动重跑；剩余状态层级、全任务同步无结果表达、等待参数和证据交技术。依据：Q22四项；Q07；Q12；Q14归并。 |
| Q23 | TECHNICAL REVIEW | 实际查询对Terminal自有缓存的影响及必要披露属于技术证据；不把准备阶段禁止操作误当作已验证的运行契约。依据：09原Q23；01 D37/D47；Q01/Q13。 |
| Q24 | TECHNICAL REVIEW | ABI/字段/分页/并发/取消、Terminal实例绑定与目标上下文证明均为技术取证，不要求用户选择底层实现。依据：01 D02/D03/D45；Q01/Q02/Q05/Q07/Q10/Q13/Q22。 |
| Q25 | TECHNICAL REVIEW | 有效性和真实零值规则已冻结；数值类型、精度、累加/舍入实现、无损ID及元数据时间语义交技术，显示位数交UI/UX。依据：Q05第四/第六项；Q06；Q11；Q13。 |

每题的Codex技术建议、推导条件与需升级的边界见[09当前清单](09-open-questions.md)。技术移交事项仍PENDING REVIEW / EVIDENCE，不是已通过。以下直接推导和保护边界一并记录，避免后续重新制造业务问卷：

### DERIVED — NO USER DECISION REQUIRED 的具体衔接

- **Q08明确非USD账户**：依据D04、D19、D28及可靠局部隔离原则，可靠确认币种非USD的Login标记不支持并跳过，其它独立可靠USD继续；不伪称账户不存在、不换汇扩大支持、不把未取得交易总体写为Candidate=0。身份/币种未知、共享数据路径不可靠或Q12发布阻断仍按各自规则处理。这是本轮受用户授权的原则推导，不虚构“用户已选择Q08 A”。
- **Q02/Q07/Q10/Q14/Q22可靠性衔接**：无法证明Candidate总体完整时不发布伪完整Summary/Rate；已知Candidate且影响可可靠定位、其它Events完整条件独立成立时局部排除；不能可靠隔离或当前整体路径不可靠时Version/Task停止且不发布。必须保留实际Time Mapping、Ordering、Coverage、Sync、Numeric等原因，不能统一改称普通FAILED或DATA_NOT_READY。
- **Q04剩余分类**：在覆盖充分与查询完成的前提下，无Raw Tick和Raw Tick全无效均不能满足Required Valid Quote资格；如何保留NO_QUOTE高层分类及区分INVALID_QUOTE相关事实是State/Error技术工作，不重新表决是否放行Event、缩短回溯或降低Coverage标准。
- **Q14空结果分层**：已知Candidate=0、Candidate>0且Eligible=0、总体未知、Future Target未成熟、Sync未就绪和真正可靠性失败必须分别表达；无正常结果不得伪造零Markout，Q11已确认的有效0%仍有效。具体枚举/层级可由Codex建议，不以命名变化修改发布资格。
- **Q18不扩大删除授权**：D39及原始删除清单仅授权Task与其全部Version/关联Results/MatchedQuotes/Diagnostics/Snapshot；不自动扩大到用户另存Export、独立Technical Logs或其它项目文件。Profile内部引用策略按历史快照独立可读和Run绑定不漂移设计；不要求用户表决归档/墓碑。运行中删除先明示停止写入/取消再实际执行是待Technical Review的安全流程建议，不是新业务决定或本轮删除操作。
- **Q21保持交付契约**：完整字段、账户隔离、指定单一Version及单一XLSX/既定CSV支持保持。推荐在同一工作簿内按账户无损分Sheet并提供清晰关联信息；不静默截断、不擅自改多个XLSX或CSV-only。命名/ZIP包装属技术编排；若真实限制迫使改变交付契约，再提交具体范围决定。
- **Q11已闭合**：Reason概览按原因独立去重并允许重叠，全部已记录原因继续保留；UI布局/标签交设计，不按Numeric Field或Reason类型继续拆同一规则来询问用户。

### Technical Review 与 UI/UX 的后续职责

技术负责实际SDK/Provider能力、历史日期时间契约、查询完整性、Ordering证据、原始量与单位、目标Login适用性、数值保真、状态层级、取消与存储一致性、凭据保护及导出容量方案；所有建议须保持已决定业务语义。Connection Profile/Terminal Instance的实际路径、会话与隔离方案属于技术工作，不授权修改现有Terminal登录、安装实例或使用等价账户。不可通过用户投票证明接口能力。

UI/UX负责Q06剩余控件/默认呈现，以及各已闭合题的布局、Label、密度、文字和状态反馈；必须保留精度、当前Statistical Scope、零/缺失/未就绪区别和Reason重叠提示。移交仅是评审责任分配，不是本轮开始设计UI或实现页面。

### 仍需用户决定的最小集合（本轮不提问）

- **Q01 / P0**：技术证据完成后确认实际启用的MT4 Quote Provider；不重开候选评估B，不预设Manager替代或来源优先级。
- **Q16 / P0**：Side与Entry / Event Type能否形成正式Summary/Curve的组合统计总体；Q11第一项第23条明确保留，不能当作UI控件细节决定。
- **Q15 / P1**：Re-analyze同Task新Version的Accounts/Time Range可编辑性及Profile身份修订边界；“默认原Scope”不能推成“已锁定”或“允许改变”。
- **Q17 / P1**：未完成/禁止正式发布版本的额外非正式文件导出、Single Trade Test独立导出/长期保存范围；不重问已规定的内部证据保留、Discarded Export或Copy Diagnostic。
- **Q19 / P1**：应用重启后尚未开始的PENDING任务是否自动启动；RUNNING中断状态命名移技术，不能把Q22禁止失败后自动重跑直接扩展为尚未执行队列的答案。

**本轮结束于归并报告，不提出下一题。** 五个保留题未代用户作出选择；其它技术证据或实现工作仍未完成。Architecture Revision仍PENDING BATCH REVISION，Implementation仍禁止。

## 评审流程确认 — 归并后恢复五项业务评审

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**HISTORICAL SNAPSHOT：本节旧计数、Money资格、取证队列及来源无优先级说明不代表当前状态。Q03/Q09与Money相关分支已被取代；AR-02已DECIDED，Q08旧USD-only拒绝分支为SUPERSEDED BY V1 MONEY MARKOUT REMOVAL / NO LONGER APPLICABLE TO V1。当前队列见文件顶部与09，不依旧清单重新提问。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)及[AR-02](#ar-02)。
<!-- /V1-MONEY-REMOVED applicability -->

用户确认上述Consolidation结果，限定仅继续Q16、Q15、Q19、Q17、Q01五项；处理顺序以文件顶部队列为准。上一节“不提出下一题”是归并报告轮的结束边界，已由本次用户明确要求开始Q16更新，不代表自动开启其他题或技术实施。

- Q01为等待证据的最后一项，当前不提问；候选许可B不重开，尚未证明Terminal / Manager历史能力时不让用户选择最终来源。
- 已Derived / Closed / Technical Review / UI-UX Design的事项不重新开放；业务正文、Calculation Specification及Implementation保持原边界。
- Q16决定后先记录并同步状态，再继续Q15；本节只确认评审流程，不替用户选择Q16的A/B。

## Q16 — DECIDED — B：两个既有统计维度采用 AND 组合

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**CURRENT APPLICABILITY — Money相关分支PARTIALLY SUPERSEDED：本节历史Money计算/指标/字段/资格/证据引用已不适用于V1；其余已确认的时间、匹配、统计Scope、隔离/发布、导出、重跑与历史冻结规则保持。具体范围见14影响矩阵，不能因删除Money扩大其它业务范围。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P0。
- Status：**DECIDED — B；Q16整体CLOSED**。
- User Choice：**B，以下27条补充边界为完整决定**。
- Decision Source：用户在Architecture Review中对Q16的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**；仅更新Decision Register、Open Questions及Review Queue，不修改Architecture / Calculation Specification正文。
- 本节替换原Q16待选提案，是唯一权威Q16 Decision；不重复创建决定，不将此前推荐A保留为当前待选项。

### 已确认业务边界

1. **允许组合**：V1允许在既定Account / Symbol Statistical Scope内，同时选择Side + Entry / Event Type，形成正式统计范围。
2. **AND语义**：组合条件采用AND。例如MT5 Side=BUY、Entry=IN，表示BUY AND IN。
3. **MT4组合**：允许BUY + OPEN、SELL + CLOSE等实际存在的组合。
4. **MT5组合**：允许BUY / SELL与IN / OUT / INOUT / OUT_BY组合。
5. **默认ALL**：两个维度均默认ALL；允许ALL + ALL、BUY + ALL、SELL + ALL、ALL + IN、ALL + OUT、BUY + IN、BUY + OUT、SELL + IN、SELL + OUT，以及其他实际存在且平台支持的组合。
6. **同条件分子分母**：Candidate与Eligible必须应用完全相同的组合筛选条件。
7. **组合计数**：以BUY + IN为例，Candidate Count是所有满足BUY AND IN的Candidate Events；Eligible Count是其中满足正式Eligibility的Events。
8. **组合Analysis Rate**：Analysis Rate = Eligible(BUY AND IN) / Candidate(BUY AND IN)，其他组合遵循相同口径。
9. **Discarded保留分母**：满足BUY + IN等当前范围但被Discarded的Event，仍进入该范围的Candidate分母。
10. **Summary总体**：Summary只使用当前组合范围内的Eligible Events。
11. **Curve总体一致**：Markout Curve使用与Summary完全相同的Eligible Event Population。
12. **禁止范围错配**：不得将BUY + IN Summary搭配BUY + ALL Curve或ALL Analysis Rate。
13. **仅改变视图**：组合筛选只改变Statistical View / Scope，不改变Event Eligibility、Discard Reason、Historical Version、Raw Data或Calculation Result。
14. **不重新分析或取数**：切换组合不触发Re-analyze、Manager API Query、Terminal Quote Query、Markout Recalculation或新Version；基于已经保存的Event Results重新聚合统计。
15. **仅两个维度**：V1只允许现有两个正式统计维度Side + Entry / Event Type组合。
16. **禁止扩展查询能力**：不因B扩展Arbitrary Query Builder、多字段复杂条件、OR、NOT、自定义表达式、多维Pivot、Cross-tab Matrix或多组合同时比较。
17. **不做多曲线对比**：V1不提供BUY+IN vs BUY+OUT vs SELL+IN vs SELL+OUT四条Markout Curve同屏比较。
18. **一次一组结果**：页面一次只展示当前选择组合的一组Summary、Analysis Rate、Markout Curve。
19. **零Candidate**：当前组合Candidate=0时，沿用Q11，Analysis Rate显示“—”，明确No Candidates。
20. **零Eligible**：Candidate>0且Eligible=0时，Analysis Rate=0%，显示No Eligible Results；不得制造正常零Markout结果。
21. **发布阻断优先**：Q12等Version-level Publication Block不能通过选择组合绕过；Version本身不允许发布正式结果时，所有组合统计同样不可作为正式Summary发布。
22. **普通调查筛选不参与**：Text Search、Deal ID Search、Table Sort、Diagnostics Filter仍不是正式Statistical Scope，不与Side / Entry组合参与正式统计。
23. **基础Scope保持**：Symbol继续是既有基础范围；实际范围概念为Account + Symbol（进入Symbol Summary时）+ Side + Entry/Event Type。
24. **禁止跨Login**：不允许跨Login组合统计。
25. **Scope表达**：UI必须清楚显示当前Statistical Scope，例如XAUUSD.r、BUY · IN；具体控件与视觉表现由UX Design决定。
26. **动态聚合与保存边界**：V1不保存每一种组合的独立Summary Snapshot；组合统计可以依据该Analysis Version已保存的Event-level Results动态聚合，具体缓存策略属于Technical Design。
27. **Export边界**：完整Event Export保留原始Side、Entry / Event Type供用户进一步分析；是否额外导出“当前筛选视图Summary”，不由Q16扩展决定。

### Q16闭合与评审状态同步

- Q16全部业务边界已确认，**CLOSED / User Decision Required = NO**；从待答Review Queue移除，不重新开放复杂交叉统计问题。
- Q11第一小项第23条曾保留的组合筛选由本决定闭合；此前原文作为历史决定保留，不重复修改Q11或其余业务决定。组合AND统计不等于批准复杂交叉矩阵、任意条件或多组合对比。
- 现有Q11统计/零候选口径及Q12等发布限制继续适用；本轮不扩展额外视图Summary Export，不新建该项用户问卷。
- 后续仅余UX控件/视觉表达及技术缓存方案，交既定责任方，不追加P2表决。
- 提问前已检查本Decision Register：Q15没有DECIDED记录。继续Q15 → Q19 → Q17 → Q01；Q01保持WAITING FOR TECHNICAL EVIDENCE，当前不提问。

## Q15（第一小项）— DECIDED — A：固定Task Scope，Re-analyze完整重新运行

- Priority：P1。
- Status：**第一小项DECIDED — A；Q15整体PARTIALLY DECIDED**。
- User Choice：**A，以下25条补充边界为完整决定**。
- Decision Source：用户在Architecture Review中对Q15第一小项的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**；仅更新Decision Register、Open Questions及Review Queue，不修改Architecture / Calculation Specification正文。
- 本节替换原第一小项待选提案，是该小项唯一权威Decision；Profile Identity仍未决定。

### 已确认业务边界

1. **固定业务分析范围**：Analysis Task创建后，其业务分析范围固定。Task Scope至少包括Platform、Accounts、From、To。
2. **禁止修改重分析范围**：Re-analyze不允许修改Accounts、From、To。
3. **完整继承Task Scope**：Re-analyze的新Version必须继承原Task完全相同的Platform、Account Set、From、To。
4. **实际Login集合**：Account Set按实际Login集合比较；不得增加、删除或替换Login后仍作为原Task的Re-analyze。
5. **MT5精确范围**：From / To必须精确一致，MT5毫秒精度属于Task Scope；例如10:00:00.250不得在Re-analyze改为10:00:00.000。
6. **MT4范围精度**：保持原SECOND Precision的From / To。
7. **新范围新Task**：增加、删除、替换账户，或扩大、缩小、平移时间范围，必须使用New Analysis创建新的Task。
8. **不新增复制功能**：V1继续不提供Duplicate Task / Copy Task；新范围通过正常New Analysis创建。
9. **Task Remark归属**：Task Remark属于Task Metadata；本题不要求通过Re-analyze修改Task Scope。
10. **Version Remark用途**：Version Remark可以说明为什么重新分析。
11. **固定输入不固定候选集合**：固定Task Scope不代表不同Version的Candidate Events必须完全相同。
12. **Manager数据变化示例**：V1时Manager Data不完整，V2时Manager返回更多真实记录，Candidate Set可以变化；这不豁免Q12等完整性与发布规则。
13. **本次环境可以变化**：Historical Quote Coverage、Symbol Specification、Account / Group Context、Calculation Environment、Technical Evidence在新的Analysis Version中可能变化。
14. **逐版实际证据**：Version必须保存本次实际取得的Candidate Data、Environment Metadata、Symbol Specification Snapshot、Quote Provider、Calculation Rule Version、Results、Diagnostics。
15. **完整重跑**：Re-analyze是使用相同Task Scope重新执行一次完整Analysis Run，不是只重算之前Discarded Events。
16. **禁止补算回改**：不得只补算某几个Event，然后修改旧Summary。
17. **新Version独立总体**：新Version必须重新形成自己完整的Candidate、Eligible、Discarded、Summary集合；正式发布仍须满足既有完整性与资格规则。
18. **旧Version冻结**：旧Version永远保持不变。
19. **差异不单归因于规则**：即使Task Scope相同，V1/V2结果差异也可能来自Manager Data、Quote Data、Specification、Account Context、Calculation Environment、Calculation Rule等变化，不能假设一定来自Calculation Rule。
20. **Version Detail可追溯**：必须能够查看本Version实际使用的分析输入和环境，以便解释差异。
21. **Task Header**：Task页面可以用Header展示固定Platform、Accounts、[From,To)；具体UI交UX Design。
22. **重分析输入只读**：Re-analyze UI的Accounts / From / To不应表现为可编辑的新分析参数，可以只读展示原Task Scope。
23. **引导New Analysis**：用户需要不同Scope时，引导New Analysis，不把Re-analyze变为Edit & Run。
24. **模型含义**：Task = Stable Analysis Scope；Version = Repeated Analysis Run。
25. **Profile Identity保留**：本项不决定Connection Profile身份发生变化后是否允许Re-analyze继续，交Q15下一小项单独确认。

### 决定范围与状态同步

- 第一小项已DECIDED — A，不再询问账户集合、精确时间范围是否可编辑；固定输入不等于冻结新Run的数据或结果。
- Task Remark / Version Remark按第9–10条记录，不额外推导未授权的备注编辑流程。
- 本决定不授权跨Profile / Server替代、不选定MT4 Quote Provider、不改变已确认的单Version同源及新Version来源取证要求。
- Q15整体仍PARTIALLY DECIDED，只剩Profile Identity边界；Q15完成后按Q19 → Q17 → Q01继续，Q01等待Technical Evidence。
- 其它Derived / Closed / Technical Review / UI-UX Design事项不重新开放；当前仍无Implementation授权。

## Q15（第二小项）— DECIDED — A：Trading Server Identity属于Stable Task Scope

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**CURRENT APPLICABILITY — Money相关分支PARTIALLY SUPERSEDED：本节历史Money计算/指标/字段/资格/证据引用已不适用于V1；其余已确认的时间、匹配、统计Scope、隔离/发布、导出、重跑与历史冻结规则保持。具体范围见14影响矩阵，不能因删除Money扩大其它业务范围。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P1。
- Status：**第二小项DECIDED — A；Q15两项A / A完整闭合，整体CLOSED**。
- User Choice：**A，以下27条补充边界为完整决定**。
- Decision Source：用户在Architecture Review中对Q15第二小项的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**；仅更新Decision Register、Open Questions及Review Queue，不修改Architecture / Calculation Specification正文。
- 本节替换第二小项待选提案，是本小项唯一权威Decision；此前原文保留的“Profile待决定”等措辞以本节最新决定为准。

### 已确认业务边界

1. **Stable Scope包含真实Server**：Analysis Task的Stable Scope除了Platform、Accounts、From、To，还必须包含Trading Server Identity。
2. **Scope定义**：Task Scope概念上为Platform + Trading Server Identity + Account Set + [From,To)。
3. **重分析须证明同Server**：Re-analyze创建新Version时，必须证明当前Connection Profile仍连接原Task对应的同一个真实Trading Server。
4. **Profile标签不是Server身份**：Connection Profile的Name、Local ID、Display Name均不能单独作为Server Identity。
5. **纯连接配置可变化**：Profile Rename、Password Update、Credential Rotation、Connection Address / Endpoint Change、Terminal Path Change及其他纯连接配置变化，本身不改变Task Scope；前提是Technical Validation证明最终仍连接原真实Trading Server。
6. **同Profile不豁免跨Server**：实际已连接另一Trading Server时，即使Profile ID、Profile Name、Login数字、Symbol名称及Platform都相同，也不能用于原Task Re-analyze。
7. **账户业务身份**：Account Identity必须结合Trading Server Identity + Login才有业务意义。
8. **时间归属Server**：Server Time的业务含义绑定对应Trading Server；From / To数值相同不能证明两个Server的Analysis Scope相同。
9. **新Server新Task**：分析新Trading Server必须通过New Analysis创建新Task。
10. **禁止跨Server强制放行**：不得通过用户确认弹窗、强制继续按钮、Ignore Server Mismatch等方式允许原Task跨Server Re-analyze。
11. **预检身份一致性**：Preflight必须验证当前Connection Profile实际Server Identity与Task Stored Server Identity是否一致。
12. **不能证明则不开始**：无法证明一致时，Re-analyze不开始。
13. **独立身份诊断语义**：明确表达Server Identity Verification Failed或Server Mismatch；最终名称由State / Error Model Review统一。
14. **不混同连接失败**：Server身份无法确认不得简单解释为Connection Failed；无法连接与连接成功但不能证明是原Server是不同问题。
15. **Task保存获证身份**：Task创建时必须保存经过验证的Trading Server Identity。
16. **逐版身份快照**：每个Analysis Version保存本次实际验证得到的Server Identity、Connection Profile Identity、Environment Snapshot。
17. **历史身份冻结**：历史Version永远显示其运行时实际Server信息，当前Profile后续修改不改变旧Version。
18. **不以Local ID作准入结论**：Profile删除、重建或Local ID改变，不应单凭Profile ID决定是否可以Re-analyze；核心是能否建立经过验证的原Trading Server Identity。
19. **不偷偷替换Profile**：原Profile被删除时，V1不允许系统偷偷选择另一个Profile。
20. **Profile替换设计移交**：是否允许用户明确选择另一个经验证连接同一原Server的Profile，如现有需求尚未明确，交Technical / UX Review。业务原则是Profile本身不是Task Scope，Trading Server Identity才是。
21. **同Server不同Profile不等于跨Server**：另一个Profile若严格证明同Platform、同Trading Server，并满足Task全部既有数据源规则，从业务身份角度不能因名称不同自动视为跨Server。V1是否提供Profile Replacement UI交UX / Technical Review，不扩展Q15为新业务流程。
22. **Provider变化不改变Server边界**：Q01后续MT4 Quote Provider决定不改变本项。Provider可依经过批准的技术方案变化，但Trading Server Identity必须与原Task一致。
23. **记录本版Provider**：Re-analyze使用的Provider若与旧Version不同，新Version记录实际Provider，旧Version保留原Provider Snapshot。
24. **单Version禁止跨Server补齐**：同一Version不得为补数据跨Server取得Trade Data、Quote Data、Symbol Specification或Profit Calculation Context。
25. **目标账户实际上下文**：Account / Group Context必须来自目标Login在该真实Server上实际适用的上下文。
26. **身份技术证据**：具体证明方法属Technical Evidence；可考虑Server-provided identifier、Server metadata、官方SDK identity或经验证的稳定组合标识，但禁止仅依赖用户填写的Profile Name。
27. **无单一ID也须获证策略**：若MT4 / MT5 SDK无法提供单一稳定Server ID，Technical Review必须提出可验证的Server Identity Strategy，不要求用户凭名称判断。

### Q15闭合与移交检查

- 第一小项A的25条与第二小项A的27条均已决定；Task固定范围、完整新Run、真实Server身份准入均已闭合。**Q15 = CLOSED / User Decision Required = NO**，从待答Review Queue移除。
- 当前权威边界为Task Scope包含Trading Server Identity，**Profile本身不是Task Scope**。旧文中的“原Profile”“不得替代Profile”不得被解释为仅因Local ID、名称或配置重建而一概禁止同Server的显式绑定；同时禁止系统偷偷替换及任何跨Server强制放行。具体Replacement UI不由本次决定自动视为已设计或已实现。
- Server Identity Strategy、Preflight技术证据、身份不一致/无法证明/无法连接的状态表达，以及同Server显式Profile Replacement交互均移交既有Technical / UX Review。不再拆为Q15业务小项，不要求用户逐字段投票；未宣称实际SDK能力已通过。
- 原版本数据/身份/Provider快照不变；新Version仍服从全部Quote Provider、目标Login上下文、时间映射与发布边界。Q01实际Provider仍WAITING FOR TECHNICAL EVIDENCE，本轮不问、不验证。
- 提问前已检查Decision Register，未发现Q19的DECIDED记录；下一项仅Q19，再Q17，最后Q01（待证据）。旧RUNNING中断状态命名仍属Technical Review，不重新开放。

## Q19 — DECIDED — A：重启恢复Pending记录，用户明确Start才首次执行

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**CURRENT APPLICABILITY — Money相关分支PARTIALLY SUPERSEDED：本节历史Money计算/指标/字段/资格/证据引用已不适用于V1；其余已确认的时间、匹配、统计Scope、隔离/发布、导出、重跑与历史冻结规则保持。具体范围见14影响矩阵，不能因删除Money扩大其它业务范围。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P1。
- Status：**DECIDED — A / CLOSED**。
- User Choice：**A，以下27条补充边界为完整决定**。
- Decision Source：用户在Architecture Review中对Q19的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**；仅更新Decision Register、Open Questions及Review Queue，不修改Architecture / Calculation Specification正文。
- 本节替换原Q19待选提案，为唯一权威Q19 Decision；对象是本次Run从未开始，包括已有旧Version而新Run仍Pending的Task。

### 已确认业务边界

1. **恢复持久化记录**：Application Restart后，恢复此前已提交但尚未开始执行的PENDING Run持久化记录。
2. **不自动执行**：这些Run不自动进入执行状态。
3. **Startup不触发运行活动**：Application Startup本身不得触发这些PENDING Run的Manager API Query、Terminal Historical Quote Query、Symbol Specification Query、Profit Calculation、Markout Calculation或Preflight Connection Activity。
4. **必须明确启动**：只有用户明确执行Start / Run后，该PENDING Run才允许进入正常执行流程。
5. **Start启动原Run**：Start不创建新Task，也不创建新Version；仅启动原已存在而尚未执行的Pending Analysis Run。
6. **身份与输入不变**：Task ID、Version Identity、Submitted Inputs保持不变。
7. **完整重新预检**：用户Start后必须重新执行完整Preflight。
8. **运行结论不默认复用**：禁止复用关闭前保存的Connection Success、Server Identity Verification、Quote Provider Availability、Historical Coverage、Time Mapping Availability等运行时验证结论；仅某项已正式定义为可复用Technical Evidence时可按该定义复用。
9. **重新验证真实Server**：Trading Server Identity必须按当前有效规则重新验证。
10. **环境不一致不强行继续**：当前环境不符合原Run要求时，不能因原状态为PENDING而强制执行。
11. **Profile异常阻止执行**：原Connection Profile被删除、不可用或已连接不同Trading Server时，用户Start须按既有规则阻止执行并明确提示。
12. **启动前可检查或取消**：用户可查看Pending Run信息、Task Scope、Version Remark，检查Connection Profile，或Cancel Pending Run。
13. **Pending不开放修改输入**：不得因应用重启后尚未执行而重新编辑PENDING Run的固定业务输入。
14. **固定Scope保持**：Accounts、From、To、Platform、Trading Server Scope继续按已确认Task / Version规则保持。
15. **输入错误另建Task**：需要纠正输入范围时，应Cancel当前Pending Run，再通过New Analysis创建正确的新Task。
16. **不新增修改/修复模式**：V1不提供Edit Pending Run、Resume With Modified Inputs或Auto Fix Pending Run。
17. **明确等待用户启动**：Startup后的History / Task List应明确识别PENDING — Awaiting User Start；具体状态名称和UI文案交State / UX Review。
18. **不是Interrupted**：从未真正开始的Run不得显示为INTERRUPTED。
19. **不是其它终态**：同样不得将其显示为FAILED、CANCELLED或NOT_READY。
20. **Start后正常流转**：用户明确Start后才进入正常Preflight → Queue / Running流程。
21. **不自动启动全部**：多个恢复PENDING Run不能自动全部启动；用户明确启动哪个，哪个才进入执行队列。
22. **可依次启动多个**：用户可以依次Start多个Pending Run，之后由既定内部Task Queue负责安全并发和排队。
23. **Start All非必须**：Start All Pending不属于V1必须功能；本项不将其新增为必做能力。
24. **已结束Version不自动运行**：Completed、Failed、Cancelled、Not Ready、Data Not Ready等结束状态的Version，不因Application Restart自动重新运行。
25. **Re-analyze仍需主动触发**：重启不自动创建Re-analyze Version，Re-analyze仍由用户主动发起。
26. **不决定RUNNING恢复**：关闭时已RUNNING的Run如何恢复，继续交现有Technical / State Review，不能从PENDING规则推导。
27. **持久化区分执行事实**：Technical Design必须区分“尚未真正开始”与“已经开始但被应用关闭中断”这两种持久化状态。

### Q19闭合与衔接检查

- **Q19 = CLOSED / User Decision Required = NO**；27条完整闭合，从待答队列移除，不重开Pending启动政策。
- Start仅触发原Pending Run首次执行，不等于Pause / Resume或Re-analyze；并非要求整个Task没有旧Version。
- Profile异常按第11条阻止当前不合格绑定启动；Q15已经确认的同Server身份边界及显式Profile Replacement设计移交不被撤销。不得将该移交当作自动替换或Auto Fix Pending Run许可，也不新设业务小项。
- 第8条正式可复用Technical Evidence例外保留；不把运行时Connection Success等旧结论误当持续有效证据，不撤销Q07已确认的证据适用性要求。
- RUNNING中断的状态/恢复表达、执行事实的持久化设计、队列及启动控件交既定Technical / State / UX Review；本轮不进行实现或架构正文修改。
- 提问前已检查Decision Register，未发现Q17的DECIDED记录；接下来仅Q17，再Q01（等待Technical Evidence）。

## Q17（第一小项）— DECIDED — B：Diagnostic Evidence Export仅输出已持久化事实

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**CURRENT APPLICABILITY — Money相关分支PARTIALLY SUPERSEDED：本节历史Money计算/指标/字段/资格/证据引用已不适用于V1；其余已确认的时间、匹配、统计Scope、隔离/发布、导出、重跑与历史冻结规则保持。具体范围见14影响矩阵，不能因删除Money扩大其它业务范围。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P1。
- Status：**第一小项DECIDED — B；Q17整体PARTIALLY DECIDED**。
- User Choice：**B，以下35条补充边界为完整决定**。
- Decision Source：用户在Architecture Review中对Q17第一小项的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**；仅更新Decision Register、Open Questions及Review Queue，不修改Architecture / Calculation Specification正文。
- 本节替换原第一小项待选提案，为本小项唯一权威Decision；Single Trade Test独立导出/长期保存仍待后续小项。

### 已确认业务边界

1. **允许主动证据导出**：对已经结束、按既有规则不能发布正式结果的Analysis Version，允许用户主动执行Diagnostic Evidence Export。
2. **明确非正式**：必须标记DIAGNOSTIC ONLY / 非正式分析证据。
3. **禁止冒充正式结果**：不得将文件命名、展示或描述为Official Markout Report、Completed Analysis、Final Result或Formal Summary。
4. **仅持久化数据**：只能导出该Version已经持久化保存的数据。
5. **不为导出取数或计算**：禁止重新查询Manager / Quote Provider、补拉Historical Tick、Recalculate Markout、补算Money / Points、重新Preflight或自动创建新Version。
6. **可导出已有事实范围**：可包含已保存的Task / Version Metadata、Version Status、Termination / Non-publication Reason、Connection / Server / Provider Metadata（无Credential）、Raw Trade / Event Data、已确定的Candidate Membership Evidence、Matched Quotes、Target Time、Quote Time、Quote Age、Bid / Ask / Mid、Price / Points / Money Markout、Calculation Status、Calculation Trace、Symbol Specification Snapshot、Diagnostics、Discard Reasons及已保存的Technical Evidence References。
7. **实际存在才输出**：Not Calculated、Not Available、Unresolved、Not Executed等保持真实状态，不把尚未取得的内容当作已有数据。
8. **不填零**：禁止Missing → 0、Not Calculated → 0、Unknown → 0、Unresolved → 0。
9. **总体未知保持未知**：Candidate Population未知时，明确Candidate Population = Unresolved，禁止生成看似完整的Total Candidate Events。
10. **不生成部分Rate**：既有规则不允许正式计算Analysis Rate时，不得在该导出中自行生成“部分Analysis Rate”。
11. **不新增非正式聚合**：不增加Partial Summary、Partial Markout Curve、Partial Account Ranking、Partial Symbol Ranking或Partial Multi-account Overview。
12. **核心原则**：Export Saved Facts，而非Generate New Analysis。
13. **局部结果与资格共同输出**：Event已有可靠局部计算结果可以导出，但必须同时保留Eligibility Status及Discard / Failure Reason。
14. **结果不等于Eligible**：不得因Event已有Points / Money就将其描述为Summary Eligible。
15. **明确绑定元数据**：文件绑定Task ID、Version、Platform、Trading Server Identity、Connection Profile / Environment Identity、Analysis Time Range、Analysis Time、Calculation Rule Version、Version Status；未实际取得的字段仍受第4、7条约束，不编造值。
16. **Metadata分类必备**：至少Export Metadata包含Result Classification = DIAGNOSTIC ONLY，并存在明显非正式标识。
17. **可见标题**：格式支持明显标题时，顶部显示Diagnostic Evidence / Not an Official Markout Result；具体UX / 文案交UI/UX Design。
18. **文件名区分**：文件名与正式Export可区分，例如Task_xxx_V1_Diagnostic_Evidence.xlsx；具体命名交UX / Technical Design。
19. **正式导出保持**：正式Completed Version既有Excel Export、CSV ZIP Export不受本题改变。
20. **沿用既有格式**：证据导出可使用Excel / CSV ZIP中适合的格式；V1不为此新增PDF、JSON、XML等格式。
21. **账户隔离**：多账户Version保持账户隔离，不因Diagnostic Export将不同Login的Event混为正式统计集合。
22. **绝不导出凭据**：Manager Password、Terminal Password、Secret、Token、Credential Material不得进入文件。
23. **不默认整份日志打包**：Technical Log不默认整份塞入导出；必要技术错误信息只输出已关联该Version / Event的必要诊断事实。
24. **显式触发**：必须由用户主动导出，不因Failed、Cancelled、Not Ready、Data Not Ready自动生成文件。
25. **Cancelled事实可导出**：可以导出取消前已经保存的事实，必须明确Version Status = CANCELLED、Result Classification = DIAGNOSTIC ONLY。
26. **Future Target未成熟事实**：NOT_READY / TARGET_NOT_REACHED有已存数据时可导出，不生成正式Summary。
27. **同步未就绪事实**：DATA_NOT_READY / Historical Sync Timeout同样可导出已有事实，不生成正式Summary。
28. **候选归属未解事实**：Candidate Set Unresolved导致不能发布时，允许导出Confirmed Records、Unresolved Membership Records及Diagnostic Evidence；不能伪造Candidate Total / Analysis Rate。
29. **不改变Version状态**：Diagnostic Evidence Export不改变Version状态。
30. **导出不代表完成分析**：不得因导出使FAILED、CANCELLED、NOT_READY、DATA_NOT_READY变成Completed。
31. **导出失败独立**：导出本身失败仅属于Export Failure，不修改原Analysis Version计算状态和历史结果。
32. **原能力保持**：本题不改变既定Discarded Export及Copy Diagnostic。
33. **Completed也可输出诊断证据**：V1允许用户从正式Completed Version的Diagnostics页面导出已保存证据，但仍必须DIAGNOSTIC ONLY，不能与正式Result Export混淆。
34. **不增加复杂认证功能**：V1不建立Evidence Package Signing、Digital Signature、Approval Workflow或Audit Certification。
35. **定位**：Internal Debug / Investigation Evidence Export，不是合规正式报告系统。

### 决定范围与状态同步

- 第一小项已DECIDED — B；用户第33条将Completed Version的已存诊断证据导出一并允许，不保留原提案“仅不能正式发布Version”的限制作为当前否决条件。正式Result Export与Diagnostic Evidence Export保持不同分类。
- 只输出持久化Version事实，保持实际可用状态；文件输出不新增取数、计算、聚合或资格，不改变任何Version发布限制。
- 第一小项不自动批准Single Trade Test独立文件导出，也不要求为测试导出先创建Analysis Task / Version或Test History；测试产物边界仍待后续决定。
- Q17整体PARTIALLY DECIDED，下一题只问Single Trade Test独立导出，应用内长期保存留独立后续小项。格式、命名、布局、保密实现等已移交Technical / UX，不重复作为业务问卷。
- 提问前已检查Decision Register，未发现Single Trade Test独立导出/长期保存的DECIDED记录。Q01继续WAITING FOR TECHNICAL EVIDENCE，当前不问；本轮不进入Implementation。

## Q17（第二小项）— DECIDED — B：Single Trade Test独立导出现有会话证据

<!-- V1-MONEY-REMOVED applicability -->
> **2026-10-15 当前适用性：**CURRENT APPLICABILITY — Money相关分支PARTIALLY SUPERSEDED：本节历史Money计算/指标/字段/资格/证据引用已不适用于V1；其余已确认的时间、匹配、统计Scope、隔离/发布、导出、重跑与历史冻结规则保持。具体范围见14影响矩阵，不能因删除Money扩大其它业务范围。 依据：[V1 MONEY MARKOUT REMOVED](#v1-money-markout-removed)。
<!-- /V1-MONEY-REMOVED applicability -->

- Priority：P1。
- Status：**第二小项DECIDED — B；Q17前两项B / B，整体PARTIALLY DECIDED**。
- User Choice：**B，以下33条补充边界为完整决定**。
- Decision Source：用户在Architecture Review中对Q17第二小项的明确回复。
- Architecture Revision：**PENDING BATCH REVISION**；仅更新Decision Register、Open Questions及Review Queue，不修改Architecture / Calculation Specification正文。
- 本节替换原第二小项待选提案，为本小项唯一权威Decision；最后剩余应用内长期保存 / Test History / 重启后恢复边界。

### 已确认业务边界

1. **支持显式独立导出**：Single Trade Test支持用户显式执行独立文件导出。
2. **测试证据分类**：文件必须标识SINGLE TRADE TEST、TEST / DIAGNOSTIC ONLY；不得描述为Official Markout Report、Analysis Version Result或Formal Account Analysis。
3. **当前会话事实**：导出当前Single Trade Test Session已经取得并稳定保存于当前会话中的结果与证据。
4. **不为导出追加运行**：禁止重新查询Manager / Quote Provider、Preflight、补Tick、重新Markout Calculation、补算Points / Money或自动创建Analysis Task / Analysis Version。
5. **可导出字段**：可以包含已取得的Platform、Connection Profile / Environment Identity、Trading Server Identity、Login、Deal ID / Ticket、适用的Order ID / Position ID、Event Type、Entry Type、Symbol、Side、Volume、Event Time、Time Precision、Trade Price、Symbol Specification Snapshot；各Window的Target Time、Quote Time、Quote Age、Bid、Ask、Mid、Price Markout、Points Markout、Money Markout、Mid Markout、Window Status；以及Calculation Trace、Diagnostic Reason、Calculation Rule Version、Quote Provider。
6. **仅已有数据**：只导出实际已经取得的数据。
7. **状态保真**：SUCCESS、NOT CALCULATED、NOT EXECUTED、UNAVAILABLE、FAILED、UNRESOLVED必须保持原义，不得统一转换为0。
8. **分阶段状态**：同Window的Quote Matching成功、Points成功、Money失败时，文件如实分别体现各阶段状态。
9. **Event无效也可留证**：Event因INVALID_TRADE_PRICE、INVALID_VOLUME、INVALID_POINT、INVALID_TIME或其它既有规则无法完成完整Markout时，仍允许导出已取得诊断证据；名称按最终State / Error Model统一，不重新定义既有无效规则。
10. **不要求测试成功**：Failed Test同样可以导出证据。
11. **不伪造Task / Version**：文件不得出现Task ID / Analysis Version，除非未来测试确实从正式Task / Version上下文进入且字段只是明确Reference。V1不得为导出伪造Task ID / Version Number；该未来Reference例外不构成本轮新增入口许可。
12. **独立测试标识**：建议使用Test Session ID，是否持久化该ID由Technical Design决定，不能赋予正式Analysis Version业务含义。
13. **Metadata分类**：必须明确Result Classification = SINGLE TRADE TEST / DIAGNOSTIC ONLY。
14. **MT5来源**：来源为MT5 Deal时，明确Source Type = MT5 DEAL。
15. **MT4来源**：来源为MT4 OPEN / CLOSE Event时，明确Source Type = MT4 EVENT，Event = OPEN / CLOSE。
16. **复用格式能力**：V1优先复用既有XLSX或CSV能力。
17. **不新增格式要求**：不因Single Trade Test新增PDF、JSON、XML等额外格式要求。
18. **单Event简单结构**：只有一笔Event，不需要ZIP多账户结构、Account Summary Sheet、Symbol Ranking Sheet或Multi-account Overview。
19. **内容结构**：围绕Trade Information、Symbol Specification、Markout Windows、Calculation Trace、Diagnostics保持简单。
20. **不生成聚合**：不生成Analysis Rate、Account Summary、Symbol Summary、Symbol Ranking或Markout Aggregation。
21. **数值窗口数据完整**：嵌入单笔Points Markout Curve图片不是V1必须要求；数值Window Data必须完整，未计算/未取得项仍如实标明，不补算或编造。
22. **禁止凭据**：Credential不得进入导出。
23. **不默认整份日志**：Technical Log不默认进入导出。
24. **错误事实限定**：所需错误信息只导出与该Single Trade Test直接关联的诊断事实。
25. **文件名可区分**：文件名明显区别于正式Analysis Export，例如SingleTradeTest_MT5_63802495_123456789.xlsx；具体命名交UX / Technical Design。
26. **导出失败独立**：导出操作失败仅属于Export Failure，不改变当前测试计算结果和状态。
27. **不自动建立Test History**：独立文件导出不自动建立Single Trade Test History。
28. **不隐含重启恢复**：本功能不意味着Application Restart后必须恢复当前Test Session。
29. **不隐含SQLite长期保存**：本功能不意味着测试自动写入SQLite长期保存。
30. **两个独立决定**：独立文件导出与应用内部长期保存测试记录是不同决定。
31. **保留Copy Diagnostic**：File Export不替代Copy Diagnostic。
32. **用途区分**：Copy Diagnostic用于快速复制给开发 / Codex；Single Trade Test Export用于完整离线逐窗口核查。该用途说明不构成自动发送消息或上传文件的授权。
33. **不绕过正式发布**：不得通过Single Trade Test Export绕过正式Analysis Version发布限制。

### 决定范围与状态同步

- Q17第二小项已DECIDED — B，不再询问单笔测试是否独立导出。测试文件来源为当前会话稳定结果，不必先创建或伪造Analysis Task / Version，也不把第一项“持久化Version事实”要求错误套用于本项。
- 导出成功与失败的测试都只输出实际已有证据，分阶段状态保真；已冻结的同一引擎、真实Manager交易、计算跳过边界和正式发布限制不变。
- 第12条测试标识持久化的技术选择，不等于批准完整Test History或恢复运行；应用内长期保存仍由最后一项决定。
- Q17整体仍PARTIALLY DECIDED，前两项B / B完整记录。只剩第三小项：Single Trade Test是否在应用内长期保存、建立独立Test History及重启后可读取。
- 提问前已核对Decision Register，没有第三小项DECIDED记录；不重开前两项或其他已闭合/移交问题。Q01继续WAITING FOR TECHNICAL EVIDENCE，当前不问。本轮不修改架构正文，不进入Implementation。

## Q17（第三小项）— DECIDED — A：不建立应用内长期Single Trade Test History

- Priority：P1。
- Status：**DECIDED — A**；Q17三项B / B / A全部完成，**Q17 = CLOSED**。
- Decision Source：用户明确选择A及以下25条边界（2026-10-14）。
- Architecture Revision：**PENDING BATCH REVISION**；本次只记录决定和评审状态，不修改架构/计算正文。

### 已确认业务边界

1. **不建立长期历史**：V1不建立应用内长期Single Trade Test History。
2. **工具定位**：Single Trade Test是临时测试/验证工具，不是正式Historical Analysis Task / Version的另一套历史系统。
3. **会话内可查看**：Single Trade Test Result只需要在当前应用会话中保持可查看。
4. **重启不恢复**：Application Restart后不要求恢复之前的Single Trade Test Session。
5. **不建立持久化生命周期**：不因为Single Trade Test建立SQLite Test History、Test History List、Saved Test Record、Test Version、Test Retention或Test Delete Workflow。
6. **不提供长期保存操作**：V1不提供Save Test、Save to History、Restore Test、Reopen Saved Test等长期测试记录功能。
7. **长期留证途径**：用户需要长期保留测试证据时，使用已确认的Single Trade Test File Export或Copy Diagnostic。
8. **正式留证方式**：Single Trade Test Export是V1正式认可的留证方式；文件仍为SINGLE TRADE TEST / DIAGNOSTIC ONLY，不因此成为正式Markout Report。
9. **接受未留证丢失**：用户没有执行Export / Copy时，Application Restart后测试结果丢失是V1可接受行为。
10. **不自动保存**：不为防止用户忘记保存而自动保存所有Single Trade Test。
11. **关闭不自动导出**：不在应用关闭时自动生成测试文件。
12. **不做测试恢复**：不需要Crash Recovery / Session Recovery来恢复Single Trade Test。
13. **页面切换**：当前会话内切换页面是否暂时保留Test Result由UX / Technical Design采用简单合理实现，不要求切换页面立即清空。
14. **临时保留边界**：当前Session暂时保留不得演变为Persistent Test History。
15. **不创建正式身份**：Single Trade Test不创建Task ID、Analysis Version或Historical Version。
16. **临时Session ID**：技术实现需要时可使用临时Session ID，但它不是持久化业务身份。
17. **不进入History**：Single Trade Test不进入History页面。
18. **正式历史范围**：History页面只管理正式Analysis Task / Version。
19. **外部文件归用户管理**：导出文件可由用户长期保存，应用不负责文件索引、文件管理、文件历史或文件重新导入。
20. **不导入测试文件**：V1不提供Import Single Trade Test Export或Reopen Export in Application。
21. **快速排查**：Copy Diagnostic继续作为快速问题排查方式。
22. **完整离线证据**：Single Trade Test File Export继续作为完整离线证据方式。
23. **V1需求覆盖**：两者足以满足V1 Calculation Verification、Debugging和Offline Review需求。
24. **Future单独评审**：未来若实际使用频繁且确需应用内回查，可在Future Version单独评审Test History。
25. **不提前建模**：不为Future Test History在V1 Architecture中提前建立复杂持久化模型。

### 决定范围与状态同步

- Q17第一项Diagnostic Evidence Export = B（35条）、第二项Single Trade Test File Export = B（33条）、第三项Single Trade Test Persistent History = A（25条）均已DECIDED，整体CLOSED；不再列入Review Queue或User Decision Required。
- 第三项补齐第二项第12、27–30条尚未决定的长期保存边界：允许临时Session ID，不由技术追踪标识引入持久化业务历史；前两项既有导出/计算/发布规则不改写。
- 正式Analysis Task / Version的历史持久化与不可变性不受本决定改变；Test文件和Copy留证仍遵守凭据排除与真实阶段状态。
- 当前唯一剩余User Decision Required为Q01最终MT4 Quote Provider，继续等待Technical Evidence；证据完成前不提问。
- 用户授权下一阶段仅Technical Evidence Completion及必要最小验证，先给Evidence Summary / MT4 Terminal Result / MT4 Manager Result / Recommended MT4 Quote Provider / Remaining Technical Risks，再在证据完成后重提Q01。未进入Implementation，不开始UI、正式SQLite Schema，不因取证结果自行修改Architecture或已确认业务规则。

<!-- V1-MONEY-REMOVED authoritative decision -->
<a id="v1-money-markout-removed"></a>
## V1 MONEY MARKOUT REMOVED — DECIDED

<!-- AR-01-02 applicability -->
> **后续决定已补齐：[AR-01](#ar-01) / [AR-02](#ar-02)均DECIDED。** 本节第9/10项中的Symbol Ranking及拟议Points排序已由Symbol Overview / Symbol List取代；本节末尾AR待确认与不扩币种说明仅为当时状态。所有USD-only限制为SUPERSEDED BY V1 MONEY MARKOUT REMOVAL。旧原文与当轮STOP保留用于追踪，当前流程以顶部队列为准。
<!-- /AR-01-02 applicability -->

- Decision ID：V1-MONEY-REMOVED-2026-10-15。
- 决定人：用户明确确认；日期：2026-10-15。
- 来源：[用户完整25节范围调整](/Users/lukeh/.codex/attachments/ef752ec3-1aec-4fa5-a6f3-4024cd3d4ed6/已粘贴的文本.txt)。
- 优先级：高于此前所有冲突的Money决定；保留历史原文，逐项标SUPERSEDED / PARTIALLY SUPERSEDED。
- 本轮授权：仅影响分析、决定记录、问题归并、取证计划修订；不改Architecture / Calculation Specification正文，不进入Implementation，不提问，报告后STOP。

### 已确认的新业务范围（对应用户25节）

1. **输出**：V1只保留Price Markout、Points Markout及辅助Mid Markout。Points是正式主指标，Price用于底层/Detail/Diagnostics；删除Money、Total Money USD、Avg Money USD。
2. **计算**：删除Native / Terminal / Specification-based Profit Calculation及Money Fallback；不为Money计算Contract Size、Tick Value、Profit Currency、Current / Historical Currency Conversion。
3. **Eligibility**：不再要求Money成功。当前资格为Candidate Membership Valid + Required Event Fields Valid + 全部Required Windows Quote Complete + Price Valid + Point Valid + Points Valid。完整推导和Version发布边界见[14第3节](14-money-removal-impact-analysis.md)，是既有规则与本决定的衔接，不是重写Calculation正文。
4. **Q09**：SUPERSEDED — MONEY MARKOUT REMOVED FROM V1；历史原文保留，不再使用Money准入门槛。
5. **Q03**：SUPERSEDED FOR V1；原生Profit / FX / USD Money决定不再适用，历史记录不删除。
6. **Q13**：只取代Money/Profit引入的Contract Size及Account/Group Calculation Context要求。正式Point、必要Digits和Points所需规格保留；字段适用性不能猜。
7. **最小Snapshot**：只保留实际用于Points Calculation、Quote Interpretation、Diagnostics的必要字段和来源/时间/上下文证据。具体集合交Technical Review；不为Future Money囤积无关规格，不把可选诊断字段变成统一资格门槛。
8. **Symbol Summary**：每个Markout Window只保留Event Count、Volume、Avg Points、Volume Weighted Avg Points；均使用当前正式Scope同一Eligible集合，删除Total/Avg Money。
9. **Account**：Candidate、Eligible、Analysis Rate、Symbol Ranking及数据质量；不再Money聚合，不建立跨Symbol Points总分。
10. **Ranking**：旧Total Money排序废止。用户提出所选Window VW Avg Points排序，但正式采用前须检查跨Symbol Point尺度。复核发现尺度误导，登记AR-01并提出按完整Symbol名称列示的最简替代；替代仍PROPOSED，不自行采用，也不重新引入Money。
11. **Multi-account Overview**：每Login独立显示Login、Candidate、Eligible、Rate及必要基本状态；删除跨账户Money展示，不创建跨Login/跨Symbol Points综合分。Symbol视图仍在Account内。
12. **Curve**：Symbol使用VW Avg Points，Deal/Event使用自身Points；无Money Curve。
13. **Export**：正式XLSX/CSV去Money字段；Diagnostic Evidence不产生旧设计Money字段；Single Trade Test Export去Money与Money Trace。Q17既有分类、事实/状态、无重查重算和不变Version原则保留。
14. **Trace**：Trade Price → Matched Quote → Price Markout → Symbol Point → Points Markout；Mid辅助，删除Money Trace。
15. **Single Trade Test**：仅验证真实Manager Trade、历史Quote、Target/Matching、Bid/Ask、Price、Point、Points、Mid与Trace，不验证Money；不创建长期Test History。
16. **Terminal依赖**：删除Profit依赖；历史报价和必要Point/元数据依赖重新取证。MT4/MT5 Manager均满足历史Quote Contract且可靠取得Point后，提出Manager-only首选方案。
17. **优先验证的数据路径**：MT4 Manager Trade History / Historical BidAsk / Point与必要Metadata，以及MT5 Manager Deal History / Historical BidAsk / Point与必要Metadata，分别进入Normalized Event → Quote Matching → Price → Points。当前是获准取证候选，不是正式实现或已批准最终Provider。
18. **Profile条件提案**：Manager-only获证后可删除Terminal Connection，仅Platform、Manager Connection、真实Trading Server Identity；现在不落地。
19. **Preflight条件提案**：相同前提下删除Terminal Running/Connected/Server检查，保留Manager Connectivity、真实Server Identity、历史Quote能力/覆盖/可用性与必要Symbol Metadata；不取消必要时间/排序等有效性验证。
20. **禁止替代指标**：不增加Risk Score、Normalized Money、Notional、Exposure、Synthetic USD、Estimated Profit、Cross-symbol Points Score等。
21. **Future**：Money只能作为Future / Optional Module；V1不预建Profit Engine、FX、Profit Account Context或复杂Money规格。
22. **可追溯**：旧Money决定保留，以本Decision ID为失效依据；不删除其他业务决定，不重算旧Version。
23. **Open Questions归并**：Money-dependent、Terminal-dependent、Account/Group-profit-dependent项逐项判断；确因Money删除而无须的关闭NO LONGER APPLICABLE TO V1。仍有实际Quote/Point条件的技术项保留，不提前全关。
24. **证据优先级**：MT4 Manager Bid/Ask、MT5 Manager Bid/Ask、Timestamp Precision、Ordering、Coverage/Retention、Server Time、Exact Symbol、Point、MT4 Partial Close Actual Volume。Native Profit / Currency Conversion / Profit Currency不再阻塞V1。
25. **阶段与停点**：本轮只完成四项评审工作，输出指定七类报告后STOP；不继续问Q01、不执行取证、不改正文、不选技术栈、不编写应用/UI/正式数据库。

### DERIVED — 当前资格与统计承接

- 统一Eligible集合继续成立，但依据变更为非Money资格；Candidate、Trade Price、实际Volume、Point、全部Required Quotes/Price/Points必须满足已有规则。Money缺失/未执行不再产生失败计数或排除。
- Mid保持辅助定位，不提升为新主指标或独立Summary准入门槛。真实0/负Markout可有效，Missing/Non-finite/Not Calculated不能填0。
- Analysis Rate仍为当前Scope Eligible / Candidate；Side AND Entry/Event条件应用到两者，普通搜索/Diagnostics不改变Scope。Candidate0显示“— / No Candidates”；总体未知、零Eligible与0 Markout严格区分。
- Q12总体未知阻断、Q07整体Ordering失败、Q22Future/Sync独立语义及可靠局部隔离、历史冻结/新Version等不因Money删除而放宽。
- Point/Quote实际所需Account/Group适用性由证据确认；只保留必要字段，不继承Profit专用上下文，也不假设所有数据必然全Server公共。
- 所有已取得的非Money事实与必要证据照原规则保留；无完整Tick流长期保存。现有历史决定的Money文字仅供决策追踪，不授权V1继续生成Money结果。

### 影响登记与当前剩余事项

[14影响分析](14-money-removal-impact-analysis.md)逐项记录Qxx / Dxx的取代范围、完整Eligibility、指标、条件Terminal依赖、Manager-only门槛与Ranking反例。[09](09-open-questions.md)为当前问题清单。

Q01继续等待技术证据。AR-01（跨Symbol Ranking）与AR-02（是否撤销仅USD产品范围）是本次变更新产生的待确认事项；新决定未授权静默扩展全部币种，也未正式采纳存在尺度问题的新Ranking。本轮仅列出，不向用户发问。原有其他CLOSED / DERIVED / Technical / UI-UX决定不重新开放。

本轮只修改评审记录与技术取证计划；统一Architecture Revision仍待后续，Implementation禁止。

<!-- AR-01-02 authoritative decisions -->
<a id="ar-01"></a>
## AR-01 — DECIDED：Symbol Overview / Symbol List，不做跨 Symbol Points Ranking

- Date：2026-10-15。
- Status：**DECIDED / CLOSED**。
- User Decision Required：**NO**。
- Decision Source：用户在Money Removal后明确确认AR-01。
- Architecture Revision：PENDING BATCH REVISION；本轮仅记录决定，不修改正文或实施UI。

### 已确认业务规则

1. **不做跨Symbol Points Ranking**：不同Symbol的Point Scale不具备直接横向比较意义。V1原Symbol Ranking调整为 **Symbol Overview / Symbol List**。
2. **每个完整Symbol独立展示**：Candidate Events、Eligible Events、Analysis Rate、Volume、Avg Points Markout、Volume Weighted Avg Points Markout，以及各关键Markout Window。
3. **允许的列表排序**：Symbol Name、Event Count、Volume、Analysis Rate。排序只改变列表顺序，不改变统计Scope、Candidate/Eligible或Calculation Result，不生成跨Symbol评分。
4. **不暗示跨品种风险排名**：不得默认或暗示Points越高的Symbol在跨品种意义上风险越高；不按VW Avg Points或其他Points值建立跨Symbol Ranking。
5. **禁止合成替代指标**：不新增Cross-symbol Points Score、Normalized Points、Risk Score、Synthetic Ranking Metric；原Money Removal禁止金额替代指标继续有效。
6. **Symbol内部分析保留**：进入单一Symbol后，继续使用该Symbol自己的Points Markout Summary、Curve、Deal/Event Detail；不因取消跨品种排名删除这些功能。
7. **旧排名失效**：所有“Symbol Ranking by Total Money Markout USD”及Money Removal后拟议“Symbol Ranking by VW Avg Points”均 **SUPERSEDED / NO LONGER APPLICABLE TO V1**。包括原D24及Money Removal决定第9/10项中的Ranking要求；旧记录保留供追踪。
8. **既有统计含义不变**：Candidate/Eligible计数与Analysis Rate保持当前正式Scope；Volume、Avg Points与VW Avg Points仍依据已确认的Eligible集合。关键Window展示不改变平台Required Windows；不跨Login混算，不把Symbol列表排序误当成统计筛选。
9. **非业务呈现移交UX**：排序控件、列名及默认呈现由UI/UX处理，必须清晰区分Candidate / Eligible计数；不因Event Count标签或布局再次创建业务问题，也不在本轮实现。

### 闭合记录

原AR-01尺度风险经用户确认，已由“不做跨Symbol Points Ranking”解决；[14中的反例](14-money-removal-impact-analysis.md)保留为决定依据，不再是待决提案。AR-01不重新开放。

<a id="ar-02"></a>
## AR-02 — DECIDED：撤销 USD-only，Account Currency 仅为 Metadata

- Date：2026-10-15。
- Status：**DECIDED / CLOSED**。
- User Decision Required：**NO**。
- Decision Source：用户明确撤销V1 only supports USD Trading Accounts。
- Supersession Label：**SUPERSEDED BY V1 MONEY MARKOUT REMOVAL**。
- Architecture Revision：PENDING BATCH REVISION；旧正文限制通过本记录明确覆盖，不直接改写正文。

### 已确认业务规则

1. **撤销旧账户币种限制**：V1不再因为Trading Account Currency为EUR、GBP、JPY、AUD或其他币种拒绝分析；不使用USD白名单。
2. **原因与边界**：Money Markout USD已移除，当前Price、Points、Mid Markout不依赖账户币种进行换汇。此决定只撤销币种限制，不扩大其他平台、数据源或计算业务范围。
3. **所有账户仍满足既有要求**：Supported MT4 / MT5 Platform、Valid Manager Trade Data、Valid Historical Quote Source、Exact Symbol、Valid Point、Valid Event Volume及全部Existing Event Eligibility Rules。
4. **Currency只可作原始Account Metadata**：可以保存和展示，但不参与V1 Markout Formula，不参与Eligibility，不进行Currency Conversion。不得把币种读取/验证变成恢复USD-only的隐含门槛。
5. **禁止重引金额逻辑**：不因支持非USD账户重引Money Markout、FX Conversion或Profit Currency Logic；不新增币种转换测试/参数作为V1计算阻塞。
6. **不改其他准确性边界**：同Platform / 真实Server / 完整Symbol、真实事件及量、Point有效性、全部Required Windows、数值有限性、统计集合和Version发布限制均保留；币种不同不允许跨Server或跨Symbol补数据。
7. **历史可追溯**：旧决定原文保留；旧Version仍不可自动改写，Re-analyze继续创建新Version。取消币种范围限制不触发自动历史重跑、补算或迁移。
8. **Q08关闭**：原“可靠确认非USD→不支持并跳过”的币种拒绝分支标 **NO LONGER APPLICABLE TO V1**；不再产生UNSUPPORTED_CURRENCY等基于账户币种的V1拒绝。其他无效账户/事件的既有隔离规则继续适用。

### USD-only Supersession 索引

以下所有限制统一为 **SUPERSEDED BY V1 MONEY MARKOUT REMOVAL**，以本AR-02为用户确认依据；历史文字保留，不作为当前约束：

| 旧位置 / 规则 | 当前状态 |
|---|---|
| 01 D19：当前仅USD交易账户，以及Architecture / Calculation / Assumptions等基线中继承的USD-only限制 | SUPERSEDED；正文留待统一Revision，不再有效 |
| Q03范围说明：不扩大非USD账户支持 | SUPERSEDED |
| 旧Q08及历史Consolidation：非USD不支持/跳过，只有USD继续 | SUPERSEDED；Q08关闭NLA |
| Money Removal阶段AR-02待澄清、暂时保留USD-only前提 | 已由本DECIDED记录解决；不再是当前限制 |
| 技术计划中以USD账户为Money/Profit验证前提 | NO LONGER APPLICABLE TO V1；不成为Points数据路径要求 |

### Review Queue 收束与下一阶段

AR-01 / AR-02各有一份权威DECIDED记录，均CLOSED。Q03/Q08/Q09为NO LONGER APPLICABLE TO V1。**剩余唯一User Decision Required：Q01 — Final MT4 Quote Provider，WAITING FOR TECHNICAL EVIDENCE。**

停止业务问题逐项评审。下一阶段为Technical Evidence Completion：按Manager优先计划完成两平台报价、精度、Ordering、Coverage/Retention、时间映射、Server/Exact Symbol、Point、Partial Close实际量与SDK数值证据。证据完成后再报告并处理Q01；不得提前让用户选择Provider，不把本轮闭题声称为运行验证通过。

本轮未改Architecture / Calculation Specification正文，未实现Manager / Terminal / UI / SQLite / Markout Engine。


