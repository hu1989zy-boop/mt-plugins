# MT4 Windows 跟单管理器

`MT4TradeCopierManager.exe` 是 MT4 同服跟单的本机管理界面，保留常规、运行状态、信息三页及独立关系编辑窗口。管理协议、配置校验和交易状态机使用 `trade-copier/shared/core` 的共同实现；MT4 配置的平台值为 `mt4`。

## 编译与连接

在 Visual Studio 2022/2026 开发者命令行进入 `plugins/trade-copier/mt4`：

```cmd
scripts\build-windows.cmd -UiOnly
```

脚本构建 Win32 Release，产物位于 `build-windows\package-ui`。只构建管理界面无需 MetaTrader SDK。完整执行器构建和加载步骤见 `docs/windows-deployment.md`（打包后为同目录的 `windows-deployment.md`）。

在 MT4 Trade Server 所在 Windows 电脑，以管理员身份运行管理器。执行器须在 `Armed=1` 且 `ManagementEnabled=1` 时运行，`Armed=0` 时仅可离线编辑 JSON。信息页的服务器标识对应插件 `ServerKey`，默认为 `1`；应用 ID 对应 `ApiAppId`，默认为 `3401`。不同执行器实例应使用独立的标识组合及账本目录。

本机管道为 `\\.\pipe\MT4TradeCopier.Management.v1.<ServerKey>.<ApiAppId>`，与 MT5 管道名称隔离；仅允许本机 Administrators / SYSTEM。管道帧标识 v1 与消息管理协议 v2 是不同层次的版本。此管理器不会通过网络连接远程交易服务器；本轮尚未实现 Web / Java 管理端或 MT4 / MT5 跨服路由。

## 配置与运行

1. 在常规页通过配置文件菜单创建或打开草稿，或连接后读取生效配置。
2. 新增或编辑信号源、跟随账号、手数模式、品种映射和启用状态。新增配置及示例关系默认停用。
3. 保存文件仅保存本机 JSON 草稿；点击应用配置后，以执行器确认结果判断是否生效。
4. 新订阅只跟加入后的新仓。修改参数后，旧仓继续使用持久化的开仓规则，新仓使用新规则。
5. 暂停新仓仍继续管理已有跟单仓位。退出、删除或停用订阅并应用，会关闭该关系所属的跟单仓位；退出中的未决请求仍需核对。
6. 在运行页查看映射、请求和只读核对结果。只读核对不会重发不确定交易请求。

关闭管理器不会停止服务端执行器。后台刷新和命令排队沿用现有管理器逻辑；修改连接标识、连接过期或响应超时后，不自动重放写命令。

## 验证边界

此界面为新 MT4 目标源码。Windows MSVC 构建、界面交互、管理员管道权限、MT4 插件加载及 Demo 交易仍需在目标环境验收；构建成功不会自动标记这些项目已验证。实际交易执行的限制及验收清单以 `windows-deployment.md` 为准。
