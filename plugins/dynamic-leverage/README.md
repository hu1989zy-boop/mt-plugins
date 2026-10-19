# 动态杠杆插件

以**交易账户**为单位，按开仓后总持仓选择新增仓位的杠杆，已有仓位保留原杠杆。MT4、MT5 各有独立 Server API 插件，共用规则引擎和本地管理界面。整个模块位于本目录，不依赖其他业务插件；管理 API 保留模块路径，方便以后整合到统一系统。

**2026-10-14 需求修正：旧的账户杠杆写入方式已停用。** 新实现方向是保持账户基础杠杆不变，通过每笔订单的保证金比例实现新增仓位档位；跨档计算已实现，原生保证金行为仍在验证。规则管理与离线预览可用；当前正式 DLL 启动会明确拒绝运行旧执行器，不启动扫描或修改账户。此前 `0.2.0` 源码包不符合修正后的需求。详见 [新仓规则与验证状态](docs/new-position-leverage.md)。

后文涉及原账户调杠杆线程、轮询和部署启用的内容为 **0.2.0 历史实现记录**，不能作为当前版本的上线操作说明。

当前交付入口：[逐单保证金源码与 Windows 编译测试步骤](docs/opening-margin-test-guide.md)。包含 MT4 比率转换组件、MT5 只读原生保证金探针和一键验证构建脚本；不是完整的逐仓执行插件。

## 开始使用管理界面

需要 Python 3.10 或更新版本，无第三方 Python / 前端依赖。从仓库根目录运行：

```sh
python3 -B plugins/dynamic-leverage/management/server.py
```

Windows 可以双击 `management/start-windows.cmd`，或使用 `py -3 -B management/server.py`。访问 `http://127.0.0.1:8765`；通过 `--port 8766` 更换端口，`--data-dir C:\DynamicLeverage\runtime` 指定配置目录。

1. 选择 MT4 或 MT5，点击“新增规则”，选择组别或账户，填写匹配条件和持仓档位。
2. 用列表中的 **↑ / ↓** 调整大规则优先级，越靠上越优先。组别与账户使用同一个顺序，**第一条已启用且命中的规则生效**，后续规则不叠加。
3. 使用“匹配预览”输入账户、组别、持仓手数，检查当前草稿将命中哪一条规则；手数输入开仓后的预计总持仓，输出仅用于新增仓位。
4. 编辑、启停、排序均先保留为草稿；点击“保存配置”才写入配置文件。刷新会重新读取已保存版本；页面会提示未保存改动，多窗口覆盖会被拒绝。

默认没有规则且总开关关闭。两个平台分别保存 `runtime/mt4.rules.json` 和 `runtime/mt5.rules.json`；账号相同也互不影响。操作记录是配置保存记录，**不是服务器交易或杠杆执行日志**。管理界面不会连接交易服务器，也不会把“文件保存成功”显示为“服务器执行成功”。

## 组别与账户匹配

| 组别条件 | 含义 |
| --- | --- |
| `*` | 所有组别 |
| `ABC` | 精确匹配 ABC |
| `ABC*` / `*ABC*` | 以 ABC 开头 / 包含 ABC |
| `!ABC` | 除 ABC 外的所有组别 |
| `!demo*,real*,vip*` | real 或 vip 开头，同时排除 demo 开头 |
| `!ABC,*` 或 `*,!ABC` | 所有组别中排除 ABC，两种写法结果相同 |

多个条件用英文逗号分隔，前后普通空格会被移除，大小写敏感。反斜杠是组别路径的普通字符；不支持 `?`。多个包含条件是“或”，**任一排除条件命中即否决**，仅有排除项时隐含包含全部其余组别。这是本插件统一的匹配语义，不依赖 MT4 / MT5 原生掩码对条件顺序的不同处理。

账户规则填写多个正整数交易账号，例如 `10001,10002`。MT4 范围为 `1..2147483647`，MT5 为 `1..18446744073709551615`；账号按字符串传输，避免浏览器大整数丢精度。规则之间可以重叠，先命中者生效；同一账户规则中重复账号会被拒绝。每个平台最多 256 条规则，每条最多 256 个匹配项。

例如：希望账户 10001 使用专属规则，其他 real 组账户使用组别规则时，将“账户 10001”规则放到“real*”规则上方。若把组别规则上移并且 10001 属于该组，组别规则会优先生效。

## 持仓与杠杆档位

新增规则提供以下默认档位，可以修改及增加至最多 32 档：

| 当前账户总持仓 | 目标杠杆 |
| --- | --- |
| 0～2 手，含恰好 2 手 | 1:1000 |
| 超过 2 手且不超过 5 手 | 1:500 |
| 超过 5 手 | 1:200 |

“超过”使用严格大于比较。第一档从 0 开始，后续手数阈值严格递增、杠杆严格递减。阈值最多 8 位小数，杠杆接受 1～100000，实际可用值受服务器限制。减少持仓或全部平仓后，后续新仓按新的总持仓选档；旧仓不因减仓、规则修改或停用而重新选档。

账户所有品种未平仓买卖手数相加，对冲不抵消；买 3 手、卖 3 手合计 6 手。挂单、历史订单、入出金不计入。MT5 净持仓账户使用当前 position 的剩余量。不同账户独立计算，组别仅用于筛选账户，不合计整组仓位。内部用每手 `100000000` 个整数单位比较，避免临界点误差。

## 旧账户调杠杆实现记录（已停用）

