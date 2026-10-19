# Markout Analyzer — Product Scope & Decision Register

状态：DRAFT FOR ARCHITECTURE REVIEW。仅设计，不构成实施授权。

## 1. 依据与边界

本套文档综合用户第 0–126 节需求：第一份附件提供 0–39，第二份提供 40–80，Downloads 中完整续篇提供 81–126；第二份中截断的 §81 以完整续篇为准。文中的「§」均指这些需求节号。证据路径见 [Review Index](README.md)。

项目根目录：`/Users/lukeh/Documents/workspace/codex/mt-plugins/MarkoutAnalyzer/`。本阶段只向其 `docs/architecture-design/` 写设计文档；不改业务代码、其他项目、Skills、SDK、Terminal、依赖或数据库。`mt-plugins` 只是多个独立项目的父目录。

标记约定：

- **CONFIRMED**：用户已冻结的规则，后续实施不得自行改变。
- **PROPOSED**：本次架构/交互建议，须 Review，不等于已经实现或验证。
- **OPEN QUESTION / Qxx**：缺失业务决定或待验证能力，详见 [09](09-open-questions.md)。
- **ASSUMPTION / Axx**：设计成立所依赖、尚未证明的条件，详见 [10](10-assumptions.md)。假设不能替代业务决定。

## 2. 产品定位与目标

Windows Desktop、单机、单用户、经纪商内部风控分析工具。用户要从账户定位品种，再检查成交和报价证据。准确性、可复核性和短操作路径优先于功能数量或装饰效果。

输出事实、Markout 计算及数据缺失原因；不输出账户高低风险、Toxic/Safe、可信度或自动处置建议。没有真实用户访谈或可用性测试结果；本次 UX 依据为需求和设计审查，不冒充实测证据。

V1 成功标准：真实成交和同平台 Terminal 报价可追溯；历史版本独立可读；任何不完整/失败数据不伪装成有效 Summary；Account → Symbol → Deal 下钻及返回保持上下文。性能和可用性的数值目标待真实数据规模与 Review，不虚构 SLA。

## 3. Decision Register — 全部 CONFIRMED

