# MT4 0.1.0 实现与验证记录

日期：2026-09-29。按 MT5 0.6.2 规则实现 MT4 同服跟单。当前结果是源码、可移植测试和 Windows 构建入口；尚未生成经过 Windows MSVC 验证的 DLL，也没有连接服务器或产生真实交易。

## 实现范围

- 公共业务逻辑位于 `../../shared/`，包括倍率、净值比例、固定手数、动态订阅、冻结开仓规则、状态机及真实文件账本。平台配置和拒绝码分别编译；MT5 原有目标名保留。
- MT4 独立适配位于 `../core/`，将每手 100 个 SDK volume 单位精确转换为公共整数数量。部分平仓依靠显式票号链，核对账户、方向、品种、原开仓时间与价格；人工减仓采用实际余量，不补开。
- Server 桥接位于 `../server/`，使用 Server API 5、MSVC Win32、32 位时间类型和原始 SDK ABI；正常请求走 `RequestsAdd`，不写订单数据库。默认 `Armed=0`，启动核对实际 Trade Server 名称及 build。
- 管理器、管道、账本工具和构建脚本都位于 MT4 目录，使用独立名称和实例身份。MT4 / MT5 跨服务器路由及 Web / Java 管理端尚未实现。

## 请求证据与恢复

提交前先持久化意图。开仓需要精确请求回调及实际历史记录相互验证，单凭 comment 或持仓增加不能认定本次请求成功。不确定结果不自动重发。

平仓可能仅收到更新通知。此时只对本进程已明确接受、保存了实际旧票号的普通市场平仓请求，允许按旧票的终态历史确认减仓目标已完成：关闭量必须等于请求量，账户、品种、方向、时间、根票号链和实际余量都需吻合。人工同量关闭也可以完成该减仓目标，但不会声称由本请求造成；普通请求仍指向已关闭的旧票，不可据此操作新的余单。重启后不重建这份易失接受证据，未决请求保持隔离。

同步返回保证金不足 `134`、没有 request ID 且明确未提交的开仓，使用独立状态结束并落盘回收。不会伪造拒单回调；有 request ID、未知结果及平仓拒绝不走该回收路径。测试覆盖真实账本持久化、压缩和重启。

订单查询最多三轮，连续两次关键字段一致才接受；暂时缺少源历史或成交证据时等待，缺失不当作零仓位。已入队但尚未准入的开仓遇到暂时快照不完整时，保留原事件序号作有界延期，最多等待 30 秒；不会刷新成新信号绕过启用门槛。源仓在正式准入前已发生平仓的，沿用 MT5 不追补规则。

## 本机验证

环境：macOS、Apple Clang 16、CMake 4.4.3、C++17、Release。仓库聚合目标和 MT4 独立目标均通过配置、编译及 CTest，分别为 **11/11** 和 **6/6**，编译日志无告警；未将 macOS 模拟当成 Windows DLL 或交易服务器验收。

| 测试 | 检查数 / 内容 |
| --- | --- |
| MT4 公共核心 | 2,918 项，数量、准入、关系图和映射规则 |
| MT4 配置 | 333 项，平台、JSON、数值、关系图和合约匹配 |
| MT4 同步未提交拒绝 | 60 项，真实账本及重启、未知结果保留 |
| MT4 适配及账本集成 | 320 项，开平仓、三种手数、部分票号链、动态关系、人工干预及恢复 |
| MT4 管理逻辑 | 137 项，异步请求、修订与状态更新；不含真实 Windows 窗口 |
| MT4 运行时辅助 | 4,078 项，参数、调用门、并发队列、唤醒、延期准入及门槛 |
| MT5 回归 | 核心 2,918、配置 333、执行 63,739、管理 137、同步拒绝平台隔离 14 项 |

MT4 适配及账本集成的 320 项检查、运行时辅助的 4,078 项检查另在 AddressSanitizer / UndefinedBehaviorSanitizer 下通过。SDK 方法、结构字段、11 个 DLL 导出及构建脚本变量、依赖和打包路径完成静态核对。

