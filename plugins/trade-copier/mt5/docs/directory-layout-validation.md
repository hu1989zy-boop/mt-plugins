# MT4 / MT5 目录分离验证（2026-09-29）

当前 MT5 版本为 0.6.2。现有实现移至 `plugins/trade-copier/mt5/`；MT4 的历史方案和禁用配置标记归入 `plugins/trade-copier/mt4/`，MT4 尚无实现或构建目标。

本次调整涉及目录、CMake 聚合入口、SDK 定位、构建脚本转发和文档。源码、测试、配置及工具的 41 个文件与迁移前逐一比对，内容完全一致。

## 本机构建与测试

环境：macOS arm64、AppleClang 16.0.0、CMake 4.4.3，Release，`BUILD_TESTING=ON`。使用三个独立的新构建目录，未复用旧缓存。

| CMake 源码入口 | 重新配置和编译 | CTest |
| --- | --- | --- |
| 仓库根目录 | 通过 | 4/4 通过 |
| `plugins/trade-copier` | 通过 | 4/4 通过 |
| `plugins/trade-copier/mt5` | 通过 | 4/4 通过 |

每个入口均通过核心逻辑 2,918 项、配置 319 项、执行与恢复 63,739 项、管理界面调度逻辑 137 项检查。包括 0.6.2 重启后新增订阅序号门槛的回归测试。管理界面调度测试不等于运行 Windows 界面。

三个构建缓存中的默认 SDK 路径均正确指向仓库根 `vendor/mt5-sdk`。检查两层 Windows 转发脚本的参数及退出码传递、实际打包脚本的 26 项输入路径、本地文档链接，以及 MT4 / MT5 子目录内构建产物、本机配置和运行数据的忽略规则，均通过。

本次原始配置、编译及测试日志保存在执行机器的 `/private/tmp/trade-copier-platforms-uc5i544k/`，分别以 `workspace-`、`plugin-` 和 `mt5-` 为前缀；临时目录不随源码提交。

## 尚未验证的范围

本机没有执行 Windows MSVC 构建，未产出或运行本次版本的 Server API DLL、Windows 管理器 EXE，也未运行 Windows 命名管道测试。实际 SDK 编译、DLL 导出检查、Windows 界面及 MT5 Demo 交易验证需在 Windows 环境完成。

在 Visual Studio x64 开发者命令行中，从仓库根目录执行：

```bat
plugins\trade-copier\mt5\scripts\build-windows.cmd -Executor -SdkRoot "D:\SDK\MetaTrader5"
```

默认产物目录为 `plugins/trade-copier/mt5/build-windows/package-executor`。旧的仓库及插件脚本入口仍转发到此 MT5 脚本；相对 `-BuildDirectory` 以 MT5 源码目录为基准。
