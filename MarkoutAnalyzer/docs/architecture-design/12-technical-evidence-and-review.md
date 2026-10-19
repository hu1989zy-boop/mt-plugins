# Technical Evidence & Review Record

核查日期：2026-10-12。仅静态、只读检查；未执行 SDK、Terminal、交易 API、安装器、仓库脚本或真实连接测试。本文将“找到接口/文件”与“已验证业务可行”分开。

## 1. 本地材料

SDK 为用户授权只读查看的既有依赖材料，不属于本项目本轮输出；没有修改、复制或重新安装。

| 证据 | 核实结果 | 能证明 / 不能证明 |
|---|---|---|
| [MT4ManagerAPI.h](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt4-manager-sdk/MT4ManagerAPI.h) | Version 400、Build 1470；TradeRecord、秒级时间、CManagerInterface、TradesUserHistory、TicksRequest、TradeCalcProfit声明 | 有相应 Manager 接口；不证明 Terminal历史tick、部分平仓量语义、历史查询覆盖或ProfitCalc完整契约 |
| [MT5APIManager.h](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Include/MT5APIManager.h) | MTManagerAPIVersion 6182；DealRequest/Page、TradeProfit/Ext 等 | 有请求及计算入口；不证明实际部署兼容或历史环境重现 |
| [MT5APIDeal.h](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Include/Bases/MT5APIDeal.h) | BUY/SELL、IN/OUT/INOUT/OUT_BY、TimeMsc、Volume/VolumeExt | 支持对应字段映射；不能回填不存在的TimeMsc |
| [DealRequestPage 文档](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtmanagerapi_dealrequestpage.htm) | from/to为服务器时区秒；offset分页；不能在IMT*Sink事件处理器内调用 | 适配层要处理秒包络与毫秒精筛；不证明并发历史更正下分页稳定 |
| [Volume](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtdeal_volume.htm) / [VolumeExt](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtdeal_volumeext.htm) | 分别为1/10000、1/100000000 lot | 不能混用单位或把raw整数当lots；实际选用字段须绑定部署版本 |
| [TradeProfit](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtmanagerapi_tradeprofit.htm) / [TradeProfitExt](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtmanagerapi_tradeprofitext.htm) | 输出指定group账户币种，涉及Profit Currency转换时采用该group当前市场价格 | 原生方法可能隐含换汇；历史价格参数本身不能确保历史Money计价环境 |
| [TimeServer 文档](/Users/lukeh/Documents/workspace/codex/mt-plugins/vendor/mt5-sdk/Docs/html/imtmanagerapi_timeserver.htm) | 此方法在应用端按服务器时区调整当前本地时钟 | 不可把当前返回值当历史DST/时间映射证据，更不能据此用Windows时间匹配成交 |
| SDK DLL/示例清单 | MT4 32/64命名DLL；MT5 native/.NET DLL和C++/.NET示例存在 | 未加载、未编译；不证明ABI、运行时、授权、线程或取消能力 |

头文件 SHA-256（仅用于本次证据定位，不是安全签名鉴定）：

- MT4ManagerAPI.h：`8d5ecc24c4ff396f5d50f614e680619f238a8fe9efc0d7f47d46002c1a7fdd4a`
- MT5APIManager.h：`afcbe576901fab2454108618b7fb886fddb13e8cfee33e410b8220850d2d395a`
- MT5APIDeal.h：`10b07951cd7b94f278eabbe762c125f200fb57b77a19084bbdc0fe09373ff83f`

针对 vendor 的文件清单检索没有定位到可直接验证的 Terminal 历史 Tick connector/MQL 源码；这只说明本次指定范围没有找到，不宣称整个环境绝无相关能力。既有其它插件的选型/服务器插件设计不作为本项目技术决定。

## 2. 官方公开接口证据

