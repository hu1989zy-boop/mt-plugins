# Markout Calculation Specification

状态：**DRAFT FOR ARCHITECTURE REVIEW；仅规格，不是实现或真实 API 测试结果。**

依据：用户需求 §0–126；冻结决定见 [01](01-product-scope-and-decisions.md)，未决项见 [09](09-open-questions.md)，未验证能力见 [10](10-assumptions.md)。本文中的 Dxx、Qxx、Axx 与上述登记表共用编号。所有示例均为明确的合成数据，不允许在 Single Trade Test 中用其代替 Manager 真实成交。

## 1. 计算范围与前置条件

**CONFIRMED — D02–D06、D15、D19、D28，§5–11、23、28、39、82。**

一次正式 Analysis Version 绑定一个 Platform、一个 Connection Profile、指定账户和 Server Time 范围。MT4 Manager 只能使用同 Profile 的 MT4 Terminal 报价；MT5 同理。完整 Symbol 包括后缀精确匹配，不能使用其他 Profile、平台或近似名称补数据。

Manager/Terminal 连接、Expected Server、报价访问能力未通过 Preflight 时不能开始 Markout Calculation。连接错误、权限错误、查询未完成和历史同步问题不能伪装为 `NO_QUOTE`。范围不设人为上限；技术上的分块不得改变成交集合、时间边界和计算结果。

范围选择的是成交事件，不是持仓生命周期。目标窗口可延伸至任务 To 之后；不得把 Target 截断在 To，也不得要求用户为此拆任务。From/To 端点包含性及输入精度尚未确认（Q06）。目标位于尚未发生的时间时如何等待或处理，须解决 Q22；不能利用允许旧 Tick 的规则掩盖未到达的观察窗口。

## 2. Event 定义与平台规范化

### 2.1 MT5

**CONFIRMED — D06–D07、D10，§11–12、15、80–81。**

- 最小分析单位为真实 BUY/SELL Deal；每个 Deal 独立，即使同属一个 Order。
- 纳入 IN、OUT、INOUT、实际存在的 OUT_BY。INOUT 不拆成开平两笔，按原 Deal 的实际 Side、Volume、Price、TimeMsc 计算。
- Balance、Credit、Commission、Charge、Bonus、Dividend、Correction 等非市场成交不形成 Markout Event。
- 不按 Manual、EA、Copy、SL、TP、Stop Out、Manager Operation、Pending Trigger 或 Reason/Comment/Magic 过滤。
- `source_time_precision = MILLISECOND`。有效 TimeMsc 必须保留，不能取整，不能用 Time 回退。
- 缺失或无效 TimeMsc：`DISCARDED / INVALID_DEAL_TIME_MSC`。这类记录在任务时间范围内的归属及 Total 计数口径待 Q12；不得悄悄使用秒级 Time 决定精确成交时点。

### 2.2 MT4

**CONFIRMED — D06、D08–D09，§11、13–14、89。**

| 原始 Order | Event | 规范化 Side | Event Price | Event Time |
|---|---|---|---|---|
| BUY | OPEN | BUY | OpenPrice | OpenTime |
| BUY | CLOSE | SELL | ClosePrice | CloseTime |
| SELL | OPEN | SELL | OpenPrice | OpenTime |
| SELL | CLOSE | BUY | ClosePrice | CloseTime |

OPEN 与 CLOSE 分别判断是否落在查询范围；只分析其中入围的事件。仍未平仓的订单不能生成虚构 CLOSE。已平仓订单两端均入围时形成两个独立 Event，不按 Ticket 合并计算。

`source_time_precision = SECOND`。`10:00:00` 在计算表示中为 `10:00:00.000`，补零不是获得真实毫秒。100ms/500ms 不支持，不能伪造时间或用报价精度补足成交精度。

不重建持仓生命周期，也不计算持仓时间。部分平仓、close-by、历史记录链和订单量是否代表各真实成交量仍待 Q10/A05，不能用“一个 Ticket 的 Lots 必然同时适合 OPEN/CLOSE”作默认假设。实际成交量未能证明时，不伪造数据，也不自行定义新的丢弃口径。

