# Technical Evidence — Manager-only 能力核查

日期：2026-10-15。阶段：Technical Evidence Phase。业务问题评审已停止。

**本轮按 MT5 Manager → MT4 Manager 顺序完成本地官方接口、数值契约与样例静态取证，运行验证仍为 NOT RUN。** 详见[当前证据报告](04-manager-only-evidence-2026-10-15.md)。两平台完整能力均 INSUFFICIENT EVIDENCE，Manager-only = UNRESOLVED，Q01 = NOT READY FOR DECISION；没有批准 Architecture Revision 或 Implementation。

权威决定：[V1 MONEY MARKOUT REMOVED](../architecture-design/13-architecture-review-decisions.md#v1-money-markout-removed)、[AR-01 — Symbol Overview / Symbol List](../architecture-design/13-architecture-review-decisions.md#ar-01)、[AR-02 — 撤销 USD-only](../architecture-design/13-architecture-review-decisions.md#ar-02)。AR-01 / AR-02 均 DECIDED / CLOSED；完整影响见[影响分析](../architecture-design/14-money-removal-impact-analysis.md)及[Open Questions](../architecture-design/09-open-questions.md)。当前仅 Q01 最终 MT4 Quote Provider 等待 Technical Evidence，不重复业务提问。

## 当前有效取证范围

| 优先项 | 已有证据层级 | 后续必须取得的证据 |
|---|---|---|
| MT5 Manager Historical Bid/Ask | 本地 SDK 6182 有 TickHistoryRequest / Raw 和毫秒 Tick 字段 | 实际历史覆盖与顺序；Raw / accepted stream 语义；秒级请求边界与毫秒返回保真；实际 Server |
| MT4 Manager Historical Bid/Ask | 本地 SDK 400 / 1470 有 TicksRequest、秒级时间与 Bid/Ask 字段 | 真实历史双边来源；RAW / NORMAL；同秒排序；Coverage / Retention；Exact Symbol；真实 Server |
| Historical Timestamp / Ordering | 官方文档与 SDK 声明提供部分契约 | 对选定 API 版本和历史数据路径有效；同秒 / 同毫秒冲突可确定或准确隔离 |
| Historical Coverage / Availability | 已知需要区分完整返回、截断、同步和保留边界 | 目标 Server 的覆盖、Retention、同步状态与完整性依据，不能仅凭空数组或 count > 0 |
| Server Time / UTC / Identity | 部分接口当前时间及名称语义已核查 | 历史期间有效的确定性映射、DST 依据与真实 Trading Server 身份策略 |
| Symbol Point / 必要 Metadata | MT4 ConSymbol / SymbolInfo 有 Point；MT5 IMTConSymbol 有 Point() | Analysis-Time 目标 Symbol / Server 适用的正式 Point；字段单位、有效性、来源和必要上下文 |
| MT4 Partial Close Event Volume | 存在 close_reopen 模式与 Volume 字段 | 真实事件链、实际 OPEN / CLOSE 成交量、Candidate 完整性，不能用原始 Order Lots 猜测 |
| Numeric Scaling / Sentinel / Precision | MT5 Deal Volume / VolumeExt 等有静态契约 | 保留实际成交量与 Point / Price 精度，字段级 Sentinel、有限性及计算阶段区分 |

V1 主要输出为 Points Markout；Price Markout 为底层 / Detail / Diagnostics，Mid Markout 为辅助诊断。原生 Profit、Money Calculation、FX、Profit Currency、专用 Profit Account / Group Context 不再是 V1 验证或阻塞项。不得为 Future Money 保留多余测试或快照字段。

## 不再适用于 V1 的取证

- V09 MT5 Native Profit、V10 MT4 Native Profit：**NO LONGER APPLICABLE TO V1**。
- 停止 Money / USD 结果、Currency Conversion、Historical / Current FX、Profit Currency、Contract Size / Tick Value Money 路径及其 fallback 验证。
- 既有 E05 Profit 静态事实保留为历史材料并标记 SUPERSEDED，不因计划取消而声称接口不存在。
- AR-02 已明确撤销 USD-only；旧限制标 **SUPERSEDED BY V1 MONEY MARKOUT REMOVAL**，Q08 的非 USD 不支持/跳过问题为 **NO LONGER APPLICABLE TO V1**。不因账户为 EUR、GBP、JPY、AUD 或其他币种拒绝分析；Account Currency 仅可选原始 Account Metadata，不参与公式或 Eligibility，不需要 FX / Profit Currency Logic。

AR-01 已确认 Symbol Overview / Symbol List：每个完整 Symbol 独立展示 Candidate / Eligible / Analysis Rate / Volume / Avg Points / VW Avg Points 及关键窗口；允许名称、Event Count、Volume、Analysis Rate 排序，不做跨 Symbol Points Ranking 或新增评分。原始 Point 与实际 Volume 仍须验证，但不再安排跨 Symbol Points 经济可比性、Normalized Points 或合成排名指标的取证。进入 Symbol 后保留该 Symbol 自身 Summary / Curve / Event Detail。

## Remaining Terminal Dependencies

Money 删除后不再有 Terminal Profit Calculation 依赖。历史 Quote 能力仍需按平台判断；Terminal 只作为条件候选或对照路径，不能在 Manager 未获证时直接从 Profile / Preflight 删除。

MT4 已核查的标准 HST / Tester 路径不满足真实历史 Bid/Ask；结论不扩展为所有可能官方 Terminal 路径绝对不存在。MT5 Terminal 历史 Tick 文档契约仍可保留为候选证据，不再预设它必须是唯一来源。

按最新取证指示：先验证两个 Manager；只有 MT4 Manager 不满足契约或证据不足时才深入 Terminal。本轮已满足该触发条件，仅复核官方标准路径，未新增 Terminal Probe。如果后续 Manager 完整满足契约，不为比较而扩大 Terminal 研究。已有且确需评审的合格 MT4 双来源再按 Q01 比较；不能用模拟报价伪造对照。单一 Version 继续禁止自动混用 Provider。当前 MT4 / MT5 Terminal Required 均为 UNRESOLVED。

## Manager-only Feasibility Requirements

只有 MT4 与 MT5 **分别**证明以下条件后，才可提出 Manager-only 为首选 Architecture Revision Proposal：

1. Manager 能完整取得真实 Trade / Deal Events，并保持 Candidate Membership、MT5 TimeMsc、MT4 Event 时间及实际 Volume 语义。
2. Historical Quote 满足同 Platform / 真实 Server / 完整 Symbol、真实 Bid / Ask、有效时间精度、可靠 Ordering、Coverage / Retention / Availability 合同。
3. Trade / Quote 时间能在实际历史期间确定性映射，不能借同一 Manager SDK 假定所有路径的时间语义相同。
4. Manager 可靠提供正式 Analysis-Time Point 与仍参与 Quote Interpretation / Diagnostics 的必要 Metadata；不通过 Digits 自行补造缺失 Point。
5. 仅对实际 Point / Quote / 事件解释相关的 Account / Group 上下文取证；不保留 Profit Context，也不先假定所有字段全 Server 统一。字段已证明公共时才可安全复用。
6. 各 Version 的来源、Point Snapshot、时间映射、技术证据适用版本及结果保持可追溯；异常沿用已确认的 Event / Version 发布边界。

不要求每个 Provider 都提供 Loading 接口。能够直接返回完整历史并证明查询完成、覆盖与可用性的路径可以参加验证；只有进入有界同步等待时，才必须有明确获证的 Loading / Sync 状态，不能从空返回推断同步。

满足条件也不自动完成业务批准。Q01 的最终 MT4 Provider 继续等待证据；若 Manager-only 不能成立，应报告具体平台和缺口，不自行切换来源、降低标准或重新引入 Money。

## 本轮状态与入口

- [当前 Manager-only 证据报告](04-manager-only-evidence-2026-10-15.md)：逐项状态、完整窗口判定、Numeric / Time 契约、来源和真实阻塞项。
- [历史接口与静态事实](01-capability-findings.md)：保留原证据，当前结论以04为准；不是运行 PASS。
- [当前有效的最小验证计划](02-minimal-verification-protocol.md)：严格 MT5 Manager → MT4 Manager → 条件 Terminal；Runtime NOT RUN。
- [已撤回的旧 Windows 指南](03-windows-evidence-setup.md)：**WITHDRAWN / DO NOT EXECUTE**；原内容保留，包含误嵌入文本和失效 Money 步骤，当前不得作为执行指南。
- [既有静态检查记录](../../temp/technical-evidence/local-static-inspection-2026-10-14.json)：历史主机、SDK 版本、哈希与声明证据，不代表 Server 能力通过。
- [本轮静态 Manifest](../../temp/technical-evidence/manager-only-static-evidence-2026-10-15.json)：本轮实际来源哈希、主机、SDK版本、NOT RUN原因。

本轮没有交付取证 EXE、加载 SDK、查询 Server、开发 UI / 正式程序、创建 SQLite Schema 或修改 Architecture / Calculation Specification。当前主机是 macOS，没有可访问的 Windows SDK 执行入口、指定 Server 取证连接及实服返回材料；没有检测到连接失败，也没有搜索凭据。后续运行所需条件见04。报告后 STOP；不提前要求用户决定 Q01。
