# Trade Copier 跟单项目

按交易平台分开维护源码、配置、测试、文档及构建脚本，MT4 和 MT5 各自独立构建。

| 平台 | 目录 | 当前状态 |
| --- | --- | --- |
| MT5 | [mt5/](mt5/README.md) | 0.7.4：合并过期记录清理、优化账本差分处理并补充清理与写入字节指标；保留只读跟单、正反向及管理账号组通配符支持 |
| MT4 | [mt4/](mt4/README.md) | 0.2.0：指定 Dealer 锁定/确认请求，含 Windows 管理器；待 Windows / Demo 验收 |

```text
trade-copier/
├── shared/           # 两端共用的业务核心、配置、账本与管理协议
├── mt5/
│   ├── server/        # MT5 Server API 接入
│   ├── windows/       # MT5 Windows 管理器
│   ├── management/    # 本机管理通信
│   ├── config/
│   ├── tests/
│   ├── tools/
│   ├── scripts/       # MT5 构建及打包
│   ├── docs/
│   ├── CMakeLists.txt
│   └── README.md
├── mt4/
│   ├── core/          # MT4 订单模型与执行适配
│   ├── server/        # MT4 Server API 接入
│   ├── windows/       # MT4 Windows 管理器
│   ├── management/    # MT4 本机管理管道
│   ├── tests/
│   ├── config/
│   ├── scripts/
│   ├── tools/
│   ├── docs/
│   ├── CMakeLists.txt
│   └── README.md
├── scripts/          # 旧命令的兼容入口，转发到 MT5
├── CMakeLists.txt     # 平台聚合构建
└── README.md
```

原 `mt5/core/` 中两端一致的逻辑已提取到 `shared/core/`，两平台使用各自的编译目标和配置校验，MT4 专属订单适配在 `mt4/core/`。原 MT5 的 SDK 调用保持独立。跨平台路由服务、Java 管理后端及 Web 前端如需实现，也作为本项目下的独立模块。MT4→MT5、MT5→MT4 跟单目前尚未实现。

## 构建

以下命令从仓库根目录执行，独立编译 MT5：

```sh
cmake -S plugins/trade-copier/mt5 -B plugins/trade-copier/mt5/build -DCMAKE_BUILD_TYPE=Release
cmake --build plugins/trade-copier/mt5/build --config Release
ctest --test-dir plugins/trade-copier/mt5/build -C Release --output-on-failure --no-tests=error
```

构建整个跟单项目时可使用 `-S plugins/trade-copier -B plugins/trade-copier/build/platforms`。平台聚合开关 `TRADECOPIER_BUILD_MT5`、`TRADECOPIER_BUILD_MT4` 默认开启。两个 Server DLL 默认均不构建；Windows DLL 分别使用 MT5 x64 和 MT4 Win32 独立构建目录。MT5 原有的 `TRADECOPIER_BUILD_*` 选项保持有效；MT4 使用 `MT4_TRADECOPIER_BUILD_*`，具体见 [MT4 构建说明](mt4/README.md)。

Windows 的 Visual Studio x64 开发者命令行中，编译、测试并打包 MT5 执行器：

```bat
plugins\trade-copier\mt5\scripts\build-windows.cmd -Executor -SdkRoot "D:\SDK\MetaTrader5"
```

SDK 默认使用仓库根的 `vendor/mt5-sdk`，也可以用 `-SdkRoot` 或 CMake 的 `MT5_SERVER_SDK_DIR` 指定。默认打包目录为 `plugins/trade-copier/mt5/build-windows/package-executor`。

仓库根 `scripts/build-windows.*` 和本目录的 `scripts/build-windows.*` 保留兼容转发，均构建 MT5，参数与退出码向上传递。所有脚本入口的默认构建目录及相对 `-BuildDirectory` 均以 `plugins/trade-copier/mt5` 为基准；绝对路径保持原义。聚合 CMake 构建有额外的平台子目录，Windows 打包脚本则直接构建 MT5。

迁移前的构建产物与运行数据未移动。旧 CMake 缓存记录了原源码路径，请使用新构建目录。MT5 的版本记录与部署细节见 [MT5 说明](mt5/README.md)。

本次三个入口的重新编译、测试结果及验证范围见 [目录分离验证记录](mt5/docs/directory-layout-validation.md)。