### 2.3 规范化输入契约

这是 **PROPOSED LOGICAL CONTRACT**，不选择语言、框架、数据表或接口实现。

| 业务字段 | MT5 来源/含义 | MT4 来源/含义 |
|---|---|---|
| Platform / Profile / Login | 本次版本来源上下文及真实账户 | 同左 |
| Source ID | Deal ID | Ticket；Event Type 参与身份区分 |
| Order ID / Position ID | 原始字段可得则保留 | Ticket 及真实可得的来源关系；不猜 Position |
| Event Type | DEAL | OPEN / CLOSE |
| Entry Type | IN / OUT / INOUT / OUT_BY 原值 | 不适用，不伪造 MT5 Entry |
| Symbol | 完整原始 Symbol | 同左 |
| Side | Deal 实际 BUY/SELL | 按上表转换 |
| Event Volume | 当前 Deal 实际成交量，规范化为明确的 lot 单位 | 当前 OPEN/CLOSE 实际成交量，映射待 Q10 |
| Price / Event Time | Deal Price / TimeMsc | 对应开/平价格和秒级时间 |
| Time Precision | MILLISECOND | SECOND |
| Raw Trade | 保存核心原始字段和原始量纲 | 同左 |

本地 MT5 SDK 文档表明 Volume 单位为 1/10,000 lot，VolumeExt 单位为 1/100,000,000 lot。Adapter 应保存使用了哪个源字段及单位映射，不同时累加两种表示，也不能把原始整数当 lots。部署 SDK 的可用性、权威字段及分页行为仍须验证（A06/A07、Q24）。MT4 volume 编码及部分平仓含义须以所用 SDK 契约确认。

原始 Profit、Commission、Swap、Comment、Magic、Reason 为核查数据；Profit/Commission/Swap 不参与 Markout（D20、§81）。SDK 字段映射位于 Adapter，不进入统一公式层。

## 3. 时间和固定窗口

**CONFIRMED — D05、D09–D12，§10、14–18。**

| 平台 | 支持的固定 Window | 不支持 |
|---|---|---|
| MT5 | 100ms、500ms、1s、3s、5s、10s、30s、60s、180s | 不允许用户添加其他窗口 |
| MT4 | 1s、3s、5s、10s、30s、60s、180s | 100ms、500ms：UNSUPPORTED_TIME_PRECISION |

对于 Event e 和支持的 Window w：

**Target(e,w) = EventTime(e) + Duration(w)**

MT5 Event=`10:00:00.357` 时，100ms Target=`10:00:00.457`，500ms=`10:00:00.857`，1s=`10:00:01.357`。MT4 Event=`10:00:00.000` 时，1s=`10:00:01.000`，3s=`10:00:03.000`。

所有业务展示与匹配使用该交易服务器时间语义，不能依赖 Windows Local Time、猜 offset 或自动校正。Manager 与 Terminal 原始时间编码不一定相同：本地 MT5 Manager 分页请求为服务器时区的秒参数，Terminal 接口可能采用 UTC。只有经证据确认的时间语义映射才能使用（Q02/A02）；不能因为显示相同格式就宣称同一时间轴。

**PROPOSED 技术要求：**对仅支持秒级查询条件的 MT5 Manager 接口，以覆盖用户范围的查询包络获取候选，再依据有效 TimeMsc 精筛。包络不改变业务端点规则；具体端点先由 Q06 决定。缺失 TimeMsc 记录单独保留来源诊断，不能靠精筛将其静默消失（Q12）。

## 4. Quote Retrieval、Matching 与有效性

### 4.1 冻结匹配规则

**CONFIRMED — D12–D15，§18–23。**

**Quote(Target) = latest valid Tick where TickTime ≤ Target。**

每个支持窗口必须同时存在有效 Bid 和 Ask；BUY 不能只验证 Bid，SELL 不能只验证 Ask。两侧任一不满足获批有效性规则，均不能把该窗口当作报价完整（§20–21，Q04/Q05）。

