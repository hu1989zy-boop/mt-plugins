# V1 Acceptance Criteria — Given / When / Then

状态：DRAFT FOR REVIEW。**这是后续验收契约，不是本轮已执行的产品测试。** 所有数据均应使用明确标注的合成 fixture 或后续单独授权的测试环境；本轮没有连接 Manager/Terminal、创建数据库或实现 UI。

标记：CONFIRMED 表示由冻结需求导出；PROPOSED 表示技术/UX契约待Review；BLOCKED(Qxx) 表示须先获决定/能力证据，不能编造预期结果。算术fixture详见[03](03-markout-calculation-spec.md)。

## 1. 来源、输入与环境

| ID / 依据 | Given | When | Then / 状态 |
|---|---|---|---|
| AC01 D02–D03 | Profile选择MT4 Manager与MT5 Terminal，或反向 | 提交预检 | 阻止计算，不替换为其它Profile/平台；提示不匹配并提供诊断。CONFIRMED |
| AC02 D03 | Manager断连、Terminal未启动/未连接、Server不符、Quote无权五种独立fixture | 分别预检或运行中持续失败 | 分别保留对应环境原因；不把事件批量标NO_QUOTE；未通过预检不开始计算。CONFIRMED |
| AC03 D04 | 粘贴重复Login、不同分隔符、一个不存在Login和一个有效Login | 校验并开始 | 去重；无效账户留ACCOUNT_NOT_FOUND；有效账户继续；无Excel/CSV账户导入或Symbol选择。CONFIRMED |
| AC04 D05–D06 | 成交与Tick已在经证实的Server Time时间线；包含范围内、外和端点数据 | 提取候选并筛选 | 非端点严格按事件时间；不靠Windows时钟猜偏移；端点预期需Q06、时间桥接需Q02。CONFIRMED / BLOCKED |
| AC05 D07/D44 | MT5 BUY/SELL及Balance/Credit等，来源包含Manual/EA/SL/TP/StopOut/Manager/Pending | 规范化 | 仅真实BUY/SELL形成Event，不按来源或Reason/Comment/Magic删事件。CONFIRMED |
| AC06 D10 | MT5记录有有效毫秒、缺失或无效TimeMsc，秒级Time都存在 | 规范化 | 有效值不取整；无效记录INVALID_DEAL_TIME_MSC且不回退Time；范围归属/Total待Q12。CONFIRMED / BLOCKED |
| AC07 D15 | 记录Symbol为完整后缀名，Terminal只有近似无后缀Symbol | 取Spec/Quote | SYMBOL_NOT_FOUND；不做模糊映射、裁剪或别的平台替代。CONFIRMED |
| AC08 D19 | 含非USD账户、USD账户但profit currency不同 | 校验Money能力 | 不执行未获准换汇，不将非USD伪装不存在；处置依Q03/Q08。BLOCKED |

## 2. 事件、窗口与报价

