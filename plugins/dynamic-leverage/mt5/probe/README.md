# MT5 订单保证金比率只读探针

这是独立的诊断 DLL，用于观察当前 MT5 Server 是否在 `TradeMarginCheck` 中使用订单的 `RateMargin`。它不启用主插件，不设置账户杠杆，不修改真实订单、请求或持仓。

当前交付是源代码。Windows/MSVC 编译、DLL 加载和真实 Demo Server 运行尚未执行；本机通过的可移植测试不能替代这些检查。

## Windows 编译

需要 Visual Studio 2022 C++ 桌面开发组件、CMake 3.21+、已获授权且未修改的 MT5 Server API 6182 SDK。以下 PowerShell 命令从仓库根目录执行；将 SDK 路径替换为本机真实路径。

```powershell
cmake -S plugins/dynamic-leverage/mt5 -B plugins/dynamic-leverage/build/mt5-margin-probe -G "Visual Studio 17 2022" -A x64 -DDYNAMIC_LEVERAGE_MT5_BUILD_MARGIN_PROBE=ON -DDYNAMIC_LEVERAGE_MT5_BUILD_SERVER=OFF -DMT5_SDK_ROOT="C:/MetaQuotes/MT5ServerSDK" -DBUILD_TESTING=ON
cmake --build plugins/dynamic-leverage/build/mt5-margin-probe --config Release
ctest --test-dir plugins/dynamic-leverage/build/mt5-margin-probe -C Release --output-on-failure
```

默认生成位置：`plugins/dynamic-leverage/build/mt5-margin-probe/Release/DynamicLeverageMT5MarginProbe.dll`。

CTest 包含可移植逻辑测试及该 DLL 的加载/导出检查。加载检查只检查 `MTServerAbout`、`MTServerCreate` 导出，不启动 Server 回调，也不能证明保证金计算或成交持久化正确。

## Demo Server 采样

1. 将 DLL 安装到独立的 MT5 Demo Server。确认测试账号确实属于该测试环境，使用一个没有持仓和挂单的专用测试账号，账户杠杆例如为 1000。选择保证金确实依赖账户杠杆的测试品种并留足测试余额。
2. 插件初始参数为 `Enabled=0`、`ProbeAccount=`、`TargetLeverage=500`。停用状态不订阅交易事件。
3. 将 `ProbeAccount` 填为一个完整的正整数交易账号，`TargetLeverage` 填为低于该账户当前杠杆的正整数，再设置 `Enabled=1` 并重新启动该插件实例。参数不接受通配符、逗号列表或密码。参数只在启动时读取，修改后需要重新启动插件。
4. 对该账号发起一笔新的市价买单或卖单。探针只处理与账号完全匹配、没有已有关联持仓、非平仓的一次请求；挂单、激活、修改、平仓和其他账号均跳过。首次符合条件的采样成功或失败后，本次启用期间都不再采样。
5. 在 Server 日志中查找 `MarginProbe sample` 或 `MarginProbe sample failed`。如需下一笔样本，停用并重新启动插件；测试完成后设置 `Enabled=0` 并停用插件。

探针在交易回调内读取该账号，并对两个独立的订单副本进行保证金检查，因此这一次请求仍有诊断开销，不能宣称零延迟影响。它不扫描其他账号；并发或递归进入时直接跳过。回调无论采样成功、失败还是抛出异常都返回 `MT_RET_OK`，让 Server 继续原来的处理流程。Server 自身仍按原规则决定成交或拒绝。

## 日志含义

| 字段 | 含义 |
| --- | --- |
| `base_leverage` / `target_leverage` | 本次读取到的账户杠杆与用于订单副本的目标杠杆；账户本身未修改 |
| `original_rate` / `candidate_rate` | 原始订单保证金比率，以及原比率乘以 `base_leverage / target_leverage` 后的副本值 |
| `baseline_code` / `adjusted_code` | 原比率副本与调整比率副本的 `TradeMarginCheck` 返回码；`not_called` 表示该步骤未执行，不能当作成功 |
| `baseline_current_margin` / `adjusted_current_margin` | 两次检查各自返回的当前账户保证金；若两者不同，可能存在并发变化，不能直接比较增量 |
| `baseline_future_margin` / `adjusted_future_margin` | 两次检查各自计算的模拟交易后账户保证金；不是已经成交后的实际保证金 |
| `baseline_future_free` / `adjusted_future_free` | 两个模拟结果对应的可用保证金 |
| `status` / `reason` | 诊断状态及原因；本 DLL 不认证生产适用性，保留 `unconfirmed_scope` 是预期行为 |

只有两次检查返回成功且当前账户状态可比，才能比较模拟结果。若两个未来保证金相同，不能据此认定改率生效；如果不同，也只能说明本次预检观察到了差异，尚不能证明成交后保存、部分平仓、重启、日终重算、对冲或净额追加仓位均正确。日志中 `nan`、非零返回码或 `not_called` 都不是通过证据。

日志仅记录指定测试账号、请求编号、比率、保证金及诊断信息，不读取或记录密码。分享日志时可遮盖账号和请求编号。

## 生命周期与内存

订单和账户副本使用 SDK `Release()` 回收，任何退出分支都不会将副本写回 Server。停止插件先标记停止、取消交易订阅，再等待已开始的采样结束。如果取消订阅失败，实例保留且尝试固定 DLL，避免 Server 仍持有回调指针时释放对象；日志会提示重试停止或重启测试 Server。不要在这种状态下强制卸载 DLL。