禁止使用 Target 之后的 Tick、nearest Tick、插值、估算、另一个窗口的结果或其他平台/Profile 的 Tick 作为替代。允许同一条真实 Tick 自身满足多个窗口的匹配规则。

`Quote Age = Target Time − Quote Time`，应为非负。V1 仅保留和展示 Age，不设置 stale 阈值、不标记“太旧”、不由 Age 自动丢弃。规则没有强制 Tick 晚于 Event；存在更早有效报价时，不能私自要求 `TickTime ≥ EventTime`。无 T0 Quote 不影响完整性（D44、§88）。

### 4.2 未决语义：必须在实现前解决

**Q04：**“latest valid”要求跳过无效 Tick，§20 同时要求无效 Bid/Ask 产生 `INVALID_QUOTE`。最新候选无效但更早候选有效时，是否继续回溯，以及无效尝试如何留痕，需求尚未完全定义。本文不暗中改成“最新任意 Tick”，也不假定无效 Tick 总可忽略。

**Q05：**有效 Bid/Ask 的完整校验条件仍未冻结，包括有限数值、零值、负值、倒挂与特殊市场条件。Event Price、Point、Volume 异常的业务处置同样未确认。不得通过任意阈值删极值。

**Q07：**同一时间戳多条 Tick 的稳定顺序、以及证明“之前没有可用 Tick”的回溯覆盖条件需要确定。只在本地搜索的有限小区间未找到，不等于整个可用历史不存在。若接口无稳定 tie-break，必须暴露技术限制，不能凭容器枚举顺序决定结果。

**Q22：**尚未到达的 Target、历史保留期外、未同步完成及有限重试耗尽的含义需要分清；不能一概记成 NO_QUOTE。

### 4.3 建议处理流程

下述为 **PROPOSED 控制流程**；Q04/Q05/Q07/Q22 未关闭时对应分支保持未决。

1. 验证同 Platform/Profile、完整 Symbol、时间语义和 Quote Access 前提。
2. 获取所需时间覆盖及必要前序报价候选；保留查询覆盖/完成证据。环境或查询异常进入相应诊断层，不返回伪造空 Tick 集合。
3. 排除 Target 之后的 Tick，按经过确认的稳定时间顺序处理；按 Q04 最终决定应用“有效性与回溯”的组合规则。
4. 成功匹配时保存窗口到 Matched Quote 的关联，计算 Mid 和 Quote Age。相同 Tick 可被多个窗口引用，但各自 Target/Age 独立。
5. 在已确认搜索覆盖下无 Tick 才进入 `NO_QUOTE`；存在报价但不能满足有效性时，按获批 Q04/Q05 规则进入 `INVALID_QUOTE`。
6. 保存所有窗口结果和原因，再判 Event 完整性。缺失结果为缺失值，不能以 0 代替。

长期保存实际使用的 Matched Tick 和窗口证据；不保存完整 Tick Stream（D37）。对失败窗口可保留必要的有界错误证据，如最后候选时间和失败字段，不将诊断扩展成全历史 Tick 仓库。

## 5. 主指标、Mid、Points 和方向

**CONFIRMED — D16–D18，§24–27。**

记 Event 成交价格为 P，匹配报价为 Bid/Ask，Terminal Symbol Snapshot 的 Point 为 U：

| 指标 | BUY | SELL |
|---|---|---|
| Main Price Markout | Bid − P | P − Ask |
| Future Mid | (Bid + Ask) / 2 | (Bid + Ask) / 2 |
| Mid Markout | Mid − P | P − Mid |
| Points Markout | (Bid − P) / U | (P − Ask) / U |

主 Markout >0 表示客户有利，<0 表示客户不利；这是成交之后的理论价格移动，不是账户风险结论。BUY 使用 Bid、SELL 使用 Ask，不能为使结果“更好看”而改用 Mid。Mid Markout 当前定义为价格差；需求没有冻结 Mid Points 或 Mid Money，不额外增加这些指标。

Point 必须来自对应 Terminal 的 Symbol Specification；不能从 Digits 猜 Point，不能假设 1 Pip=10 Points。Point 缺失、零或无效不能执行除法，后续状态/资格由 Q05/Q09 确认，不能输出 infinity 或 0 冒充有效结果。

