# MT4 跟单：历史方案与接入提醒

此前开发阶段优先实现 MT5 Server API DLL + 独立 Windows 管理界面，MT4 方案暂缓。2026-09-29 收到 Server API 5 与 Manager API build 1470 的 SDK 后，已按 MT5 0.6.2 规则实现 MT4 0.1.0 同服跟单源码、管理器及模拟测试。Windows DLL 编译、实际服务器加载和 Demo 交易仍待验收，MT4 / MT5 跨服互跟尚未实现；详情见 [SDK 接入说明](sdk-integration-notes.md)。

当前入口为 [MT4 README](../README.md) 和 [MT5 规格](../../mt5/docs/mt5-live-copier.md)。旧的 [MT4 配置文件](../config/mt4-copier.example.json) 保留为历史暂缓状态标记，不能作为活动配置加载。

实际部署前仍需验证 SDK 与服务器的兼容性。业务规则沿用 MT5，但以下模型使用 MT4 独立适配并需 Demo 验证：

- MT4 活动订单、挂单和历史订单不能假定具有 MT5 Orders / Deals / Positions 三层关联和持仓 identifier。
- 部分平仓后 ticket 是否变化、剩余单的后继关联及通知顺序必须按 SDK 和测试服验证；启用前旧单的剩余单不能因新 ticket 被误跟。
- 源和目标的剩余单映射、Close By、数量单位、ABI 与线程限制分别验证。

以上保留为接口模型及验收提醒，具体实现边界以当前 MT4 README 为准。[MT4 官方 API 说明](https://www.metatrader4.com/en/brokers/api)只说明接口定位，不能代替用户持有的 SDK。
