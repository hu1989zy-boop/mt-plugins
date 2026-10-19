# Windows 测试服务器：构建与首次加载

> 目录调整：当前 MT5 实现位于 `plugins/trade-copier/mt5/`。本文保留对应版本的源码包和现场路径；在当前仓库复跑命令、查找产物时，请按 [目录与构建入口](../README.md#目录与构建入口) 执行。

当前交付版本为 0.2.0：原生离线配置编辑器 + 只读 Server API 诊断插件源码。**诊断插件不会执行跟单，配置编辑器保存的 JSON 也不会自动在服务器生效。** 本教程用于先验证 Windows 构建、插件兼容性和实际成交回调，为后续跟单执行接入提供依据。

## 1. 版本与环境

本项目按用户提供的 SDK 开发，实际头文件声明 **Server API 6182**，日期 **2026-09-05**。安装器资源版本为 **5.0.0.6191**。这两个数字不是同一个含义，也不能用客户端“帮助 → 关于”的 Build 代替 Trade Server 支持的 API 版本。

测试 MT5 Trade Server 需要支持插件声明的 API 6182。插件在创建时检查服务器传来的 API 版本；服务器自身也会校验插件兼容性。数值达到下限只是必要检查，真正的兼容结果以加载日志和运行测试为准。当前并未验证先前提及的 Build 6140。不要修改 SDK 版本宏来绕过检查。

准备以下内容：

1. Windows x64 编译电脑。可以使用独立测试服务器，也可以在另一台 Windows 电脑编译后，仅把产物复制到测试服。
2. Visual Studio 2022 / 2026 或相应 Build Tools，安装 **使用 C++ 的桌面开发**、Windows SDK 和 CMake 工具。脚本按当前开发者环境选择：2022 使用 **v143、CMake 3.21+**；2026 使用 **v145、CMake 4.2+**。CMake 组件安装入口见 [Microsoft 官方说明](https://learn.microsoft.com/en-us/cpp/build/cmake-projects-in-visual-studio?view=msvc-170)；VS2026 的生成器与工具集见 [CMake 官方说明](https://cmake.org/cmake/help/latest/generator/Visual%20Studio%2018%202026.html)。
3. 完整仓库源码，例如放到 `C:\mt-plugins`；本插件目录是 `C:\mt-plugins\plugins\trade-copier\mt5`。
4. 用户提供的 `MetaTrader5SDK.exe`，或已经提取的 SDK 目录。若运行安装器，记下真实安装位置，确认其下有 `Include\MT5APIServer.h`。教程不假定安装器使用哪个默认目录。
5. 在 MT5 Administrator 中管理独立测试 Trade Server 插件配置的权限。

源码包不包含专有 SDK；请把现有 SDK 一同带到自己的 Windows 环境。无需向本项目提供服务器密码或客户账号密码。

## 2. 编译并检查产物

推荐从开始菜单打开已安装版本的 **x64 Native Tools Command Prompt for VS**，通过新增的 CMD 启动器编译。以 D 盘为例，逐行执行，每行按一次回车，使用英文双引号：

```cmd
cd /d "D:\mt-plugins\plugins\trade-copier\mt5"
scripts\build-windows.cmd -SdkRoot "D:\SDK\MetaTrader5"
```

这个 CMD 启动器会显式运行 PowerShell，并继承当前 Visual Studio 开发者环境。它不会更改 Windows 的持久执行策略。不要在 CMD 中直接打开 `.ps1`，否则可能只弹出记事本。CMD 的 `cd /d` 会同时切换盘符和目录。

如果你已经打开 **Developer PowerShell for VS 2022/2026**，也可以直接运行 PS1。假设 SDK 根目录为 `C:\SDK\MetaTrader5`，执行：

```powershell
cd C:\mt-plugins\plugins\trade-copier\mt5
.\scripts\build-windows.ps1 -SdkRoot 'C:\SDK\MetaTrader5'
```

请把示例 SDK 路径改成实际路径。也可以把提取后的 SDK 放到仓库根目录的 `vendor\mt5-sdk`（例如 `C:\mt-plugins\vendor\mt5-sdk`，从插件目录访问为 `..\..\..\vendor\mt5-sdk`），这样直接执行脚本即可：

```powershell
.\scripts\build-windows.ps1
```

脚本只在项目内构建和整理文件，不会安装插件、启动 MT5 或修改 Windows 服务。它会：

- 验证 SDK 头文件声明 6182。
- 识别当前 VS 2022/2026 开发者环境，检查 CMake 能力，并使用相应工具集编译 x64 Release，统一使用静态 C++ 运行库 `/MT`。
- 运行核心与配置两组测试；零测试或任一失败都会终止。
- 用 `dumpbin` 检查 DLL 是否导出 `MTServerAbout` 和 `MTServerCreate`。
- 在测试成功后生成带文件哈希的构建清单。

成功产物位于 `C:\mt-plugins\plugins\trade-copier\mt5\build-windows\package-probe`（D 盘示例对应 `D:\mt-plugins\plugins\trade-copier\mt5\build-windows\package-probe`）：

| 文件 | 用途 |
| --- | --- |
| `TradeCopierManager.exe` | Windows 离线配置编辑器 |
| `TradeCopierSdkProbe.dll` | MT5 只读加载与成交回调诊断 |
| `copier.example.json` | 所有关系默认停用的配置样例 |
| `build-manifest.json` | 本次构建时间、SDK 版本、测试结果和产物 SHA256 |
| `windows-test-deployment.md` | 本教程 |
| `editor-readme.md` / `probe-readme.md` | 界面操作与探针参数说明 |

编译通过并不代表 DLL 已经加载或界面已运行。清单中 `mt5_loading_verified` 和 `windows_ui_run_verified` 仍为 `false`，这是有意保留的验证边界。

脚本会检查已有构建缓存。如果之前使用过另一版 VS、另一个安装实例或把源码挪过位置，可指定一个新的构建目录，保留旧文件：

```cmd
scripts\build-windows.cmd -SdkRoot "D:\SDK\MetaTrader5" -BuildDirectory "D:\mt-plugins\plugins\trade-copier\mt5\build-vs2026"
```

这时产物位于所选新目录的 `package-probe`。默认仍为 `build-windows\package-probe`，C 盘/D 盘不影响编译规则。

若先试界面，可不带 SDK 构建：

```powershell
.\scripts\build-windows.ps1 -UiOnly
```

这时产物在 `build-windows\package-ui`，不生成诊断 DLL。

## 3. 试用独立 Windows 配置界面

1. 双击 `TradeCopierManager.exe`，点击“打开 JSON”，选择同目录的 `copier.example.json`。
2. 在规则列表中选择一行，编辑信号源账号、跟单账号、手数模式及参数，点击“更新 / 加入列表”。
3. 用“新增关系”建立 A → X、A → Y、B → X 等多对多关系，每条关系可用不同模式。
4. 用“保存”或“另存为”导出配置，再打开文件核对内容。默认所有关系停用；“启用”只改变草稿。
5. 尝试重复组合、自跟单、循环关系、零参数和不合法 JSON，确认能够提示错误且不丢失原配置。

规则改变源或目标账号时会生成新关系 ID，并自动停用。已有关系只修改模式/参数则保留 ID。保存的是 UTF-8 JSON，支持中文文件路径。界面没有服务器连接、发布或真实交易按钮；当前的 JSON 不能粘贴到其他厂商插件的 Rules 参数中使用。

更多界面验收见产物中的 `editor-readme.md`（源码包内为 `windows\README.md`）。

## 4. 在独立测试 Trade Server 加载诊断 DLL

先在测试服务器上找到 **该 Trade Server 的真实安装根目录**。它不是交易客户端的数据目录，也不是 Windows `System32`。

1. 将 `TradeCopierSdkProbe.dll` 复制到测试 Trade Server 根目录的 `plugins\TradeCopierSdkProbe\` 子目录。SDK 文档说明服务器会扫描 `plugins` 及其子目录。
2. 打开 MT5 Administrator，连接独立测试环境，在插件配置区域添加配置。
3. 名称填写 `TradeCopier SDK Probe`；模块选择 `TradeCopierSdkProbe.dll`，关联服务器选择要观察的 **Trade Server**。Access Server 不支持插件。
4. 初次保留停用。不要勾选“经理配置”；诊断阶段不需要它，也不需要给独立编辑器提供远程管理通道。
5. 确认参数：`WatchLogin` 类型 **String**、值 **0**；`MaxEvents` 类型 **Integer**、值 **100**。`MaxEvents` 允许 0–10000；`WatchLogin = 0` 或 `MaxEvents = 0` 不订阅账户成交事件。详情见 `probe-readme.md`（源码包内为 `server\README.md`）。
6. 勾选“启用”并保存配置。在服务器日志中搜索 `TradeCopierSdkProbe`，核对启动记录中的编译 API、服务器传入 API 和实际 Server Build。

官方文档说明启用配置后插件开始运行，正常启停配置不需要重启服务器。初次列表找不到模块时，先检查目录、服务器选择及日志；不要为了排错直接重启或升级服务器。

## 5. 观察一个测试账户的事件

加载成功后，准备一个仅用于测试的 Hedging/USD 账号：

1. 停用诊断插件并保存。
2. 把 `WatchLogin` 改为该测试账号，`MaxEvents` 保持 100；重新启用。探针在启动时读取参数，未实现运行中热更新。
3. 由测试操作员手动开一笔市场仓位，按该测试品种允许的数量部分平仓，再全部平仓。
4. 再观察测试挂单实际成交、SL/TP 实际平仓以及 Close By。测试条件允许时分别覆盖内部撮合和网关成交路径。
5. 保存对应时间段的日志，核对回调来源、成交 ID、持仓 ID、方向/进出场类型及原始数量字段。

这些操作用于观察事件，**目标账户不会自动产生订单**。同一成交可能在多个回调中出现；日志保留回调来源用于分析，不能按日志行数统计成交数量。Close By 可能带两笔成交，应分别核对。原始 SDK 数量字段不能在未核对单位前直接当成“手”。

达到 `MaxEvents` 限额后停止继续记录成交事件；重新停用/启用才能重置本轮计数。正常停用时不要求看到自定义“停止成功”日志：官方生命周期约束不允许在 `Stop` 内继续访问服务器日志 API。

SDK 的这些接口只用于诊断。正式跟单还需要持久化启用基线、关联真实目标持仓、异步请求确认、恢复核对以及净值时点验证；本阶段加载成功不等于这些功能已经完成。

## 6. 停用、更新与移除

1. 在 Administrator 停用使用本诊断 DLL 的全部配置并保存。一个 DLL 可以有多个配置实例，不能只停用其中一个就假设模块已卸载。
2. 确认服务器已卸载模块后，才替换或移除自己放入的 `TradeCopierSdkProbe.dll`。
3. 更新后核对文件哈希，重新启用对应配置，检查新一轮启动日志。

诊断插件不会平仓、创建订单或修改账户；卸载也不会操作任何仓位。若文件仍被占用，检查是否还有启用的实例，不要强制终止 Trade Server 进程。

## 7. 常见问题与需要回传的结果

| 现象 | 检查事项 |
| --- | --- |
| 找不到 `cmake` / `ctest` / `dumpbin` | 使用 VS 2022/2026 的开发者窗口，确认安装 C++、Windows SDK 和 CMake 组件 |
| `.ps1` 在记事本打开 | 当前是 CMD，使用 `scripts\build-windows.cmd` 启动器，并用英文双引号包住路径 |
| VS2026 生成器不可用 | CMake 需要 4.2+；更新 VS 的 C++ CMake 组件或使用较新版 CMake |
| 找不到 SDK 头文件 | `-SdkRoot` 必须是含有 `Include` 的目录，不是安装器文件或 `Include` 本身 |
| SDK 不是 6182 | 使用本次确认的 SDK；更换 SDK 后需要重新审查，不能改版本宏 |
| Visual Studio 生成器、平台或工具集冲突 | 保留构建日志，将本项目旧 `build-windows` 改名后重新生成 |
| DLL 不在模块列表 | 检查目标 Trade Server 的插件目录、x64 位数、导出检查文件以及服务器加载日志 |
| 创建失败 / API 不支持 | 保存实际版本和服务器错误；使用支持该 SDK 的测试环境，不能靠改宏解决 |
| 没有账户事件日志 | 确认 `WatchLogin` 非零、已重新启用、账号在该 Trade Server、未达到记录上限 |
| 修改 JSON 后没有跟单 | 当前编辑器只保存草稿，诊断 DLL 不加载跟单规则，也不执行交易 |

首次验证后提供以下文本结果即可，无需密码：

- 构建失败时：`build-windows\build.log` 的错误部分。
- 构建成功时：`build-manifest.json`，以及界面打开/保存是否正常。
- 加载验证时：带 `TradeCopierSdkProbe` 的启动日志或拒载错误，以及少量测试成交回调日志。

## 依据与当前验证状态

SDK 内的 `Server API → Configuration of Plugins` 说明插件目录、Administrator 配置与启停方式；`Interaction with Servers` 说明位数和 API 支持要求；`IMTServerPlugin::Stop` 说明宿主在停止前自动退订。已提取的对应页面为：

- [插件配置](../../../../vendor/mt5-sdk/Docs/html/serverapi_configure_plugin.htm)
- [服务器与插件交互](../../../../vendor/mt5-sdk/Docs/html/serverapi_interaction.htm)
- [Stop 生命周期](../../../../vendor/mt5-sdk/Docs/html/imtserverplugin_stop.htm)

这些链接在保留 SDK 的完整项目内有效；不含 SDK 的源码包可在用户安装的 SDK CHM 中阅读同名章节。

macOS 上已完成 CMake 构建、严格编译与两组业务/配置测试。用户随后回传的 VS2026 截图确认生成 EXE/DLL，两组 CTest 全部通过且打包成功。**原生界面运行及 MT5 加载仍待验证。** 证据边界见 [Windows 构建记录](windows-build-validation.md)。