Price、Mid、Quote Time、Age 和完整 Trace 进入 Detail、Diagnostics、Single Trade Test、Full Export。主 Summary 不展示 Price/Mid，不将 Money/Mid/Price 叠加到 Points 曲线（D17、D25）。

## 6. Money Markout 边界

### 6.1 已确认要求

**CONFIRMED — D19–D21，§28–29、42、49、81、96–97。**

- 单位为 USD；当前只支持 USD Trading Account，不进行 Currency Conversion。
- 使用实际 Event Volume、Side、成交 Price 及主指标对应的 Future Bid/Ask。
- 优先使用对应 Terminal 或 SDK 的 Profit Calculation 能力；只有无法直接调用时才按已验证的 Calculation Mode 与完整 Specification 严格计算。
- 禁止所有 Symbol 一律套用 `Price Difference × Lots × Contract Size`；也不能把真实 Deal Profit 当成 Money Markout。
- 不含 Commission、Swap、Rebate、Agent Commission、Trading Fee、Bonus。
- 每 Analysis Version 固定所用规格、规则和结果；后来 Symbol Specification 变化不能改写历史。

### 6.2 必须解决的可靠性边界

本地 MT5 TradeProfit/TradeProfitExt 文档明确提到按 group 的当前市场价格完成 Profit Currency→Deposit Currency 换算。**USD 账户不等于计算没有换汇，也不等于历史金额可精确重建。**优先调用原生方法不是忽略这一冲突的许可（Q03）。MT4 TradeCalcProfit 在现有材料中只有接口声明，不足以证明完整语义或无副作用（A09）。

尚须确定：非 USD 账户处置（Q08）；原生 Money 不可调用、未知模式、缺规格、失败返回值时 Event 资格（Q09）；规格快照是分析当时可得规格还是存在历史规格，以及 Account Group/Terminal 账户条件差异（Q13）。当前不得宣布任一 SDK 路径已通过真实计算验证。

不能以金额缺失=0、复制其他窗口金额、通用 Contract Size 公式或隐含当前汇率来掩盖失败。若所需历史环境无法复现，应说明“已保存原生返回值与输入可核查”与“同一经济条件下可独立重算”的区别；前者不能冒充后者（A13）。

### 6.3 Money Calculation Trace 逻辑契约

**PROPOSED** 每次 Money 输出关联以下非敏感证据；原生 API 没有暴露的输入应明确标记未知，不伪造：

| 分类 | 需记录的内容 |
|---|---|
| 方法 | MoneyMethod、原生能力/入口标识及版本，或明确的 CalcMode 公式分支及规则版本 |
| 输入 | 规范化 Side、Volume lots、原始 Volume 字段/单位、成交 Price、所用未来 Bid/Ask、Symbol |
| 账户条件 | 分析 Login、账户币种、Account Group/计算 group 的非敏感标识，Terminal 计算账户条件；两者一致性检查结果 |
| 规格 | Spec Snapshot ID、来源、获取时点、Point、Tick Size/Value、Contract Size、Profit Currency、CalcMode 及该模式实际所需字段 |
| 环境 | Platform、Profile Identity、Server Identity、调用/分析时点、Server Time Context、API/SDK/Application 版本可得值 |
| 原生结果 | 输入参数的业务含义、返回状态、返回值、输出币种、原生 API 是否隐含当前市场数据；已知的转换输入及来源 |
| 可复核性 | 保存结果及证据；必要信息未公开时标出复算限制，不重新联网替换旧值 |

不存密码、token 或连接秘密。Trace 可展开每个窗口的 BUY/SELL 算式及 Points 分母；普通 Summary 不显示上述技术字段。

## 7. 完整性、丢弃和计数资格

**CONFIRMED — D09、D14、D29，§14、21–22、40、72、83。**

