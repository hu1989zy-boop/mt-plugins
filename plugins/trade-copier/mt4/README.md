# MT4 Server API 跟单 0.2.0

按 MT5 0.6.2 的业务规则实现 MT4 同服跟单，包含独立的 Server API DLL、Windows 管理器、账本检查器及测试。手数计算、配置、执行状态机、持久化和管理协议来自 `../shared/`；MT4 订单模型和 SDK 调用在本目录内维护。

0.2.0 新增指定 Dealer 执行：普通请求入队后，仅锁定本实例刚获得的请求号，再由 `DealerLogin` 确认。全部跟单开平仓采用该路径。默认 `DealerLogin=0`，仅为未配置占位，`Armed=1` 必须填写真实 MT4 Dealer 账号；不需要账号密码。详见 [Dealer 执行与验收](docs/dealer-execution-0.2.0.md)。

当前仍待完整 Windows / Demo 交易验收。2026-09-30 现场日志已确认此前 0.1.0 DLL 在 API 5、build 1470、x86 Demo 上加载并进入 READY；黄金品种匹配后，请求已入队但未确认成交，用户确认跟随账户没有对应订单。0.2.0 的锁定/确认路径、只读账户实际成交和保证金拒单需重新现场验证；源码和便携测试不能证明旧问题已解决。MT4→MT5、MT5→MT4 的跨服务器通信和路由尚未实现。

## 跟单规则

- 支持一对多、多对一，配置拒绝自跟随及循环关系。
- 支持手数倍率、提交前净值比例、每笔固定手数；按目标步长向下取整，低于最小手数跳过，超过最大手数拒绝。
- 每条订阅仅跟随启用后的新仓，启用前、暂停期间及停机期间的旧仓不补跟。处理开仓时源仓已发生平仓的，不追补这笔开仓，与 MT5 的处理规则一致。
- 不复制挂单或 SL/TP 参数；挂单成交形成市价仓后可跟随，源仓因 SL/TP 实际关闭时同步关闭结果。
- 源部分平仓按“源关闭量 / 关闭前源余量 × 目标实际余量”计算；全平关闭该映射的实际余量。目标人工全平后不补开，人工部分平仓后按真实余量继续。
- 支持运行中加入、修改、暂停和退出订阅；老仓保留开仓时冻结的规则，退出只关闭该关系归属的仓位。
- 请求先落账本，再提交；不确定请求不自动重发。异常按映射隔离，其他关系继续运行；重启保留归属并重建本次事件队列门槛。

MT4 配置使用 `"platform": "mt4"`；MT4 / MT5 配置互相拒绝，管理管道、二进制名称和账本身份也相互隔离。

## MT4 接入与边界

接口基准为用户提供的 `ServerApiVersion=5`，通过 `RequestsAdd` 提交普通市场执行开平仓请求，再调用 `RequestsLock(id, DealerLogin)` 与 `RequestsConfirm(id, dealer, prices)`。请求中的客户账号仍是跟单账户，未将 `orderby` 当成 Dealer 账号，也未改为资料不足的 Server `RequestsAdd(TT_BR_*)`。不直接修改订单数据库或客户只读标志。启动时核对实际 Trade Server 的名称和 build，默认 `Armed=0`。

首版支持 USD 账户、Market Execution、允许对冲且非 FIFO 的账户组，以及 `close_reopen=0` 的标准部分平仓模式。`close_reopen!=0`、Close By、恢复订单或无法核实的票号关联会拒绝或隔离相应映射。

部分平仓按根票号管理，并验证历史单与余单的 `to #票号` / `from #票号`、状态、账户、品种、方向、开仓时间与价格。不会按“最新订单”猜测归属。现场如果改写这些关联字段，需要先用 Demo 记录确认适配规则。

MT4 时间字段只有秒级精度。为避免把启用前旧仓当作新仓，启用、加入或恢复订阅同一秒内的开仓会保守跳过；从下一秒开始跟随。订单快照采用有界核对，缺少历史证据不视为零持仓。单账户查询上限为 100,000 条市价订单记录，超过上限拒绝继续推断；尚无生产容量或延迟验收结论。

## 目录与构建

复制源码到其他电脑时，需同时复制本目录和同级的 `../shared/` 完整目录，保持 `mt4/`、`shared/` 并列。仅复制 `mt4/` 无法构建；MT5 平台目录不参与 MT4 独立构建。SDK 另行指定。

```text
mt4/
├── core/          # MT4 票号链、数量转换和执行适配
├── server/        # SDK 桥接、DLL 生命周期与事件回调
├── management/    # MT4 专属本机管理管道
├── windows/       # MT4 Windows 管理界面
├── config/        # 默认关闭的配置示例
├── tests/         # 适配器、账本集成、运行时辅助及管理逻辑测试
├── tools/         # MT4 账本检查器
├── scripts/       # Windows 构建、检查及打包
└── docs/
```

从仓库根编译可移植部分：

```sh
cmake -S plugins/trade-copier/mt4 -B plugins/trade-copier/mt4/build -DCMAKE_BUILD_TYPE=Release
cmake --build plugins/trade-copier/mt4/build --config Release
ctest --test-dir plugins/trade-copier/mt4/build -C Release --output-on-failure --no-tests=error
```

Windows 的 Visual Studio x86 / Win32 开发者环境中，从仓库根执行：

```bat
plugins\trade-copier\mt4\scripts\build-windows.cmd -SdkRoot "D:\SDK\mt4-sdk"
```

默认输出到 `mt4/build-windows/package-executor/`，包含 `MT4TradeCopierExecutor.dll`、`MT4TradeCopierManager.exe` 和 `MT4TradeCopierJournal.exe`。省略 SDK 参数时使用仓库根 `vendor/mt4-sdk`，Manager API SDK 不参与插件构建。

详细步骤见 [Windows 构建与 Demo 部署](docs/windows-deployment.md)，测试范围见 [实现与验证记录](docs/implementation-validation.md)，接口依据见 [SDK 接入说明](docs/sdk-integration-notes.md)。旧的 `config/mt4-copier.example.json` 仅为历史禁用标记；实际可编辑示例为 [copier.example.json](config/copier.example.json)。
