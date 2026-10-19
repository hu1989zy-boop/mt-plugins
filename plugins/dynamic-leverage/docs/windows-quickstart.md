# 动态杠杆 Windows 构建与部署

> 2026-10-14：账户杠杆写入已停用，当前正式 DLL 会拒绝启动旧执行流程。新需求是新仓使用新杠杆、旧仓保持原杠杆。以下是旧版构建与运行说明，其中部署启用和账户杠杆变化的步骤已失效。当前状态见 [新仓保证金实现](new-position-leverage.md)。

本插件运行在券商 MT4 / MT5 **Trade Server**，不是终端 EA。管理页面维护规则，Server API DLL 读取当前账户与持仓，实际更新账户杠杆。两套平台在独立进程和配置文件中运行，不会合并账号或仓位。

## 从源码构建

Windows 安装 Visual Studio 2022 C++ 桌面开发工作负载（含 x86 / x64 工具链与 Windows SDK）及 CMake 3.21+。将源码压缩包解压到仓库根目录，得到 `plugins\dynamic-leverage\`；源码包不含商业 SDK，另将原始 MT4 Server API 5 放入 `vendor\mt4-sdk`，原始 MT5 Server API 6182 放入 `vendor\mt5-sdk`。

在 PowerShell / 命令提示符从仓库根运行：

```bat
plugins\dynamic-leverage\scripts\build-windows.cmd
```

默认分别创建 Win32 / x64 构建目录并构建两套插件。只编译一端或指定 SDK：

```bat
plugins\dynamic-leverage\scripts\build-windows.cmd -Platform mt4 -Mt4SdkRoot "D:\SDK\mt4-sdk"
plugins\dynamic-leverage\scripts\build-windows.cmd -Platform mt5 -Mt5SdkRoot "D:\SDK\mt5-sdk"
```

可用 `-BuildDirectory "D:\Build\DynamicLeverage"` 指定输出根目录，`-Generator "Visual Studio 17 2022"` 指定生成器；默认 Release、静态 CRT。构建失败或任一测试失败时不产生新的部署包，旧包仍保留，不能将其误认为本次产物。

脚本完成 CMake 编译、CTest、实际 Windows DLL 加载 / 卸载与导出符号检查，以及示例规则的原生校验。成功后生成 `build-windows\package-<时间>-<标识>.zip`，包含：

```text
bin/mt4/MT4DynamicLeverage.dll
bin/mt4/DynamicLeverageRulesCheck.exe
bin/mt5/DynamicLeverageMT5.dll
bin/mt5/DynamicLeverageRulesCheck.exe
management/                       本地管理页面和服务
config/                           默认禁用的示例
manifest.json                     构建信息及逐文件 SHA-256
README.md                         本说明
docs/                             验收说明
```

只选一个平台时仅包含对应的 `bin` 子目录。日志保存在 `build-windows\build-<时间>-<标识>.log`；CTest 的具体失败日志在对应平台构建目录的 `Testing\Temporary`。这些检查不调用真实 Server API，不能代替服务器装载与交易行为验收。

## 部署与配置

将成功构建的部署包解压到固定本机目录，例如 `C:\DynamicLeverage`。不要覆盖已有的 `runtime` 文件，新的包不携带真实运行配置。

1. 目标服务器安装 Python 3.10+，双击 `management\start-windows.cmd`。浏览器访问 `http://127.0.0.1:8765`。服务只允许本机访问，可通过远程桌面进入服务器使用。
2. 在 UI 选择平台，新增组别 / 账户规则、按箭头排序、设置手数区间，保存后生成 `runtime\mt4.rules.json` / `runtime\mt5.rules.json`。初次保持总开关关闭。
3. 使用券商现有的服务器插件安装流程放置对应 DLL：MT4 用 32 位 `MT4DynamicLeverage.dll`，MT5 用 64 位 `DynamicLeverageMT5.dll`。不要将其复制进终端 `MQL4` / `MQL5` 目录。
4. 在 Administrator 插件参数中设置 `Enabled=0`，`Accounts` 留空，`RulesFile` 填对应 JSON 的**绝对路径**。MT4 原生参数路径最多 127 字节，建议使用短的 ASCII 路径；MT5 支持 Unicode 路径。`Tiers` / `PollIntervalMs` 在文件模式下以 JSON 为准。
5. 确认交易服务器服务账户可读取 JSON、管理服务运行账户可写入其目录。两个进程必须指向同一文件；页面显示“保存成功”只表示文件已写入。
6. 在专用 Demo 账号上确认规则后，启用页面总开关并保存，再设置插件参数 `Enabled=1`。两个开关都开启才允许调整。不要在尚未验证的真实账户上直接启用全部组别。

目录与示例路径可以复制；MT4 与 MT5 文件中的 `platform` 必须与 DLL 一致。页面排序就是大规则优先级，第一个启用且命中的规则生效；组别规则也以每个账户自身持仓计算。

## 不连接交易服务器的原生检查

检查工具复用 DLL 的 C++ 解析与匹配核心，运行时不读真实账户、不写杠杆：

```bat
bin\mt5\DynamicLeverageRulesCheck.exe --platform mt5 --file "C:\DynamicLeverage\runtime\mt5.rules.json"
bin\mt5\DynamicLeverageRulesCheck.exe --platform mt5 --file "C:\DynamicLeverage\runtime\mt5.rules.json" --login 10001 --group "real\VIP" --lots 5.00000001
```

输出单个 JSON 对象，成功退出 0，非法参数 / 文件退出非 0。可选的 `preview` 显示规则 ID 与目标杠杆；若文件总开关关闭则不匹配。工具仅验证 JSON 规则，不知道 Administrator 的 `Enabled` 状态，也不能证明服务器已读取该版本。

## 验证与故障处理

在日志中检查规则加载 / 挂起、account、rule、lots 与 leverage 原值→目标值。先检查空仓、跨越 2 / 5 手、部分平仓、全部平仓，再检查两条规则同时命中后的上移和停用、新开户、迁组、服务重启。详细场景见 `docs/demo-validation.md`。

- DLL 加载失败：先看构建时的 DLL 加载测试及目标 Windows 错误码；确认使用了正确位数、SDK / server build，且未部署旧包。
- 文件非法 / 缺失 / 平台不符：插件暂停新写入；修复文件后自动恢复，不回退旧规则。
- 无调整：核对两个总开关、命中顺序、账户实际组别和文件读取权限；杠杆已等于目标时不会重复写。
- 停用或移出规则：停止管理并保留最后杠杆，不自动恢复启用前手工值。已进入 SDK 的写入可能先完成一次。

SDK 更新不是原子 compare-and-swap；持仓、迁组与管理员改动之间仍有竞态窗口。实际保证金是否采用账户杠杆由品种和组设置决定。完成目标服务器 Demo 验收后再确定上线配置。