从仓库根复跑：

```sh
cmake -S . -B build/workspace -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build/workspace --parallel
ctest --test-dir build/workspace --output-on-failure --no-tests=error

cmake -S plugins/trade-copier/mt4 -B plugins/trade-copier/mt4/build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build plugins/trade-copier/mt4/build --parallel
ctest --test-dir plugins/trade-copier/mt4/build --output-on-failure --no-tests=error
```

## 尚待目标环境验证

Windows MSVC Win32 编译、链接、DLL 加载、真实插件生命周期与配置回滚、管理管道权限及界面运行尚未验证。Server API 桥接未包含在 macOS 原生编译目标中。还需使用实际 Demo 验证普通请求的成交与拒绝返回、回调顺序、部分平仓评论及继承字段、净值返回约定、账户权限、断线和重启行为。

首版限定 USD、Market Execution、允许对冲且非 FIFO、`close_reopen=0`。不支持 Close By、恢复订单、跨服务器互跟或未知票号关系。MT4 秒级时间导致启用同一秒的新仓保守跳过。单账户快照上限为 100,000 条市价订单记录；尚无生产容量或延迟结论。

Windows 构建和逐项验收见 [部署说明](windows-deployment.md)，SDK 依据见 [接入记录](sdk-integration-notes.md)。

## 2026-09-30 请求诊断修订 1

现场已确认此前 0.1.0 DLL 加载并进入 READY（实际 server=Demo、API 5、build 1470、x86）。黄金源品种 XAUUSDr 已匹配目标 XAUUSDp；日志显示 intent=1/request=1 入队，约 30 秒后隔离，用户核对目标持仓及历史没有对应订单。尚不能确定是排队等待还是后续取消/拒绝，亦不能把请求受理当作成交验收通过。

本次补充原始提交字段、被动 Apply/Restore 回调日志，以及无匹配证据时的明确原因。诊断使用独立有界队列，日志异常不改变 SDK 提交结果；原始文本做单行转义。没有更改 RequestsAdd 路由、重试、持久化或成交确认规则。Windows 构建脚本同步检查新增的两个导出（共 13 个），清单记录诊断修订号。

macOS Release 仓库回归 **11/11** 通过：MT4 适配与账本集成现为 **327** 项，运行时辅助 **4,084** 项。新增回归验证超时不重发、证据缺失原因与请求日志关联，以及控制字符不会伪造多行日志。此结果不包括原生 Server SDK 桥接的 Windows 编译和运行；当前诊断 DLL 尚待 Windows 构建及现场验证。上文的 sanitizer 数量是此前版本的验证记录。

## 2026-10-14：0.2.0 Dealer 执行

新增 DealerLogin 参数、Dealer 身份/组别/品种组预检，以及 RequestsAdd 后对确切请求 ID 的 RequestsLock/RequestsConfirm。全部跟单开平采用此流程；不修改客户只读标志，不把确认返回当作成交，不重发结果不明的请求。旧 0.1.0 的加载记录仍不代表本版已通过现场验证。

本地 CMake Release 配置、编译和 CTest **8/8** 通过：核心 2,918、配置 386、同步拒绝 60、管理逻辑 137、Port/Engine/FileJournal 543、runtime 4,098、Dealer 权限 96、Dealer 执行顺序 339 项。新 Dealer 权限与流程测试、完整 Port 集成测试另通过 ASan/UBSan。

独立组掩码验证将安全的有界旧语义实现与 SDK 原始 CheckGroup/CheckTemplate 对照，共 1,409,894 项通过；同时验证本插件允许访问时，旧 SDK 示例也允许。对复杂模式两套匹配语义的分歧采取拒绝，已修复严格排除匹配可能少排除账户的问题。

CMake 构建过程中 macOS 工具包装器报告不可写的 xcrun 缓存路径；编译最终退出 0，全部测试通过。无 Windows MSVC、SDK 桥接动态加载、真实 Dealer 或保证金验证结果。完整验收要求见 [Dealer 执行说明](dealer-execution-0.2.0.md)。