| ID / 依据 | Given | When | Then / 状态 |
|---|---|---|---|
| AC09 D08 | MT4 BUY Order，其OPEN/CLOSE都在范围，真实量已验证 | 规范化 | 两个Event：OPEN BUY用OpenPrice/Time；CLOSE SELL用ClosePrice/Time，分别计数。CONFIRMED |
| AC10 D08 | MT4 SELL Order，两端均在范围 | 规范化 | OPEN SELL、CLOSE BUY；不能沿用订单原方向计算CLOSE。CONFIRMED |
| AC11 D06 | MT4订单9月20日开、10月5日平，查询10月1–10日 | 提取Event | 只有CLOSE入围；不补全生命周期。另一fixture范围内开、以后才平须证明Manager读取覆盖（Q10）。CONFIRMED / BLOCKED |
| AC12 D09–D11 | MT4 Event原始秒时间 | 生成窗口 | 七窗1/3/5/10/30/60/180s；.000只为计算表示；100/500ms为UNSUPPORTED_TIME_PRECISION，不计失败/丢弃。CONFIRMED |
| AC13 D10–D12 | MT5 Event 10:00:00.357 | 生成目标 | 九窗；100ms=.457，500ms=.857，1s=01.357，余窗按精确加法；保留MILLISECOND。CONFIRMED |
| AC14 D12 | Target=.457，候选tick .440/.450/.460，其中≤Target候选有效 | 匹配 | 选择.450，不选未来.460或nearest；QuoteAge=7ms。CONFIRMED |
| AC15 D12–D13/D44 | 已证明同一旧有效Tick为所有Target前最新tick，甚至早于Event，无T0 | 匹配全部窗口 | 可复用；每窗Target/Age独立，不设stale阈值，不因无T0丢弃。CONFIRMED |
| AC16 D13–D14 | 支持窗只有Bid有效或只有Ask有效，获批Q04/Q05认定无可接受匹配 | 计算BUY/SELL | 两侧都必须有效；单侧足够主公式也不能视为完整；INVALID_QUOTE导致整Event不进任意窗Summary。CONFIRMED / BLOCKED |
| AC17 D13–D14 | 已确认全搜索覆盖；第一条tick在100ms Target之后，之后八窗有效 | 计算MT5 | 100ms NO_QUOTE；整Event DISCARDED/INCOMPLETE_QUOTES，九窗Summary均不含它；诊断保留九窗证据。CONFIRMED |
| AC18 D12–D13 | 最新tick无效，更早tick有效；或同毫秒多不同tick | 匹配 | 按已批准Q04/Q05/Q07才有唯一预期；本轮不替用户选回溯/并列规则。BLOCKED |
| AC19 D03/D12 | Terminal返回部分tick并报同步超时/缓冲不足 | 获取数据 | 不把count>0当完整，不把缺口转NO_QUOTE；按有限重试/失败契约保留覆盖证据。PROPOSED / Q07,Q24 |
| AC20 D06/D12 | Event在To之前，180s Target越过To | 获取报价 | 不能截断Target或因超过To丢弃Event；目标仍在未来时依Q22。CONFIRMED / BLOCKED |
| AC21 D07/D22 | 同一MT5 Order有两个真实Deal，另有一条INOUT | 计算与计数 | Deal各自独立；INOUT一个Event不拆开平。CONFIRMED |
| AC22 D08 | MT4部分平仓/close-by样本 | 映射量与身份 | 需先以Q10证实真实事件量/ID；不能通过Ticket去重或生命周期重建默默改变Event。BLOCKED |

## 3. 计算、聚合与Money

| ID / 依据 | Given | When | Then / 状态 |
|---|---|---|---|
| AC23 D16–D18 | 03合成BUY：P4000.20、Bid4000.50、Ask4000.52、Point.01 | 算Price/Mid/Points | 主Δ+.30、MidΔ+.31、+30 Points；正值表示客户有利；同例完整九窗才可VALID。CONFIRMED |
| AC24 D16–D18 | 合成SELL：P4000.50、Bid4000.18、Ask4000.20、Point.01 | 计算 | 主Δ+.30、MidΔ+.31、+30 Points，使用Ask；换成亏损fixture须负值，不能翻转客户方向。CONFIRMED |
| AC25 D18 | Digits与Point不可推断，给定合法Terminal Point | 算Points | 只用已保存Point作分母，名为Points，不输出Pips、不猜十倍。Point异常资格待Q05/Q09。CONFIRMED / BLOCKED |
| AC26 D19–D21 | 明确仅用于fixture的USD/无FX模式：1lot、TickSize.01、TickValue10USD/lot、Δ+.30 | 按该fixture已假定适用的公式计算 | +300USD；不能推广成所有Symbol通式或声称实际SDK已验证。CONFIRMED fixture |
| AC27 D19–D20 | 原始真实Profit/Commission/Swap变化，而Markout输入不变 | 计算 | Markout不随这些审计字段变化；Money不包含Fee/Rebate等。CONFIRMED |
| AC28 D19/D29 | Native方法需要当前group转换，或未知CalcMode/缺字段 | 选择Money路径 | 不隐含接受FX、不返回零补缺；方法、输入、输出、限制可查；资格由Q03/Q09/Q13决定。BLOCKED |
| AC29 D22 | 两个完整有效Event：Volume1/3、Points10/30、Money100/900 | 汇总该Symbol同窗 | Count2、Volume4、AvgPoints20、VWAvg25、TotalMoney1000、AvgMoney500；相同有效集合用于所有支持窗。CONFIRMED |
| AC30 D14/D22–D23 | 再加入一个某窗失败Event，以及第二个不同Symbol | 汇总 | 失败Event不进入任何窗；Account可以加Money，不显示跨Symbol核心Points；无自动极值剔除。CONFIRMED |
| AC31 D23/D28 | 两Login都有同名Symbol和可重号SourceId，缓存共享合规 | 查看/导出 | 每账户独立数值、事件、诊断和归属，无跨Login总计；不能因共享Quote混入其它账户。CONFIRMED |
| AC32 D23 | 零有效事件、Volume和为0、Total为0、筛选改变集合 | 生成指标 | 空值/分母/筛选口径依Q11；不得以0、100%或零曲线遮掩未决或失败。BLOCKED |

