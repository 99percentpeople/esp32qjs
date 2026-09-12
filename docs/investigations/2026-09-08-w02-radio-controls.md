# W-02：国家与信道控制

持续目标仍为完成全部剩余 Wi-Fi 功能及最终实机功能测试；长时间 soak 延到
BLE API 完成后。上一 goal turn 已产生扫描字段/信道列表的代码和 C5 编译证据，
属于进展。本批继续实际控制 API，不以本批完成代替完整 Wi-Fi 目标。

基线：firmware HEAD `d7db8d1`，本地 SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`。增量留在独立 firmware 工作区。

## 正式 v1 API

wifi.setCountry(code, options?) 返回 WiFiCountryStatus。

- code 为两个大写 ASCII 字母或 01；具体国家代码支持由固定 SDK 判定。
- policy 为 auto/manual，ieee80211d 为 boolean；默认 auto/true，两个字段同时
  提供时必须一致。未知字段、NUL 后缀、错误类型与多余参数在 driver 操作前拒绝。
- 必须先显式 wifi.start，并保持 Station 已断开、无 native operation、清理或
  其他 feature owner。原有单 application + STA helper 组合允许操作。
- 不隐式初始化/启动/断开或重启；运行 AP/APSTA 时的国家变更仍待 configure 的
  配置事务实现。可以先在 Station 空闲时设置国家，再 stop/startAP。
- SDK set_country_code 后 get_country 读回；返回的是本地 driver 国家，而不是
  scan record 中的 AP 国家。getter 返回无有效代码时按 INVALID_RESPONSE 报错。
- 固定 SDK esp_wifi.h 明确提到国家配置写 flash、切换 PHY 数据；不将此接口
  宣称为 RAM-only 国家配置。是否持久化须按具体 Build Context 实机验证。

wifi.setChannel(channel, { secondaryChannel? }) 返回 WiFiChannelStatus：
channel、secondaryChannel、band、channelGeneration。

- 仅实际标准信道数字；secondaryChannel 为 none/above/below，默认 none。
- 非 5 GHz 芯片提前拒绝 5 GHz；5 GHz 的副信道由 SDK 决定，above/below 明确
  拒绝，不静默忽略。2.4 GHz 的 primary 和相差 4 的 extension 都校验国家范围。
- 仅已启动空闲 STA 或独占 AP。外部 owner、扫描/连接操作、清理和 APSTA 均在
  SDK write 前拒绝。已连接 STA 只允许当前信道的无变化读回请求，不调用 setter。
- AP 单模式按固定 SDK 支持发起 CSA；返回值只是即时读回，可能仍是旧信道。
  不把方法返回当成所有客户端完成切换；后续真实 home-channel 由已有 listener
  与 status.radio 观察。SDK 文档不承诺此 setter 的 channel 跨 stop/start 保留。
- 与内部 feature 的固定信道 claim 分开：本 setter 不创建固定 lease，因此
  后续 scan/connect 不会被自己先前设置的固定约束永久挡住。

## 并发与错误

Radio configuration owner 校验、driver mutation 和 readback 共用同一任务 mutex，
不在 snapshot critical section 内调用 SDK；release、shutdown、native operation
admission 无法在中间穿插。AP lease lookup 仅在 runtime task 取得指针，仍在
mutex 内重新核验精确 identity，旧指针/旧 token 不能绕过 owner 检查。

错误 WIFI_COUNTRY_FAILED / WIFI_CHANNEL_FAILED 的 details 包含 espCode、stage、
driverAccepted。只有 setter 返回 ESP_OK 才把 driverAccepted 置 true；该标志
不证明 AP CSA 已完成，也不保证异常返回时没有驱动内部部分副作用。readback
失败保留 true，不谎报回滚；JS 返回对象分配失败时设置同样可能已经改变。

普通 setter 错误不锁存成不可恢复 Radio init fault。所有选项和返回对象使用
现有 GC roots；不新增资源 pool、持久句柄或 native completion lane。

## 验证范围

本批只执行代表 C5 immutable Build Context 编译与正式类型/注册表/manifest、
feature 文档及 recorded SDK map 的一致性检查。完整 Host 与错误注入、C3/S3/
disabled 矩阵、真实 AP CSA、国家持久化、关联/扫描竞争和 GC/OOM 留到 Wi-Fi
API 完成后的阶段测试。测试 not-run，尚不能提升稳定等级。

后续生产路径测试应覆盖：未启动/连接中/已连接/共享 owner/旧 identity；参数
错误无 SDK 调用；country getter/setter 和 channel getter/setter 各自失败；write
成功而 readback 失败；AP CSA 延迟完成；无变化连接信道请求无 setter；channel
设置后 scan/connect 能继续；所有新对象和选项解析的移动 GC/第 N 次分配失败。

本批未串口操作、刷写、提交、推送或更新父仓库 gitlink。完整剩余范围继续见
[Wi-Fi 清单](2026-09-08-wifi-api-remaining.md)。

最终本批检查：C5 编译通过（退出码 0），MQuickJS syntax 59 sources /
47 snippets 通过；manifest 43 classes / 392 functions、feature 文档 27 项及
recorded SDK map 一致性通过。日志见 build/w02-controls-c5-build.txt 和
build/w02-controls-check-js.txt，源码/产物 hash 见 build/w02-controls-evidence.json。
这些结果不覆盖完整 Host/全目标矩阵/硬件。