管理服务与 DLL 应部署在同一台服务器并访问同一份配置文件。DLL 不依赖管理服务持续运行，保存后的文件仍会独立读取。管理服务只监听本机回环地址，远程操作可通过服务器桌面打开；此版本没有远程登录或公网管理功能。

先在管理页面保存对应平台的配置，在服务器插件参数中填写：

| 参数 | 文件规则模式 |
| --- | --- |
| `Enabled` | 首次部署用 `0`；检查完成后设置 `1` |
| `RulesFile` | 对应 JSON 的绝对路径，例如 `C:\DynamicLeverage\runtime\mt5.rules.json` |
| `Accounts` | 留空，与非空 RulesFile 互斥 |
| `Tiers` | 保留默认值，文件模式不用此项 |
| `PollIntervalMs` | 文件模式以 JSON 的 `poll_interval_ms` 为准 |

**SDK 的 Enabled 与界面总开关必须同时启用。** DLL 周期读取 JSON，规则变更后自动重新计算。文件缺失、非法、平台错误时暂停写入，不回退旧规则；修复后自动恢复。管理界面默认每 1000 毫秒复核，可配置 100～60000 毫秒。存在组别规则时周期枚举账户，账户数量很大时应在目标服务器测量负载并调整间隔。

参数示例见 [parameters.example.ini](config/parameters.example.ini)，JSON 示例见 [MT4](config/mt4.rules.example.json) / [MT5](config/mt5.rules.example.json)。没有设置 `RulesFile` 时仍兼容旧版 `Accounts` + `Tiers` 参数模式。平台字段长度、SDK 行为和部署步骤见 [MT4](mt4/README.md) / [MT5](mt5/README.md)。

交易 / 用户变更事件触发后台复核，周期扫描补查漏事件、新账户及迁组。写入前复读仓位、账户组别、原杠杆及配置签名，只更新杠杆字段。读取失败、不稳定快照或溢出会跳过并重试，不把已识别的异常当空仓。MT4 数组接口没有独立错误码，必须在目标环境确认失败与真实空数组的区别，详见平台说明。

这是持仓变化后的账户杠杆调整，不是下单前保证金拦截。SDK 读取与更新不构成原子事务，已开始的写入可能在停用或迁组后先完成一次，随后通过事件与周期复核收敛。账户杠杆对实际保证金的影响取决于服务器的品种、组和计算配置，需要 Demo 验证。

## 目录与测试

```text
dynamic-leverage/
├── shared/       # 多规则匹配、严格 JSON、账户同步、原生检查工具及测试
├── mt4/          # MT4 Server API、构建及测试
├── mt5/          # MT5 Server API、构建及测试
├── management/   # 本地 Python API、Web 管理界面及测试
├── scripts/      # Windows 双平台构建打包、独立源码打包
├── config/       # 安全默认配置示例
├── docs/         # Demo 验收清单
└── runtime/      # 本机生成的配置及保存记录，不提交
```

可移植 C++ 测试：

```sh
cmake -S plugins/dynamic-leverage -B plugins/dynamic-leverage/build -DCMAKE_BUILD_TYPE=Release
cmake --build plugins/dynamic-leverage/build --config Release
ctest --test-dir plugins/dynamic-leverage/build -C Release --output-on-failure --no-tests=error
```

管理接口测试（需要允许回环监听）和界面规则测试：

```sh
python3 -B -m unittest discover -s plugins/dynamic-leverage/management/tests -v
node --test plugins/dynamic-leverage/management/web/tests/*.test.mjs
```

仓库聚合构建默认包含本插件，可用 `MT_PLUGINS_BUILD_DYNAMIC_LEVERAGE=OFF` 排除；平台开关为 `DYNAMIC_LEVERAGE_BUILD_MT4` / `DYNAMIC_LEVERAGE_BUILD_MT5`。实际 DLL 需在 Windows 使用各自脚本编译，MT4 为 Win32、MT5 为 x64。管理 UI/API 路径是 `/api/plugins/dynamic-leverage/{mt4|mt5}`；后续系统可复用这一模块，当前不修改其他业务插件。


## 服务端构建工具（尚不能部署逐仓执行）

在 Windows 仓库根执行下面的统一入口，分别以 Win32 / x64 构建、测试并生成含 DLL 的部署包：

```bat
plugins\dynamic-leverage\scripts\build-windows.cmd
```

单独编译使用 `-Platform mt4` / `-Platform mt5`。构建包含真实 Windows DLL 加载与必要导出检查，但不连接交易服务器。步骤及目录见 [Windows 构建与部署](docs/windows-quickstart.md)。源码打包入口为 `python3 -B plugins/dynamic-leverage/scripts/package-source.py`，只包含本插件，不包含商业 SDK、运行配置或 DLL。

可用 `DynamicLeverageRulesCheck` 复用 DLL 原生引擎校验文件 / 模拟匹配，不执行任何交易调用：

```sh
plugins/dynamic-leverage/build/shared/DynamicLeverageRulesCheck --platform mt5 --file /absolute/path/mt5.rules.json --login 10001 --group 'real\VIP' --lots 5.00000001
```

独立默认构建会生成此工具，可用 `DYNAMIC_LEVERAGE_BUILD_RULES_CHECK=OFF` 关闭。在找到 Python 3.10+ 时，插件聚合测试还自动验证管理端落盘文件与原生工具计算的一致性，覆盖两个平台、规则排序、排除条件、精确手数和 64 位账号。