## 4. 持久化、版本与执行

| ID / 依据 | Given | When | Then / 状态 |
|---|---|---|---|
| AC33 D21/D31/D46 | V1已保存，Terminal关闭，Profile改名或不可用，当前Spec已变 | 打开历史V1 | 只读V1输入/规格/规则/结果/诊断快照；不连接/补tick/重算，不套当前Profile名或Spec。CONFIRMED |
| AC34 D31–D32 | V1完成且Profile可用 | 明确Re-analyze | 新建V2，V1不覆盖；默认原Platform/Profile/Accounts/Range，无Duplicate Task或Diff引擎；允许修改范围待Q15。CONFIRMED / BLOCKED |
| AC35 D31/D46 | V2失败或Profile已不存在 | 查看历史/重跑 | V1可显式打开；不自动换Profile或把最新失败标成功；说明V2失败和不可重跑原因。CONFIRMED |
| AC36 D33–D35 | Pending任务与正在处理多账户的任务 | 分别Cancel | Pending移出执行队列；Running真实停止后CANCELLED，保留任务/诊断/日志；部分结果不作正式Summary，无Resume。CONFIRMED |
| AC37 D35 | Cancel和完成提交竞争；SDK暂不能中断 | 请求取消 | 按获批提交裁决单一终态；真实停止前显示正在取消/等待当前读取，不能假报停止。PROPOSED / Q24 |
| AC38 D33–D34 | 长范围任务含大量数据 | 执行与导航 | 内部有界分块/安全队列、真实阶段和计数；无用户线程控件、强制手拆日期或伪ETA；性能阈值待A12。CONFIRMED / PROPOSED机制 |
| AC39 D31/D37 | 磁盘满/保存事务失败 | 发布新版 | 不能COMPLETED但缺结果，不泄露部分正式Summary；旧版一致可读。PROPOSED事务验收 |
| AC40 D33/§101 | 重启前有完成、失败、取消、RUNNING、PENDING | 启动 | 前三类事实保留；RUNNING不得自动COMPLETED；新INTERRUPTED/FAILED及队列恢复依Q19，无自动Resume。CONFIRMED / BLOCKED |
| AC41 D39 | 有多个版本和诊断的Task | 点击Delete后取消/再确认 | 取消不变；确认删除整Task和其拥有全部版本/结果/匹配报价/诊断/快照；不影响其它Task/Profile或项目。运行中策略Q18。CONFIRMED |

## 5. Diagnostics、Test与导出

