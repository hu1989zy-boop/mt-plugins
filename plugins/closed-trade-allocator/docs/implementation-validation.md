# 平仓订单概率分配：实现与验证记录

日期：2026-10-01。本文区分平台无关测试、Windows SDK 编译以及实际服务器验收。通过本地测试不代表目标服务器历史和余额已验证。

## 本地公共核心验证

测试入口：`shared/tests/core_tests.cpp`，C++17、独立 `main`、无第三方测试框架。已在 macOS / AppleClang 16 下独立构建并通过 CTest：**23 个案例、125 个断言、0 失败**（含事件版本新增的预算与源历史删除恢复验证）。

复现命令（从仓库根运行）：

```sh
cmake -S plugins/closed-trade-allocator/shared -B build/closedalloc-core -DBUILD_TESTING=ON
cmake --build build/closedalloc-core --parallel
ctest --test-dir build/closedalloc-core --output-on-failure
```

| 测试 | 核对的业务行为 |
| --- | --- |
| `probability_boundaries` | 0% / 100%、均匀抽样值不达到 1、恰好等于阈值不入选、盈利/亏损独立候选统计 |
| `net_currency_rounding_and_exclusions` | 带符号费用只计一次、毛利转净亏、币种精度下持平跳过、部分平仓排除且不消耗抽样 |
| `durable_decisions_and_payload` | 重复事件、改概率和重启保留决定；API 标记稳定；含 SL/TP、空 Comment 及二进制字节的快照完整持久化 |
| `uncertain_write_recovery` | 外部写入后模拟断线，重启读取持久进度，暂停/未开执行闸门仍可核对，不盲目重发 |
| `unknown_outcomes_never_resubmit` | 目标记录或资金不明时保持待核对，多次轮询只读查询 |
| `positively_unwritten_can_retry` | 只有明确未写入才允许下一次轮询重试，沿用原抽样和 API 标记 |
| `unresolved_route_serializes_balance_effects` | 同一关系先核对未决写入，再提交新的目标历史；本轮出现待核对后立即停止后续提交 |
| `pump_submit_budget_counts_failures_and_rotates` | 失败同样消耗提交预算；有限预算公平轮转，避免单笔失败占满后台处理 |
| `pump_zero_budget_and_unlimited_default` | 零预算不调用适配器；旧调用方的默认行为保持兼容 |
| `pump_budget_preserves_review_isolation_and_next_turn_retry` | 预算耗尽不能绕过未决写入；刚证明未写入的任务必须等下一轮才能重试 |
| `pump_reconcile_budget_is_fair_across_recovered_jobs` | 有限恢复预算公平处理重启后的未决任务，异常也消耗预算 |
| `not_written_cannot_discard_target_evidence` | 带目标标识的结果不能宣称未写入，也不能授权重试 |
| `later_progress_cannot_erase_receipts` | 后续结果或检查点不能缩减、替换既有目标回执 |
| `cross_entity_receipts_preserve_order_and_duplicates` | MT5 订单/成交标识可在不同实体中数值相同，持久化保留完整顺序和重复值 |
| `proven_preflight_failure_allows_fresh_evidence` | 仅有写前核对且证明未调用外部写入时，允许更新下次核对证据，仍保留原抽样 |
| `paused_and_armed_gates` | 未开执行闸门不写入；暂停及暂停/恢复同秒跳过；恢复保留原待执行决定 |
| `startup_and_pause_boundaries_survive_restart` | 首次启用时间固化，暂停区间跨重启保留，正常停机期间符合条件的订单可补查 |
| `history_revision_isolated` | 源历史修改后保留原快照并隔离，禁止自动补写和重新抽样 |
| `source_lookup_and_selected_invalidation` | 源历史删除/失效保留冻结决定，阻止后续提交；未知标识与重复通知不新增账本事件 |
| `completed_source_invalidation_preserves_receipts_after_restart` | 已完成记录的源历史失效保留目标回执、资金证据及原抽样，跨重启继续隔离 |
| `route_identity_and_exclusive_lock` | 同一账本排他持有；不能更换源/目标/服务器身份复用账本 |
| `invalid_configuration_and_codec` | 无效概率、自分配、缺失身份与错误编码拒绝；整数、浮点边界完整往返 |
| `ledger_corruption_is_rejected` | 截断、字节损坏和尾部垃圾拒绝加载，不丢弃证据继续执行 |

