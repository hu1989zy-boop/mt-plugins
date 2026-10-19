# MT4 逐单保证金测试组件

本次实现的是可移植的订单转换组件，以及使用原始 Server API 5 类型编译的 `probe_sdk_order_margin()` 包装。**尚未接入 `plugin.cpp` 的交易回调；现有 DLL 启动保护仍会拒绝旧的账户杠杆执行路径。编译该 DLL 不会自动启用本组件。** 当前没有 Windows / MT4 Demo 环境，未声称原生编译、服务器持久化、成交前保证金检查已通过。

## 已实现的计算与记录

`transform_order_margin()` 使用明确的开仓前账户总手数，加上该新订单手数，调用共享 `quote_opening()`。例如旧仓 1.5 手，再开 1 手，整笔新仓按 2.5 手所在档位 1:500 计算，旧仓记录不变。

```text
新订单比率 = 订单原始 margin_rate × 固定账户基础杠杆 / 该新订单目标杠杆
```

固定基础 1:1000、目标 1:500、原始比率 1.2 时，写入 2.4；原始比率为 1 时写入 2。不会把换算率一律当作 1，也不会修改账户杠杆。观察到账户杠杆与调用方保存的基础值不同，或目标杠杆高于该基础值，组件会拒绝处理，不自动改账户，也不静默降低规则目标。

只占用全部为零的 `api_data[4]`：第 0 格为 `DLK1` 版本标记，第 1 格为锁定目标杠杆，第 2、3 格用 `memcpy` 保存原始 double 比率。重复回调从原始比率重建结果，不重复乘倍数。外来数据不会覆盖。标记不代替服务器持久化证明，也不是校验和；上线前需确认与其他插件的字段分配互不冲突。

`OPEN_NEW` 且状态为普通活动仓位才允许新选档。已有本组件标记的余单、恢复或隔夜重开记录，使用保存的目标杠杆；未标记的这类旧记录不变。未验证来源的 `OPEN_API` 不新选档。挂单、历史、余额、信用记录不处理。配置关闭或改规则不重定价已有标记。

SDK 包装另外检查账号、组别、品种及账户杠杆身份；初期仅支持 `MARGIN_CALC_FOREX`、`MARGIN_CALC_CFDLEVERAGE` 且无固定初始/维持保证金覆盖的探针。对冲抵扣、组别特殊保证金、其他插件覆盖及全部执行入口尚未完成实服验收。

## Windows 编译与本地测试

安装 Visual Studio 2022 C++ 桌面开发工具与 CMake 3.21+。在仓库根目录执行；SDK 路径替换为本机原始 MT4 Server API 5 目录：

```powershell
cmake -S plugins/dynamic-leverage/mt4 -B plugins/dynamic-leverage/mt4/build-order-margin -G "Visual Studio 17 2022" -A Win32 -DDYNAMIC_LEVERAGE_MT4_BUILD_SERVER=ON -DMT4_SERVER_SDK_DIR="D:/SDK/mt4-sdk" -DBUILD_TESTING=ON
cmake --build plugins/dynamic-leverage/mt4/build-order-margin --config Release
ctest --test-dir plugins/dynamic-leverage/mt4/build-order-margin -C Release --output-on-failure
```

新增测试目标是 `mt4_dynamic_leverage_order_margin_tests`；SDK 包装 `order_margin.cpp` 随 `MT4DynamicLeverage` 目标编译。当前可移植检查覆盖比率换算、重复回调、继承余单、规则变化、外来字段、损坏标记、非有限数、溢出以及输入身份。可移植测试通过不等于 DLL 实服验证通过。

## 之后的 Demo 回调探针接入

这些是独立 Demo 插件的接入步骤，当前源码没有自动执行这些步骤，也没有自动移除生产启动保护。

1. 固定一个 Demo 账号和测试品种；保存基础杠杆、符号/组别保证金设置、订单原始比率。使用无对冲、无固定保证金覆盖的简单场景，账户基础杠杆保持不变。
2. 先记录 `MtSrvTradeTransaction`、`MtSrvTradeRequestFilter`、`MtSrvTradesAddRequest`、`MtSrvTradesAddExt`、`MtSrvTradesUpdate`、`MtSrvTradePendingsApply` 的先后、请求号/票号、mode、手数、比率；确认同一成交调用了哪些接口。不要直接在所有新增回调都当作一笔新成交累计手数。
3. 在确定的唯一新成交回调中构造 `OrderMarginContext`，调用 `probe_sdk_order_margin()`。传入的 `before_volume` 必须不包含当前新单；若 SDK 快照已包含它，核对票号后扣除恰好一次。不要从过期 worker 快照推算。`observed_account_leverage` 来自该回调的 `UserInfo`，`fixed_account_leverage` 是预先保存的基线。
4. 先用原始比率 1、基础 1000、目标 500，预期新单比率 2。分别核对回调返回后 `OrdersGet` 的字段、账户 margin、终端显示，随后在下一 tick、重登、重启及隔夜后重复核对。字段写入成功但账户保证金未变，仍属验收失败，需要定位正确生效时点或缓存刷新机制。
5. 验证旧仓 1.5 手加新仓 1 手。若合约为每手 100,000、保证金币种与账户币种一致、无其他保证金调整，旧仓保持 150，新仓需要 200，总计 350。不能把全部 2.5 手改为 1:500。再用原始比率不为 1 的跨币种场景验证因子保留。
6. 验证部分平仓、CloseBy 余单、恢复、隔夜重开及修改 SL/TP。余单应继承原目标及原始比率；未继承标记不能用当前档位补写。重启后的重复回调仍不得重复乘倍数。真实新增挂单应在触发成交时选档，不能在挂单创建时提前锁档。
7. 在可用保证金足够按基础杠杆开仓、但不足按目标杠杆开仓的边界测试拒单。`TradesMarginCheck` 没有候选逐单比率参数；必须验证并实现对应的成交前检查和同账号并发控制。新增订单回调返回 `void`，仅在该回调改比率不能证明成交前校验正确。

只有原生写入时序、保存/重启、余单继承、全部执行入口、成交前拒单及并发条件通过，才能将该探针整合为可启用的执行器。当前组件不会提交订单、刷新保证金缓存或提供这些尚未验证的保证。