| ID / 依据 | Given | When | Then / 状态 |
|---|---|---|---|
| AC42 D29 | 一Event多个失败窗口，含不同原因；另有账户跳过和环境失败 | 打开Diagnostics/原因下钻 | 四层分离；Event总数去重，WindowCount独立，重叠原因明确；点击后列全失败窗，不只首个。CONFIRMED |
| AC43 D29 | 有成功/失败窗及Native Money证据的Event | Copy Diagnostic / Trace | 包含身份、Side/量/价/精度、支持窗Target/Quote/Age/Bid/Ask/Mid/算式、Spec、Money方法、规则/环境；不含凭据；无Tick字段明确为空。CONFIRMED |
| AC44 D30 | 输入MT5 Profile/Login/DealID；MT4 Profile/Login/Ticket+OPEN/CLOSE | Single Trade Test | 从Manager取真实记录并核验归属，不能编辑Symbol/Side/Volume/Price/Time；同引擎同规则；全窗、曲线、Spec/Trace可查；不进正式History/Summary。CONFIRMED |
| AC45 D30 | Test和正式分析获同一完整事件/报价/规格/规则及Money上下文 | 比较输出 | 结果一致；上下文不同不能假称同一输入；Native黑盒限制明示。CONFIRMED契约，能力验证待Q03/Q13 |
| AC46 D40–D41 | 用户正在看V1而V2已完成，V1含多个账户和discarded事件 | 显式Export V1 | 文件只含V1；XLSX账户独立，无每Symbol一表；CSV多账户ZIP；完整字段不随UI隐藏列减少。CONFIRMED；建议命名需Review |
| AC47 D41 | 导出含所有支持窗口及复杂64bit IDs/后缀Symbol/原始文本 | 检查产物 | Task/Version/Profile/Range/AnalysisTime/Rule/Spec及窗口证据可追溯、ID无损；表名/重复/行限不静默丢数据，具体策略Q21/Q25。CONFIRMED / PROPOSED |
| AC48 D31/D40 | 指定版本导出中磁盘不足或目标文件占用 | 导出失败 | 页面可重试且保持所选Version；不修改分析终态、旧产物或创建成功假象。PROPOSED |
| AC49 D35/D40 | 失败/取消版或SingleTest结果 | 尝试完整导出/保存Test | 依Q17决定，不能默认为正式完整报告或悄悄增加测试历史。BLOCKED |

## 6. UX、保密和隔离

| ID / 依据 | Given | When | Then / 状态 |
|---|---|---|---|
| AC50 D24–D27 | Account→Symbol Ranking→Symbol→Deal | 下钻/返回 | Symbol Ranking首次打开默认1s、TotalMoney DESC；返回恢复window/filter/sort/scroll/focus；明确Task/Version/Login/Symbol，不跳另账户或最新版本。CONFIRMED；其它页面默认排序及恢复细节PROPOSED |
| AC51 D25 | MT5/MT4 Symbol和Deal详情 | 查看曲线 | 分别9/7窗；Symbol为VWAvgPoints、Deal为Points；单曲线、单位清楚，无Money/Mid/Price叠加。窗数/指标/单曲线CONFIRMED；零线和同源数据表呈现PROPOSED UX |
| AC52 D26–D27 | Side与Entry筛选、OUT_BY不存在；筛选无匹配 | 操作筛选 | 单维选项准确、OUT_BY仅实际存在显示、无复杂交叉统计；组合影响待Q16，空集提示不冒充读取失败。CONFIRMED / BLOCKED |
| AC53 D42–D43 | 仅键盘、长Login/Symbol、大量列、125/150/200%DPI及Light/Dark/HighContrast | 完成主要流程 | 可见焦点/语义标签/快捷键路径，表内横向滚动、身份列可达；正负/失败不用颜色单独表达；主界面无Trace墙。PROPOSED验收，未实测 |
| AC54 D36 | Profile密码或SDK错误包含敏感片段 | 保存/Copy/Export/Log/CrashReport | 不出现密码/token/认证对象；只保留受保护引用和脱敏诊断；SDK自身行为需Q20验证。CONFIRMED / BLOCKED |
| AC55 D01/D37/D47 | 本项目所有未来文件/数据写入及当前设计产物 | 检查路径 | 当前只docs；未来仅项目或明确应用数据目录；不改其它插件/Terminal/SDK，不在mt-plugins根散落，不存全流tick到SQLite。CONFIRMED |
| AC56 D32/D44/D47 | 功能清单和界面审核 | 检查V1范围 | 无风险评分、自动处置、T0强依赖、Slippage/LP/Bridge、Pause/Resume、Duplicate、Web后端、多用户或自定义公式/窗口；本轮无实现。CONFIRMED |

## 7. 验收执行与关闭规则

先由用户关闭业务Q并批准PROPOSED契约，再由技术负责人补齐A类能力证据。BLOCKED用例不是“默认通过”或“忽略”；不能把Q04的一种示例分支直接实现为选择。具体测试数据规模、响应时间、取消延迟、数字精度与格式由后续验证计划确定，本轮不编造目标。

本轮已经做的是文档交叉审查、链接/编号核对和03合成算术复核；没有产品测试通过记录。后续证据需记录所用SDK/Terminal build、RuleVersion、fixtures、预期/实际值与问题归属，避免只截一个漂亮屏幕作为正确性验收。
