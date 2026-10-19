# MT5 0.7.2：管理账号组通配符修复

现场：管理账号 `2016` 的 Groups 为 `*`，跟单账户 `50002` 属于 `real\VU\ECN\BP-L500`。0.7.1 错误地将授权条目按完整组名比较，导致 `follower group unavailable to dealer DealerLogin=2016`，请求没有提交给 MT5。0.7.2 修正这一误拦截；无需修改现有的 `*` 授权或客户只读设置。

## 匹配行为

- 支持完整组名以及 `*`，例如 `real\*`、`real\VU\ECN\BP-*`；`*` 可跨组路径的反斜杠。
- 条目中的逗号分隔条件，条件两端的普通空格忽略；区分大小写，`?` 和方括号按普通字符处理。
- `!` 是排除条件，需要至少一个包含条件命中。单条掩码语法参照所提供 SDK 的 `CMTStr::CheckGroupMask`。
- 对管理授权列表的多行条目，插件采用保守预检查：任何一行的排除命中都拒绝，即使另一行已包含该组。这是插件限制；不宣称完整复现 MT5 管理授权多行规则的先后语义。当前现场的单行 `*` 不受此限制影响。
- 空授权列表、空目标组、控制字符、缺少排除内容和超过 255 字符的非空条件不会放行；过长条件不会被截断成另一条授权规则。
- 保留管理账号身份、启用状态和 Dealer 权限检查。MT5 仍会处理提交请求并决定最终交易结果。

这版同时补充所有提交前拒绝分支的 `NOT_SUBMITTED` 日志，包含 `intent`、`follower`、`source_deal`、`dealer`、`retcode` 和具体原因。组检查失败会附带实际 `follower_group`。按 `intent` 搜索可与 `PREPARED` 的源仓位对应。

## 编译和部署

1. 将本源码包解压到独立目录 `D:\work\TradeCopier-MT5-0.7.2`，此目录内应直接出现 `scripts`、`plugins` 和 `README-WINDOWS.txt`。
2. 在 x64 Native Tools Command Prompt 中执行：

   ```bat
   cd /d "D:\work\TradeCopier-MT5-0.7.2"
   scripts\build-windows.cmd -Executor -SdkRoot "D:\SDK\mt5-sdk" -BuildDirectory "D:\MT5Build\TradeCopier-0.7.2"
   ```

3. 看到 `Build and tests passed` 后，产物位于 `D:\MT5Build\TradeCopier-0.7.2\package-executor`。关闭管理界面，按已有流程停用并卸载旧插件，再更新该目录中的 `TradeCopierExecutor.dll`、`TradeCopierManager.exe` 和 `TradeCopierJournal.exe`，重新加载。
4. 保留现有配置和账本路径；插件参数 `DealerLogin` 为 String，值 `2016`；管理账号 Groups 保留 `*`；50002 的 Enable trading 保持未勾选。启动日志包含 `dealer group masks enabled (0.7.2)`。
5. 用 50001 新开一笔有效最小手数以上的源订单。检查 50002 的实际持仓、方向、数量，再验证部分平仓和全平。`READONLY_COPY` 只表示进入提交流程，不是成交确认。

本次保留配置 schema 2/3/4、账本 schema 7 和管理协议 v2。既有失败记录不会自动补跟或清除。本次不改变 MT4。

## 验证边界

回归覆盖现场的 `*` 与 `real\VU\ECN\BP-L500`、前后缀与跨路径匹配、排除优先、空或不匹配的授权、边界和异常输入。还使用本地提供的 SDK 匹配函数对有效单条掩码进行 30,275 次差分对照；SDK 代码只用于本地验证，不随源码包发布。

macOS 上的本地检查不能替代 Windows DLL 编译与服务器成交测试。新版本 Windows 编译、加载和真实只读跟单仍需在测试服务器验证。包内 `VALIDATION_STATUS.json` 记录实际完成的检查。