| ID | 冻结决定 | 依据 |
|---|---|---|
| D01 | Windows Desktop Standalone Single-user；SQLite 本地存储；无 Web 后端/浏览器产品/云端 | §0–4,56,102–104 |
| D02 | 每任务一个 Platform、一个 Profile；MT4 Manager 仅配 MT4 Terminal，MT5 同理，禁止交叉及替代 Profile | §5–9,47 |
| D03 | 预检 Manager、Terminal 运行/连接、Expected Server、Quote Access；环境失败不能归为 NO_QUOTE | §7,40,54 |
| D04 | 输入仅 Platform/Profile/Accounts/From/To/Remark；粘贴解析、去重、验证账户；部分无效账户保留 ACCOUNT_NOT_FOUND，继续有效账户；不导入 Excel/CSV，不选 Symbol，范围内所有真实成交 Symbol 均纳入候选 | §8,66,79,108 |
| D05 | 使用 Server Time；不以 Windows 本地时间匹配，不猜时差，不自动时区纠正 | §10 |
| D06 | 范围选择成交事件；MT5 用 TimeMsc；MT4 OPEN/CLOSE 分别判断在范围，不要求完整持仓生命周期 | §11–13 |
| D07 | MT5 只纳入实际 BUY/SELL Deal；IN/OUT/INOUT/OUT_BY（存在时）；INOUT 不拆，多 Deal 不合并 | §12,80 |
| D08 | MT4 BUY Order：OPEN BUY、CLOSE SELL；SELL Order 相反；分别使用真实开/平时间和价格 | §13 |
| D09 | MT4 SECOND 精度，计算表示可补 .000，不声称毫秒；100/500ms 为不支持，不是 NO_QUOTE | §14 |
| D10 | MT5 必须有效 TimeMsc；缺失/无效时 INVALID_DEAL_TIME_MSC，禁止回退 Time | §15 |
| D11 | 固定窗口：MT5 100/500/1000/3000/5000/10000/30000/60000/180000ms；MT4 为其中 ≥1000ms 七窗 | §16 |
| D12 | Target=EventTime+Window；匹配最新有效 Tick≤Target，不用未来 Tick、插值、估算；允许一 Tick 多窗复用 | §17–18 |
| D13 | 各支持窗同时要求有效 Bid 和 Ask；保存 Target/Quote Time/Bid/Ask/Mid/QuoteAge；无自设 stale 阈值；缺失 NO_QUOTE、无效 INVALID_QUOTE | §19–20 |
| D14 | 全部支持窗口报价完整才可进入聚合；任一支持窗 NO_QUOTE/INVALID_QUOTE 则整 Event DISCARDED，保留诊断；MT4 两个不支持窗口不计完整性；其他计算失败资格待 Q09 | §21–22,83 |
| D15 | Symbol 使用完整精确名称和后缀，无模糊匹配或重命名映射；不存在 SYMBOL_NOT_FOUND | §23 |
| D16 | 主 ΔPrice：BUY=FutureBid−Price；SELL=Price−FutureAsk；正值客户有利，负值客户不利 | §24 |
| D17 | Mid=(Bid+Ask)/2；Mid Markout 仅详情/诊断/测试/完整导出；Price 差同样不作主 Summary | §25–26 |
| D18 | Points=主 ΔPrice/Terminal Spec Point；名称为 Points，不是 Pips | §27 |
| D19 | Money 为理论 USD Markout；当前仅 USD 交易账户；不做 Currency Conversion；优先 Terminal/SDK ProfitCalc，否则严格按模式和规格；绝非全品种通式 | §28 |
| D20 | Raw Profit/Commission/Swap 等只为核查；Money 不计利润字段、佣金、Swap、Rebate、Fee | §28,81 |
| D21 | 每版本保存 Spec/Rule/Environment 及结果证据，历史结果不依赖之后的当前规格或连接 | §29,49,96–98 |
| D22 | Symbol 各窗：Valid Event Count、Event Volume、Avg Points、Volume Weighted Avg Points、Total Money、Avg Money | §30–31,83 |
| D23 | Account 可跨 Symbol 汇总 Money，不以跨 Symbol Points 作核心指标；显示 Analyzed/Total、Analysis Rate，不评分 | §32,86 |
| D24 | Ranking 默认 1s、Total Money 降序；主窗 MT5 100ms/500ms/1s/5s/30s/180s，MT4 1s/5s/30s/180s | §33 |
| D25 | Symbol Curve 单曲线 Volume Weighted Avg Points；Event Curve 单曲线 Points；全支持窗口，不混叠 Money/Mid/Price | §34,38 |
| D26 | Direction ALL/BUY/SELL；Entry MT5 ALL/IN/OUT/INOUT/OUT_BY（实际存在才显示），MT4 ALL/OPEN/CLOSE；不增加复杂交叉统计 | §35–36 |
| D27 | 成交表一 Event 一行；Symbol/Side/Entry或Event/Status 筛选，Points/Money 排序；全部计算细节渐进展开 | §37–38,70–71 |
| D28 | 多账户分别概览和下钻，绝不合并 Login Summary；缓存复用不得改变账户结果边界 | §39,82,85 |
| D29 | Diagnostics 与日常 UI 分离；Task/Account/Event/Window 四层问题；一事件多缺窗均可查，Copy Diagnostic/Trace 不含凭据 | §40–42 |
| D30 | Single Trade Test 输入 Platform/Profile/Login 和 MT5 Deal ID 或 MT4 Ticket+OPEN/CLOSE，核验记录归属；仅 Manager 真实记录、同一引擎；不允许手造或改 Symbol/Side/Volume/Price/Time，不进入正式任务汇总 | §43–44 |
| D31 | History 只读已保存结果，不自动连 API/补 Tick；Re-analyze 产生新版本，保留旧版本；默认原 Platform/Profile/Accounts/Time Range | §45–49 |
| D32 | 无复杂版本 Diff、Tolerance、Duplicate Task；无 Pause/Resume | §46,50,106–109 |
| D33 | 当前确认任务业务状态为 PENDING/RUNNING/COMPLETED/FAILED/CANCELLED；技术扩展状态须另行 Review；安全内部排队，不暴露线程控制 | §45,51,72,101 |
| D34 | 真实阶段/计数进度；无伪百分比或强制 ETA；无强制范围上限/手工拆日期 | §52,77,100 |
| D35 | Cancel 可用于 Pending/Running，保留任务/日志/诊断；部分结果不是正式 Summary；连接失败有限重试，持续失败为 FAILED | §53–54 |
| D36 | Profile 配对配置与凭据分离；禁止凭据明文 SQLite/日志/导出/诊断/Copy/Crash；保护方案待核实 | §55,94 |
| D37 | SQLite 保存任务/版本/账户/事件/窗结果/匹配报价/规格/规则/诊断；不保存完整历史 Tick 流 | §56–57,95–98 |
| D38 | Technical Logs，不建 User Audit Logs；History 搜索 TaskID/Login/Remark，筛选平台/Profile/状态，无指标墙 | §58–59 |
| D39 | 永久保留、用户手动二次确认删除整个 Task 及其所有版本/结果/报价/诊断/快照；无单版删除/自动清理 | §60,93 |
| D40 | 导出指定单一 Version 的 XLSX；账户独立，不每 Symbol 一表；CSV 多账户 ZIP、账户独立；具体表/文件命名与编排见下方建议 | §61–64,99 |
| D41 | 导出完整字段含版本/平台/Profile/时间范围/规则/规格信息；不能因屏幕简化而丢审计字段 | §61–64,99 |
| D42 | 包含新分析、历史、单笔测试、诊断、连接配置功能，日常分析与诊断分离；Account→Symbol→Deal，下钻返回保留状态；五入口编排为下方建议 | §65–69 |
| D43 | 桌面键鼠、密集表格、长时使用、DPI/Light/Dark；避免 Landing Page、卡片墙、粗糙工具式 UI；不自动选框架 | §68–71,110 |
| D44 | 无 T0 强制数据、Slippage/Bridge/LP、持仓时长/生命周期、自动风险结论、异常阈值剔除或用户公式/窗口配置；不按 Manual/EA/Copy/SL/TP/Stop Out/Manager/Pending 等来源过滤；Reason/Comment/Magic 仅核查 | §80,86–91,106–109 |
| D45 | Adapter 隔离平台对象并输出 NormalizedTradeEvent；共用匹配和计算；不得用另一个平台补报价 | §73–78 |
| D46 | 历史引用使用版本快照，不受 Profile 改名/改配置影响；不可用 Profile 仍可读历史 | §48,97–98 |
| D47 | 本阶段只交架构、逻辑模型、规则、UX、线框、验收、问题和技术栈建议；不建 app、不装运行库、不调用真实 API、不实现 SQLite/UI | §103–126 |