公共核心测试中的 SL/TP 断言验证的是不可变快照字节的落盘和传递；真正的 SDK 字段赋值与历史回查仍需平台适配测试和 Demo 验收。

## 当前验证边界

| 层级 | 状态 |
| --- | --- |
| 公共概率/账本核心构建和 CTest | 已通过，见上文 |
| MT4 / MT5 平台数据模型测试 | 已通过；验证快照、SL/TP、空备注标记、部分平仓排除及参数等可移植逻辑，未运行 SDK 写入 |
| 仓库聚合 Release 构建与 CTest | 已通过 16 / 16 组，包括原跟单测试与分配核心、两平台模型、MT5 事件队列及源任务调度测试 |
| 新插件 AddressSanitizer / UndefinedBehaviorSanitizer | 分配核心、两平台模型、事件队列及源任务调度测试 5 / 5 通过 |
| MT5 事件队列 ThreadSanitizer | 已通过；覆盖四生产者并发入队、容量耗尽与复用、关闭回调入口及竞争退出 |
| SDK 头文件及 HTML 文档静态复核 | 已核对导出/方法签名、API 标记范围、开平仓字段适用条件、完整来源成交集合及写前容量限制；对应可移植回归通过 |
| Windows MSVC DLL 构建、架构及导出验证 | 轮询版修正后用户已实际加载；本次事件版未在 Windows 编译，不能用旧版加载代替验证 |
| 插件实际加载、参数保存与服务重启恢复 | 用户已展示轮询版参数及抽样日志；32 笔待写均因目标账号交易权限被阻止。本次事件版加载/订阅恢复待验证 |
| 历史记录写入及真实余额/净值/保证金效果 | 未完成 Demo 验收 |
| SL/TP 和空 Comment 在服务器历史及报表中保持 | 未完成 Demo 验收 |
| 目标无新持仓、源账户保持不变 | 未完成 Demo 验收 |
| 真实中断、丢回执、部分历史写入的恢复 | 公共状态机已模拟；实际 Server API 场景未验证 |

本轮未取得可用的 Windows 编译环境，未产出已验证的部署 DLL。实际 SDK 字段和回调另外做了本地头文件、HTML 文档的静态核对，但这不替代 MSVC 编译和服务器加载。

最后一轮事件版源码修正后，仓库 Release 构建及全部 16 组测试通过；插件 ASan/UBSan 构建及全部 5 组测试通过，事件队列 ThreadSanitizer 检查通过。既有跟单代码未在本次功能中修改。

## MT5 事件版验证

源事件队列测试验证固定容量、重复复用、多生产者下成功入队的事件恰好读取一次，以及停止时回调入口关闭和已进入回调退出。源任务调度测试验证同一 PositionID 合并、两次快照读取的稳定窗口、快照变化重新等待、重复事件不延长未变化的稳定窗口、未来平仓时间、到期任务顺序与容量限制。

SDK 静态复核覆盖成交/历史订阅签名、回调内不调用数据库查询、停止前 SDK 自动取消订阅、按账号与 PositionID 查询完整成交集合、部分 dataset 结果拒绝，以及源历史全部删除或持仓重新出现时保留冻结快照并隔离。启动/事件溢出后的恢复必须同时完成分段补扫与账本已有来源核验，才允许继续提交；写入前再检查恢复和停止标记。

