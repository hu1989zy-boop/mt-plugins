# MT5 同服跟单插件与 Windows 管理界面

当前目标：开发 **MT5 Server API C++ DLL 插件**，在 MT5 服务端执行跟单，并配套一个**独立 Windows 管理界面**配置关系、查看状态和日志。Administrator 参数列表截图仅作为配置风格参考；当前不要求把界面嵌入 Administrator，也不表示兼容截图中的第三方插件。

## 已确认需求

- 所有账户在同一个 MT5 Live，账户模式均为 Hedging，账户币种均为 USD。
- 多个源账户可以跟到多个目标账户；每条 `source → follower` 关系都有独立的手数模式、参数和启用边界。
- 只跟每条关系启用后新开的源交易，启用前已有仓位不补单。
- 三种手数模式：源手数乘倍率、按跟单提交前查询的目标/源净值比例乘系数、每个新源交易固定手数。
- 固定模式部分平仓以“源关闭数量 / 平仓前源剩余数量”为比例，对目标实际剩余量减仓；其他两种模式当前默认统一采用此规则。源全部平仓时关闭对应目标剩余量。
- 不复制挂单；源挂单成交后形成的新仓按新仓规则处理。
- 不复制或设置目标 SL/TP；源仓位因 SL/TP 实际平仓后，仍同步其平仓结果。
- 目标对应仓位被手动全部关闭后，不再操作该仓位或补仓。手动部分关闭后记录实际余量，后续继续按比例减仓，这是当前实现假设。

MT4、Manager SDK 外部跟单服务、跨平台跟单和历史订单迁移均暂缓，当前开发入口以 MT5 Server API 方案为准。

## 当前交付状态

按用户提供的 SDK 开发，基准为 **Server API 6182**（安装器资源版本为 5.0.0.6191）。当前是可在独立测试环境继续验证的源码工程，**还不是能执行跟单的完整插件**。

| 组件 | 当前状态 |
| --- | --- |
| C++17 业务核心 | 三种手数计算、部分平仓、关系图、启用边界及仓位映射状态逻辑；SDK 无关 |
| 配置库 | 严格 JSON 校验、64 位账号、多对多关系、三种模式、完整导入导出 |
| `TradeCopierManager.exe` 工程 | 原生 Windows 离线编辑器源码，可编辑/保存规则；未连接 MT5 |
| `TradeCopierSdkProbe.dll` 工程 | 只读 Server API 6182 加载与成交事件诊断源码；不下单 |
| 跟单执行与恢复 | 尚未实现 SDK 开平仓、持久化、请求核对与重启恢复 |
| Windows 与服务端验证 | 用户截图确认 VS2026 构建及两组测试通过；界面试用和 MT5 加载待验证 |

净值时点已由用户补充确认：收到源开仓成交后，在提交跟单前查询双方当前净值并冻结，分别记录源成交时间、源查询时间和目标查询时间；不宣称原子同一时点快照。配置采用 `before_follower_submission`，旧时点定义的草稿会被拒绝，避免静默改变含义。

## 从 Windows 测试环境开始

完整步骤见 **[Windows 构建与测试服部署教程](docs/windows-test-deployment.md)**。在 VS 2022/2026 的 x64 Native Tools Command Prompt 中，位于项目根目录执行：

```cmd
scripts\build-windows.cmd -SdkRoot "D:\SDK\MetaTrader5"
```

把示例路径替换成实际 SDK 根目录。脚本识别当前 VS 开发者环境（2022/v143 或 2026/v145），构建 x64 Release、运行测试、检查 DLL 导出，并把产物放到 `build-windows\package-probe`；不修改 MT5 服务或安装插件。VS2026 需要 CMake 4.2+。只试界面可使用 `-UiOnly`，无需 SDK。

配置界面导出的 JSON 当前仅是本地草稿，不能自动发布到服务器，也不兼容其他厂商插件的 Rules 文本。所有样例关系默认禁用。只读诊断 DLL 用于验证版本、加载和回调，其启动不意味着跟单已启用。

## 本地验证

已在 macOS 上以 AppleClang/C++17 严格警告编译并通过 CMake/CTest：核心 2918 项、配置 253 项检查。结果覆盖已实现的计算和状态规则，不代表真实 MT5 执行、Windows UI 或全部业务验收已通过。

用户回传的 Windows 截图已确认 VS2026 生成 EXE/DLL，两组 CTest 全部通过，并完成打包。详见 [Windows 构建验证记录](docs/windows-build-validation.md)；界面运行与 Trade Server 加载仍待验证。

具备 CMake 3.21+ 和 C++17 工具链后：

```sh
cmake -S . -B build
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure --no-tests=error
```

Windows 默认构建离线编辑器。SDK 探针默认关闭，使用上面的 PowerShell 脚本构建，或显式设置 `TRADECOPIER_BUILD_SDK_PROBE=ON` 和 `MT5_SERVER_SDK_DIR`。

## 接入资料

- [已提供 SDK 的版本、接口与限制核对](docs/sdk-integration-notes.md)
- [业务规格与完整验收目标](docs/mt5-live-copier.md)
- [Server API 与独立 Windows 界面架构](docs/mt5-server-ui-architecture.md)
- [Windows 界面使用与验收](windows/README.md)
- [只读 SDK 探针说明](server/README.md)
- [多对多配置样例](config/copier.example.json)

SDK 已提取到 `vendor/mt5-sdk` 并校验全部 1003 个原始文件。SDK、安装器、本机配置和运行状态均被 Git 忽略；源码包不包含专有 SDK。测试服应支持 API 6182，兼容性以真实加载日志为准。

## 暂缓资料

- [MT4 历史方案说明](docs/mt4-live-copier.md)
- [Manager SDK 历史方案说明](docs/manager-sdk-architecture.md)

这些资料只保留之前讨论的背景，不构成当前交付承诺。