## 4. 范围分类

**PROPOSED，沿用用户建议**：五个一级入口为 New Analysis、History、Single Trade Test、Diagnostics、Connection Settings；XLSX 使用 TaskSummary 与每 Login 的 Summary/Deals/Discarded，CSV 多账户 ZIP 使用每 Login 的三类文件。精确布局与命名并非冻结计算规则，具体实现策略留给后续设计。

| 分类 | 内容 |
|---|---|
| Must Have V1 | 双平台独立适配（能力门槛须验证）、批量账户、固定窗 Markout、Account/Symbol/Event 下钻、Diagnostics、Single Test、持久化历史与新版本、导出、取消、连接配置与安全凭据 |
| Simplified V1 | 单人单机；少量主指标；单条曲线；内部队列/有限重试；读取版本而非 Diff；仅本地 SQLite；无用户高级计算选项 |
| Out of Scope | Web/云/多用户/RBAC/SSO、自动风险评分、货币转换、跨平台数据、真实交易执行、Pause/Resume、Duplicate Task、生命周期重建、任意自定义窗口/公式/线程、全流 Tick 数据仓库 |
| Future / 非承诺 | 后续用户若提出的新分析维度、币种或多人场景，必须单独需求与评审；本次不为其预建插件框架或服务层 |

## 5. 评审门槛

文档可在 Qxx 未结时进入 Architecture Review，但不可进入实现。优先解决 MT4 历史 Tick、时间语义、Money 路径、事件资格及 SDK 行为；之后才做 Stack Review 和实现计划。新增可行性问题不等于获准降低数据规则。
