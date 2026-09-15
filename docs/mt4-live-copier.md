# MT4 跟单方案：暂缓

MT4 需求曾在讨论范围内，之后也讨论过 Manager SDK 外部程序。目前用户明确优先 **MT5 Server API DLL + 独立 Windows 管理界面**。MT4 尚未接入 SDK 或实现，不属于当前交付。

当前入口为 [项目 README](../README.md) 和 [MT5 规格](mt5-live-copier.md)。旧的 [MT4 配置文件](../config/mt4-copier.example.json) 已改为暂缓状态标记，不能作为活动配置加载。

若后续恢复 MT4 开发，需要重新确定使用 Server API 还是 Manager SDK，并取得匹配 Build 的 SDK。业务规则可以参考当前 MT5 规格，但以下模型不能直接照搬：

- MT4 活动订单、挂单和历史订单不能假定具有 MT5 Orders / Deals / Positions 三层关联和持仓 identifier。
- 部分平仓后 ticket 是否变化、剩余单的后继关联及通知顺序必须按 SDK 和测试服验证；启用前旧单的剩余单不能因新 ticket 被误跟。
- 源和目标的剩余单映射、Close By、数量单位、ABI 与线程限制分别验证。

以上保留为历史研究提醒，未定义 MT4 的新实现或承诺交付时间。[MT4 官方 API 说明](https://www.metatrader4.com/en/brokers/api)只说明接口定位，不能代替用户持有的 SDK。