| ID | 官方来源及发现 | 设计影响 |
|---|---|---|
| E01 | MQL4 [历史序列目录](https://docs.mql4.com/series)与[SymbolInfoTick](https://docs.mql4.com/marketinformation/symbolinfotick)分别提供bar历史/当前报价，未找到与MT5完整历史双边Tick检索等价的公开接口 | MT4 Terminal能力未证实；不能用bar、模拟tick、Manager Tick或MT5替代；Q01 |
| E02 | MT5 [CopyTicksRange](https://www.mql5.com/en/docs/series/copyticksrange)：毫秒范围双端包含、旧到新；超时/缓冲不足可能只返回部分数据 | 分块边界、同时间顺序及完成度要单独处理；收到非零count不等于完整；Q07/Q24 |
| E03 | MT5 [CopyTicks](https://www.mql5.com/en/docs/series/copyticks)：未变化字段沿用已知值，flags表示变化；历史同步涉及Terminal自有数据库 | flags不能代替双边有效性；应用不存全流与Terminal正常缓存是两个范围；Q05/Q23 |
| E04 | MT5 [Python copy_ticks_range](https://www.mql5.com/en/docs/python_metatrader5/mt5copyticksrange_py)使用UTC输入/时间语义 | 这是时间契约差异证据，不是选用Python的建议；不能未经确认就与Manager服务器时间整数直接比较；Q02 |
| E05 | [OrderCalcProfit](https://www.mql5.com/en/docs/trading/ordercalcprofit)基于当前账户和当前市场环境，输出账户币种 | 不自动等于被分析账户group条件，也不承诺历史经济条件重放；Q03/Q13 |
| E06 | MQL4 [OrderProfit](https://docs.mql4.com/trading/orderprofit)读取已有订单利润；[Symbol Properties](https://docs.mql4.com/constants/environment_state/marketinfoconstants)定义不同profit mode及账户币种tick value | 不能用OrderProfit代替任意窗口Money；不能用一条公式覆盖所有模式 |
| E07 | [MQL4 datetime](https://docs.mql4.com/basis/types/integer/datetime)、[MT5 Deal属性](https://www.mql5.com/en/docs/constants/tradingconstants/dealproperties)、[MqlTick](https://www.mql5.com/en/docs/constants/structures/mqltick)支持秒/毫秒精度区分 | 与本地SDK字段分别核查；报价毫秒不能把MT4成交秒“升级”到毫秒 |
| E08 | [Windows进程位数](https://learn.microsoft.com/en-us/windows/win32/winprog64/running-32-bit-applications)限制执行DLL装载 | 先核实实际DLL，再决定是否需要最小本地worker，不默认服务化 |

## 3. Issue → Evidence → Impact → Options → Recommendation

| 问题 | Evidence / Impact | 可选处理与建议 | 未决 |
|---|---|---|---|
| MT4历史Tick能力 | E01、本地仅见Manager TicksRequest；无法保证严格同平台Terminal历史双边报价 | 首选提供受支持Terminal接口/供应商证据；若确无能力，由用户决定范围或来源规则变更，不能偷偷替代 | Q01 |
| 时间轴不明 | E04与Manager分页文档时间表述不同；错误偏移会污染所有窗 | 保留原始编码并证明时间语义；不猜固定offset，不自动“修正” | Q02 |
| Money环境/换汇 | E05及本地TradeProfit文档；USD账户不自动消除转换 | 先限定合规无换汇可验证路径，或用户明确调整理论金额语义；不私自改变规则 | Q03/Q13 |
| 历史范围部分返回 | E02；有tick不代表所有相关报价已得到 | Provider提供完成度、重试与覆盖证据；未完成不转NO_QUOTE | Q07/Q22/Q24 |
| MT4托管接入 | C++虚接口+factory导出；平面P/Invoke假设错误 | 评估官方/受支持binding或最小native桥接；与Qt原生路径比较 | Q24 |
| Terminal缓存副作用 | E03；正常读取可能同步其内部数据库 | 后续接入时披露并确认边界。本阶段未调用、未修改；不把准备阶段禁令扩大为既成运行时冲突 | Q23 |

## 4. 本轮验证范围

已完成静态需求阅读、公式与合成算术检查、SDK头文件/随附文档及官方文档核查。未完成真实Windows运行、SDK连接、历史Tick覆盖、Money准确性实测、UI可用性/可访问性/性能测试；不能在Acceptance Criteria中将未来验收写成当前已通过。

有未决项并不阻止设计评审，但阻止提前进入实现。

## 5. Cross-document Consistency Review — 已完成

本轮对 Architecture、Calculation、Logical Model、State/Error、UX/Wireframes、Acceptance 与 Q/A/Decision Registers 做了交叉审查。审查包含独立计算/SDK/UX检查和主审整合；不是产品运行或用户测试。

| 检查面 | 对齐结果 |
|---|---|
| 输入与平台隔离 | 单Platform/Profile；账户无效不阻断其它有效账户；无其它平台/近似Symbol补数据 |
| Event / Time / Volume | MT4 OPEN/CLOSE反向、SECOND；MT5 TimeMsc无回退、多Deal独立；原始与规范量纲分开 |
| Window / Matching | 9/7窗、Target不截To、Tick≤Target、允许旧Tick复用；两侧报价都须有效；NO_QUOTE与访问失败分离 |
| 资格 / Summary | 全支持窗报价完整；支持窗报价失败整Event排除；Money/异常数值资格保留Q09；相同有效集合跨窗聚合 |
| Money / Snapshot | 理论USD、无自行FX、无真实Profit/费用混入；原生输入/输出可核查不冒称历史环境重建 |
| Model / State / History | Version拥有证据与结果；COMPLETED依发布成功，Failed/Cancelled不发部分Summary；历史离线、新版不覆旧版 |
| UX / 导航 / 图表 | Account→Symbol→Event上下文保持；Diagnostics独立；Symbol VWPoints、Event Points单曲线；未决行为不设默认 |
| Export / Delete / Credentials | 显式Version完整字段、账户独立；整Task二次确认删除；秘密不进任何结果/Copy/日志/快照 |
| Acceptance / Open Questions | 56条Given/When/Then区分确认/建议/待决定；25项Q具备指定字段；21项需用户决定，4项为技术取证/方案 |

已修正审查发现的具体表述问题：

- 将“任何失败整Event丢弃”缩为已冻结的报价失败，保留Money资格未决。
- 将五入口/精确导出命名标为建议；单账户CSV包装不预先写成ZIP。
- 明确全部支持窗双边价要求，收紧NO_QUOTE与INVALID_QUOTE的区分并保留Q04/Q05。
- MT4例子使用整秒合成Tick，避免误示已有毫秒报价能力；同时验证TickTime=Target可匹配。
- 不重新开放已创建Task的历史保留规则；Q14只保留终态问题。
- Ranking默认1s/TotalMoney DESC不推广到所有子页面；零线/旁表呈现保持UX提案。
- 纠正MT4 C++虚接口互操作描述，区分版本查询与factory导出；不宣称已运行SDK。

实际执行的文档检查：13份Markdown的本地链接、围栏、D01–D47/A01–A16/AC01–AC56编号完整；Q01–Q25字段齐全；12组线框。使用Decimal复核03合成方向、Points、条件Money与加权汇总，复核七个MT4 Quote Age；均通过。没有对真实市场Money或SDK能力作测试通过声明。

目录/修改检查：现有项目skills/temp/discovery共65项的内容及链接指纹与写文档前一致；全局UI/UX Pro Max SKILL.md哈希不变；三份被检查SDK头文件哈希不变。没有创建app/data/exports/logs/tests、数据库、应用项目、运行依赖或其它业务源码。本轮写入仅限本设计目录。

**结论：Ready for Architecture Review = YES。Implementation Authorization = NO。** 关键Q/A仍公开保留，文档完成后停止，等待用户Review。
