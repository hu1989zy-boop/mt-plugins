# 平仓订单概率分配

独立于实时跟单的 Server API 插件：源账号 A 完整平仓后，按盈利/亏损类别独立抽样，向同服账号 B 写入入选交易的完整历史。例如 `LossPercent=30`、`ProfitPercent=70` 分别表示亏损单 30%、盈利单 70% 的入选概率。入选后按 1:1 写入，不将手数或盈亏金额乘以该概率。

已实现 MT4 / MT5 源码、公共概率引擎、持久账本、Windows 构建脚本和本地测试。**不处理部分平仓；保留 SL/TP；目标 Comment 为空。** 默认 `Armed=0`，需在目标服务器完成 Demo 验收后确认实际历史和资金效果。

## 实现范围

- MT4：独立 `MT4ClosedTradeAllocator.dll`，通过 `OrdersAdd` 写入完整已平仓记录，回查字段与一次净资金变化。直接历史写入的实际资金效果未完成 Demo 验证，默认额外关闭 `DirectClosedAddVerified`。
- MT5：独立 `ClosedTradeAllocatorMT5.dll`，成交事件触发后台定向查询、低频增量补扫和限速写入；支持 hedging 账号中一次完整开平仓，写入两笔历史订单与两笔成交；核对完整历史后执行一次持久化意图的历史余额校正，再验证资金变化。
- 共享核心：净盈亏分类、币种精度、独立概率、启用/暂停边界、一次抽样、源快照冻结、去重、排他账本和中断回查。未决写入阻止继续改变同一目标资金。
- 查询：服务器参数配置与日志，以及自动导出的 TSV 状态报表。首版无独立图形管理器。

两平台均要求同服同币种及专用接收账号，排除部分平仓和不支持的交易结构。源 A 不修改；目标历史保留源交易时间与 SL/TP，来源关联使用非备注 API 标记及账本。结果不明时只读核对，不重新抽样或盲目重发。

## 构建与验证

可在支持 C++17 的本机运行公共核心与平台模型测试：

```sh
cmake -S plugins/closed-trade-allocator -B build/closed-trade-allocator -DBUILD_TESTING=ON
cmake --build build/closed-trade-allocator --parallel
ctest --test-dir build/closed-trade-allocator --output-on-failure
```

以上默认构建不生成服务器 DLL。Windows DLL 分别使用 MSVC Win32（MT4）和 MSVC x64（MT5），需要对应授权 SDK，详见 [Windows 构建与部署](docs/windows-deployment.md)。实际目标 Windows 构建、Server API 加载和 Demo 资金验收状态见 [实现与验证记录](docs/implementation-validation.md)，本地测试不能替代实机验收。

| 文档 | 内容 |
| --- | --- |
| [需求与验收标准](docs/requirements.md) | 已确认业务规则，包括部分平仓排除、SL/TP 和空备注 |
| [Windows 构建与部署](docs/windows-deployment.md) | 架构、SDK、参数、写入流程、状态及恢复 |
| [实现与验证记录](docs/implementation-validation.md) | 已运行测试与尚未验证的服务器行为 |
| [SDK 接口核查](docs/sdk-feasibility.md) | 历史库写入与资金接口语义 |
| [MT4 实现说明](mt4/README.md) | MT4 字段、限制与 API 5 适配 |
| [MT5 实现说明](mt5/README.md) | MT5 历史关联与余额核对 |

```text
plugins/closed-trade-allocator/
├── CMakeLists.txt
├── shared/                 # 公共引擎、持久账本、测试
├── mt4/                    # MT4 Server API DLL 与平台模型测试
├── mt5/                    # MT5 Server API DLL 与平台模型测试
├── docs/                   # 需求、部署、SDK 与验证记录
└── README.md
```

SDK 从仓库根 `vendor/` 引用，不复制分发，也不依赖 `trade-copier` 的内部代码。每条关系始终保留原账本；换路径、删除账本或回滚旧备份不能用作失败重试。