| 情形 | 已确认处理 |
|---|---|
| MT5 任一支持窗口 NO_QUOTE / INVALID_QUOTE | 整 Deal DISCARDED，不进入任意窗口 Summary |
| MT4 任一七个支持窗口 NO_QUOTE / INVALID_QUOTE | 整 Event DISCARDED，不进入 Summary |
| MT4 100ms / 500ms | UNSUPPORTED_TIME_PRECISION；不计算、不计完整性，不导致丢弃 |
| MT5 无有效 TimeMsc | DISCARDED / INVALID_DEAL_TIME_MSC；不能回退秒级时间 |
| 完整 Symbol 不存在 | SYMBOL_NOT_FOUND；保留事件来源和诊断，不做近似匹配 |
| 无效 Login | ACCOUNT_NOT_FOUND；Account 级记录，继续其他有效账户；不能虚构 Deal |
| Manager/Terminal 环境故障 | Task/Connection 层处理；不是 NO_QUOTE |
| 任一 Money 路径不可用 | Q09；不能擅自选择丢弃、部分有效或填零 |

全部支持窗口报价完整是进入聚合的**必要条件**。由于 Q05/Q09 尚未确认，不能把“报价完整”直接解释为在所有异常输入场景下均已获准 VALID。

不完整 Event 仍保存于版本结果/Diagnostics。其已成功窗口可提供真实已获得的计算证据，但不因此部分进入正式聚合。DISCARDED 保留全部失败窗口、Window Reason、Target 和可得报价证据；不要只保存一个缺失窗口。

`INCOMPLETE_QUOTES` 可用作 Event 级原因，`NO_QUOTE`/`INVALID_QUOTE` 为相应 Window 原因；完整主因优先级属于 State/Error Model 建议，不能丢掉多原因事实。诊断界面应区分 unique Event count 与 Window count；多个原因的计数相加可能重复，不能当作 Discarded Events 总数。

## 8. Summary 公式与集合边界

**CONFIRMED — D22–D28，§30–39、82–86。**

对固定 Version、Login、完整 Symbol 和已确认过滤范围，令 E 为其中所有 VALID Events，N=|E|。同一个 E 用于该平台的所有支持窗口；不能让各窗口因局部有效而使用不同 Event 集合。对 Event e，v(e) 为实际 Event Volume，p(e,w) 为 Points，m(e,w) 为 USD Money。

| Symbol 指标 | 每个支持窗口 w 的公式 |
|---|---|
| Valid Event Count | N |
| Volume | Σ[e∈E] v(e) |
| Avg Points Markout | Σ[e∈E] p(e,w) / N |
| Volume Weighted Avg Points Markout | Σ[e∈E] p(e,w)×v(e) / Σ[e∈E] v(e) |
| Total Money Markout USD | Σ[e∈E] m(e,w) |
| Avg Money Markout USD | Σ[e∈E] m(e,w) / N |

Money 已按各 Event Volume 计算，金额聚合时不能再乘一次 Volume。VW Points 的权重是规范化实际成交量，不是金额、订单数或绝对 Markout。MT4 开平两个 Event 均入围时会各自贡献 Volume，这是 Event Volume，不是唯一持仓量或订单去重后的 lots。

Account 跨 Symbol 主要聚合 Money，每 Login 独立；不将跨 Symbol Points 均值或跨 Login Money 总额新增成产品核心指标。Multi-account Overview 每行一个账户，不产生合并客户 Summary。

有效账户的数据质量包含 Total Events、Analyzed Events、Discarded Events 及 `Analysis Rate = Analyzed Events / Total Events × 100%`。DISCARDED 进入 Total/Discarded/Diagnostics，不进入 Markout 均值/金额。非市场记录本来不形成 Event；无效账户没有可查询事件时不能臆造 Total=0。无效 TimeMsc 归属见 Q12；分母、筛选前后计数口径及空集合见 Q11。

N=0 时平均值没有可定义的除法结果；Volume=0 时 VW 也不能除零。**PROPOSED 表现**为 `—` 并显示“无有效事件”或“分母为 0”，不绘制虚假零曲线；空集合 Total Money 是否显示 0、Analysis Rate 分母为 0 的表达、零量 Event 是否可保留须由 Q05/Q11 决定。不得将此建议当作已冻结的业务口径。

