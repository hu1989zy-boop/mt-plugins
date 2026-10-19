# MT4 0.2.0：指定 Dealer 执行测试版

本版将 MT4 跟单开仓、部分平仓和全平接入 Server API 5 的指定 Dealer 路径。目的在于处理此前只把请求放入队列、没有主动 Dealer 确认的情况，并允许通过权限预检的只读跟单账户进入这套流程。**Windows 编译、只读账户实际成交、保证金拒单和旧问题是否解决，仍需 Demo 验收。**

## 执行方式

1. 保存 Prepared 意图，核对客户、组别、合约、数量、精确平仓票号以及 Dealer 权限。
2. `RequestsAdd` 提交普通市场请求，客户账号仍为跟单账户。只有得到受理返回及正请求 ID，才继续。
3. `RequestsLock(id, DealerLogin)` 领取本实例刚提交的确切请求，不扫描或接管其他请求。
4. 重新检查客户组别没有变化、Dealer 权限、目标订单及当前组别报价，然后调用 `RequestsConfirm(id, dealer_user, prices)`。
5. 继续根据精确请求回调与实际订单历史确认成交。锁定成功、确认返回或 Apply/Restore 回调均不独立代表成交。

SDK 的 `AdvancedDealerHelper` 示例明确展示锁定及 Dealer 确认，`TradeCollector` 展示普通市场开平请求的确认。MT5 的 Dealer action 与 MT4 不是同一接口。本版没有把 Manager API 的 `TT_BR_*` 直接塞入 Server `RequestsAdd`，也没有用 DealerLogin 替代 `TradeTransInfo.orderby`。

`RequestsLock` 使用非零为成功的 BOOL 约定；确认返回值原样记录。确认失败或异常后，仅尝试释放已明确获得的锁一次，保留原始受理请求供核对，不重发。**释放锁不是取消订单，原请求仍可能被服务器或其他 Dealer 处理。** `RequestsFree` 的原生返回值仅记录，不猜测其成功约定。启动、停止、重启或修改参数均不自动取消旧未决请求。

## 参数与账户权限

新增插件参数 `DealerLogin`，默认 **0**。0 只是未配置占位，不能用于 `Armed=1`；部署时填写真实 MT4 管理账号，范围 1 到 2147483647。不使用 MT5 的 2016 作为默认账号，不需要密码。

指定账号须启用、非只读，并拥有 `ConManager.broker`（Deal）权限、目标账户组权限、对应品种组权限及允许的手数范围。`trades` 的增改删交易权限不能替代 Deal 权限。品种组手数上限必须为正；下限 0 按字面下限处理，不把最大值 0 猜测为无限。

组别掩码按 MT4 示例处理逗号、`!`、`*`，区分大小写，首个匹配项生效，空格按字面处理。旧示例在复杂通配模式下存在重叠匹配等特殊行为；本版同时比较旧行为与完整匹配，对有分歧的项拒绝访问，避免扩大授权。一般的 `*`、精确组名、前后缀匹配均可使用。

客户的 `enable_read_only` 和服务端账户记录不作修改。只读账户在规划阶段也会验证 Dealer 身份与目标组授权；真正提交前再次验证品种组与数量。账户及组启用、USD、非 FIFO、允许对冲、`close_reopen=0`、Market Execution、品种开平权限、数量步长和精确票号等原有检查保留。

报价使用目标组别 bid/ask，并要求基础行情时间不为零、不在未来、距当前服务器时间不超过 5 秒。服务器请求流程负责最终交易拒绝。所附 SDK 未说明 `TradesMarginCheck` 输出的完整语义，本版没有猜测公式或增加保证金绕过代码；**仍须在 Demo 证明不足保证金时拒绝开仓**。

## 编译与更新

将完整源码包解压到 `D:\work\TradeCopier-MT4-0.2.0`。根目录应有 `scripts`、`plugins`、`CMakeLists.txt` 和 `README-WINDOWS.txt`。MT4 与 shared 已一起打包，不能只复制 mt4 子目录。

