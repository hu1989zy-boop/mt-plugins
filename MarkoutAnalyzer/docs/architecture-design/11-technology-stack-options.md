# Technology Stack Options — Recommendation Only

状态：DRAFT FOR STACK REVIEW。**没有选定最终栈，没有创建 WPF/WinUI/Qt/C++/C# 项目，没有安装任何依赖或执行 PoC。** 此建议服务于后续独立评审，不把 mt-plugins 其他项目的选择继承到 Markout Analyzer。

## 1. 约束与条件性建议

Windows 单机、数据表格和键盘操作优先、少量单曲线、SQLite、两个平台 Manager/Terminal 集成。界面现代化不能以失去密集表格能力或引入 Web Backend 为代价。

**建议 WPF + .NET 优先进入后续小型验证候选；Qt Widgets + C++ 为原生集成成本较低时的备选；WinUI 3 + .NET 列次选。** 这是需求与公开能力对照后的工程判断。用户批准 Stack Review 并验证 SDK/Terminal 门槛前，不能据此开始工程或把语言、运行时版本写入冻结决定。

## 2. 候选比较

| 维度 | WPF + .NET | WinUI 3 + .NET | Qt Widgets + C++ |
|---|---|---|---|
| 密集表格 | 自带 DataGrid、键鼠交互明确；需额外验证列虚拟化及数据分页 | 需单独解决表格控件来源/能力；现代外观不能代替完整 Grid | QTableView/model-view 适合查询型表格；需要专门模型与分页设计 |
| 键盘、焦点、可访问性 | 标准控件基础较直接，自定义模板仍需验证 | 有桌面键盘与辅助功能体系，复杂Grid取决于最终控件 | 提供键盘焦点/辅助技术接口，定制模型与控件仍须验证 |
| SDK边界 | MT5可评估所带.NET绑定；MT4需要受支持绑定或薄原生桥接 | 与WPF同样承担.NET/native适配；换UI框架不消除SDK风险 | 可直接面向原生C++接口，仍有ABI、内存所有权、线程/回调风险 |
| 少量曲线 | 图表能力需后续单独评估；不先装库 | 同左 | 同左，且具体Qt图表模块许可不可按主框架推断 |
| 部署成本 | 需核对.NET部署形式与SDK运行库 | 另需考虑Windows App SDK分发、打包形式 | 需Qt库/插件及原生运行库，核验分发许可 |
| 主要风险 | 托管与MT4 C++虚接口互操作成本；UI虚拟化不等于数据虚拟化 | 表格需求可能带来额外依赖/采购/定制 | C++维护与UX定制成本，ABI和模块许可验证 |
| 本次排序 | 条件性第一候选 | 次选 | 原生SDK适配更合适时进入等权比较 |

## 3. 官方依据及其边界

- WPF DataGrid [行虚拟化](https://learn.microsoft.com/en-us/dotnet/api/system.windows.controls.datagrid.enablerowvirtualization)与[列虚拟化](https://learn.microsoft.com/en-us/dotnet/api/system.windows.controls.datagrid.enablecolumnvirtualization)是分别控制的能力；[默认键鼠行为](https://learn.microsoft.com/en-us/dotnet/desktop/wpf/controls/default-keyboard-and-mouse-behavior-in-the-datagrid-control)覆盖常见表格导航。不能把默认样式当作可交付金融工作台。
- [WPF性能文档](https://learn.microsoft.com/en-us/dotnet/desktop/wpf/advanced/optimizing-performance-controls)区分 UI 虚拟化和数据虚拟化；仍须应用层分页、排序和有界内存设计。
- Microsoft 的 [WinUI 表格数据说明](https://learn.microsoft.com/en-us/windows/apps/get-started/line-of-business/display-tabular-data)说明没有第一方 DataGrid。不能默认 ListView 已满足冻结列、调宽、比较数值和复杂排序。
- Qt 官方 [QTableView](https://doc.qt.io/qt-6/qtableview.html)、[QAbstractItemModel](https://doc.qt.io/qt-6/qabstractitemmodel.html)和[可访问性说明](https://doc.qt.io/qt-6/accessible.html)支持表格模型、增量供数和辅助技术设计方向；性能仍需应用实测。
- [.NET部署](https://learn.microsoft.com/en-us/dotnet/core/deploying/)、[Windows App打包部署](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/)、[Qt Windows部署](https://doc.qt.io/qt-6/windows-deployment.html)均需与实际 SDK 依赖一起评估，不推断必须通过 Store 或管理员权限运行。
- [Qt许可说明](https://doc.qt.io/qt-6/licensing.html)需按所用模块/分发方式核对；这是一项后续依赖选择 Gate，本轮不作具体法律合规结论或安装任何控件。

## 4. 已核实的本地 SDK 事实

本地 MT4 头文件定义 CManagerInterface 的 C++ 虚方法，MtManVersion 是版本查询导出，MtManCreate 是创建 CManagerInterface 的 factory 导出。**这不是整个 Manager API 的平面 C 接口**。不能写成“P/Invoke 直接调用所有 MT4 API”；可评估受支持 managed binding，或薄 native C ABI bridge / C++/CLI（是否适合目标运行时另验证）。桥接应仅是本项目边界，不修改现有 SDK 或别的插件。

本地 MT5 有 native/.NET DLL 和示例；文件存在不证明目标现代 .NET、位数或 ABI 兼容。Windows [进程位数约束](https://learn.microsoft.com/en-us/windows/win32/winprog64/running-32-bit-applications)使不匹配 DLL 不能直接装入同一进程。仅实际验证必要时才提出最小本地 worker，不能因此默认改为服务或后端。

MT4 Terminal 历史 Tick、统一 Server Time、Money上下文的风险高于UI框架选择。无论选哪种 UI，均不能解决 Q01/Q02/Q03 的业务与数据能力问题。

## 5. 后续选择 Gate（本轮不执行）

1. **能力**：证明两平台的同源 Terminal Tick、时间语义、完整性证据、Money边界符合获批规则。
2. **互操作**：核实实际 SDK build、位数、ABI、释放/回调、线程/重入、超时/取消；验证不执行交易修改。
3. **工作台**：使用约定合成规模、列数与单曲线测试筛选/排序、冻结标识列、下钻返回、Copy数值、内存与取消响应，不以UI虚拟化替代全量排序正确性。
4. **桌面体验**：键盘全流程、可见焦点、读屏语义、125/150/200% DPI、多显示器、Light/Dark/High Contrast。
5. **交付**：干净Windows环境、离线部署路径、SDK依赖、用户权限、目录隔离、第三方控件许可；具体数值门槛依 A12/A14 由评审确定。

WPF若通过互操作与表格验证则优先；若托管适配成本显著且团队具备C++维护能力，Qt进入同等比较；WinUI需先证明表格能力与新增依赖值得采用。任何决定都需记录到后续 Stack Review，不在本轮生效。