Direction 支持 ALL/BUY/SELL，Entry/Event 支持需求指定集合；不建立 BUY×IN 等复杂交叉统计矩阵。Summary 是否允许同时叠加 Side 和 Entry 筛选尚待 Q16；Deal 表已有字段筛选能力不能自动推导出新 Summary 业务口径。

Symbol Ranking 默认 1s、Total Money USD 降序，关键窗口符合 D24；Symbol Curve 为全支持窗口 VW Avg Points 单曲线，Deal Curve 为全支持窗口 Points 单曲线。不得因窗口长度不均匀而擅自插入新的数值采样点。

## 9. 合成示例的共同前提

以下均标记 **SYNTHETIC DESIGN FIXTURE**：不是生产交易、不是 API 调用、不是测试通过报告。它们只演示冻结公式及在明确条件下的期望值；不决定 Qxx。

除专门错误例外外，假定：事件确实在最终确认的查询范围内；Platform/Profile/Server/Symbol 完全一致；账户 USD；价格/报价/Point/Volume 满足最终获批校验；历史获取已完整成功；时间轴一致；无环境故障；Money 方法和全部资格条件均满足。所有称为 VALID 的例子都覆盖该平台全部支持窗口。

**仅本节使用的条件 Money fixture M：**假设某合成 Symbol 的已验证计算模式恰好支持下式，Profit Currency 和 Deposit Currency 都为 USD，Volume 以 lot 表示，Tick Size=0.01，Tick Value=10 USD/lot，且该例无币种转换、费用或额外模式参数：

**Money = Main Price Markout / 0.01 × 10 USD/lot × Event Volume(lot)。**

这是条件测试 oracle，不是一个已选定真实 SDK 枚举或已验证真实品种，也不是所有 Symbol 的通式。生产方法必须重新通过 §6 的能力/模式/输入验证。

### 9.1 MT5 BUY、全部九窗与同一 Tick 复用

Event：合成 Deal D-BUY-1，BUY，IN，Volume=1 lot，Price=4000.20，TimeMsc 表示服务器时间 `2026-10-01 10:00:00.357`。Point=0.01。

假设该 Symbol 在 `10:00:00.400` 有有效 Tick：Bid=4000.50，Ask=4000.52；其后直到最后 Target 无新 Tick，历史获取已完成。全部九窗各自根据同一匹配规则选择此 Tick：

| Window | Target Time | Quote Time | Quote Age(ms) | Main ΔPrice | Points | Money（仅 fixture M） |
|---|---|---|---:|---:|---:|---:|
| 100ms | 10:00:00.457 | 10:00:00.400 | 57 | +0.30 | +30 | +300.00 USD |
| 500ms | 10:00:00.857 | 10:00:00.400 | 457 | +0.30 | +30 | +300.00 USD |
| 1s | 10:00:01.357 | 10:00:00.400 | 957 | +0.30 | +30 | +300.00 USD |
| 3s | 10:00:03.357 | 10:00:00.400 | 2957 | +0.30 | +30 | +300.00 USD |
| 5s | 10:00:05.357 | 10:00:00.400 | 4957 | +0.30 | +30 | +300.00 USD |
| 10s | 10:00:10.357 | 10:00:00.400 | 9957 | +0.30 | +30 | +300.00 USD |
| 30s | 10:00:30.357 | 10:00:00.400 | 29957 | +0.30 | +30 | +300.00 USD |
| 60s | 10:01:00.357 | 10:00:00.400 | 59957 | +0.30 | +30 | +300.00 USD |
| 180s | 10:03:00.357 | 10:00:00.400 | 179957 | +0.30 | +30 | +300.00 USD |

每窗 Mid=4000.51，Mid Markout=+0.31。全部支持窗完整，在共同前提下 Event VALID。较大 Quote Age 不自动产生 stale 或丢弃。

该例 100ms 的 Trace：`4000.50−4000.20=+0.30`；`+0.30/0.01=+30 points`；仅在 fixture M 条件下 `+0.30/0.01×10×1=+300.00 USD`。只知道差价 0.30 和 Point 0.01 时，**不能**推出 300 USD。

