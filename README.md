# MT 插件开发仓库

本仓库用于开发各类 MetaTrader 插件，按业务用途将每个插件放在 `plugins/<插件名>/`，再用 `mt4/`、`mt5/` 区分平台。各平台自行维护源码、配置、测试、文档和构建脚本。

| 插件 | 目录 | 当前状态 |
| --- | --- | --- |
| 交易跟单 | [plugins/trade-copier](plugins/trade-copier/README.md) | MT5 0.7.4；MT4 0.2.0 Dealer 执行测试源码，待 Windows / Demo 验收；跨平台路由尚未实现 |
| 账户状态批量更新 | [plugins/account-status-updater](plugins/account-status-updater/README.md) | 已建立 MT4 / MT5 目录，尚无读取或更新实现 |
| 平仓订单概率分配 | [plugins/closed-trade-allocator](plugins/closed-trade-allocator/README.md) | 0.1.0：独立 MT4 / MT5 Server API 源码、持久化核心及测试；复制 SL/TP、备注留空；待 Windows / Demo 验收 |
| 动态杠杆 | [plugins/dynamic-leverage](plugins/dynamic-leverage/README.md) | 组别 / 账户多规则与 Web 管理；正在改为新仓独立保证金，旧账户调杠杆执行已停用 |

## 目录结构

```text
mt-plugins/
├── plugins/
│   ├── trade-copier/
│   │   ├── shared/        # 两端共用的业务逻辑、配置和账本
│   │   ├── mt5/           # 现有 MT5 实现，独立源码、测试、文档与构建
│   │   ├── mt4/           # MT4 独立适配、管理器、测试与构建
│   │   ├── scripts/       # 兼容入口，转发到 mt5/scripts
│   │   ├── CMakeLists.txt # 跟单项目的平台聚合构建
│   │   └── README.md
│   ├── account-status-updater/
│   │   ├── mt4/
│   │   ├── mt5/
│   │   └── README.md
│   ├── closed-trade-allocator/
│   │   ├── docs/          # 平仓后按盈亏概率分配的业务需求
│   │   ├── shared/        # 独立概率核心、持久化账本及恢复测试
│   │   ├── mt4/
│   │   ├── mt5/
│   │   └── README.md
│   └── dynamic-leverage/
│       ├── shared/        # 账户持仓分档规则、配置与同步核心
│       ├── mt4/           # MT4 服务端适配、测试及独立构建
│       ├── mt5/           # MT5 服务端适配、测试及独立构建
│       ├── management/    # 本地管理 API 与 Web 界面
│       ├── config/        # 插件参数及规则 JSON 示例
│       ├── docs/          # Demo 验收清单
│       └── README.md
├── vendor/                # 本机共享 SDK，忽略提交
├── scripts/               # 仓库级兼容入口
├── CMakeLists.txt         # C++ 插件聚合构建
└── README.md
```

以业务插件为一级分类，再按 MT4、MT5 分开。跟单项目的通用业务核心归入 `shared/`，平台适配、Windows 管理器、配置和构建脚本分别归入 `mt4/`、`mt5/`。跨平台服务、Java 后端及 Web 前端也归入 `plugins/trade-copier/`，在实际实现时建立相应模块。详细目录见[跟单项目说明](plugins/trade-copier/README.md)。

SDK 统一放在仓库根目录的 `vendor/mt5-sdk`、`vendor/mt4-sdk` 等路径，由需要它们的插件通过构建参数引用。SDK、实际服务器配置和运行账本不提交到 Git。

平仓订单概率分配有独立的 [构建与 Demo 验收说明](plugins/closed-trade-allocator/docs/windows-deployment.md)。它在源完整平仓后写入目标历史，使用独立账本和 API 标记，不依赖跟单内部模块；不处理部分平仓。

## 构建与测试

从仓库根目录构建当前可用的 C++ 插件：

```sh
cmake -S . -B build/workspace -DCMAKE_BUILD_TYPE=Release
cmake --build build/workspace --config Release
ctest --test-dir build/workspace -C Release --output-on-failure --no-tests=error
```

