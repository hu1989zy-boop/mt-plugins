> **WITHDRAWN / DO NOT EXECUTE — 2026-10-15**
>
> 本指南已撤回，当前不是有效部署或执行步骤。下方 PowerShell 代码段存在误嵌入的业务调整文本，代码已损坏；旧 Money / Native Profit 准备要求也已被 [V1 MONEY MARKOUT REMOVED](../architecture-design/13-architecture-review-decisions.md#v1-money-markout-removed)取代。请勿复制执行任何下方命令或按旧步骤准备 Profit 验证。
>
> 原内容及用户编辑完整保留，不删除、覆盖、运行或修复脚本。当前有效范围仅见 [2026-10-15 Manager 优先取证计划](02-minimal-verification-protocol.md)及[影响分析](../architecture-design/14-money-removal-impact-analysis.md)。本轮仅修订计划，不提供可运行部署程序；Terminal 只为条件 Quote 候选，Manager-only 尚待两平台分别获证。

# Windows 技术取证环境部署步骤

日期：2026-10-14。用户已确认有一台Windows机器，由用户在该机器操作。本阶段仍为Technical Evidence Completion。

**目前没有已交付的取证EXE、Manager连接工具或Native Profit测试程序。** 以下步骤可完成目录、环境和输入准备，并在已有可用MT5测试终端中手工取得小范围历史Tick样本。复制SDK DLL不会自动变成可运行工具；没有Windows运行结果之前，证据仍为PARTIAL。

## 1. 定位Windows上已有的mt-plugins

在资源管理器中找到这台机器上实际已有的mt-plugins目录。不要把Mac路径粘贴到Windows，也不要自行假设C盘或D盘路径。

如果有多个同名目录，先确认哪个是实际工作目录；如果Windows上不存在mt-plugins，先反馈这一事实，本指南不要求创建第二个mt-plugins或把MarkoutAnalyzer放到其他项目中。

可以在普通PowerShell中运行下面的**只读路径检查**。它不创建目录、不下载文件、不修改系统，也不调用Manager / Terminal。

```powershell
$MaParentInput = Read-Host '请输入这台Windows上已经存在的mt-plugins完整路径'
$MaParentItem = Get-Item -LiteralPath $MaParentInput -ErrorAction Stop
if (-not $MaParentItem.PSIsContainer -or 重大业务范围调整：

V1 取消 Money Markout。

这是用户明确做出的新业务决定，
优先级高于此前所有与 Money Markout 相关的 Decision。

当前仍处于 Architecture Review。
不要进入 Implementation。

==================================================
1. V1 Markout 输出重新定义
==================================================

V1 只保留：

Price Markout
Points Markout
Mid Markout（按此前已确认的辅助诊断定位）

正式主要指标：

Points Markout

Price Markout 继续作为底层/Detail/Diagnostics 数据。

V1 删除：

Money Markout
Total Money Markout USD
Avg Money Markout USD

==================================================
2. 删除 Money Calculation
==================================================

V1 不再需要：

Native Profit Calculation
Terminal Profit Calculation
Specification-based Money Calculation
Money Calculation Fallback

也不再需要为了 Money：

Contract Size Calculation
Tick Value Calculation
Profit Currency Calculation
Currency Conversion
Historical FX Conversion
Current FX Conversion

==================================================
3. Money 相关 Eligibility 删除
==================================================

Event Eligibility 不再要求：

Money Markout Calculation Success。

正式 Event Eligibility 应重新收敛为：

Candidate Membership Valid
+
Trade/Event Required Fields Valid
+
Required Windows Quote Complete
+
Price Markout Valid
+
Point Valid
+
Points Markout Valid

具体完整 Eligibility Contract
请根据所有已确认 Decision 重新整理。

==================================================
4. Q09 失效
==================================================

此前 Q09：

“Money 无法计算时整个 Event 是否进入 Summary”

因 V1 删除 Money Markout：

标记为：

SUPERSEDED — MONEY MARKOUT REMOVED FROM V1

不要删除历史 Decision。

保留原记录用于 Decision Traceability，
但明确它已经不再适用于当前 V1。

==================================================
5. Q03 Money / Currency Conversion
==================================================

此前 Q03 关于：

Native Profit Calculation
Currency Conversion
USD Money Result

同样标记：

SUPERSEDED FOR V1

不要删除历史记录。

==================================================
6. Q13 重新评估
==================================================

重新检查 Q13 中所有因为 Money Calculation 引入的：

Contract Size
Profit Calculation Context
Account / Group Calculation Context

哪些已经不再需要。

但是：

Point 仍然用于 Points Markout。

因此：

Symbol Point
Digits（如技术上需要）
以及 Points Calculation 所需规格

继续保留。

不要因为删除 Money
把 Point Specification 一起删除。

==================================================
7. Symbol Specification 简化
==================================================

V1 Symbol Specification Snapshot
只保留实际仍参与：

Points Calculation
Quote Interpretation
Diagnostics

所需字段。

不要为了 Future Money 功能
继续保存大量 V1 不需要的 Money Specification。

具体最小字段集
由 Technical Review 根据 Manager API 能力确定。

==================================================
8. Summary 重新定义
==================================================

Symbol Summary 的核心指标改为：

Event Count
Volume
Avg Points Markout
Volume Weighted Avg Points Markout

按每个 Markout Window 计算。

删除：

Total Money Markout USD
Avg Money Markout USD

==================================================
9. Account Summary
==================================================

此前因为不同 Symbol Points
不能直接无意义混算，

Account Summary 不再使用 Total Money Markout USD。

因此 Account 层重新定位为：

- Candidate Events
- Eligible Events
- Analysis Rate
- Symbol Ranking
- 数据质量概览

不要自动创建新的跨 Symbol Points 总分。

==================================================
10. Symbol Ranking
==================================================

此前：

Symbol Ranking
默认按 Total Money Markout USD

需要废止。

V1 改为：

在用户当前选择的 Markout Window 下，
按照该 Symbol 的：

Volume Weighted Avg Points Markout

进行 Ranking。

但在正式修改前：

请检查不同 Symbol 的 Point 定义是否会使这种 Ranking
产生业务误导。

如果存在问题：

列为 Architecture Review Issue，
给出最简单的替代 Ranking 方案。

不要自行重新引入 Money。

==================================================
11. Multi-account Overview
==================================================

删除所有：

Total Money Markout USD

跨账户展示。

Multi-account Overview
只保留：

Login
Candidate
Eligible
Analysis Rate
必要的基本状态

不要跨 Login / 跨 Symbol
创建新的 Points 综合评分。

进入 Account 后再看 Symbol Ranking。

==================================================
12. Markout Curve
==================================================

Symbol Markout Curve：

继续使用：

Volume Weighted Avg Points Markout。

Deal Markout Curve：

继续使用：

该 Event 的 Points Markout。

不再存在 Money Curve。

==================================================
13. Export
==================================================

正式 Excel / CSV：

删除 Money Markout 字段。

Diagnostic Evidence Export：

对于旧设计文档中的 Money 字段，
V1 当前规则下不再产生。

Single Trade Test Export：

删除 Money Markout 和 Money Calculation Trace。

==================================================
14. Calculation Trace
==================================================

V1 Calculation Trace 简化为：

Trade Price
→ Matched Quote
→ Price Markout
→ Symbol Point
→ Points Markout

Mid Markout 保留为辅助信息。

删除：

Money Calculation Trace。

==================================================
15. Single Trade Test
==================================================

Single Trade Test 不再验证 Money Calculation。

重点验证：

Manager Trade
Historical Quote
Target Time
Quote Matching
Bid / Ask
Price Markout
Point
Points Markout
Mid Markout
Calculation Trace

==================================================
16. Terminal Dependency 重新评估
==================================================

删除 Money Markout 后：

重新评估 V1 是否还存在任何必须依赖：

MT4 Terminal
MT5 Terminal

的业务能力。

如果：

MT4 Manager API
和
MT5 Manager API

经过 Technical Evidence
均能够满足 Historical Quote Contract，

并且 Points 所需 Symbol Point
也能通过 Manager API 可靠取得，

则提出：

Manager-Only Architecture

作为首选方案。

==================================================
17. Manager-Only 目标架构
==================================================

优先验证是否可以收敛为：

MT4 Manager API
├── Trade History
├── Historical Bid/Ask
└── Symbol Point / Required Metadata
        ↓
Normalized Trade Event
        ↓
Quote Matching
        ↓
Price Markout
        ↓
Points Markout


MT5 Manager API
├── Deal History
├── Historical Bid/Ask
└── Symbol Point / Required Metadata
        ↓
Normalized Trade Event
        ↓
Quote Matching
        ↓
Price Markout
        ↓
Points Markout

==================================================
18. Connection Profile
==================================================

如果最终 Manager-Only Technical Evidence 成立：

Connection Profile 可以删除：

Terminal Connection

只保留：

Platform
Manager Connection
Trading Server Identity

但现在先作为 Architecture Revision Proposal，
等待 Technical Evidence。

==================================================
19. Preflight
==================================================

如果 Manager-Only 成立：

删除：

Terminal Running
Terminal Connected
Terminal Server

相关 Preflight。

保留：

Manager Connectivity
Trading Server Identity
Historical Quote Capability
Historical Coverage / Availability
Required Symbol Metadata

==================================================
20. 不重新引入替代金额指标
==================================================

禁止因为删除 Money Markout
自行增加：

Risk Score
Normalized Money
Notional Value
Exposure Value
Synthetic USD Value
Estimated Profit
Cross-symbol Points Score

等替代指标。

==================================================
21. Future
==================================================

Money Markout 可以记录为：

Future / Optional Module

但 V1 不为它：

提前实现 Profit Engine
Currency Conversion
Account Calculation Context
复杂 Symbol Specification

==================================================
22. Decision Register 处理
==================================================

不要删除以前已经确认的 Money Decisions。

为了可追溯：

将所有因本次决定失效的 Decision
标记为：

SUPERSEDED

并引用新的决定：

V1 MONEY MARKOUT REMOVED

这样能够知道设计为什么发生变化。

==================================================
23. Open Questions 重新归并
==================================================

执行一次影响分析：

找出所有：

Money-dependent
Terminal-dependent
Account/Group-profit-dependent

Open Questions / Technical Questions。

如果因为 Money 删除而不再需要：

关闭并注明：

NO LONGER APPLICABLE TO V1。

==================================================
24. Technical Evidence 优先级
==================================================

下一步技术取证优先验证：

1. MT4 Manager API Historical Bid/Ask
2. MT5 Manager API Historical Bid/Ask
3. Historical Timestamp Precision
4. Ordering Semantics
5. Historical Coverage / Retention
6. Server Time Semantics
7. Exact Symbol
8. Symbol Point
9. MT4 Partial Close Actual Event Volume

Native Profit Calculation
Currency Conversion
Profit Currency

不再属于 V1 阻塞项。

==================================================
25. 当前阶段要求
==================================================

现在只执行：

Decision Impact Analysis
Decision Register Revision
Open Questions Consolidation
Technical Evidence Plan Revision

不要立即修改 Architecture / Calculation Specification 正文。

不要进入 Implementation。

完成后只告诉我：

1. Superseded Decisions
2. Closed Open Questions
3. Remaining V1 Metrics
4. Remaining Terminal Dependencies
5. Manager-Only Feasibility Requirements
6. Remaining User Decisions
7. Remaining Technical Evidence

然后 STOP。$MaParentItem.Name -ine 'mt-plugins') {
    throw '路径必须是已存在的mt-plugins目录；停止，不创建其他父目录。'
}
$MaParent = $MaParentItem.FullName
$MaRoot = Join-Path $MaParent 'MarkoutAnalyzer'
$MaSkills = Join-Path $MaRoot 'skills'
$MaTemp = Join-Path $MaRoot 'temp\technical-evidence'
[pscustomobject]@{
    'mt-plugins Root' = $MaParent
    'MarkoutAnalyzer Root' = $MaRoot
    'Project Skills' = $MaSkills
    'Temporary Workspace' = $MaTemp
} | Format-List
if (Test-Path -LiteralPath $MaRoot) {
    Get-ChildItem -LiteralPath $MaRoot -Force |
        Select-Object Name, Mode
}
```

核对输出确为选定父目录的直属MarkoutAnalyzer子目录。如果父目录或MarkoutAnalyzer是链接、Junction或映射到其他项目的位置，先确认实际落盘位置，不能绕过隔离边界。

确认路径后，在资源管理器中只创建缺少的目录：MarkoutAnalyzer及其docs、skills、temp；取证时再创建temp\technical-evidence。已有目录先检查，不删除、覆盖或重新初始化。当前不创建app、SQLite或正式工程。Windows不需要为本次取证安装任何Skill。

## 2. 记录现有环境，不先安装开发套件

在Windows“设置 → 系统 → 系统信息/关于”记录Windows版本和系统类型（x64或ARM64）；不需要产品密钥、设备ID、序列号。

| 项目 | 现在需要做什么 |
|---|---|
| Windows | 记录版本、Build及x64 / ARM64 |
| MT4 / MT5 Terminal | 记录是否已有可用于测试的实例、Build、安装路径、实际Data Folder和当前Server；不修改正在使用的交易实例 |
| Manager访问 | 确认已有获授权的历史查询权限，能够连接所需真实Server；凭据仅在Windows本地使用 |
| SDK | 使用公司已有合法SDK来源，保留与已审查版本的对应关系 |
| 编译器 / 运行库 | 当前手工准备阶段不要求安装Visual Studio、Build Tools、Python、.NET、Qt或WPF组件 |

本地已审查SDK为MT4 Manager API400 / Build1470与MT5 Manager API6182，不等于要求升级Server或Terminal到这些版本。也不代表已证明兼容现有Server。

后续取证程序必须提供自己所需架构与依赖清单。程序、SDK DLL和运行库架构须匹配；Windows ARM64不能直接视为已验证MT4兼容。若后续明确需要VC++运行库，再按构建版本和架构从[Microsoft官方页面](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist?view=msvc-170)取得对应版本，不下载单独的未知DLL来补错误。

## 3. 准备文档与SDK材料

把本项目docs\technical-evidence中的指南、能力报告和最小验证清单复制到Windows的同一项目docs目录；如有同名文件，先比较，不直接覆盖。不要同步整个mt-plugins父目录，也不要夹带其他插件文件。

SDK原件保持不动。后续工具需要本地副本时，仅将经过授权、与清单对应的SDK材料放入MarkoutAnalyzer\temp\technical-evidence\sdk下；不要改SDK头文件，不覆盖Terminal DLL，不复制到System32，不注册DLL，不运行SDK随包交易样例。SDK不上传聊天或公共仓库。

可在需要核对某个文件时使用PowerShell内置命令；路径需填实际SDK文件：

```powershell
$MaSdkFile = Read-Host '输入需要核对的SDK文件完整路径'
Get-FileHash -LiteralPath $MaSdkFile -Algorithm SHA256
```

Mac端已审查文件的SHA256和二进制架构见[静态检查清单](../../temp/technical-evidence/local-static-inspection-2026-10-14.json)。复制后哈希不同表示材料不同，不能直接沿用旧版本结论。无需把全部SDK打成取证回传包。

## 4. 确认测试实例与输入

优先使用已经存在、允许只读核查的测试实例。不要为了本次准备给正在交易的Terminal换账户、路径、设置、插件或EA；已有实例不适用时记录缺口，后续单独确定隔离实例部署方式。

Terminal安装目录不一定等于数据目录。可通过Terminal的File → Open Data Folder查看实际位置；只记录路径，不复制账户库或配置文件。[MT5官方目录说明](https://www.metatrader5.com/en/terminal/help/start_advanced/start)。

准备以下材料，保存在项目取证目录的说明文件中：

- Platform和实际Server名称；名称是识别线索，不能单独当成已获证的Server Identity。
- 一个实际完整Symbol，包括后缀；不要选Custom / 外部导入品种。
- 一段已经结束、确定有交易或报价的历史时间，先用短区间，例如5–10分钟；这是取证样本大小，不是产品范围或最大回溯限制。
- 一笔真实测试对象的Login、Deal / Ticket及事件时间；需要Money验证时，对象须为实际目标USD账户，不能用另一账户代替。
- MT4已有Partial Close样本的相关历史链；本阶段不为测试新开仓或平仓。
- 如已有历史时区/DST、Retention或SDK方法契约，保留其来源和适用期间。

Manager凭据和Terminal凭据分别由用户保留在本机，不放入这份说明、命令行示例、CSV、截图或回传材料。记录所需字段即可，不整份导出User / Group结构或账户配置。

## 5. 现在可做：MT5手工历史Tick样本

仅在上述已有可用测试实例中操作：

1. 打开Market Watch（市场报价），右键进入Symbols（交易品种）管理窗口。
2. 选定真实Server提供的完整Symbol，进入该窗口的Ticks页签。
3. 选择已发生的短历史区间，点击Request，请求历史Tick。
4. 记录请求区间、界面显示的时间精度、所选Symbol、Terminal Build、显示的Server，以及是否出现等待/错误/不完整提示。
5. 点击Export，保存CSV到MarkoutAnalyzer\temp\technical-evidence内唯一的新文件名，避免覆盖已有样本。
6. 保存原始导出内容；不要在Excel中打开后重存，不排序、不去重、不补Bid/Ask、不改变时间。缺失、空白或单侧字段保持原样并记录。

该入口是Symbols窗口的**历史Ticks**，不是Market Watch当前Tick图、当前报价表Export，也不是Bar导出。官方支持该历史Tick CSV流程。[MT5官方历史数据导出说明](https://www.metatrader5.com/en/terminal/help/trading/market_watch#history)。

历史请求可能使Terminal正常下载和缓存数据；这不涉及修改安装文件或交易配置，但不是磁盘零写入操作。若当前实例不允许这类缓存活动，就不要在该实例执行。

这个CSV只能作为初步样本，不等于直接API调用已经通过。还需核实导出字段语义、是否稀疏字段、毫秒精度、Ordering Contract、请求完成情况、覆盖及时间映射。导出成功不能单独证明Server身份、无遗漏或Native Profit能力。无需导出数月完整Tick历史；有限样本按证据流程管理。

## 6. MT4与原生Money：当前不要运行不存在的程序

| 检查 | 当前可做 | 后续仍需要 |
|---|---|---|
| MT4 Terminal | 记录Build、实际Server及已有历史能力材料 | 不能用HST / Tester / 模拟Ask代替真实历史双边报价 |
| MT4 Manager | 准备SDK、已有只读权限、Symbol与短历史样本条件 | 独立最小查询工具及RAW/NORMAL、时间、排序和部分返回契约 |
| MT5 Manager / Terminal Profit | 准备实际目标Login / Group / USD及规格条件 | 经审查的原生计算验证工具，记录输入、返回码、输出及上下文 |
| MT4 TradeCalcProfit | 收集相同版本官方方法说明 | 先证明非交易持久化副作用和参数语义，再考虑验证；不据函数名直接调用 |

当前没有可运行取证包，因此不提供虚构的EXE名称或启动命令。后续交付的最小工具应与正式程序分离，并附：源文件/二进制状态、Windows原生编译状态、文件哈希、依赖、限定查询功能、准确运行命令及输出字段。工具若尚未编译或未实测会明确标记，不把计划说成已交付。

## 7. 准备完成后回传的内容

通过既有文件传输方式提供：实际Windows项目路径、Windows版本/架构、Terminal Build与实际实例路径、是否已有Manager只读访问，以及第5步有限CSV样本和请求说明（具备条件才执行）。账户信息需要脱敏时保持稳定的一一映射，保留用于关联的关系；不改价格、量、时间或Symbol来“脱敏”。

不需要远程桌面密码，也不需要开放公网端口、关闭防火墙、修改ExecutionPolicy或安装远程代理。不要回传Manager / Terminal密码、Token、账户库、完整配置或SDK包。

拿到这些材料后，才能把Windows原生最小工具的依赖/命令与实际环境对应起来，并完成剩余运行取证。部署准备、CSV样本、SDK接口存在是三个不同证据层级；均不会自动关闭Q01。Q01仍由用户在技术证据报告完成后最终确认。