将 MT4 **Server API** SDK 放到 `D:\SDK\mt4-sdk`，确认 `include\MT4ServerAPI.h` 存在；若 SDK 已在其他目录，修改下面的参数。不要使用 MT5 SDK 或 Manager API SDK 替代。

在 Visual Studio 2022/2026 的 **x86 Native Tools Command Prompt** 中运行：

```bat
cd /d "D:\work\TradeCopier-MT4-0.2.0"
scripts\build-windows.cmd -SdkRoot "D:\SDK\mt4-sdk" -BuildDirectory "D:\MT4Build\TradeCopier-0.2.0"
```

看到 `Build and tests passed` 后，产物位于 `D:\MT4Build\TradeCopier-0.2.0\package-executor`。该脚本构建 Win32/x86，检查 DLL 导出及运行 CTest；不能复用 MT5 x64 构建目录。源码包不含 SDK 或预编译 DLL/EXE。

在独立测试服务器核对旧未决请求及实际仓位，停止新测试，停用并卸载旧插件，备份停止写入后的完整配置、账本和伴随文件。同步替换 `MT4TradeCopierExecutor.dll`、`MT4TradeCopierManager.exe`、`MT4TradeCopierJournal.exe`，保留原配置和账本，不导入 MT5 数据。

先保持 `Armed=0` 保存实际 `DealerLogin` 和原有参数，再改为 `Armed=1`。日志应包含 `MT4TradeCopierExecutor 0.2.0`、`request_diagnostics=2`、`dealer_execution=lock_confirm` 与正确 DealerLogin。构建清单应为 source_version 0.2.0、build_script_revision 3。新参数不会改变原账本身份、管理协议或配置 JSON。

## 首轮 Demo 验收

先用单源、单跟随、小手数逐项验证，再开始压力测试：

- 正常账户开仓、部分平仓和全平，逐单核对实际成交与 MT4 票号链。
- 将跟随账户设为只读，确认客户终端无法自主交易，再重复开平仓；不得通过临时开放客户权限实现。
- 分别测试 Dealer 无 Deal 权限、无目标组权限、无品种组权限，应在提交前拒绝并记录原因。
- 保证金不足、闭市、品种禁用、无报价及过期报价应被拒绝；如果服务器允许本应拒绝的交易，停止验收并保留日志。
- 锁定/确认失败及异常，确认原请求只提交一次。按原请求号检查可能的后续成交，不直接重新下单。
- 未决请求及已成交状态重启恢复，核对不重复开仓、不串票号。

关键日志为 `MT4_REQUEST_SUBMIT`、`MT4_DEALER_PREFLIGHT`、`MT4_DEALER_CONFIRM_PREFLIGHT`、`MT4_DEALER_EXECUTION`、`MT4_REQUEST_APPLY/RESTORE` 和 `ISOLATED_MAPPING`。Dealer 日志包含确切请求号、DealerLogin、lock/confirm/release 是否调用、native 返回及异常标记。`native=none` 表示未返回，不等于 0。

`ManagersGet` 目前采用 BOOL 风格查询校验，并要求返回记录的 login 精确相等；由于原厂样例没有该函数调用，现场若失败，应提供日志中的 native 与 returned_login，不通过删除权限校验绕过。

## 本地验证范围

新增测试覆盖参数边界、组掩码和品种组权限、锁定/确认调用顺序，以及 `Engine + Port + FileJournal` 下的成功/失败/异常/超时/重启。确认返回成功而缺少交易证据时仍保持未决；历史或错误请求回调不能冒充精确成交。

可移植代码在 macOS 编译测试，独立 ASan/UBSan 检查覆盖新权限策略与 Dealer 流程。原生 SDK 桥接仅完成 API 字段/签名审查，不能替代 Windows ABI、DLL 生命周期、只读和保证金行为验收。具体本地结果保存在源码包的 `VALIDATION_STATUS.json` 与 `validation/`。
