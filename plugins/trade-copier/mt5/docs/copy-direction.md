# MT5 跟单方向

每条跟单关系可以分别选择“正向”或“反向”，默认正向。同一信号源可以让不同跟单账号采用不同方向；同一跟单账号也可以对不同信号源分别设置。本次仅开放 MT5，MT4 配置会拒绝反向关系。

| 源开仓 | 正向跟单 | 反向跟单 |
| --- | --- | --- |
| 买入 Buy | 买入 Buy | 卖出 Sell |
| 卖出 Sell | 卖出 Sell | 买入 Buy |

方向仅决定跟单仓位的买卖方向。手数比例、净值比例、固定手数继续使用原来的计算方法，不需要填写负数。品种匹配和交易权限检查以实际跟单方向为准，例如反向跟随源买单时，目标品种必须允许卖出。

在管理界面读取生效配置后，添加或编辑关系，在“方向”中选择正向或反向并确定。需要生效时，点击“应用配置”，核对确认内容后提交，再读取生效配置确认方向。应用配置保留当前暂停状态；若此前已手动暂停，需按需恢复新仓。保存 JSON 只保存本地草稿。

修改方向只影响之后接收的新源仓。已有映射继续使用开仓时保存的规则，不会因修改方向而翻转仓位或补开仓。重启后也按账本保存的规则恢复。源部分平仓仍按比例平掉对应的实际跟单仓；源全平仍关闭对应跟单仓的剩余数量。跟单账户手动全平后终止该映射，不会反向补仓。

配置兼容规则：

- 旧 schema 2 和 3 没有逐关系方向字段，读取时全部默认为正向。
- 存在反向关系时保存为 schema 4，`trade_policy.direction` 为 `per_relationship`，每条关系包含 `direction: "forward"` 或 `direction: "reverse"`，并保留 schema 3 的品种字段。
- 全部正向时继续按字段内容保存为 schema 2 或 3，保持旧配置的规范化结果。schema 4 中的全部正向配置也可正常读取。
- 未识别的方向值、缺失 schema 4 的方向字段、无效枚举都拒绝导入或应用，避免被静默当作正向。

完整示例见 [正反向混合配置](../config/copier.direction.example.json)。示例账号为占位值，全部关系和全局状态默认停用。

更新时应一起替换新版 `TradeCopierExecutor.dll`、`TradeCopierManager.exe` 和 `TradeCopierJournal.exe`，保留并备份现有配置及运行账本。0.7.0 首次写入后账本升级为 schema 7，需要 0.7.0 或更新程序读取；旧 schema 1–6 可升级读取。不要用旧版 DLL 或账本工具直接打开新版运行账本。

测试服务器验收：同一个源账号分别跟到正向、反向两个账号，测试源 Buy/Sell、部分平仓、全平；带仓修改方向后确认老仓仍按原规则关闭、新仓按新方向开仓；重启插件后重复平仓检查。真实成交、权限和成交价格仍以服务器返回结果为准。

## 编译和升级

已于 2026-10-08 根据用户截图确认：Windows 测试环境的 MT5 SDK 根目录为 `D:\SDK\mt5-sdk`，其下包含 `Include`。后续提供该环境的编译命令时使用此路径；除非用户通知迁移，不再使用旧示例路径 `D:\SDK\MetaTrader5`。

从当前源码包根目录，在 Windows 的 x64 Native Tools Command Prompt 中运行：

```bat
scripts\build-windows.cmd -Executor -SdkRoot "D:\SDK\mt5-sdk" -BuildDirectory "build-direction-0.7.0"
```

编译并测试通过后，产物位于 `plugins\trade-copier\mt5\build-direction-0.7.0\package-executor`。源码包不含 MetaTrader SDK 或已编译的 Windows DLL。

先在独立测试服务器更新：关闭管理器，按现有部署流程停用并卸载旧执行器，备份配置和账本；同步替换同一编译包内的 `TradeCopierExecutor.dll`、`TradeCopierManager.exe`、`TradeCopierJournal.exe` 后重新加载。沿用原配置和账本路径，不要用示例覆盖正在运行的配置。先读取生效配置，旧关系应显示正向，再编辑需要反向的关系并应用。

本次本地验证通过核心、配置、执行、管理请求和账本恢复测试；Windows 原生界面、DLL 编译与 MT5 实机加载/成交需要在测试服务器验收。
