# 交易账户 Status 批量更新

用于开发 MT4、MT5 交易账户 `status` 批量更新功能。

## 目录结构

```text
plugins/account-status-updater/
├── mt4/       # MT4 实现
├── mt5/       # MT5 实现
└── README.md
```

当前已初始化目录，尚未实现账户读取或更新逻辑。

本目录是独立插件项目，尚未加入根 CMake 构建。后续源码、配置、测试和文档均在此目录维护；平台 SDK 由仓库根 `vendor/` 共享，按实际接口需要引用。

后续开发需明确 `status` 对应的账户字段、目标值、账户列表输入方式，以及各平台使用的 SDK 和连接方式。
