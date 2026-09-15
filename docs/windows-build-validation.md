# Windows 构建验证记录

2026-09-11，用户回传 Windows 开发者命令行截图。以下结果依据用户截图，未通过远程工具读取测试服务器，也未取得二进制文件及其哈希。

## 截图确认的结果

- 使用 VS2026 开发者环境；输出显示 MSBuild `18.10.1-1.26427.6+3cd27c13e`。
- 源码目录为 `D:\MT5Copier`，构建目录为 `D:\MT5Copier\build-windows`。
- 生成 `server\Release\TradeCopierSdkProbe.dll`。
- 生成 `windows\Release\TradeCopierManager.exe`。
- `tradecopier_core_tests` 与 `tradecopier_config_tests` 均通过，CTest 显示 `100% tests passed, 0 tests failed out of 2`。
- 构建脚本输出 `Build and tests passed`，产物目录为 `D:\MT5Copier\build-windows\package-probe`。

按已发脚本的执行顺序，成功提示出现于测试、导出符号检查与打包之后；具体产物 SHA256 和 SDK 头文件哈希仍应以用户机器的 `build-manifest.json` 为准。截图没有展示完整编译器版本、CMake 版本或构建清单，不能从中补造这些字段。

## C4310 警告

警告位置为 `tests/config_tests.cpp` 中构造非法 UTF-8 的高字节转换，例如 `char(0xc0)`。这些值有意用于验证拒绝超长编码、Unicode 代理区、越界码点和截断序列，代码只属于测试程序。它们未阻断本次构建或测试，无需为继续试用界面而重复编译。

## 尚待验证

- 原生 Windows 界面启动、显示及配置打开/保存。
- 诊断 DLL 在 MT5 Trade Server 中加载、版本与事件日志、卸载。
- 本次产物的文件哈希与加载目标对应关系。

当前诊断 DLL 不执行跟单。真实跟单执行、净值查询、状态持久化与重启恢复仍未实现；编译成功不代表这些功能已完成。