也可以单独构建 MT5 跟单：

```sh
cmake -S plugins/trade-copier/mt5 -B plugins/trade-copier/mt5/build -DCMAKE_BUILD_TYPE=Release
cmake --build plugins/trade-copier/mt5/build --config Release
ctest --test-dir plugins/trade-copier/mt5/build -C Release --output-on-failure --no-tests=error
```

跟单项目本身也提供聚合入口：`-S plugins/trade-copier -B plugins/trade-copier/build/platforms`。仓库聚合构建可通过 `-DMT_PLUGINS_BUILD_TRADE_COPIER=OFF` 排除跟单项目；平台聚合开关为 `TRADECOPIER_BUILD_MT5`、`TRADECOPIER_BUILD_MT4`，均默认开启。SDK 探针和交易执行器默认不构建，Windows 管理器在 Windows 上默认构建。两个 DLL 需分别使用 MT5 x64、MT4 Win32 构建目录。账户状态更新目录目前不参加构建。

平仓分配核心及两个平台的数据模型测试默认加入聚合构建，可通过 `-DMT_PLUGINS_BUILD_CLOSED_TRADE_ALLOCATOR=OFF` 排除。实际 Server DLL 默认关闭，用该插件各平台脚本分别构建 MT4 Win32 与 MT5 x64，不在同一个构建目录混用两种架构。

动态杠杆插件在 `plugins/dynamic-leverage/` 独立管理，默认加入公共规则与平台模型测试；可通过 `-DMT_PLUGINS_BUILD_DYNAMIC_LEVERAGE=OFF` 排除。其 MT4 / MT5 Server DLL 使用各自构建入口，见[动态杠杆说明](plugins/dynamic-leverage/README.md)。

在 Windows 的 Visual Studio x64 开发者命令行中，从仓库根目录构建、测试并打包跟单执行器：

```bat
plugins\trade-copier\mt5\scripts\build-windows.cmd -Executor -SdkRoot "D:\SDK\MetaTrader5"
```

省略 `-SdkRoot` 时使用仓库根目录的 `vendor\mt5-sdk`。默认产物位于 `plugins\trade-copier\mt5\build-windows\package-executor`。SDK 路径也可通过 CMake 的 `-DMT5_SERVER_SDK_DIR=...` 指定。打包脚本直接构建 MT5；手动聚合构建的产物路径会包含额外的平台子目录。

原来的根入口 `scripts\build-windows.*` 和 `plugins\trade-copier\scripts\build-windows.*` 保留为转发入口，参数及失败状态传给 MT5 脚本。所有脚本入口的默认构建目录及相对 `-BuildDirectory` 都以 `plugins/trade-copier/mt5` 为基准；绝对构建路径保持原义。

目录迁移前的 `build/`、`build-windows/`、`out/` 和本地运行数据没有移动。CMake 缓存记录旧源码路径，请使用上述新构建目录，勿直接运行旧目录中的程序作为本次源码的验证结果。历史版本文档已归入插件目录；部署与交易行为见[跟单项目说明](plugins/trade-copier/README.md)。

## 新增插件约定

新插件与关键执行逻辑变更遵循 [AGENTS.md](AGENTS.md) 和 [插件开发流程](docs/plugin-development.md)：先明确业务口径与 SDK 依据，再验证关键机制、实现并分别记录本地测试和原生验收状态。可复用 [技术分析模板](docs/templates/plugin-analysis.md)；已有插件分析文档直接维护，不重复建档。

1. 在 `plugins/` 下使用独立业务名称建立目录，先添加 README 说明用途、支持平台和实现状态。
2. 插件自己的源码、配置、测试、工具和文档按平台放入 `mt4/`、`mt5/`；公共协议或管理端在实际需要时建立独立模块。
3. C++ 插件提供独立 `CMakeLists.txt`，再通过根 CMake 的显式开关加入聚合构建；其他技术栈维护自己的构建入口。
4. 只有已被多个插件使用的代码才提取为仓库级公共模块，避免插件直接依赖其他插件的内部目录。
