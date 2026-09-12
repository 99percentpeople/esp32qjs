# W-02 Station 连接的嵌套 driver 配置

firmware `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接 [connect PMF](2026-09-09-w02-connect-pmf.md)。当前正式 v1 已支持
`wifi.connect(ssid, {driver: {...}})`，覆盖已经实现的 Station driver 配置字段。
完整目标仍见[剩余清单](2026-09-08-wifi-api-remaining.md)。

## 输入和实现

正式类型是 `Partial<Omit<WiFiStationDriverConfig, "ssid">>`；SSID 保持位置参数，
timeoutMs 保持顶层。driver 必须是 plain options object，undefined 等同未提供。
未知键（含 NUL 后缀）、driver.ssid/timeoutMs/driver 均拒绝。一个 defined 字段只能
在顶层或 driver 中出现一次；即使值相等，双层定义也拒绝。undefined 视为缺省。
不设静默优先级，不通过嵌套配置绕开认证、PMF 或 PHY 的安全/构建限制。

生产 capture 使用生成的 Station driver 字段白名单，排除 ssid 后分别验证两层。
每个输入字段读取一次，存入受 GC root 保护的临时普通对象；随后调用现有完整
Station parser 一次。跨层依赖在合并后处理，例如密码与 OWE、PMF 与 WPA3 threshold
冲突都不会分开校验后误放行。普通和嵌套字段沿用同一默认值及参数校验。
Future prepare 接入该 capture，之后使用现有原生配置副本与生命周期路径。

失败清零 native config；临时对象和原始输入仅在 capture 期间保留 root，无新增
异步 JS root 或常驻 owner/预算账本。合并对象包含调用者提供的值，临时分配成本
尚待集中 OOM/GC 和内存验收。能力发现新增 stationOptions.driver，源类型与 API
文档同步。没有新增 callable 或旧格式别名。

## 验证

C5 immutable Context `build/wireless-contexts/c5`（8 MB/no PSRAM）生产构建 exit 0，
日志 `build/w02-connect-driver-c5-build.txt`。binary 2,771,664 bytes，比上一批增加
720。九项静态 Radio/owner/policy/restart 账本保持不变；链接符号、源码 hash 和
仓库边界见 `build/w02-connect-driver-evidence.json`。

现有完整 Station MQuickJS fixture 已接入生产 merge capture 和共享 parser，新增
双向参数合并、重复字段/未知键/空值、跨层 PMF/WPA3/OWE 冲突、PHY build gate、
原始输入读取次数，以及原有第 N 次失败和移动 GC 覆盖。相关旧 architecture fixture
的 parser 提取范围和已存在的 WiFiConnectResult 返回契约同步到当前实现。
两份 fixture 仅 AST 解析，未导入、编译或执行，不能报告这些行为测试已通过。

manifest 49 classes/469 functions、features 27、live SDK schema 35 STA/21 AP、
MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map、whitespace 检查通过。
Host/Python/VM/GC/OOM/fault、C3/S3/feature-disabled、Wi-Fi 完整阶段和实机功能均
not-run，仍集中进行；长时间 soak 放在 BLE API 完成后。没有刷写、串口操作、
擦除 workspace、前端构建、提交、推送或更新根 gitlink。
