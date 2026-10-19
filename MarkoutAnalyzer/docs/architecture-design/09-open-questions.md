# OPEN QUESTIONS — Money Removal Consolidation

状态：Architecture Review业务问题收束 → Technical Evidence Completion。更新日期：2026-10-15。权威决定：[V1 MONEY MARKOUT REMOVED](13-architecture-review-decisions.md#v1-money-markout-removed)、[AR-01](13-architecture-review-decisions.md#ar-01)、[AR-02](13-architecture-review-decisions.md#ar-02)。AR-01/AR-02均DECIDED / CLOSED，唯一剩余用户决定Q01继续等待证据。停止逐项业务提问，不修改Architecture / Calculation Specification正文，不进入Implementation。本轮未执行新的运行验证。

[Decision Register](13-architecture-review-decisions.md)保留历史决定及SUPERSEDED标记；[14影响分析](14-money-removal-impact-analysis.md)同步当前非Money Eligibility、Symbol Overview及取消USD-only的范围。旧Consolidation和AR待决快照不再是当前队列。

## Remaining Questions Summary

原Q01–Q25各计一个主分类，共25题；复合题的技术/UX残余不重复计算。只有Q01仍需最终业务决定；Q03/Q08/Q09不再适用。Q08原非USD拒绝规则已由AR-02明确撤销，其他无效账户/事件的隔离原则不变。

| 原问题主分类 | 数量 | 编号 |
|---|---:|---|
| P0 — User Decision Required | 1 | Q01 |
| P1 — User Decision Required | 0 | 无 |
| P2 — User Decision Required | 0 | 无 |
| NO LONGER APPLICABLE TO V1 | 3 | Q03、Q08、Q09 |
| Derived / Closed by Existing Decisions（适用部分） | 8 | Q05、Q11、Q12、Q13、Q15、Q16、Q17、Q19 |
| Technical Review | 12 | Q02、Q04、Q07、Q10、Q14、Q18、Q20、Q21、Q22、Q23、Q24、Q25 |
| UI/UX Design | 1 | Q06 |

新增范围问题另计2项，均已闭合，不与原25题混算：

| 编号 | 优先级 | 当前状态与一句话说明 |
|---|---|---|
| AR-01 | P1 | DECIDED / CLOSED：Symbol Overview / Symbol List，不做跨Symbol Points Ranking；允许Name / Event Count / Volume / Analysis Rate排序 |
| AR-02 | P0 | DECIDED / CLOSED：撤销USD-only；Account Currency仅为可选原始Metadata，不进公式/Eligibility，不换汇 |

因此当前剩余User Decision Required共 **1项：Q01**。停止业务问题逐项评审，进入Technical Evidence阶段；Q01必须等证据完成后再处理。AR-01/AR-02不重问，其他已闭合内容及Technical / UI-UX事项不重新开放。

## 归并原则

- Q03 / Q09的旧Money口径不再引用为当前资格；当前统一Eligible Contract见14，由新决定与Q05/Q06/Q07/Q11/Q12/Q13适用部分/Q15/Q16/Q22承接。
- Money-dependent问题只在确实不再需要时关闭；Point、真实Volume、报价可靠性和最小必要上下文不能随Money一并删除。
- Candidate Population不确定仍阻断Version正式发布；已知Candidate且影响可隔离、其他Events独立可靠才局部排除；未就绪与真正FAILED保持各自语义。
- Technical Review不等于获证或Implementation许可；最小字段、SDK接口、时间/顺序/覆盖证据由技术完成，不逐项让用户表决。
- UI/UX只负责不改变业务语义的呈现，不扩大范围、不创建新指标，本轮不开发UI。
- Ranking及账户币种范围已由AR-01/AR-02明确解决，不再保留为歧义或重新拆成问卷。Currency不是Required Markout Input，不增加币种读取/转换资格门槛。

## Q01 — 最终 MT4 Quote Provider

**Status: P0 — USER DECISION REQUIRED / WAITING FOR TECHNICAL EVIDENCE。**
**User Decision Required: YES；当前不提问。**

原B候选评估许可不重开；最新决定调整取证优先级为Manager优先，符合全部门槛后提出Manager-only首选方案，不等于已批准替代Terminal。MT4同Server、精确Symbol、真实历史Bid/Ask、时间、Ordering、Coverage/Depth及Provider追踪不变；两个候选都具备合格能力时必须比较，不自动fallback。都不合格则MT4不可用，不降低准确性标准。

MT5 Manager也已进入获准技术候选评估，不能沿用“Terminal不可替代”的旧前提。两平台分别获证才可提出完整Manager-only；不要求用户凭业务判断证明接口能力。下一阶段先完成Technical Evidence，提交各来源结果、推荐及风险后才回到Q01；本轮闭题不代表能力已通过。

## Q02 — 历史时间映射与语义证据

**Status: TECHNICAL REVIEW — NO USER DECISION REQUIRED。**
**User Decision Required: NO。**

Q02 A有效：实际API时间语义、历史Offset / DST、精度及Platform / Server / 历史期间适用证据待完成。不得用当前Offset、Windows Local Time或价格走势猜历史映射。MT5保留TimeMsc，MT4不造毫秒。影响成员判定依Q12；成员已知的可隔离失败按Event处理，不能隔离则不发布。TIME_MAPPING原因不混为NO_QUOTE。新决定不降低时间要求。

## Q03 — 原生 Profit / Currency Conversion

**Status: NO LONGER APPLICABLE TO V1；历史决定 SUPERSEDED FOR V1。**
**User Decision Required: NO。**

Native Profit、内部FX来源、USD Money定义及历史金额重建问题全部退出本次V1。关闭专属取证，无须决定Current / Historical FX或转换路径。原文保留在13并引用V1 MONEY MARKOUT REMOVED；历史不可变通用原则继续有效。AR-02已明确撤销USD-only，不因账户币种拒绝分析，也不恢复任何FX/Profit逻辑。

## Q04 — 无报价与全无效报价的原因分类

**Status: TECHNICAL REVIEW — NO USER DECISION REQUIRED。**
**User Decision Required: NO。**

Q04 A的latest valid Tick规则保持。在覆盖充分、查询完成时，无Raw Tick与Raw Tick全无效都不能满足完整报价资格；保留不同诊断子因，名称交State/Error Review。Coverage未知、Retention、同步、Ordering失败不能混入NO_QUOTE / INVALID_QUOTE。不缩短回溯，不新增Quote Age阈值。资格依据改为新非Money契约，不沿用Q09 Money条件。

## Q05 — Numeric Validity 与执行边界

**Status: CLOSED BY EXISTING DECISIONS；Money分支PARTIALLY SUPERSEDED。**
**User Decision Required: NO。**

六项A/A/A/A/B/A不重问。Bid / Ask / Trade Price / 实际Event Volume / Point均要求>0，Ask≥Bid，所有实际输入Present/Parseable/Finite/Unit Known，真实零结果与错误分开。第五项关于Point失败后的Money执行顺序关闭NO LONGER APPLICABLE TO V1；Point失败仍不Eligible，已得非Money事实保留。不从Digits推算替代正式Point，不默认、手填或替换。Scaling、Sentinel和数值保真继续Q24/Q25技术评审。

## Q06 — 时间输入与默认呈现

**Status: UI/UX DESIGN — NO USER DECISION REQUIRED。**
**User Decision Required: NO。**

业务B/B保持：[From,To)、MT5可选毫秒、秒输入=.000、MT4拒绝非零毫秒、历史/导出不丢原精度。To仅筛Candidate，不限制Required Targets。时间控件、默认位数、快捷范围、Label移UI/UX，不再逐项问，也不在本轮实施。

## Q07 — Ordering 与 Historical Coverage

**Status: TECHNICAL REVIEW — NO USER DECISION REQUIRED。**
**User Decision Required: NO。**

局部Ordering可隔离A与整体不可靠当前Run FAILED B保持；去掉Money成功/聚合引用。验证接口顺序契约、Sequence/ID、重复/同时间戳冲突、完整响应及证据适用性；稳定数组返回不构成真实顺序证明。无固定最大lookback，有限读取空返回不能证明NO_QUOTE。无Provider长期封禁/解锁系统。

## Q08 — 原非 USD 账户拒绝 / 跳过规则

**Status: NO LONGER APPLICABLE TO V1 / CLOSED。**
**Superseded By: AR-02 — SUPERSEDED BY V1 MONEY MARKOUT REMOVAL。**
**User Decision Required: NO。**

原D19仅USD及旧Q08“非USD→不支持并跳过”的业务限制已被明确撤销。EUR、GBP、JPY、AUD及其他账户币种不再构成拒绝原因；不创建UNSUPPORTED_CURRENCY类V1资格分支。历史原文仅作决策追踪，不能作为当前Preflight、Candidate或Eligibility门槛。

Account Currency可以作为原始Metadata保存展示，不参与Markout公式、Eligibility或Currency Conversion。仍必须满足支持平台、真实Manager数据、合格同源历史Quote、Exact Symbol、有效Point与真实Event Volume及其他全部资格/发布规则；其他无效账户/事件隔离与Q12总体未知限制不变。

## Q09 — Money 成功与正式 Summary 资格

**Status: NO LONGER APPLICABLE TO V1；历史决定 SUPERSEDED — MONEY MARKOUT REMOVED FROM V1。**
**User Decision Required: NO。**

整题旧决定保留但失效。新的统一资格无需再投票：Candidate确定、Required Event Fields有效、全部Required Quotes完整、Price有效、Point有效、Points有效。Price/Points/Volume等规则见14。统一Eligible集合、可用事实保留、Rate分母和Version发布边界继续，不设置Money Success或Money-only/Points-only双样本选择。

## Q10 — MT4 Partial Close、身份和候选完整性

**Status: TECHNICAL REVIEW — NO USER DECISION REQUIRED。**
**User Decision Required: NO。**

仍必须证明真实OPEN/CLOSE事件来源与实际Volume，包括Partial Close / Close-by、仍持仓及区间后才关闭交易的读取覆盖；不能以原始Lots、Comment猜链、Profit反推或Ticket错误合并。真实Volume仍用于VW Points和Summary Volume，不能随Money删除。成员已确定而量无效按Event失败；总体/归属不可证按Q12，不发布伪完整Summary/Rate。

## Q11 — Analysis Rate 与 Reason Breakdown

**Status: CLOSED — B / A / A。**
**User Decision Required: NO。**

同Scope Eligible/Candidate、零候选“— / No Candidates”与有效0%、Discarded按Event去重/Reason独立重叠计数完整保留。Money Failure分支不再存在；全部实际已记录的非Money原因保留，不为补原因多做已禁止计算。状态/枚举交技术，文案交UX。

## Q12 — Candidate Membership 不确定

**Status: CLOSED — DECIDED B；唯一权威决定保留。**
**User Decision Required: NO。**

总体/范围归属不确定→Version正式Summary/Rate阻断；成员确定但MT5 TimeMsc无效→Event级资格失败。禁止TimeMsc猜测、秒回退、伪Candidate Total/No Candidates。删除Money不放宽此规则，不重复写Decision。

## Q13 — Analysis-Time Point / Required Specification

**Status: CLOSED（业务适用部分）；Money-only部分NO LONGER APPLICABLE TO V1。**
**User Decision Required: NO。**

Analysis-Time Point与必要字段继续逐Version保存、重新分析重新取得，旧版不变。不要求Event-Time历史规格库。Snapshot只包含Points、Quote Interpretation、必要Diagnostics实际使用内容；不再强制Contract Size / Tick Value / Profit Currency / Calculation Mode等Money-only字段。

Native Profit专用目标Login/Group绑定及等价计算环境问题关闭；Point/Quote实际字段仍须适用真实目标上下文，不能猜公共性或用其它上下文填缺。已证Server/Symbol公共数据可复用。最小字段/是否确受Group影响/来源获取方法交Q24技术证据，不建立Profit Context系统。

## Q14 — 空结果、未就绪及失败终态模型

**Status: TECHNICAL REVIEW — NO USER DECISION REQUIRED。**
**User Decision Required: NO。**

保留零Candidate、零Eligible、总体未知、未来未成熟、Sync未就绪、真正可靠性失败的不同语义；不伪造零Markout，不把Future/Sync都称FAILED。建议区分执行状态、结果可用性及原因，Version保存事实、Task展示相应Version；具体枚举/层级由技术建议。Money错误分支关闭，不因改名改变发布政策。

## Q15 — Task Stable Scope / Re-analyze

**Status: CLOSED — A / A。**
**User Decision Required: NO。**

Platform + Trading Server Identity + Login Set + 精确[From,To)固定。Re-analyze完整新Run/新Version，允许本次真实数据/Point/必要环境变化但旧版不变；不改范围、不补算旧版。Profile名称/ID不是Server身份；同Server证明、用户明确选择替代Profile的简单UX/技术处理保持，不偷偷换源。删去Money-only上下文，真实Trade/Quote/Point同Server规则不变。跨Server不能强制继续。

## Q16 — Side AND Entry / Event

**Status: CLOSED — B。**
**User Decision Required: NO。**

既有两个维度AND组合，同条件作用于Candidate/Eligible、Summary/Rate/Curve；基于保存结果聚合，不重查、不重算、不新Version。不扩任意查询/Pivot/多组合比较，不跨Login。Version发布阻断不能由过滤绕过。移除Money不重新开放复杂交叉统计问题。

## Q17 — Export / Single Trade Test 生命周期

**Status: CLOSED — B / B / A；Money字段PARTIALLY SUPERSEDED。**
**User Decision Required: NO。**

受限Version诊断导出、单笔测试文件导出继续显式触发且只输出已保存/稳定会话事实，不重查/补算/新Task或Version。保留DIAGNOSTIC ONLY / SINGLE TRADE TEST分类、真实阶段状态、无Credential、账户隔离、导出失败不改源状态；删除Money字段和Money Trace。Unknown不能填0或生成伪总体/Rate。

Single Test不建应用内长期History/SQLite测试历史、不进正式History、不要求重启/Crash恢复、不自动保存或导出、不提供重新导入；当前会话保留方式交技术/UX。正式XLSX/CSV格式和Copy Diagnostic不变。

## Q18 — Profile 引用与删除所有权

**Status: TECHNICAL REVIEW — NO USER DECISION REQUIRED。**
**User Decision Required: NO。**

历史快照独立可读、活动Run绑定不漂移、Task整体删除边界保持。删除不自动扩到用户另存Export、独立Technical Logs或其他项目。运行中删除安全流程、引用/墓碑方案交技术，不新增业务删除权限。Profit上下文引用关闭；Terminal引用仅随最终Quote/Point数据路径条件适用。

## Q19 — 重启恢复 Pending Run

**Status: CLOSED — A。**
**User Decision Required: NO。**

恢复持久化PENDING，不自动Preflight/查询/计算；用户明确Start原Run，不创建Task/Version、不改固定输入，完整重新验证环境/Server，必要稳定技术证据复用须满足适用条件。已结束Version不自动重跑，RUNNING中断独立交State Review。Profit路径已删除，不影响这些启动边界。

## Q20 — Credential / SDK Log 保护

**Status: TECHNICAL REVIEW — NO USER DECISION REQUIRED。**
**User Decision Required: NO。**

Manager凭据不入明文存储/日志/导出/Diagnostics/Crash；SDK只取必要白名单字段，不dump整账户结构。保护API与实际日志行为仍需证据。Terminal凭据仅在保留其候选路径时条件适用；不因为Manager-only提案改现有Terminal/SDK或秘密配置。

## Q21 — Export 容量与组织

**Status: TECHNICAL REVIEW — NO USER DECISION REQUIRED。**
**User Decision Required: NO。**

指定单Version、账户隔离、当前完整非Money字段、既定单XLSX与CSV ZIP能力保持。Sheet容量/拆分/命名由技术设计，不静默截断或擅改多XLSX/CSV-only；推荐同工作簿按账户可追溯分Sheet。删Money不增加PDF/JSON/XML或新报告种类；真实限制迫使改交付契约时再提交具体冲突。

## Q22 — Future / Retention / Synchronization

**Status: TECHNICAL REVIEW — 四项A / B / A / B业务已决定。**
**User Decision Required: NO。**

未来Target不等待、任一候选未成熟整体Not Ready；已证Retention缺失可可靠隔离；有明确历史Loading证据才有限可取消等待；Sync超限可可靠隔离时发布其它完整Eligible结果，不能隔离则整体Data Not Ready。Q22第三项第15条已由第四项限定，不回到“任意局部超时禁止整版结果”。

仅删除Money成功条件；Price/Point/Points仍须完整。区分NO_QUOTE / Invalid / Retention / Future / Sync / Connection，不后台重试/自动新Version/旧版补丁。最终状态层级、全任务零结果表达与等待参数交技术，不再询问。

## Q23 — Terminal 自有历史缓存

**Status: CONDITIONAL TECHNICAL REVIEW — 非整体关闭。**
**User Decision Required: NO。**

Terminal Profit依赖NO LONGER APPLICABLE TO V1；历史Quote/必要元数据候选是否仍需Terminal尚待证据。若保留，查询触发自有缓存/历史同步的实际行为与边界仍需查证；若Manager-only获证并被采纳，该平台相关项才可标不适用。当前不修改或安装Terminal，不把连接成功当历史能力证明。

## Q24 — SDK / Provider / 最小元数据契约

**Status: TECHNICAL REVIEW；Money-only子项NO LONGER APPLICABLE TO V1。**
**User Decision Required: NO。**

保留ABI/架构/生命周期/字段/分页/读取完整性/并发/取消、Server Identity、Manager历史Quote、Point实际来源和必要上下文证据。Terminal实例绑定仅条件适用。

关闭Native Profit、FX、Profit Currency、Money-only Account/Group Context与Spec计算Fallback。Point / Quote确受Account/Group影响时只保存必要实际字段，不做等价账户引擎，也不假设所有字段公共。最小字段技术建议见取证计划，不需要用户逐字段决定。

## Q25 — Numeric Precision / Normalization

**Status: TECHNICAL REVIEW；金额分支NO LONGER APPLICABLE TO V1。**
**User Decision Required: NO。**

保留Trade Price、Bid/Ask/Mid、Point、实际Volume、Price/Points及加权聚合的单位/Scaling/Sentinel/数值保真，无损ID与时间精度。VolumeExt精度仍重要，不能因为删除ProfitExt一并丢掉。无效输入与Overflow计算失败分层；真实零不因浮点伪差变成异常。显示位数交UX，不设业务Epsilon阈值，不选实现类型作为本轮技术栈。

## AR-01 — Symbol Overview / Symbol List

**Status: DECIDED / CLOSED。**
**User Decision Required: NO。**

用户确认不做跨Symbol Points Ranking。每个完整Symbol独立展示Candidate Events、Eligible Events、Analysis Rate、Volume、Avg Points、VW Avg Points及各关键Window；允许Symbol Name、Event Count、Volume、Analysis Rate排序，不将Points高低暗示为跨品种风险高低。

不新增Cross-symbol Points Score、Normalized Points、Risk Score或Synthetic Ranking Metric。进入Symbol后继续其自身Points Summary / Curve / Deal/Event Detail。旧Total Money排名及拟议VW Avg Points排名均不适用。权威记录见[AR-01](13-architecture-review-decisions.md#ar-01)，不重复询问控件或排序Label。

## AR-02 — 撤销 USD-only

**Status: DECIDED / CLOSED。**
**User Decision Required: NO。**

所有旧USD-only限制标记 **SUPERSEDED BY V1 MONEY MARKOUT REMOVAL**。币种仅可作原始Account Metadata，不参与公式/Eligibility、不换汇；不因EUR/GBP/JPY/AUD或其他币种拒绝。其他平台、数据源、Symbol、Point、真实Volume及完整Event Eligibility要求保持，禁止恢复Money / FX / Profit Currency Logic。

D19、Q03旧范围、Q08非USD跳过及历史Consolidation限制由此覆盖；正文待统一Revision。权威记录见[AR-02](13-architecture-review-decisions.md#ar-02)。

## 当前后续边界

1. AR-01 / AR-02已记录并关闭，业务问题逐项评审停止；唯一Q01等待Technical Evidence。
2. 下一阶段按[当前取证计划](../technical-evidence/02-minimal-verification-protocol.md)完成Manager优先的技术证据；已有静态文档不等于运行通过。本轮没有新增Runtime Evidence。
3. 旧Windows指南仍已撤回，下一阶段执行前须提供与当前非Money范围一致的有效步骤，不执行受损代码；这不是新的业务问题。
4. 证据完成后先给出Evidence Summary、MT4 Terminal Result、MT4 Manager Result、Recommended MT4 Quote Provider、Remaining Technical Risks，再处理Q01。
5. 不修改Architecture / Calculation Specification正文；不实施正式Manager/Terminal、UI、SQLite或Markout Engine；Manager-only仍是证据成立后的条件提案。
