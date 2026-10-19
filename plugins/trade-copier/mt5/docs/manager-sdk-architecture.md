# Manager SDK 方案：暂缓历史资料

此前讨论过通过 MT4 / MT5 Manager SDK 运行外部常驻跟单服务。当前用户已明确选择 **MT5 Server API DLL 执行跟单 + 独立 Windows 管理界面**；Manager SDK 不再是当前执行架构，也不是独立界面的既定依赖。

当前设计见 [MT5 Server API 与界面架构](mt5-server-ui-architecture.md)，业务规则见 [MT5 规格](mt5-live-copier.md)。README 与 `config/copier.example.json` 均以 Server API 方案为准。

若以后重新启用 Manager SDK 方案，需要单独验证：

- 数据可见范围和为目标账户执行交易的权限，不能以登录成功代替执行能力验证。
- 连接、会话、回调和线程限制，以及断线后的快照、历史补查与未决请求核对。
- SDK 位数、ABI、运行库及 MT4 / MT5 独立适配；不能假设两套 SDK 可直接共用同一原生实现。
- 连接凭据的本地受保护存储及服务器身份校验；现有配置样例不含凭据。

此文件只保留先前方案的关键约束，没有对应可运行服务。官方 API 定位参考：[MT4](https://www.metatrader4.com/en/brokers/api)、[MT5](https://www.metatrader5.com/en/brokers)。
