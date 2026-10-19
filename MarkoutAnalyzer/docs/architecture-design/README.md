# Markout Analyzer — Architecture & UX Review Pack

状态：**READY FOR ARCHITECTURE REVIEW；NOT APPROVED FOR IMPLEMENTATION**。日期：2026-10-12。全部为设计文档，未创建应用、数据库、API连接或正式UI。

## 阅读顺序与文件

| 文件 | 内容 |
|---|---|
| [01 Product Scope & Decisions](01-product-scope-and-decisions.md) | 47条确认规则、建议与V1范围边界 |
| [02 System Architecture](02-system-architecture.md) | 模块/数据流、Adapter、Quote Provider/Matcher、Money、执行/历史/安全 |
| [03 Markout Calculation Specification](03-markout-calculation-spec.md) | 事件/时间/窗/报价/公式/完整性/聚合及合成例子 |
| [04 Logical Data Model](04-data-model.md) | 逻辑实体、身份、快照、事务/发布、删除；无DDL |
| [05 State & Error Model](05-state-and-error-model.md) | Task及四层诊断、取消/恢复、页面错误状态 |
| [06 UI/UX Specification](06-ui-ux-specification.md) | IA、页面/动作契约、表格/曲线/格式、键鼠/DPI/主题 |
| [07 Wireframes](07-wireframes.md) | 低保真页面和关键状态；不是运行UI |
| [08 Acceptance Criteria](08-acceptance-criteria.md) | 56个Given/When/Then条目；区分确认/建议/待决定 |
| [09 Open Questions](09-open-questions.md) | 25项开放问题，选项、建议、责任与用户决定标记 |
| [10 Assumptions](10-assumptions.md) | 16项尚未证明的设计条件及验证方式 |
| [11 Technology Stack Options](11-technology-stack-options.md) | WPF/WinUI/Qt条件性比较；未选最终栈 |
| [12 Technical Evidence & Review](12-technical-evidence-and-review.md) | 本地SDK/官方证据、技术发现与交叉审查记录 |

建议先读01、03、09的Q01–Q13，再看02/04/05与06/07。若只评审界面，也须理解Money、资格与空值未决项，避免以线框默认值替代业务决定。

## 本轮原始需求与适用范围

- 第0–39节：[第一份需求附件](/Users/lukeh/.codex/attachments/67341826-1330-490d-be19-a3adcecce172/已粘贴的文本.txt)（文件止于§40标题）。
- 第40–80节：[第二份需求附件](/Users/lukeh/.codex/attachments/8d3c8b84-1f32-4802-8e6c-364acb134ddc/已粘贴的文本.txt)（§81不完整，使用完整续篇）。
- 第81–126节：[完整续篇](/Users/lukeh/Downloads/MarkoutAnalyzer_Architecture_UX_Prompt_Part2_81-126.txt)。

三份共同构成完整指令；未猜补截断规则。文件只保留在指定来源，本轮没有修改附件或将其内容散落父目录。

项目根：`/Users/lukeh/Documents/workspace/codex/mt-plugins/MarkoutAnalyzer/`。
本轮输出：`docs/architecture-design/`。
现有 `skills/`、`temp/` 和Skill识别配置不变。没有创建app/data/exports/logs/tests或其它MarkoutAnalyzer目录。

## 采用的 Skills

- [UI/UX Pro Max](/Users/lukeh/.codex/skills/ui-ux-pro-max/SKILL.md)：桌面密度、表格、主题、键鼠、DPI、可访问性设计目标。
- [product-design-and-ux](/Users/lukeh/Documents/workspace/codex/mt-plugins/MarkoutAnalyzer/skills/product-design-and-ux/SKILL.md)：IA、任务流、状态/恢复、界面契约和工程交接；现有固定commit安装保持不变。

本轮完整读取并应用两个Skill，没有安装其它Skill。product-design-and-ux引用的其它专项Skills并未安装，本次直接依据用户需求完成文档，不宣称完成其正式专项评估或原生可访问性合规认证。Skill提供方法，不替用户冻结业务规则；其中平台示例也不决定技术栈。

## 优先评审的技术发现

1. MT4 Terminal 完整历史Bid/Ask Tick尚未证实；本地Manager Tick接口不是许可的替代源（Q01）。
2. Manager与Terminal时间契约须证明一致，不能猜Server Time偏移/DST（Q02）。
3. 原生Money方法可能使用当前group条件/隐含换汇；USD账户不足以证明无转换或历史环境可重现（Q03/Q13）。
4. 最新无效Tick的回溯、并列Tick、Money失败资格、MT4部分平仓与空值计数等规则须明确（Q04–Q12）。
5. 实际SDK ABI、线程、分页、取消和凭据保护仍只有静态材料，没有真实运行验证（A类清单/Q20/Q24）。

完整证据链接在12；这些限制没有通过选择UI框架被解决。

## 评审状态与下一道门槛

本套文档足以进行Architecture Review：确认规则、建议和未知项已分离，关键页面、流程、数据、状态及可验证条目有对应关系。**这不表示设计已获批准，也不表示双平台实现已可交付。**

业务YES问题由用户决定；NO技术问题由技术负责人提供版本对应证据。关闭后同步Decision Register、受影响文档与AC，再由用户授权下一阶段。当前到此停止，不自动进行PoC、技术栈落地、API接入、SQLite或UI实现。