后台批次数量、轮转、提交间隔、低线程优先级和慢调用冷却已落实；这些是源码控制和可移植测试证据，不是 Trade Server 实测结果。单次 SDK 调用仍不可中断，目标完整历史核对及余额校验仍占用共享资源。正常客户交易延迟、CPU/I/O 和错误率须按 [部署文档](windows-deployment.md#从轮询版升级到-mt5-事件版) 做 Demo 对照验收。

## Windows 编译反馈：MAXULONG 未定义

用户回传 MT5 构建截图：`src/plugin.cpp(112,27)` 报 C2065，`MAXULONG` 未声明。该位置检查传给 `BCryptHashData` 的长度是否可转换为 `ULONG`。

已显式包含 `<limits>`，将宏依赖替换为 `(std::numeric_limits<ULONG>::max)()`，直接使用 API 参数类型的最大值，并避免 Windows `max` 宏干扰。长度越界保护保留。Windows 副本更新 `mt5/src/plugin.cpp` 后，沿用原构建目录重新运行 `mt5/scripts/build-windows.cmd` 即可；原来指定的 `-SdkRoot` 参数继续保留。

修复后，本地公共核心与 MT5 数据模型测试重新运行，2 / 2 通过；这些测试不编译 Windows DLL。截图尚不能证明 DLL 构建或 CTest 完成，仍需取得 Windows 重跑结果。

## Windows 加载反馈：错误 126

用户随后展示已部署的 `ClosedTradeAllocatorMT5.dll`，以及 `APIDispatcher` 三次加载失败、错误 126 的日志。DLL 文件存在但依赖缺失是可能原因；尚未取得实际 DLL 的导入依赖列表，具体缺失模块未确认。

构建静态复核发现默认 `/MD` 与 SDK 官方 ServerPlugin 示例 `/MT` 不一致。已在 `shared/mt4/mt5` 的 CMake 文件中为全部七个相关目标显式指定静态 MSVC runtime，覆盖平台独立构建和仓库聚合构建，不更改其他插件的配置。MT5 `DllMain` 同时保留静态 CRT 需要的线程通知。SDK 对象通过 SDK `Release()` 释放，插件对象由自身 `Release()` 释放。

修正后仓库聚合工程重新配置/构建成功，公共核心、MT4 模型和 MT5 模型 CTest 3 / 3 通过。更新文件、重编及部署后的依赖检查步骤见 [部署文档](windows-deployment.md#mt5-加载错误-126找不到模块或其依赖)。本地可移植测试不能证明 Windows DLL 依赖和目标服务器加载结果。

## Windows 重编反馈：静态/动态 CRT 混用

用户按上次文件列表更新后，链接出现 `msvcprt.lib(MSVCP140.dll)` 的 LNK2005、`MSVCRT` 冲突的 LNK4098，以及 `closedalloc_core.lib(core.obj)` 导入 `calloc` 的 LNK4217。证据定位到此次链接的公共库对象仍引用动态 CRT，但不能仅凭截图认定是旧对象还是远端源码/外部构建配置未生效。

已在 MT5 构建入口统一设置 DLL、公共库及两组测试的静态 runtime，即使公共库目标由外层工程创建也覆盖其原值，并输出目标的实际源码目录。Windows 构建脚本改用 `--clean-first`，部署文档改为使用新的 `build-windows-static` 目录，撤回此前直接复用旧目录即可的排查步骤。没有添加 `/NODEFAULTLIB`。目标 Windows 构建与 DLL 加载仍待验证。

本地以临时父工程先将公共库及其测试设为动态 CRT，再引入真实 MT5 CMake 入口，验证三个可移植目标最终属性及 Release 求值均为 `MultiThreaded`。该工程使用 `--clean-first` 从头构建成功，公共核心和 MT5 模型测试 2 / 2 通过。此检查验证 CMake 配置传播，不运行 MSVC 或 Windows PowerShell 脚本，也不验证实际 DLL 导入表。

## Demo 验收取证

使用同服同币种的受控源账号及专用接收账号，依 [部署说明](windows-deployment.md) 配置。每个平台分别保留以下数据：

1. 实际 Trade Server 名称、build、SDK 版本及 DLL 哈希；配置与账本路径。
2. 一笔盈利、一笔亏损、一笔毛利为正但扣费后净亏损的源完整平仓，以及持平和部分平仓排除案例。
3. 同一品种分别测试 SL/TP 为 0 与非 0；B 的字段与源快照一致且 Comment 为空。
4. 每次写入前后目标余额、信用额、净值、可用保证金、所有历史记录、持仓和挂单；源历史和余额对照。
5. 用 0% / 100% 验证业务边界；30% / 70% 观察长期分组入选率，不能要求小样本精确比例。
6. 人工构造重复查询、重启、改概率、暂停/恢复、源历史修订和故障场景，核对每个源标识只有原抽样决定。
7. 对单次历史成功但丢失回执、多条历史只完成一部分和资金校验未知，确认插件仅回查而不自动重写完整交易。

资金结果必须和整套目标历史一致。发现差异应保存账本与服务器记录进行核对，不能删除账本重新抽样，也不能用手工补余额掩盖未验证的写入行为。
