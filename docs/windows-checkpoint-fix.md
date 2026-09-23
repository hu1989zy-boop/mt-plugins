# 0.4.7：Windows 账本检查点替换修复

## 本次失败

用户的 0.4.6 Windows 构建已进入 CTest，五组中四组通过，执行测试报：

```text
FAILED: ActiveConfigRealJournal: journal checkpoint publish failed
```

这是测试运行中的账本文件替换失败，构建脚本因此返回退出码 8。失败发生在测试临时账本，尚未运行 10,000 条专项测试，不能据此判断为容量不足。旧错误消息没有 Windows 错误码，无法仅从截图区分具体的文件占用、权限或文件系统错误。

代码检查发现旧实现调用 `MoveFileExW` 时仍持有主文件和临时文件句柄。0.4.7 去除对这种替换方式的依赖，并补充可诊断的 Win32 错误信息。Windows 打开文件的共享限制一直持续到句柄关闭；`OPEN_EXISTING` 在文件丢失时会失败而不是新建空文件，参见 [Microsoft CreateFileW 文档](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew)。

## 修正流程

1. 持续持有独立 `.lock` 文件的独占写租约。
2. 在主账本同目录创建 `.checkpoint.tmp`，写入并刷盘完整新状态，记录文件标识。
3. 关闭临时文件和旧主文件的数据句柄，保留 `.lock` 句柄。
4. 使用 `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)` 替换主文件，不预先删除或截断主文件。失败返回值及错误信息处理依据 [Microsoft MoveFileExW 文档](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-movefileexw)。
5. 用 `OPEN_EXISTING` 重新打开主文件，校验文件标识、全部字节和文件尾，再定位到末尾供后续追加。
6. 全部成功后才向引擎报告保存完成；关闭、替换、重新打开或校验失败都会让当前账本实例拒绝后续写入。

替换失败保留旧主文件和临时证据；替换成功但重新打开/校验失败时也不会冒充保存成功。仍不自动加载临时文件、回退旧归档或重发交易。Windows 文件操作失败消息现在包含操作阶段和 `(Windows 数字)`，例如外部程序阻止替换时的 5 / 32。

10,000 条容量、schema 4、64 MiB 检查点阈值、只保留在管和异常记录的规则均保持不变。该修正未宣称已完成 NTFS 断电恢复或实际服务器性能验收。

## 重新构建

使用 `out/mt5-trade-copier-0.4.7-windows-checkpoint-source.zip`，解压到新的目录。在 **x64 Native Tools Command Prompt** 中进入解压后的 `mt-plugins`：

```bat
cd /d D:\work\mt5-trade-copier-0.4.7-windows-checkpoint-source\mt-plugins
scripts\build-windows.cmd -Executor -SdkRoot "D:\SDK\MT5ServerAPI" -BuildDirectory build-windows-0.4.7
```

`D:\SDK\MT5ServerAPI` 是示例，换成本次 0.4.6 构建实际使用的 Server API 6182 SDK 根目录，内含 `Include\MT5APIServer.h`。源码包不含 SDK。新构建目录避免混用旧 CMake 缓存和产物。

验收输出应为 **五组测试全部 Passed，0 failed，且没有 Skipped**。脚本通过后才会生成部署包。不要跳过 `tradecopier_execution_tests` 使用失败构建的 DLL。

若仍失败，请保留新的完整错误消息，以及：

```text
build-windows-0.4.7\build.log
build-windows-0.4.7\Testing\Temporary\LastTest.log
```

常规测试通过后，可在同一 Windows 上验证 10,000 条容量：

```bat
build-windows-0.4.7\Release\tradecopier_execution_tests.exe --capacity-soak
```

专项只使用模拟交易端口和临时文件；验证 10,000 条在管映射、部分平仓、重启和全部平仓，不连接 MT5 或操作测试账号。Windows 构建脚本不会自动运行该长测试。

## 部署与验证边界

停用插件后备份现有程序、配置和账本；同步更新构建包中的 `TradeCopierExecutor.dll`、`TradeCopierManager.exe`、`TradeCopierJournal.exe`。保留真实 `executor-two-to-one.journal`、`.lock` 及可能存在的临时证据，不通过删除账本恢复运行。启动日志和管理界面应显示 0.4.7。

本次本地 ASan / UBSan 四组回归通过；Windows EXE 交叉编译及 Server API 6182 辅助语法检查通过。新增了重复替换后追加/重载、中文路径，以及 Windows 外部句柄阻止替换时的错误码、旧文件保留、持续独占和失败后拒绝写入测试。**Windows 专用测试已编译，仍需用户本机执行确认；尚未部署 Demo。** 完整记录见 [构建验证记录](windows-build-validation.md)。
