# Assumption Register

状态：DRAFT FOR REVIEW。以下均为 **UNVERIFIED**；不是用户已经决定的业务规则，也不授权本轮接入/运行 API。

| ID | 待验证假设 | 当前证据与限制 | 验证责任 / 后续证据 | 失败影响 / 关联问题 |
|---|---|---|---|---|
| A01 | 两平台可通过指定 Terminal 取得所需历史双边 Tick | MT5 官方有 CopyTicksRange；MT4 未发现等价公开接口。Manager Tick 能力不等于 Terminal 能力 | 技术负责人，以实际版本、授权接口文档及受控历史样本证明；用户批准下一阶段后进行 | MT4 V1 能力阻断；Q01 |
| A02 | Manager Event 与 Terminal Tick 可在同一 Server Time 时间线上比较 | MT5 Manager 本地文档用 Server timezone；Terminal Python 官方接口要求 UTC；没有已证明历史 DST 映射 | 技术负责人，用已知服务器事件、原始编码、版本文档和 DST 边界证据验证；不靠 Windows 当前时钟猜测 | 全部匹配可错位；Q02 |
| A03 | SDK/Terminal 访问权限、历史范围和保留期满足用户查询 | 未连接服务，无权限/保留期实测 | 用户确认授权环境；技术负责人以请求完成度和覆盖证据核实 | 无法把请求失败或未覆盖区间当 NO_QUOTE；Q07/Q22 |
| A04 | 账户币种、group 和 Terminal 合约条件可一致解释 | USD 账户不等于 Profit Currency USD；本地 MT5 TradeProfit 说明采用当前市场转换 | 用户决定 Money 边界；技术负责人核实参数、币种和实际计算路径 | 无法证明币种、账户条件及计价时点符合确认口径；Q03/Q08/Q13 |
| A05 | MT4 订单记录足以表达范围内 OPEN/CLOSE 的真实成交量和身份 | 本地字段和历史查询存在，但部分平仓语义与范围查询覆盖尚未验证 | SDK 负责人提供 open-in-range/close-later、跨界、部分平仓、close-by 样本；不进行生命周期重建 | 漏 Event / 重复 Volume；Q10 |
| A06 | MT5 TimeMsc 有效且 SDK volume 编码可无损规范化 | 本地 TimeMsc 与 Volume/VolumeExt 有文档；部署版本未核验 | 绑定准确 SDK build；对真实只读样本检查 ms 和 1e4/1e8 lot 单位；无效 TimeMsc 不回退 | 精度、分母和资格错误；Q12/Q24 |
| A07 | 历史查询分页在一次运行中能实现可追溯、无重复的读取 | MT5 有 DealRequestPage；未证明运行期间历史更正和 offset 稳定性 | 验证固定范围重复页/边界页/历史更正行为；保留来源和抓取上下文 | 漏/重 Event；不能宣称数据库级远端快照；Q24 |
| A08 | 获得的 Spec 足以驱动选定 calc mode 且可解释其时点 | Manager ConSymbol/MT5 Symbol 字段存在，需求要求 Terminal Spec；不能静默代替 | 验证 Terminal 可读字段、group/account 条件、规格时点与 Money 所需参数 | Points/Money 路径受阻；Q03/Q13 |
| A09 | SDK 函数适合只读收益计算且可记录完整输入 | MT4 TradeCalcProfit 原型存在、语义未明；MT5 TradeProfit 明示当前转换；未执行 | 阅读所用版本契约、核实副作用/币种/模式后再审批测试 | 不能用真实 Profit 或通式凑 Money；Q03/Q09 |
| A10 | 所选 SDK 的位数、ABI、生命周期、线程/取消支持可满足桌面集成 | 本地 DLL/.NET/C++ 样例存在；MT4 为 C++ 虚接口及 factory 导出，并非完整平面 C ABI；存在不等于已在目标 Windows 工作 | Stack Review 核实 binding/bridge、DLL、运行库/位数、对象释放、阻塞超时 | 可能需极小进程隔离；不预先引入服务；Q24 |
| A11 | 合适的系统/SDK 凭据保护机制可用于单用户桌面 | 未读取任何真实凭据；没有已确认 SDK 安全凭据接口 | Windows Credential Manager/DPAPI 或经证明 SDK 方案对比，明确恢复与撤销策略 | 配置可保存但不能明文保存秘密；Q20 |
| A12 | SQLite + 分块计算足够真实单机数据量 | 未提供最大账户/事件数、磁盘或运行时基准 | 用户给规模与机器边界；后续合成和授权数据测量吞吐/内存/取消响应 | 仅能给有界内存架构，不能承诺无限资源或 SLA |
| A13 | 原始值、计算方法与快照能支持离线复核 | 原生计算器内部模型及当前行情输入未必全部公开 | 对每 MoneyMethod 指明“可重算”还是“原生结果及输入可核查”；不夸大黑盒复现 | Q03/Q13；已保存值能展示 ≠ 历史经济环境可重建 |
| A14 | 风控用户接受建议的密度、页面层次和快捷键 | 只有需求、无观察研究/可用性实测 | 用户 Review 后，获准再用合成案例做键鼠、DPI、任务恢复评估 | UX PROPOSED；不能声称已降低几成操作时间 |
| A15 | 后续正常 API 使用可以接受 Terminal 自身历史同步/缓存 | 官方 MT5 CopyTicks 会同步 Terminal 自有数据库；本阶段没调用，未修改 Terminal | 后续接入设计披露副作用；只有用户把禁止修改扩展到运行时自有缓存，才需额外决定 | 当前不是已发生冲突；若未来要求完全禁止缓存写入需重评；Q23 |
| A16 | 正式完成前可通过一次本地提交发布完整结果 | SQLite 提供事务机制；尚无实现与故障注入 | 后续验证磁盘满、提交失败、Cancel/Commit 竞争和重启恢复 | 当前仅 PROPOSED TECHNICAL CONTRACT，不算已测通过 |

## 处置规则

1. 不能把假设失败改写为数据正常为空。
2. 用户业务决定进入 Decision Register；SDK 能力用版本对应文档和后续授权测试证据关闭。
3. 未通过的关键能力应阻止对应平台/计算路径发布；若希望缩减 V1 范围，须用户重新决定。
4. 本轮只做静态规则、文档和本地 SDK 材料核查。任何后续测试均需进入单独获准阶段，不在本轮启动。