### 9.2 MT5 SELL 与符号方向

独立合成 Deal D-SELL-1，SELL，OUT，Volume=1 lot，Price=4000.50，Event=`2026-10-01 12:00:00.357`。Point=0.01；唯一后续有效 Tick=`12:00:00.400`，Bid=4000.18，Ask=4000.20，其后到 +180s Target 无更新。

全部九窗的 Target/Quote Age 与 9.1 对应（小时改为 12）。每窗：SELL Main=`4000.50−4000.20=+0.30`；Points=+30；Mid=4000.19；Mid Markout=+0.31；fixture M Money=+300.00 USD。九窗齐全，满足共同前提时 VALID。Entry=OUT 不改变使用实际 Deal Side 的规则。

对照合成 BUY 价格 4000.20、未来 Bid=3999.90 时，Main=−0.30、Points=−30；fixture M 且 Volume=1 时 Money=−300.00 USD。负号是客户不利的方向，不构成账户风险评级。

### 9.3 MT4 OPEN BUY / CLOSE SELL 与 OPEN SELL / CLOSE BUY

两个合成、普通全量开平的订单，每个对应 Event Volume=1 lot；这些例子不替代部分平仓验证。所有原始成交时刻只有秒精度。

| Ticket | 原 Order | Event | Event Time（SECOND） | 规范化 Side / Price | 此 Event 的匹配 Tick Time | Bid / Ask |
|---|---|---|---|---|---|---|
| T4-B | BUY | OPEN | 10:00:00.000 | BUY / 100.00 | 10:00:01.000 | 100.10 / 100.12 |
| T4-B | BUY | CLOSE | 11:00:00.000 | SELL / 101.00 | 11:00:01.000 | 100.88 / 100.90 |
| T4-S | SELL | OPEN | 12:00:00.000 | SELL / 101.00 | 12:00:01.000 | 100.88 / 100.90 |
| T4-S | SELL | CLOSE | 13:00:00.000 | BUY / 100.00 | 13:00:01.000 | 100.10 / 100.12 |

同一天，每个列示 Tick 之后直到该 Event 的 +180s 无更新；各段不会互相覆盖。Point=0.01。四个 Event 分别具有下列完整窗口结果：

全部报价均为合成数据；不证明 MT4 Terminal 的真实历史 Tick 能力或报价时间精度（Q01仍未验证）。本例用整秒 Tick 同时检验 TickTime=Target 时允许匹配。

| Window | Target 相对秒级 Event | Quote Age(ms) | 四个 Event 各自的主 ΔPrice / Points | fixture M Money（各自） |
|---|---|---:|---|---:|
| 1s | +1.000s | 0 | +0.10 / +10 | +100.00 USD |
| 3s | +3.000s | 2000 | +0.10 / +10 | +100.00 USD |
| 5s | +5.000s | 4000 | +0.10 / +10 | +100.00 USD |
| 10s | +10.000s | 9000 | +0.10 / +10 | +100.00 USD |
| 30s | +30.000s | 29000 | +0.10 / +10 | +100.00 USD |
| 60s | +60.000s | 59000 | +0.10 / +10 | +100.00 USD |
| 180s | +180.000s | 179000 | +0.10 / +10 | +100.00 USD |

四个 Event 的 Mid Markout 均为 +0.11。100ms/500ms 的结果为 `UNSUPPORTED_TIME_PRECISION`，无计算值，不是 0、NO_QUOTE 或插值值；不妨碍七窗完整 Event VALID。如果查询只覆盖 T4-B 的 CLOSE 时点，则只纳入 CLOSE，不把此前 OPEN 补入任务。

### 9.4 NO_QUOTE 与禁止未来 Tick

合成 MT5 BUY Event=`10:00:00.357`，100ms Target=`10:00:00.457`。假定成功、完整的获批回溯覆盖证明此前不存在 Tick；第一条有效 Tick 时间=`10:00:00.500`，之后至最后 Target 无更新。

100ms 为 NO_QUOTE；不得因为 `.500` 距离近而选它。500ms 及以后八窗可以按规则匹配 `.500`，仍不能救回整个 Event。Event DISCARDED / INCOMPLETE_QUOTES，所有窗口明细保留，不进入任何 Summary。如果失败其实来自 Terminal 断线或查询未完成，本例前提不成立，不能沿用此 NO_QUOTE 结果。

### 9.5 INVALID_QUOTE 与尚未确认的回溯

合成 MT5 Event 的某窗口已找到不晚于 Target 的 Tick，但该条记录没有可用 Bid，仅有 Ask=4000.52，无法满足双边有效报价要求；其他八窗具备双边有效 Tick。

本例验证的是：**当获批的 Q04/Q05 匹配与校验规则把该窗口判为 INVALID_QUOTE 时**，整 Event DISCARDED，失败窗口和原始缺字段证据保留，不能用 Mid、Ask 当 Bid 或别窗数值修补。不将本例冒充 Q04 已决定的判定算法。

补充待决向量：Target 前最新 Tick 无效、更早 Tick 有效。应否回溯到更早 Tick、无效候选如何记录，必须先由 Q04 决定，再锁定期望状态。现在不能为同一向量写两个互相冲突的“已通过”结果。

### 9.6 同 Order 多 Deal / Partial Fill 与聚合

两个合成 BUY Deal 同属 Order O-PARTIAL，均 IN、同 Login/Symbol。Point=0.01。共同有效 Tick=`10:00:00.450`，Bid=100.30、Ask=100.32；其后直到较晚 Event 的 +180s Target 无新 Tick。

| Deal | TimeMsc 对应 Server Time | Volume(lot) | Price | 所有九窗 Points | 所有九窗 Money（仅 fixture M） |
|---|---|---:|---:|---:|---:|
| D-P1 | 10:00:00.357 | 1 | 100.20 | +10 | +100.00 USD |
| D-P2 | 10:00:00.400 | 3 | 100.00 | +30 | +900.00 USD |

D-P1 的最早 Target 为 `.457`，D-P2 为 `.500`，均不早于共同 Tick `.450`；其余八窗同样可匹配该 Tick，因此两 Deal 各自九窗完整。它们不因 Order 相同而合并，INOUT 若出现在另一个真实 Deal 也不拆分。

每个支持窗口：Count=2；Volume=4 lots；Avg Points=`(10+30)/2=20`；VW Avg Points=`(10×1+30×3)/4=25`；Total Money=`100+900=1000 USD`；Avg Money=`1000/2=500 USD`。不能再用成交量二次加权 Money Sum，也不能把两 Deal 合成一笔均价后重算取代原始结果。

## 10. 历史复核、数值表示与审查出口

**CONFIRMED — D21、D30–D31、D37、D41，§41–49、57、64、96–99。**

历史版本读取保存的 Event、各 Window、Matched Quote、Spec Snapshot、Rule Version、Money Trace 和 Environment Metadata，不自动重新获取 Tick。Re-analyze 使用新 Version，不覆盖旧计算值。Single Trade Test 与正式分析共用同一规则引擎，但其结果不进入正式 Summary。

每窗口证据至少包括 Target Time、Quote Time、Quote Age、Bid、Ask、Mid、主 Price/Points/Money、Mid Price Markout、状态与原因。原始 Event 数据、时间精度、源 ID、实际 Volume、所用 Point 和 MoneyMethod 必须可追溯。一个窗口的数据不会因 UI 只显示关键窗口而从 Full Export 消失。

**PROPOSED 数值原则（Q25）：**计算与展示分离；保留源数据精度、单位和运算依据，不把屏幕舍入值反馈给聚合；排序基于实际数值；未知值保持缺失，不转成零；64-bit ID 按完整标识保存和展示，不经过会损失整数精度的表示。具体数值类型、舍入位数/模式、USD 显示精度及极小正负值显示等待技术评审，不在本阶段选择运行库或实现。

在 Q01–Q13、Q22、Q24–Q25 所涉及的适用能力/规则得到明确结论前，不能把本文视作可直接上线的完整算法契约。确定规则可以进入 Architecture Review；未决分支必须显式保留，禁止实施时自行补齐。
