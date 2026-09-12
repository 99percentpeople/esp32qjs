# Wi-Fi API 当前实现与剩余工作

当前实现为 firmware `70f4e88` 加并发修复 `388069d`，固定 ESP-IDF
`fff9895c82d744c7237be8847347bdd1b07c6643`。F-CORE 已提交（`4026f7f`、`1ec39a5`），
W-00 已提交（`d7db8d1`）；本阶段 Wi-Fi 提交及镜像结果见最新验收记录。

## 当前状态（2026-09-13）

本轮实现与可执行的短时验收已收尾。合并镜像 `9e697840…` 在 XIAO C5 上完成十五项短时功能回归与六次
带 pending/retained 资源的 runtime restart。后创建 Station 网络接口的真实崩溃已修复，
原触发顺序及独立联网用例均通过。最终提交镜像 `20f988e2…` 已 preserve 刷入，
四项受影响路径复测、实机覆盖元数据和 workspace 核对通过。完整证据见
[当前短时验收记录](2026-09-13-wifi-c5-short-acceptance.md)。

| 范围 | 实际状态与边界 |
| --- | --- |
| Radio、基础 Wi-Fi、Driver / STA / APSTA | 公开实现已接入；Driver 重建、速率恢复与后创建 Station 本次实机通过；基础联网/APSTA 有分批实机证据 |
| Monitor、Raw TX、CSI 与 wire | 公开实现已接入；当前镜像的保留视图、GC/饱和、普通/LR 发送及恢复、CSI 对应包与 Batch 文件/RPC 通过 |
| 配网、Enterprise、roaming、FTM/TWT、NAN、Mesh | 公开实现与目标编译 gate 已接入；按生产 fixture 和各配置构建逐项验证；缺少对端的 RF 项目不算通过 |
| 共享预算、关闭与 runtime restart | 六次实机 restart 后 owner/operation/CSI pool 归零；同等 healthy 状态的 memory manager 账本相等，largest block 不变 |
| ESP-NOW 对齐 | offline 和共享 Radio 当前镜像通过；五项新增 ESP-NOW 能力仍按 W-10 范围排除，保留 contract-pending |
| 契约、生成物与交付 | 四项生成物检查通过；完整 Python 已执行，两个旧断言修订后 26 项相关回归通过；165 个覆盖条目同步证据，15 个已审查条目达到 implemented；实现提交与最终镜像核对完成 |

本轮提交及最终镜像核对完成，剩余为下列尚未执行的硬件资格与后置阶段。
完整 Python 的原终态为 1252 项、2 failures / 1 skipped；修订后的相关 26 项通过，
原失败记录保留。SDK 覆盖表未满足完整字段审查的条目继续保留审查状态，
不把 source/fixture/build 证明提升为硬件资格或 feature 稳定等级。

Host 适配提交为根仓库 `27fa8dd`。提交只包含 firmware 与必要 Host Build Context / CSI 诊断依赖；不纳入根仓库其他
改动，不自动更新 firmware gitlink。

C3/S3/C5 构建与 feature-disabled 的通过不代表三种 MCU 的实机 RF 资格。
C3/S3 实机、配套对端、独占 USB 与外部 tshark 尚未执行的项目仍保留 not-run。
BLE 新功能在 Wi-Fi 相关验收之后推进；长 soak / 500 次完整生命周期明确延至 BLE
API 完成之后，本阶段不执行。

## 历史记录

以下是逐批实现时的原始状态，含当时尚未接入、仅编译、测试待执行等记录。
**它们不是当前未完成功能清单。当前状态以本页上表和最新验收记录为准。**

<details>
<summary>展开此前的功能推进与审查记录</summary>


**当前验收结果**：`9e697840…` 合并镜像的九项 C5 短测、六次带 pending/retained 资源的 runtime restart 已通过；后创建 Station 的崩溃已实机复测修复。相同 healthy 状态的两次取样 memory manager 账本完全相等、largest block 不变，workspace 核对一致。四项受影响配置构建通过，最终完整 Python 运行中。以[最新短时验收记录](2026-09-13-wifi-c5-short-acceptance.md)为准，下方保留历史批次。

最新状态（2026-09-13）：`ae20d5b9…` 合并镜像四项短测通过，worker watchdog/RPC 缺陷已修复并实机验证。后续 CSI 联网暴露 Station IP helper 后创建时漏掉 netif start 的崩溃；生产回归已复现，代码修复及相关 29 项回归通过，修复镜像待实机验证。资源/重启/共享 Radio、最终契约及提交尚未收尾。历史批次请按对应镜像阅读。

本表按用户“Wi-Fi 还剩什么功能，可继续”的要求，以当前代码重新核对，而非按旧
任务书复选框推断缺口。firmware HEAD 为 `d7db8d1`，Wi-Fi 实施增量仍在独立
firmware 工作区；SDK 为 `fff9895c82d744c7237be8847347bdd1b07c6643`。

依据：`src/core/mqjs_stdlib_esp32.c`（位于 components/esp32_mquickjs）、
`api-manifest.json`、`types/esp32qjs-c-api.d.ts`、实际 Wi-Fi/Radio 实现，以及
02 任务书第 3/7/8 节与 W-01～W-12。生成 manifest 只证明 callable 注册，不能
证明参数覆盖、运行正确性或 RF 能力。后续批次须更新本表的实际状态。

## 2026-09-12 当前剩余摘要

2026-09-13 实机最新进度：Monitor、关联 CSI、Raw TX、SoftAP/APSTA、Driver 重启
已有短时通过记录。停止态速率、AP 保存 SAE 字段和 LR 速率 SDK 校验缺陷均已修复；
Station/AP 完成回调已确认临时 9 Mbps→恢复 6 Mbps，以及 LR 500 kbps→恢复
250 kbps。CSI 对应包及批量文件/RPC 通过。当前 `022f083f…` 合并镜像已刷入，
最新四项受影响配置增量构建通过；剩余为合并镜像短时资源/重启/共享 Radio 检查、
最终 workspace/内存核对及契约文档/提交收尾。独占 USB、缺少对端的 RF 和长 soak
分别保留未测/后置。详见[阶段测试入口](2026-09-12-wifi-stage-entry.md)当前段落，
下方保留历史批次。

### 当前交付状态与后续节奏

- F-CORE 已收尾并提交（`4026f7f`、`1ec39a5`）；W-00 清单提交为
  `d7db8d1`。后续 Wi-Fi 功能增量仍在工作区，尚未提交。
- Radio 生命周期和基础 Wi-Fi、Monitor、Raw TX、Vendor IE、Action/ROC、
  FTM、Enterprise、roaming、C5 TWT、SmartConfig、Station/AP WPS 已有公开
  API 实现；各模块剩余边界仍按下表追踪，当前均不能据此视作运行/RF 验收通过。
- DPP 配网捕获已接通公开 `startEnrollee`、Session status/watch/receive/close/cancel，
  URI 与完整配置列表转换成功后才消费，原生行保留到关闭。TX/dwell/实际 buffer 回收已分开记录；HMAC 入队后
  唤醒失败只重试空 wake，一份有界后续帧副本等旧 buffer 回收后再提交。CHM
  deadline/数字身份隔离与后台 worker 已接入。新增 Radio binding、共享 Station
  预约和原生 Session，接通 bootstrap/listen、URI/配置 copy/commit、关闭后缀与
  runtime 清理。独立配置选择的认证预验证、原生新 identity/配置安装以及无需旧配网
  auth 对象的 Network Introduction TX/重试/超时已编码。Station 连接交接与公开
  `connect(index, options)` 已接入源码、类型和注册；`configurationSelection` 为 true，
  `autoConnect` 仍为 false。连接成功要求获得 IP 并核对实际 BSSID/协商认证，Session
  关闭依次断连、清理原生协议、恢复原 Station 配置/信道/storage。当前保存配置的
  PMF-disabled 状态已接入符合 SDK 安全前置条件的 STOP/PMF 恢复/START 事务，APSTA
  需显式 `allowApRestart`。已知恢复 START 失败可用 `session.recover()` 请求一次显式恢复，
  原故障在 STOP/fence 完成前保留；普通 close 不重试 START。未知来源及未满足前置条件
  的旧状态仍在修改前拒绝。连接批次已补实际 Radio、
  Station、Session/Future、原生认证及真实 VM 转换的回归 fixture，运行验证仍后置。
  最近一次实际 C3 增量镜像仍是 worker 批次的 2,843,776 bytes；本批新增接入只作
  生产源定向语法编译与 fixture AST，未构建/链接新镜像，也未执行动态用例。
  连接批次的局部核对及精确源码 hash 见 `build/w08-dpp-public-evidence.json`。
- 共享资源预算和四项公开 diagnostics 已接通；整机归属/峰值仍待实测。
  NAN 发布/订阅、Service ready/status/close/cancel 与发现/消息接收事件队列已接通；
  `service.send` 已接入独立发送 identity、Future、回调完成时间与真实 buffer 退休。
  后续 NDP 管理帧已接入同一个 32 槽账本，完整 NAF 发送回调纳入关闭屏障；
  请求超时/终止提交不再提前释放 host NDL，确认事件 OOM 不再终止已完成的连接。
  NDP 独立操作 identity、早到原生/host 绑定、精确服务选择以及 Radio 串行的
  request/response/end 内部入口已编码并通过定向编译、局部链接。管理帧 RX、空数据
  TX 回调和两类定时器隔离已接通；普通数据/空数据帧已在原生入队前纳入同一池，
  独立连接的 buffer/callback/timer 退休及精确记录释放已接通内部 Radio/SDK 入口。
  开放数据连接已接通 Service 的 requestDataPath/receiveDataPath 与独立 DataPath
  句柄的 ready/respond/status/close/cancel；使用现有 worker/Future，入站请求未显式
  接受时默认拒绝。未能证明原生退休的记录仍保留至父 Session STOP。
  当前公开批次通过 C5 十个生产单元编译和十九对象局部链接，运行验证仍后置。
  后续已接通 NCS-SK-128 服务凭据、组保护策略及 Vendor 属性，安全开/关分支定向
  编译和局部链接通过，动态验收仍后置。USD 原生启动/关闭、定时身份和 Wi-Fi 任务
  命令调度已编码并定向编译。后续 USD 已接通共享 Action/ROC 及 buffer 退休账本、
  Radio 启动/关闭、Session/Service、独立发送结果和公开 mode/TTL/channel/dwell 参数。
  本批十六个 C5 生产单元编译及十六对象局部链接通过，共享 ticket 变更的 C3 DPP
  单元也编译通过。后续已将公开注册、Session/Future、Radio 与诊断改为支持独立
  USD gate，关闭 Sync 时不注册 DataPath 方法和类，不保留同步回调位图。
  C3 USD-only 的 18 个生产单元、C5 Sync+USD 的 19 个生产单元及两组局部链接
  已通过；实际 ROM 中发现/发送入口均存在，DataPath 只在 Sync 构建存在。
  S3、其他禁用配置、完整镜像和运行/RF 验收仍未执行。
  pairing 原生初始化、数字定时身份、后续帧取消及父 Radio 关闭已接入固定 SDK
  构建副本。后续已编码精确服务/对端服务绑定、显式 NPK 选择、同服务 NIRA 核验、
  服务独立的 ND-PMK 缓存和 Auth/follow-up 共享发送回收。显式 PIN 配对的 Service
  配置、preparePairing、confirm、ready/status/close/cancel 和 Radio/Future 已接通；
  后续已接通 pairingCredentials Future、credentialId 选择和显式重验证；原生 NIK/NPK
  在协议/TX/buffer 完成后才提交到 RAM 缓存，旧 ID/过期凭据不授权认证。
  后续 PIN bootstrap 的 requestPairing/receivePairing、显式确认、同服务对端隔离及发送
  回收已接通；配对开/关定向编译、局部链接和实际 ROM 注册核对通过，运行/RF 验证继续后置。
  固定 SDK 已包含 pairing/PASN，不需要新增另一套 Wi-Fi Aware 组件。
  WAPI 实际启停策略和公开控制已编码，C5 开/关配置定向编译及局部链接通过；支持目标的 Mesh、
  剩余 Driver/AP 控制及故障恢复继续实施，
  其后集中验收。
  Mesh 已加入原生启停/收发、两类 retained receive 与控制事件捕获；C5 定向编译及
  SDK 局部链接通过。后续已编码 Radio 排他交接、配置保存/恢复、checked netif、
  root DHCP 和原生锁外收发。后续已接通公开 Session/Future、send/receive、watch、
  路由表/组地址、ToDS 状态、connect/disconnect/flushUpstream、关闭和显式恢复。
  本批已接通配置读回、router/ID/type、自组织/固定根/冲突、投票和信道切换、IE
  加密及 duty 控制/查询。随后已接通 setParent、scan、receiveScan、flushScan 四个
  方法，源类型、manifest、API 文档及 SDK 映射已同步。本批七个 C5 受影响生产
  单元编译、三十三对象与 Mesh/net80211/WAPI 原生库局部链接、三个 disabled Mesh
  单元编译通过。83 个 public Mesh 函数均有实际入口或框架生命周期映射，Mesh API
  实现范围已收齐，运行/RF 验收未完成。

### 本次进度核对与测试启动条件

最新实机补充（2026-09-13）：XIAO C5 已完成保留 workspace 刷写；扫描/联网、
Monitor configure、实际收包与 retained/GC、CSI offline 和预热后 10 次生命周期
已有通过记录。实机发现的信道 enum、MAC mode 与 RX 长度关系已修复；Driver
重启的默认开放 AP PMF 读回缺陷也已修复，八项定向回归及预热后 3 次完整重启/
停止实机通过。当前不是“等待开始测试”，
而是实机缺陷闭环阶段；Wi-Fi 增量尚未提交，最终镜像的统一验收仍待完成。

此前软件门槛（2026-09-13）：完整 Python 1241 项已结束，原失败为同一 Monitor GC 测试的
三个目标子场景，修订后定向复测通过，外部 tshark 1 项跳过。天线增量、Host CSI
诊断及 Budget Context 的修复与针对性检查均通过；八配置完整镜像矩阵通过。
XIAO C5 正式 Board 按体积优化后保留原应用/workspace 分区，已启动 preserve
刷写；实际终态、重连和功能验收以[阶段执行记录](2026-09-12-wifi-stage-entry.md)
及 `build/wifi-stage-tests/hardware-preflight/` 为准。下文为此前批次历史。

**阶段测试已开始。** 本批静态门槛已通过；首批 Host C 138/138、Enterprise/restart
22/22、MQuickJS 61 源码／69 文档片段通过。完整 Python 首轮已结束但未通过，
完整三目标／禁用矩阵与实机仍待执行；见[阶段执行记录](2026-09-12-wifi-stage-entry.md)。
历史条目继续保留，未验收能力不升级稳定等级。

2026-09-13：修正共享测试的 C 提取、SDK 类型/开关、allocator 和数字 GC 注入边界，
合并 AP/CSI/状态/scan/天线及 GC/OOM 定向复测 **30/30 通过**。完整 Python 首轮
1134 项，457 failures、27 errors、33 skipped；它已导入旧测试代码，失败包含
配置子用例。158 个模块在固定 SDK 下并行复查约 220 秒完成，71 个通过，87 个
仍失败；此后 SDK lifecycle、TWT、NAN/Mesh 的定向结果另列在阶段记录中。
高级功能的失败继续分组处理，完整构建和实机没有因此视为通过。

同日后续：Monitor/Raw TX/NAN 与关联 RX 模块统一复测 43 个模块通过；FTM、watch、
TWT 结果、Driver 读回及扫描参数 ASAN 也已完成定向复测。逐模块台账现为
修复前为 **184 通过、1 失败**（共 185 个）；随后 `test_wifi_restart_configs` 的 C5
快照重试已修复，两个测试（含各目标与 SoftAP 开/关）通过，正确的 Mesh 模块另行
复测 24/24 通过。最新 Host C、MQuickJS 及四项生成物检查通过；完整 Python 正在
运行。八配置完整矩阵修正 Python 环境及旧 Context 预算缺失后已重启，实机仍未
执行。另有 1 项外部 tshark 检查未运行。DPP 失败
START 后显式恢复被关联读取挡住的生产缺陷已修复，保留普通断连保护和 STOP/fence
边界；生产回归及 C3/C5 受影响文件编译通过。最新证据及剩余清单见阶段执行记录，
没有宣称全量回归、完整镜像矩阵或实机验收通过。

2026-09-12 最新收尾：Enterprise restart 已接通健康未关联 Station 的精确凭据／helper
交接、完整 checkpoint 重建、原安全策略重装和失败 borrower 的跨 generation 清理。
普通 stop 继续关闭 EAP；APSTA restart／retry 均要求显式 allowApRestart。新准入误把
started 来源要求为 event_live==0 的问题已在静态审查修正，动态回归尚未执行。
Driver／配网／漫游／TWT 的已支持恢复、清理与拒绝范围已按生产实现收敛为固定表，
不新增通用强制恢复 API。见[本轮实现与测试入口](2026-09-12-wifi-stage-entry.md)。
本批 C3/C5 受影响文件编译与契约检查完成后立即进入集中测试；下方批次记录中的
“Enterprise 完整 restart 待做”和泛化恢复待做为历史状态，以本段与阶段入口表为准。


2026-09-12：SmartConfig SDK 栈中秘密副本的代码尾项已补齐。固定 C3/S3/C5
构建副本中的五个解码函数，在返回／尾调用前清理已审查的局部区，覆盖密码、
凭据事件、ESPTouch v2 AES key 与中间解码缓冲。保留 RISC-V 保存寄存器、
尾调用参数和 S3 call8 的窗口保存区，没有新增分配或存活账本。沿用完整 SDK
hash 校验，并增加函数段 hash；拒绝未审查代码或重复补丁。RISC-V 清理跳转使用
隐藏符号，随链接松弛更新地址；固定段偏移试补丁曾被静态检查否决，未执行或刷写。
三目标生成和隔离重定位链接通过，链接后的 15 个目标/函数、23 个退出点均已
逐一核对清理入口和返回位置；这是静态 SDK 指令证据，隔离链接忽略外部符号，
不代表完整固件链接或运行通过。新增生产转换器、实际清理指令的范围/寄存器
用例及真实 RISC-V 松弛链接回归 fixture，按安排仅检查 AST，未导入、编译或执行。
证据为 `build/wifi-smartconfig-stack/evidence.json`。完整 OOM、ABI/中断、实机
秘密擦除和生命周期留到阶段测试，长 soak 继续后置至 BLE 完成。

2026-09-12：无完整 restart 快照的 mode/protocol/bandwidth 故障已接入现有
`wifi.driver.restore()`。只接受已初始化、完整 STOP、零 owner 且无无关原生清理
的来源；不扩大 SDK 默认值加载器的重置范围。失败保留第一故障及当前清理步骤，
下次显式调用只重试 defaults/readback/storage 中未完成的后缀；读取或 storage
失败不会重复已接受的默认加载。全部接受后恢复 stopped，保留 generation 和
RAM/FLASH 选择。没有新增 API、快照或存活账本，也不声称 NVS 耐久性或凭据擦除。
C3/C5 各三个受影响生产单元编译通过，manifest/覆盖表/配置 schema 一致；生产
回归 fixture 已扩展逐步骤失败、重复调用、首错保留、无关故障/owner 拒绝和
AP-disabled 读回，只检查 Python AST，未导入、编译或执行。证据见
`build/wifi-driver-restore-recovery/evidence.json`。
测试前仍需完成其他故障来源的拒绝/重启语义闭合，以及 Enterprise 完整 restart
策略等已知代码尾项；SmartConfig 栈中副本清理见上方已完成批次。漫游/TWT 动态疑问、
GC/OOM、共存/RF 和整机内存证据进入阶段测试，不新增为实现门槛。

2026-09-12：WPS 的明确代码尾项已收齐本批：AP 的实时 5 GHz RF band 与 Station
频段读取失败时错误默认 2.4 GHz 已修复，M1/M2/M2D 均在消息分配/nonce/密钥工作前
拒绝未知频段。11 个 C5 受影响生产单元编译、局部链接及 registrar 关闭分支检查
通过；这不代表集中运行测试开始或通过。

SDK 覆盖表的 49 个仅标 task 函数已全部核对：上一批补齐 Station WPS 的 3 项，
本批对余下 46 项确认 29 个已有/本批接通的映射、12 个仍缺失 Wi-Fi 行为的 SDK
函数、5 个本阶段不扩展的 ESP-NOW 功能。现在 function 条目没有仅标 task 的记录；
这表示审查完成，不表示功能完成。29 项使用实际 manifest 注册名；另修正历史
Class.method 与 Class.prototype.method 混用，generator 对已审查映射执行注册核验。

本批补齐 `wifi.enterprise.status().checkCertificateTime`：在原有原生资源观察中
调用实际 SDK getter，没有 binding、控制忙或读取不可用时返回 null；不回显待安装
profile，不修改安全策略。两枚布尔值复用 snapshot 原有 padding，没有扩容账本。

本批已收齐其中三项 SDK 行为：全局扫描默认值 get/set 与 CSI 实际配置读回。
扫描参数包含完整四字段验证、零值/null reset、精确 owner 准入、读回/回滚和
STOP/START/restart 恢复；隐藏 Station 的保存值仅作为下一次激活意图，不冒充 getter。
固定 C3/S3/C5 SDK setter 均从 16-byte 参数复制 44 bytes，已用清零的有界 staging
适配。CSI 读取实际原生配置；SDK 库中 C3/S3 没有 getter 实现，capability 明确为
false，调用返回 not-supported；C5 读取全部 HE 字段。未使用请求配置回显填补缺口。

本批继续接通 BSS color collision reporting 的公开控制、策略状态和 post-START
restart 恢复；随后已接通 HE RX/TX 公开控制、实际配置查询及 STOP/START/restart
存储退休。TWT 的实际 flow bitmap、关联节点唤醒偏移和共享策略读写也已接通。
NAN 的三个查询也已接通：`getServiceInfo/getPeerInfo/getPeerRecords` 读取真实 Sync
缓存，在一个原生数据锁内复制服务 ID/name/count、peer type/NMI 和服务归属匹配的
NDI/NDP；列表最多 15 项。USD-only 不注册方法，组合构建的 USD Session 明确拒绝。
这份已审查的 SDK function 缺口表现在没有未接通的函数；覆盖表仍保留 in-progress，
因为阶段运行测试未执行，不能用函数数量宣称 Wi-Fi 全部验收完成。

干净未初始化与 initialized off 的 Driver restart 均已接通；off 保留配置并最终回到
停止/off，AP 策略需要显式 allowApRestart。完整 checkpoint 的失败重建已接通显式
重试，沿同一生命周期 STOP/helper 清理后使用原快照重建。无完整快照的独立
mode/protocol/bandwidth 故障及 restore 自身失败已接入显式 defaults/后缀重试。
其他故障来源及配网/
认证/漫游/TWT 的恢复边界仍需收尾。
运行竞争、GC/OOM、RF、内存峰值留到测试阶段。实现与契约闭合后立即运行 Host/VM
和一次统一目标构建，再做实机功能与集成；不等待 BLE 或长时间 soak。
五个 ESP-NOW 条目是 OUI get/set、SDK version、remain-on-channel、switch-channel TX，
按 02 第 24 节/W-10 的现有 API 对齐范围明确延期，未伪称实现或 target-unsupported。
证据表为 `build/wifi-final-api-audit/mapping-audit.json`；本批生产检查另存同目录。

### 进入统一测试前的剩余门槛

2026-09-12 进度核对：当前仍在实现阶段。Mesh 的公开 Session/Future、收发、watch、
路由/组地址、ToDS、配置/root/vote/duty 控制及手动 parent/扫描已加入生产代码、
正式类型和 manifest，当前共 42 个 Mesh 公开方法。最新 parent/扫描批次的七个
受影响生产单元编译、三十三对象与实际 SDK 库局部链接和三个 disabled Mesh 单元
编译通过；API 文档、类型、manifest 和覆盖清单已同步，动态验证仍按阶段后置。

- [x] Mesh API 实现范围收齐：手动 parent/扫描、清理边界及契约已接通并完成本批
  编译/链接核对。83 个 public 函数已映射；此勾选只完成实现门槛，不代表运行验收。
- [x] 剩余 Driver/AP 控制实现范围接通：保持 Station 连接的 AP 配置激活已接入固定 SDK
  的原生 AP 分配前窗口；天线/GPIO、setCountryDetails 与接口配置读写已接通；
  Driver 状态/能力查询与 restore、健康停机无历史来源 restart 已接通；
  干净未初始化与 initialized off 来源已接通；完整 checkpoint 的失败重建已接通显式重试；
  独立 mode/protocol/bandwidth 故障及 restore 自身失败已接通默认恢复/后缀重试。
  其他来源的拒绝/重启语义已按阶段入口表固定，共享策略与 HE 统计控制已接通。
- [x] 配网/认证、漫游和 TWT 的恢复／拒绝实现范围已固定；Enterprise restart 已接通。
  SDK 无法证明安全退休的状态保持拒绝并诊断，共存与运行验证进入集中测试。
- [x] SmartConfig 五个 SDK 解码函数的局部栈秘密清理已接入三目标构建副本；
  返回/尾调用与链接松弛边界已作静态核对，运行和实机资格验证留到阶段测试。
- [x] 全局扫描默认值、CSI 实际读回、BSS color 和 HE 统计控制已接通；各批生产编译/局部链接及契约检查通过。
- [x] TWT flow bitmap、关联内唤醒偏移和共享策略读写已接通，START/restart 策略恢复已编码。
- [x] NAN 三项原生查询已接通；C5 Sync+USD / C3 USD-only 定向编译、局部链接、ROM 注册边界和契约检查通过，运行用例后置。
- [x] 上述功能的源码、公开注册、正式类型、manifest、API 文档和覆盖清单已完成本批一致性检查；
  Enterprise 最终批 C3/C5 各七个受影响生产文件编译通过。完整目标链接与禁用矩阵进入阶段测试。

这些项目是进入测试的条件。满足后即集中运行 Host/VM、
竞争/GC/OOM 和 C3/S3/C5/feature-disabled 构建，再做实机功能、资源账本及集成验证。
已有模块的动态正确性、RF、整机内存峰值和硬件能力差异属于该测试阶段，不能因
这些尚未测试而反复延后开始测试。BLE API 和长时间 soak 不阻塞 Wi-Fi 测试入口；
BLE 在 Wi-Fi 相关测试完成后推进，长 soak 仍留到 BLE API 完成后。

测试启动顺序明确为：Driver 与配网/认证/漫游/TWT 恢复边界闭合 →
统一契约核对 → 开始集中测试。NAN 查询实现门槛已收齐。剩余实现仍有实质代码工作，
不能把目前状态描述为“仅剩跑测试”或依据方法数量给出完成率；尚无可靠的小时级
完成时间。实现期间发现的动态正确性疑问进入测试清单，只有明确的缺失实现和
编译/契约问题继续阻塞测试入口；不把 RF、整机内存或长周期证明提前设为入口条件。

按用户最新要求，后续以完整功能批次和公开 API 接通为推进/汇报节点。
按完整功能批次合并实现，批次结束后才做必要的受影响生产源语法和契约检查；
不为每次编辑重复编译，不重复检查未变化的输入。需要链接验证时只做受影响配置的
增量构建，不逐次修改构建、不反复运行完整配置矩阵。C3/S3/C5 与 feature-disabled
全矩阵留到 Wi-Fi 阶段收尾，提前运行仅限存在具体构建失败或目标差异需要定位。
新增运行测试仍在 Wi-Fi API 完成后集中执行，随后做实机功能验收、进入 BLE；
长时间 soak 留到 BLE API 也完成后。下方逐批记录是历史证据，旧的“尚未接入”
须结合后续公开实现记录阅读，不能重复计为当前缺口。

当前剩余工作按功能批次收敛如下，不以注册数量计算完成率：

| 批次 | 尚需完成 |
| --- | --- |
| 配网与认证 | DPP 未知故障来源及未满足 SDK PMF 恢复前置的旧状态；WAPI 控制已接通，原生/故障/RF 验收待执行；WPS/SmartConfig/Enterprise 的剩余安全、恢复和共存边界 |
| 拓扑与高级操作 | NAN 同步 Session、发布/订阅、follow-up、开放及 NCS-SK-128 DataPath、组保护策略和 Vendor 属性已接通 Candidate；独立 identity、完整回调、buffer/timer 退休及父 Session 清理已编码，待集中运行验证。USD 独立公开入口已接通，C3 USD-only 与 C5 Sync+USD 定向编译和局部链接通过；显式 PIN 配对、缓存元数据 Future 和 credentialId 重验证已接通；PIN bootstrap 请求/接收与显式确认已接通，配对开/关定向编译及局部链接通过。Mesh 核心、root/vote、实时配置、电源控制及手动 parent/扫描的 API 实现与本批契约核对已收齐，动态验收待执行；TWT 完整故障恢复和剩余漫游能力继续实施 |
| Driver 与 AP | 保持 Station 连接的不同 AP 配置激活、显式 allowDisconnect 配置切换和 deauthClient 已接通，运行/RF 待验收；天线/GPIO、setCountryDetails、getInterfaceConfig/setInterfaceConfig 已接通；Driver 状态/能力查询与 restore 已接通；剩共享策略及故障/未知来源恢复 |
| CSI 与资源诊断 | 共享总预算/control reserve、无线 EventQueue、Wi-Fi helper/ingress RTOS、共享 Future service/worker stack/TCB、显式无线捕获存储、Raw TX、模块 control/复制数据和 CSI/Monitor View/Source/读取租约已接通；CSI header 过滤、公共接收元数据、identity 耗尽、对应原始包、跨代池及总槽子预算已接通 Candidate。统一 snapshot、driver dump、清单覆盖查询、观察计数 reset、队列峰值/reconnect 统计和 Build Context 显式配额/实际配置约束已接通。完整归属及整机峰值仍待测量；driver/SDK/NimBLE/JS/TLS/静态段按 26.5 独立列出，新模块存储仍须接入共享预算 |
| 阶段验收与交付 | Wi-Fi API 齐备后集中运行 Host/VM/竞争/GC/OOM、C3/S3/C5/disabled 构建、实机功能与集成验收、契约冻结；随后 BLE，长 soak 最后 |

后续优先更新本表和对应 API 文档；仅在存在需要独立保留证据的缺陷或 SDK/target
差异时新增调查记录。以本批打通的公开行为和剩余阻塞为汇报内容，减少内部步骤的
重复记录与重复构建。

### 实施记录

2026-09-12：完整 checkpoint 保留时，`wifi.driver.restart()` 已接通一次显式重试。
原子准入核对原 lifecycle/config/policy identity、完整捕获、零 owner/operation/wake、
无其他 feature recovery 及无 reboot-required 状态；不分配新 lifecycle、不重新读取
故障代作为源，不预先清除原 fault/cleanup。lease identity 余量不足在 driver mutation
之前拒绝。成功准入只清空本次 configuration diagnostic，避免新 STOP/init 失败被
错误归到上次的配置结果；原始 Radio 错误保持至实际 shutdown 完成。

runtime 随后 STOP/fence、退休旧或部分新建的 AP/Station helper，沿现有 rebuild/
replay/resume 后缀恢复原 checkpoint；off 标记和 RAM/FLASH 意图不变，off 的 AP
临时启动仍需显式同意。失败继续保留原 token/快照；不会循环重试 START。部分
源快照、NVS/未证明 SDK init、其他 feature 的恢复、残留 owner 均不被此入口接管。
用户也可选择 stop/runtime cleanup 结束原生命周期；快照释放后不承诺再恢复旧来源。

本批 C5/C3 各四个受影响生产单元定向编译通过；已加入生产 retry 准入、运行时
先后顺序/STOP 失败/AP 分配失败/冷来源/off 以及真实 VM 错误详情的待测用例，
仅 AST，不导入或执行 fixture。阶段运行、完整矩阵、实机与 RF 仍 not-run。
本批证据为 `build/wifi-driver-retry/evidence.json`。

当前 Driver 故障来源按实际行为继续核销：有完整冻结来源的重建失败已接通显式
重试；初始化结果不明、identity 耗尽和 PHY antenna 故障保留设备重启要求；storage/
临时 rate 的专属修复继续受精确 owner 与故障来源检查。缺少完整来源的配置/PHY
失败和配网/认证/漫游/TWT 仍需收尾，不把新重试入口写成无限制 fault recovery。


2026-09-12：initialized off 的 `wifi.driver.restart()` 已加入生产路径。原子准入
输出独立 cold/off 标记与工作 mode；off 在首次 SDK 修改前保存接口/global 配置，
使用 RAM 和临时 Station START 读取实际运行值，之后沿用现有 checkpoint/rebuild/
replay。保存的 AP 11b/FTM responder 策略需要 APSTA 时，必须显式设置严格布尔
`allowApRestart:true`；默认拒绝且不领取 token。临时 AP 可能广播，Station 不关联。

最终恢复通过现有 START/策略/配置验证后，内部临时 lease 在锁内归还；继续 STOP、
事件排空、mode=off 写入和读回，然后恢复原 RAM/FLASH 策略。同一个生命周期和
含秘密 checkpoint 仍保留至 AP/Station helper 清理完成，不提前向运行时发布 leases。
任一步失败交由原 central cleanup；新 restart 不重试部分重建，也不把失败 init 当冷来源。

本批 C5/C3 各六个生产单元定向编译通过；manifest/map/schema 一致性检查通过。
已编写实际准入、capture/replay/最终 off 后缀、resume 身份保留、运行时清理和公开
参数 GC/OOM 的待测用例，仅作 AST，不导入、编译或执行运行 fixture。本批没有完整
镜像、实机或 RF 结果。证据见 `build/wifi-driver-off/evidence.json`。其他故障来源与
配网/认证/漫游/TWT 恢复边界继续按实际实现核销，不再把 off 算作缺失代码。


2026-09-12：公开 `wifi.driver.restart()` 已接通干净未初始化来源。原子零 owner
准入返回无前驱标记，运行时沿用现有空 checkpoint 与 Station/RAM 冷启动默认值；
在驱动 init 前不创建 Station helper，不伪造或读取不存在的前驱配置，START/事件
屏障和读回完成后才交接 Application/Station leases。失败后同一 lifecycle 与 cleanup
后缀保留，新 restart 不重复初始化。没有新增持久状态、API 或另一套恢复执行器。

同一判定继续拒绝 partial init、fault/cleanup、已持有 driver 却缺失 storage、owner、
事件及 native pending。共享 NVS helper 的首个失败结果是 boot 内缓存；SDK init
失败不能证明原生清理，因此两者仍要求设备重启，不因新冷启动入口降低故障要求。
此结论来自实际 NVS/Radio 实现；已编写生产准入和运行时排序/失败清理用例，运行
后置。C5/C3 各四个受影响生产单元编译及 manifest/map/schema 检查通过，新增 fixture
只作 AST；本批无完整构建或运行测试。证据：`build/wifi-driver-recovery/evidence.json`。

当前 Driver 剩余实现已进一步明确：initialized off 仍需保留配置重建并最终回到
停止/off，不能按无前驱冷启动默认值处理；其他故障来源按实际 cleanup/所有权及
完整 checkpoint 分开核销。NVS/未知 init、天线和 identity 耗尽等明确设备重启边界
不要求编造运行时恢复能力；其动态拒绝、资源保留和竞态证明进入阶段测试。


2026-09-12：NAN 查询批次已接通三个 Sync Session 方法，严格验证原生服务 ID/name
和非零单播 peer ByteSource。结果为独立元数据快照；未找到返回 null，已存在的空
服务返回空 peers。列表/count 在一次原生数据锁内捕获，15 项上限、数量不一致或
异常记录失败时清空输出；无队列消费、原生写操作、密钥或指针输出。

固定 SDK 单对端查询仅按 NMI 绑定 NDL，列表查询在 NDL 属于其他服务时可能保留
调用者旧字段；新路径清零并核对服务归属，订阅方复用现有受管理 NDP 服务匹配。
精确 Session/Radio token 保护父关闭与原生缓存退休；输入捕获和转换期间独立 retain，
临时元数据使用有界栈空间，ByteSource 复制沿用共享预算。C5 七个生产单元、C3
USD-only 六个单元编译通过；七对象与固定 SDK/ROM 局部链接解析七个指定符号，
实际 ROM 的三个方法仅在 Sync 构建出现。manifest 62 classes / 662 functions、
覆盖清单 1267 项、Station/AP schema 35/21 一致性检查通过。新增生产 SDK/Radio、
Session 关闭与 runtime gate、实际 VM 的 GC/Nth OOM 用例只作 AST，未导入/编译/
运行；证据为 `build/wifi-nan-queries/evidence.json`。完整目标矩阵与实机/RF 仍待执行。


2026-09-12：TWT 附加控制已接通 `getConfig/configure/getFlowStatus/setTargetWakeTimeOffset`。
原生配置是两个实际 PM 字节的有界观察；flow bitmap 由真实 SDK native getter 提供，
范围 0..255，不从 managed handle 计数推断。偏移严格验证 0..102400 微秒，在同一
Wi-Fi task 回调内核对关联、调用实际 handler 并读回节点 scalar；不跨关联或 restart
回放。共享策略允许精确已管理 TWT owners，拒绝其他 owner、未取得的 token、忙状态
和 fault；普通 START 恢复显式意图，物理 restart 捕获/回放实际策略并最终读回。
配置不隐式连接或启用 modem sleep，keep-alive/event 副作用已写入 API 文档。

固定 SDK 的三个 public wrapper、对应 ioctl handler、PM policy setter 和 flow getter
反汇编已保留。原生 handler 等价路径通过既有 blocking dispatcher 串行运行，不在
Wi-Fi task 中递归进入 public ioctl。C5 六个受影响生产单元、C3 两个单元定向编译和
六对象/实际 SDK/ROM 局部链接通过，11 个指定原生和公开符号解析成功。manifest 为
62 classes / 659 functions，覆盖清单 1267 项；原生 dispatch、owner、restart 及实际
VM GC/OOM fixture 只做 AST，没有导入、编译或运行。证据为
`build/wifi-twt-controls/evidence.json`。当前仅余 NAN 查询三个 SDK 函数与恢复边界收尾；
完整目标矩阵、运行/实机/RF 仍留到实现完成后的测试阶段。

2026-09-12：BSS color collision reporting 已接通公开 API。严格 boolean、HE gate、
已启动 Station/APSTA 和精确 framework owners；不创建 JS 事件订阅。SDK 接受会清空
原生 collision bitmap，包括重复值；返回值只证明写入接受。复用现有 boolean policy
revision/error/history，新增状态仅存在于 HE 构建；restart 在 Station START 后回放，
不含 Station 的目标模式在破坏源状态前拒绝。C3/S3 返回 not-supported。

HE 统计的真实 SDK 分配边界发现并修复三条确定代码路径：RX HAL 丢弃普通统计的
分配错误、被 MU 成功覆盖且最终返回 0；重复普通 RX enable 覆盖旧指针；TX 部分
分配失败调用 disable 时 ACI bit 尚未置位，导致清理跳过。构建内 HAL wrapper
保留首个 RX 错误、替换前释放旧 RX pair；TX 失败通过 SDK 原 allocator 释放本 ACI
三个 base，五个 interior alias 只清零，不触碰其他 ACI。固定 libpp 和 C5 ROM data
layout 均有 SHA256 gate，结构大小有 production static assertion；共享 SDK 未改。
后续已接通 `getStatisticsConfig()`、`configureRxStatistics({ordinary,multiUser})`
及 `setTxStatistics(category,enabled)`；公开参数严格预验证，C3/S3 capability 为 false。
实际配置通过 Wi-Fi task 上的原生 owning pointers/ACI bitmap 有界观察，不回显请求。
SDK 错误保留；读回不可证明时记录 Radio fault，已知失败结果不猜测回滚计数器。

STOP 在 Wi-Fi task 内先捕获开关并释放所有统计 owner，再提交原生 STOP；分发未执行
时保留 obligation，阻止 STOP/deinit。NAN 的独立 STOP 入口也接入同一清理。普通 START
恢复独立保存意图；显式 restart 在已有预算内 snapshot 中冻结配置，post-START 恢复
并最终核对实际状态。RX 与四个 TX 类别保留成功前缀，后续失败不重复已经成功的 RX
替换。shutdown、driver restore 清空意图；不会承诺跨 runtime restart 保留配置或计数。

本批同时修正 `getScanParameters` capability 的 available=false 错标，并将扫描 getter
归入正确的 read-error operation/stage 类型。新增/扩展的实际 Radio、restart、HAL
分配/dispatch 及 MQuickJS GC/OOM 回归源码只做 AST，没有导入、编译或运行 fixture。
C5 八个生产单元、C3 五个生产单元编译通过；八对象与实际 libnet80211/libpp/C5 ROM
局部链接通过，29 个指定符号已解析，实际 ioctl relocation 指向两个修复 wrapper。
manifest 为 62 classes / 655 functions，覆盖清单 1267 项；完整矩阵、动态/实机/RF
仍未运行。证据 `build/wifi-he-controls/evidence.json`。此批次结束时余两组六个 SDK 函数及
恢复边界；Wi-Fi 增量未提交，长 soak 后置。


2026-09-12：全局扫描默认参数与 CSI 实际配置读回已接通。新增三个公开方法，
manifest 为 62 classes / 651 functions。扫描控制使用现有 helper/Radio mutation
串行化；SDK busy/OOM 拒绝后先观察前值，只在必要时写回并验证，保留首个错误。
failed rollback 或 activation restore 保留故障，既不清除原始错误也不自动重新 START。
保存值采用同一个 driver generation 的小型 RAM 记录，restart 快照沿用现有无线
control 预算。普通 owner 接入不重复读写扫描参数；恢复只在 Station 激活时触发。

三个固定 target 库的反汇编均证明 scan setter 拷贝 44 bytes，public struct 为
16 bytes。生产 helper 使用 44-byte zeroed union，不修改共享 SDK、不替换 ioctl。
新增未执行 ASAN 对照源码用于复现未适配传参并验证生产 helper；同时补 Radio
owner/失败/隐藏意图、实际 restart、CSI lease 和真实 VM GC/OOM 转换回归源码。
已有 restart、STOP snapshot、restore fixture 的相关依赖同步；本批仅 AST，没有
导入、编译或执行任何运行 fixture。

CSI target 差异由实际库符号核对：C5 有 `esp_wifi_get_csi_config`，C3/S3 所有 Wi-Fi
静态库均无该定义。`supports.captureConfigReadback` 对 C3/S3 为 false；公开方法
有明确 not-supported 结果且不引用缺失原生符号。C5 复制实际 HE 配置，独立于
`status().requested`；`enable` 只代表配置 bit，不能用作 Session 存活证明。

七个 C5 生产单元编译、七对象与实际 net80211 局部链接、十个指定符号解析通过；
C3 Radio/Driver/CSI 三个生产单元编译通过，getter 不支持分支无未解析 SDK 引用。
manifest、1267 项 SDK map、STA 35/AP 21 字段契约检查通过。证据为
`build/wifi-scan-csi-review/evidence.json`。完整构建矩阵、动态/实机/RF 仍 not-run，
Wi-Fi 增量仍未提交；长 soak 后置。当前余四组共九个函数及上述恢复边界。


2026-09-12：SDK 函数映射审查与 EAP 时间策略观察。本批 46 项分别落实为 29 项
实际映射、12 项六组明确 Wi-Fi 缺口和 5 项 ESP-NOW 范围外扩展，保留 planned/
contract-pending，未以框架独占掩盖未实现行为。剩余清单已由模糊恢复批次收敛为
具体公开行为；仍需完成相应代码和恢复边界，不能据零 task-only 数量声明 API 完成。

EAP status 新增实际 SDK `checkCertificateTime` 观察；忙/无绑定/不可用为 null。
读取错误不伪造默认策略，也不抹掉同次资源观察；复用原有 native task dispatch。
生产 static assertion 保证 snapshot 仍为 16 bytes。七个 C5 受影响生产单元编译，
七对象局部链接和七个指定原生/框架符号解析通过。三个 Python 源文件仅 AST，新增
回归分别覆盖生产 snapshot 的读取/dispatch 失败以及生产覆盖校验器的注册 identity；
均未导入、编译或执行 fixture。manifest 62 classes / 648 functions、1267 项映射
和 STA 35/AP 21 字段生成物一致。证据 `build/wifi-final-api-audit/evidence.json`。
完整 C3/S3/C5/disabled 矩阵、动态/实机/RF 仍 not-run，长 soak 后置。


2026-09-12：恢复/共存清单核对与 AP WPS 5 GHz 批次。SmartConfig custom data/watch
已在公开实现中，修正文档开头的过期 pending；NAN 三个 DataPath 函数已接通正式
Service/DataPath，补齐遗漏的 reviewed 契约映射，运行状态仍为 not-run。

确认 AP WPS 的 2.4 GHz 限制来自固定 SDK registrar 写死 RF band。本批已改为原生
初始化和每个 M2/M2D 读取当前运行频段；初始化读错保留原错误，后续未知/非法
RF band 在分配消息及派生密钥前失败，不回退或沿用旧 2.4 GHz。Radio 接纳目标和
国家策略允许的 5 GHz AP；APSTA 后续换频反映到新协议消息，普通关联/互通仍待测。
capabilities 新增 apRegistrar5GHz，只有 registrar 和 5 GHz build 同时启用才为 true。
已补原始/修补回调、M2/M2D、Radio 准入和能力转换回归源码，仅 AST，未执行。
同批已修复 Station SDK band-mode 读取失败/未知值也默认 2.4 GHz 的问题；M1
使用与 M2/M2D 相同的单次读取和分配前检查，保留原生协议失败路径，不新增公开
错误码。Station 文档的旧 2.4 GHz-only 限制已按实际 Radio 准入修正。
本批 11 个 C5 生产单元编译、11 对象与实际 net80211 库局部链接通过；九个指定
符号已解析。registrar 关闭配置的 WPS 公开单元编译通过；六个 Python 源文件仅
AST。证据 `build/w08-wps-band-evidence.json`；完整矩阵、动态和 RF 仍为 not-run。

SDK 函数清单另有仅填写 task 的历史条目；不能把它们的默认分类当作已实现。
后续逐项核对实际替代入口、框架独占边界或缺失控制，继续保留原定 API 目标，
不以“没有 planned 字样”作为启动测试的证据。

2026-09-12：健康停止来源 restart 批次。已移除对完整 STOP 历史的无条件依赖；
已初始化、完整停止的 STA/AP/APSTA 在无其他 owner、原生绑定、事件退休或临时
策略恢复责任时，可由同一个 lifecycle 接纳。完整合格历史仍走原观察路径；
无历史/停止后改写先准备源 Station/AP helper，在预算内分配并捕获可读配置与
已知策略，再以 RAM 启动源配置。启动后重新读取实际配置和全局设置，连同运行
功率/信道/阈值进入快照，避免混合启动前后状态。Station 不连接，AP 可短暂广播。

source RAM/START 或后续读取失败保留同 identity 的部分快照、原生故障与中央
清理责任；不重放部分结果、不自动重复 START。失败前未修改原生状态的临时配置
立即清零释放。已补生产准入、源 START/SDK 逐点失败、OOM、配置规范化及 runtime
helper 顺序/失败退休的 fixture 源码；按用户要求不导入、编译或执行测试。
本批四个 C5 生产单元编译、44 对象与修补 SDK 库局部链接、SoftAP 关闭配置
下两个受影响单元编译通过；manifest 62 classes / 648 functions、1267 项 SDK
覆盖与配置字段生成物一致。六个 fixture 文件仅 AST；未导入/编译/执行。证据
build/w07-restart-source-evidence.json；无完整矩阵、动态/RF、实机或刷写。

剩余恢复审查继续区分具体来源：未初始化/off、faulted 或原生退休不明，以及不能
从 driver getter/已知记录恢复的策略值；本批已接通的健康停机来源不再重复列缺口。
TWT/Action/FTM/Raw TX 已有显式 recover，DPP 已有已知 START 恢复失败的 recover；
剩余需核对其拒绝来源与清理/共存边界，不能按历史草案把整个入口重新计为未实现。

2026-09-12：Driver discovery/restore 公开批次。capabilities 覆盖当前 Driver
注册方法及其主要 SDK 控制、准入说明、secret-readback 构建开关和 Station/AP
字段表；status 使用与 wifi.status().radio 相同的转换，不初始化 Station/netif。
restore 只在已初始化、完整 STOP、无 owner/原生清理/临时策略/重启快照时执行；
不隐式关闭 Session。保留物理 generation/identity 空间，模式读回后重选 storage；
原生失败保留原始 stage/error，撤销旧 STOP 与策略观察，不猜测回滚或运行时恢复。

固定 SDK 的 wifi_nvs_restore 和 wifi_restore_process 原本覆盖默认加载器的错误
返回，包括分配失败；已在现有 build-local archive 补丁中保留原生错误，未复制 SDK
可变状态。C3/S3/C5 补丁生成与实际 ELF 反汇编核对通过。加载器内部仍不传回所有
NVS setter/commit 错误，因此 true 不证明持久化或凭据擦除。运行验证须注入原生
加载器 OOM 和 NVS 故障，仍在阶段测试清单，未把静态指令证据写成动态测试通过。

本批六个 C5 生产单元编译、四十四对象与实际修补 SDK 库局部链接通过；类型、
注册、manifest 和 API 文档已同步，manifest 62 classes / 648 functions。新增生产
Radio 准入/逐 SDK 故障及真实 VM 能力转换 GC/OOM fixture，并适配共享状态转换和
SDK 补丁字节范围 fixture；只做 AST，未导入/编译/执行测试。证据见
build/w07-driver-final-evidence.json。仍需共享策略及未知故障恢复、配网/认证/漫游/TWT
恢复共存、统一契约核对，然后集中测试；无完整矩阵、刷写、提交或根 gitlink 修改。

2026-09-12：Driver 接口配置读写批次收尾。getInterfaceConfig 返回独立 Station/AP
快照，setInterfaceConfig 复用完整参数捕获及 stopped/零 owner 配置事务，保持
mode/storage 选择；setter 返回实际读回且始终隐藏凭据。字段表已补读回方向，
生成 C 字段宏与正式快照类型，覆盖 35 个 Station、21 个 AP 非保留公开字段。
原始 SSID 字节、实际枚举 ID、两个 PMF 标志及目标字段均保留，不把字段存在视作
功能已启用，也不将只读快照当成可写配置 patch。

新增默认关闭的 WIFI_ALLOW_SECRET_READBACK 构建开关。未请求秘密时，原生 read
在返回前清除密码/SAE identifier；失败清除整个输出。显式请求在开关关闭时于
SDK 调用前拒绝；开启时返回有界文本/字节。捕获、事务和转换中的配置存储统一
使用 control 预算，所有退出均清零释放。共享 unsupported 错误保留实际 operation。

六个默认 C5 生产单元编译及四十四对象原生库局部链接通过；独立、保留的
secret-read-enabled Build Context 下两个受影响单元编译和同规模局部链接通过。
修正了实际 SDK SAE identifier 的无符号字节指针转换。manifest 62 classes / 645
functions、1267 项映射和字段生成物一致。新增三个 Radio/VM 方法，并扩展原始
捕获和 schema 测试共三个方法；本批只作 AST，未导入、编译或执行测试 fixture。
生产编译不等于动态/实机/RF 验收；完整矩阵、运行与硬件测试仍待 Wi-Fi 收齐后
执行。证据 build/w07-interface-config-evidence.json；无完整镜像、刷写、提交或
根 gitlink 修改。后续继续 Driver 状态/能力查询、restore 与恢复/共存边界。

2026-09-12：国家配置批次收尾。公开 setCountryDetails 复用 configure 参数捕获和
Radio 配置事务，要求已初始化、完全停止、零 owner；Station/AP/APSTA/off 均可使用。
完整范围/环境/5 GHz mask 在写入前验证；返回实际读回的只读功率。环境字段保留 SDK
第三字节 X，源码、正式类型、文档及映射同步。语义相同只读回；修改失败恢复前值，
保留原始错误与独立 rollback 诊断，不将 RAM 读回冒充 NVS 恢复。

同时修正 wifi.setCountry 只验证读回国家码格式、未验证请求值的路径：成功要求
code/policy 匹配，失败按实际前值回滚并记录到 radio.configuration；原生 setter
返回错误也按可能已修改处理。不能确认恢复时保持 Radio fault/cleanup。
本批编写了实际 Radio 函数的 SDK 失败、错误读回、旧 lease/排他准入与回滚场景，
以及生产 JS 捕获/转换的 GC/OOM/参数预验证用例。四个新增方法仅 AST，未导入、
编译或执行；旧 code 读回缺口按源码确认，动态失败复现留到用户指定的统一测试阶段。

六个 C5 生产单元编译、四十四对象与实际 SDK 库局部链接通过；三个国家配置 SDK
调用和新增框架入口已解析。manifest 62 classes / 643 functions、1267 项覆盖清单
一致。没有完整矩阵、运行测试、实机/RF、刷写、提交或根 gitlink 修改。证据
build/w07-country-details-evidence.json。Driver/AP 下一项为接口配置读写；测试入口
仍为剩余实现收齐后集中验收，RF/整机内存及长 soak 不提前作为入口条件。

2026-09-12：天线控制实现批次收尾。setAntenna/setAntennaGpio 已接入生产事务、
公开注册和正式类型。完整参数捕获及成功结果分配先于原生写入；Radio stopped/零
owner、BLE adapter/controller 关闭、可选 IEEE 802.15.4 disabled 与 SDK PHY access
lock 共同约束写入，sleep/idle 不冒充释放。GPIO 使用四项按需 control 预算账本，
原子预约新 pin，保留原 pad/output 路由；替换/移除恢复前值，读回同时检查 SDK
保存值与真实输出信号选择。外部已有/被外部改动的路由拒绝覆盖。全部取消选择后
释放 pin 与账本；配置和 ownership 按共享 PHY 语义跨 Wi-Fi deinit/runtime restart
保留。无法确认回滚时保留 pin 预约并锁存设备重启故障，Wi-Fi init 与 BLE open
共同拒绝，原始失败和 rollback 诊断仍分别可读。

C5 的 13 个受影响生产单元（包含相关 SDK 调用实现）编译、44 对象与原生库局部
链接通过；11 个天线直接 SDK 调用及新框架符号已解析。新天线路由单元及两个 PHY
SDK 替换单元在 C3/S3 定向编译通过；Wi-Fi-disabled 新天线单元为空对象。修正了
C3/S3 与 C5 输出反相寄存器字段的差异。manifest 62 classes / 642 functions 与
1267 项清单一致。新增/扩展 9 个 Python 回归方法仅 AST，未导入、编译或执行；
运行、完整矩阵、RF、实机和整机资源验收均待集中测试。证据
build/w07-antenna-write-evidence.json。没有完整镜像构建、刷写、提交或根 gitlink 变更。
Driver/AP 剩余共享策略和其余恢复，随后完成配网/认证/漫游/TWT 边界并进入统一测试。

2026-09-12：共享 AP 不同配置激活已接通。配置/PMF 专用操作和安全读回在原生 AP
分配前完成；确认并修正分配后写入可触发提前 AP start 的原生顺序缺陷。请求存储
按精确 lifecycle 保留到同步原生命令返回后清零。已知未启动的 AP 通过事件 marker
与 stopAP 退休；仅 RAM 完整回滚及资源清理后解除本事务故障，FLASH/未知来源及
不完整回滚继续保留故障。三个受影响 C5 生产单元编译、34 对象与原生库局部链接、
一个 Wi-Fi-disabled 生产空对象检查通过。三个目标私有入口生成与原生 AP slot
偏移已核对；并未运行完整构建矩阵。8 个 Python 用例仅 AST，Host C 新增 3 个清理
场景仅编写；运行/RF 未执行。详见共享 AP 重开记录和
build/w02-ap-prestart-evidence.json。Driver/AP 批次剩共享策略、天线/GPIO 控制及
恢复，不把本项完成写成整个 Wi-Fi 或硬件验收通过。

2026-09-12：Mesh 手动 parent/扫描批次收尾。setParent、scan、receiveScan、flushScan
复用同一 Session worker 和排他 Radio。手动扫描使用阻塞 SDK 调用，以原生返回确认
完成；未知终止保留 owner 内 SSID/BSSID 存储及 scan identity 至物理 STOP。结果只在
有界原生记录分配成功后从 SDK 取出，完整 JS/ByteView 转换后才按精确 identity
提交；转换失败或等待取消可重读同条。flush 校验原生列表已清空才释放占用。

状态 getter 已有断连失效保护，经核对保留；另补直连 router 的 MESH_STA 角色 DHCP，
与 root 共用同一网络处理，node/leaf 继续不运行 DHCP。DHCP/IP 查询后重验原生角色
及事件 revision。新增实际 helper/SDK/Session 回归源码；本批累计 24 个用例仅 AST，
未导入、编译或执行。测试阶段需覆盖实际断连、角色切换及扫描时序。

七个 C5 受影响生产单元编译通过，本批最终修改只重编 Radio/Session/diagnostics。
三十三对象与实际 Mesh/net80211/WAPI 库局部链接通过，84 个实际调用的 Mesh/Wi-Fi
SDK 符号解析；三个 disabled Mesh 单元编译为空对象。manifest 62 classes / 640
functions，42 个 Mesh 方法；1267 项清单核对通过，其中 83 个 public Mesh 函数
及 topology 类型共 84 项映射保持 in-progress，未提升为运行/硬件完成。当前 C5
ROM 实际 160 个 Future driver，全部 feature 注册集合 178，小于既有动态上限 256。
源类型、API 文档及覆盖清单同步；证据 build/w08-mesh-manual-evidence.json。
没有全量镜像构建、动态测试、实机/RF、长 soak、提交或根 gitlink 变更。

Mesh API 实现不再阻塞后续 Driver/AP；测试入口仍为两个剩余实现批次及统一契约
核对完成后开始集中测试，不以未执行 RF/整机内存测试推迟测试启动。

2026-09-12：进度核对补记。Mesh setParent/scan/receiveScan/flushScan 已在生产
adapter、worker、SDK 边界、runtime 注册及正式类型中接通，manifest 当前为
62 classes / 640 functions，其中 Mesh 42 个方法。现有本批记录显示 Radio、
Mesh SDK/Session/adapter、Wi-Fi adapter、js_stdlib 六个生产单元定向编译通过。
SDK/Session/controls/manual 共 23 个用例仅 AST；本批最终 SDK 链接、覆盖清单、
API 文档与清理边界仍需收尾。构建记录在 build/w08-mesh-manual-review/，并非
本批最终验收证据。本次进度核对仅更新文档，没有再次构建或运行测试，没有操作设备。

2026-09-12：Mesh 配置与控制公开批次，新增二十项 Future 方法：configuration、
setRouter/setMeshId/setType、setSelfOrganized/setFixedRoot/setRootConflicts、
setAssociationExpiry/setRootHealingDelay/setIEEncryption、waiveRoot/switchChannel、
setDeviceDuty/setNetworkDuty/signalDuty、subnet/hasGroup/upstreamCapacity/powerStatus/tsfTime。
启动选项补 allowChannelSwitch、allowRouterSwitch 和 votePercentage，投票阈值在
SDK start 前设置。原有 worker/Radio/job 边界保留，扩展命令按需分配对齐数据，
取消/关闭不提前释放 SDK 仍引用的输入。配置默认隐藏秘密，显式读回之外的凭据副本
在 SDK 返回后清零；IE 两步修改报告成功前缀和失败阶段。角色前置在原生执行时检查。

SDK 二进制核对确认 duty signaling 的次数 + 1 被存入单字节，公开输入限定 0..254；
关联过期 setter 可在内部 OOM 后返回成功且忽略 ioctl 错误，增加原生读回，不匹配
报告未确认错误，不自动重放。get_layer 仅在已观察 parent 连接后采样。SDK 尚不支持
指定替代 root 和 UPLINK duty rule，依据固定头文件拒绝这些参数，没有占位接口。

七个 C5 受影响生产单元编译通过，最后两处边界修正只重编 JS/SDK 两单元。
三十三对象及 Mesh/net80211/WAPI 库局部链接通过，实际使用的 43 个控制 SDK 符号
全部解析。Wi-Fi disabled 三个 Mesh 单元为空对象。manifest 62 classes / 636
functions、1267 项清单一致；74 项 Mesh 映射保持 in-progress，不能视作 RF 通过。
SDK/Session/控制共十七个用例只 AST，未导入/编译/执行；完整矩阵、运行、实机/RF、
整机内存和 soak 未执行。没有全量镜像或提交。证据为 build/w08-mesh-controls-evidence.json。

本批后 Mesh 剩余手动 parent 选择、扫描处理及最终 SDK 操作覆盖；Driver/AP 和
配网/认证/漫游/TWT 收尾继续按上述门槛实施。


2026-09-12：Mesh 核心公开批次。新增 open/status/ready/close/cancel/recover、
send/receive/watch、routingTable/groups/addGroups/removeGroups、setToDSState、
connect/disconnect/flushUpstream。使用现有共享 worker、Future、EventQueue 和预算；
公开身份不回绕，job payload 复制并按 mesh_addr_t 对齐。SDK 调用提交、返回、原始
错误与等待超时分开；Future 释放不提前释放仍在使用的 native input。

Session 关闭会清理 worker polling 期间新入队、尚未提交的命令；启动前取消也清零
配置。SDK STOP 先在原生状态捕获，worker 接管收尾；设备重启类故障不反复排入
无效 worker。接收完整转换后才 commit，失败立即关闭私有 ByteView，原生槽可重试。
watch 饱和只丢观察，commit try-lock 失败不会再次发布同一 sequence。runtime 先关闭
观察，再等待 native Session/worker 退休。status 包含 parent/角色、DHCP、原生队列
及恢复阶段，无凭据字段。parent/root/vote、实时配置和其余策略仍未收齐，不宣称
完整 Mesh；关闭/恢复及 SDK 事件/数据时序仍待动态验证。

收尾核对发现共享 Future 的固定 128 项注册表不足：当前实际 C5 ROM 有 136 个
需注册方法，manifest 条件并集有 154 个。改用原 runtime allocator 按需分配的
16 项稳定 chunk，最多 256 项；不搬移 GC roots，小配置不预留全部容量，OOM
不丢已注册前缀。teardown 在 native Futures 退休后移除 roots 并归还 chunk。
同时修正 scheduler 丢弃 on_timeout 成功返回值的问题：Mesh/NAN 空接收的 null
现在作为成功等待结果交付，结构化异常仍拒绝；取消/清理期间结果保持 GC root。
这些结论来自生产代码与 ROM 核对，尚未运行启动/超时回归。

十个 C5 受影响生产单元编译、实际 SDK 三十三对象局部链接通过，Mesh 相关符号均
解析；参数宏缩进修正只重编失败单元。随后补齐角色/队列转换、payload 对齐与
共享 Future，只重编变化单元。Wi-Fi disabled 的三个 Mesh 生产单元编译后无定义
符号，共享 Future 也定向编译通过。manifest 62 classes / 616 functions、1267 项
清单一致；36 项 Mesh 映射保持 in-progress，不把公开接入算作运行通过。
SDK fixture 七例、生产 Session 调度五例、新增生产 registry/deadline 五例及更新的
容量架构三例仅 AST，未导入/编译/执行；JS GC/OOM、完整矩阵、实机/RF 与整机峰值未执行。
无全量镜像、soak 或提交。证据见 `build/w08-mesh-public-evidence.json`。

2026-09-12：Mesh Radio 交接批次。健康零 owner STOP 或冷初始化准入，中央
lifecycle 和精确 Mesh lease 保留到原生退休、STOP、netif detach/fence 及配置恢复。
复用共享 checkpoint/replay，C5 先以空 Station 切换 2.4 GHz，再启动 Mesh 临时 AP；
原配置包含 AP 时要求 allowApRestart，恢复验收会短暂启动原 AP，最终保持 STOP。
阻塞 SDK 调用释放 Radio mutex，busy pin 与独立 seal 防止关闭提前释放 native input。
原生 deinit 已完成时只重试后续清理；重放/START 失败保留原快照，由显式 recover
授权重新进行物理恢复，不重复创建 Mesh。未知 Mesh/driver init/deinit 失败保留故障。

checked netif 创建保留部分失败对象，复用既有 detach/fence 退休；root DHCP 依据
原生控制状态及有效角色快照，IP 与 ToDS/Internet 可达分开。共享 status 增加
clients.wifiMesh；未增加 Mesh Session 类或 callable。C5 本批八个受影响生产单元
定向编译和既有对象局部链接通过；修正 SDK bandwidth 常量后只重编 Radio 单元。
构造、关闭、GC/OOM、事件竞争、DHCP/角色变化及恢复仍待统一运行验证；新增 seal
竞争 fixture 仅 AST。没有全量构建、实机/RF、soak 或提交。精确输入和局部检查见
`build/w08-mesh-radio-evidence.json`。下一批接通 Mesh Session/Future 与公开控制面。

2026-09-12：Mesh 原生层批次。核对固定 SDK 三目标库及 lifecycle，新增单 owner
的原生 init/config/start/deinit、发送、普通/ToDS 接收、诊断和事件 copy/commit。
不在临界区调用 SDK；close 先封闭准入，正在执行的发送保留原生输入与 owner。
init 部分失败不能用返回成功的空 deinit 证明恢复；未知清理失败保留原始错误且不
重复释放。Mesh 默认事件 post 为零等待，包装层先捕获控制状态，队列满仍记录断连。
SDK 与框架输入副本分开记账，框架配置副本在原生 setter 返回后清零。

C5 两个生产单元编译和实际 Mesh 库局部链接通过；Wi-Fi disabled 只编译新增单元，
对象无定义符号。C5 DWARF owner 748 bytes，加共享预算内的 3000 bytes 接收池；
本层静态符号共 9 bytes，SDK/整机峰值仍未测。六个生产实现用例只作 AST，未导入/编译/运行。没有全量镜像、
实机、RF、soak 或提交。证据见 `build/w08-mesh-evidence.json`。当前没有 Mesh 公共
callable、类型或注册项；下一批接入 Radio 交接、Session/Future、root DHCP 与控制面。

2026-09-12：WAPI 控制公开批次。原始 C3/C5 RISC-V 与 S3 Xtensa 库核对确认，
WAPI 初始化由 supplicant 调用；三次分配的 -1/-2/-3 失败各自释放前缀，原生 deinit
则可能先释放状态再返回 callback 注销错误。构建核验固定库/调用源码 hash，仅包装
原来的 init/deinit：记录实际原生状态、非回绕代次与策略 revision，关闭策略跳过
原生创建；错误不被成功的外层 Wi-Fi deinit 吞掉，未确认清理不再重试释放或创建。

新增 wifi.wapi.capabilities/status/enable/disable。未初始化时设置策略；已初始化时
要求健康 STOP 和零 owner，checkpoint/旧 helper 退休后才提交策略，复用实际物理
deinit/init 和既有配置重放。恢复后接口会启动，API 明确这一副作用。状态区分请求
策略、已应用策略、实际 enabled 与未知状态；不保存新凭据，不宣称 WAPI-only
selector。控制沿用 Radio 排他生命周期和中央清理，不增加后台任务或一套 JS owner。

同步修复集中能力表仍把已实现 broadcast TWT、RRM/WNM、SmartConfig 写为 false，
以及遗漏 diagnostics/Enterprise/roaming/provisioning namespace 的旧记录。三个
生产 SDK/Radio/runtime/VM 回归 fixture 仅 AST。C5 WAPI 开/关配置各九个受影响
生产单元编译通过，复用既有对象后各完成二十三对象局部链接；相关桥接符号已解析。
manifest 61 classes / 598 functions 与 1267 项覆盖清单一致。本批未构建完整镜像。
实际 ROM 的四项 WAPI 方法只在开启配置存在；局部链接重定位确认 supplicant 调用
包装层，包装层再调用原生库。证据见 `build/w08-wapi-evidence.json`。
新功能均为 Candidate；完整三目标/disabled、运行/实机/RF 与最终 Wi-Fi 集成仍后置。

2026-09-12：NAN PIN bootstrap 公开批次。订阅方 requestPairing 发送请求，发布方
receivePairing 通过 Future 交付未确认句柄；发布方确认并启动 responder 后才回复接受，
订阅方同时需要本地确认、对端接受及请求帧回收。复用一个 Session worker、消息
identity/TX pool 和共享预算；不新增事件队列或配对后台任务。入站从到达起 30 秒
截止，一个待领取请求；转换失败/等待取消可重新领取，到期未确认默认拒绝。原生
responder 仍有 10 秒建立超时，整体 timeout 不延长原生限制。

SDK 接收回调没有 cookie，每服务保留八个对端服务 ID/MAC 组合，存活服务内不复用；
COMEBACK 缺少完整原生字段，明确拒绝。协商响应不授权认证，不宣称新服务生命周期
消除了 RF 旧帧。完整 TX 回调与实际 recycler 分别记录，父 STOP 先退休消息再脱离
Pairing/Service。补齐原生 parser 请求状态保留与日志前空指针检查；新 SDK 和生产
owner/Future/GC 用例仅 AST，未导入、编译或运行。

两组 C5 配对开/关配置各八个受影响生产单元编译通过，分别完成十四/十九对象
局部链接；复用了未变化的已编译单元。关闭配对的 Sync+USD 同时重编共享 TX 状态
消费者 esp_nan_usd.c。实际 ROM 仅在开启配对时包含 requestPairing/receivePairing；
manifest 61 classes / 594 functions 与 1267 项覆盖清单一致。八份 Python 文件
仅 AST。C5 DWARF：Session 200 B、Service 3384 B、Pairing 144 B、Future 104 B、
共享 TX/control pool 2768 B，均沿用已有预算；整机峰值未测。完整证据与源码
hash 见 `build/w08-nan-bootstrap-evidence.json`；无全量镜像、运行/实机/RF、soak
或提交。NAN 本批公开流程收齐，完整动态验收仍后置，WAPI/Mesh 及其余 Wi-Fi
功能和恢复边界继续实施。

2026-09-12：NAN 缓存重验证公开批次。`service.pairingCredentials()` 通过原 worker/
Future 返回同服务 hash 的已完成、未到期凭据元数据；`preparePairing({credentialId,...})`
保留显式确认，重验证禁止 PIN，原生提交重新核对 boot 内不复用的 ID、服务和 NIRA/NPK。
`ready()` 在实际 TX 成功及 buffer 退休后提交缓存并返回新 ID；原生更新先暂存在既有
binding，失败/未完成不会覆盖此前有效凭据。两个 RAM 槽共享原 TX/control allocation，
有效缓存满时明确失败，不自动淘汰；过期槽可回收，重验证不延长 NIK 寿命。
Session 关闭后凭据失效；RAM-only 启动不再从 NVS 隐式加载旧身份/凭据，也不擦除 NVS。

本批源码核对修复两处公开配对缺口：`pairing:true` 自动捕获 `security_reqd` 与
NCS-PK-PASN-128 descriptor，最多再接收三份静态 NCS-SK-128 凭据；历史 Pairing
句柄在服务/父 Session 脱离后可安全查询、关闭和释放，构造期间也不在解锁后读取
可能已脱离的父对象。新增/扩展实际 owner/Future/SDK cache/VM 转换用例，执行后置。

配对开启配置九个受影响生产单元、十四对象局部链接通过；关闭配对配置七个单元、
十九对象局部链接通过。新增 registry 字段后先更新 manifest/coverage，再生成 ROM；
实际 ROM 中 pairingCredentials 只在 pairing 开启时存在。一次缺失 timer 头文件的
编译失败已修正，仅重编该单元。随后 RAM/NVS 隔离修改只重新准备并编译 nan_app。
manifest 61 classes / 592 functions，coverage 1267 项。C5 编译 DWARF：Pairing
对象 112 B、Service 3320 B、共享 TX pool 2760 B、binding 104 B、NAN Future state
104 B；这是结构大小，不是整机峰值或实机 placement 证明。证据见
`build/w08-nan-pairing-cache-evidence.json`。没有全量镜像构建、动态测试、实机/RF、
soak、提交或 SDK/root gitlink 修改。NAN bootstrap 交互及其余 Wi-Fi 功能继续实施。


2026-09-12：NAN 显式 PIN 配对公开批次。服务通过 `pairing: true` 启用 setup；
`preparePairing` 创建未确认句柄，发布方 responder、订阅方 initiator，显式确认和
六位 PIN 才触发认证。拒绝、确认到期、重复决定和构造失败不启动认证。一个活动
lane、最多八个存活句柄，使用既有 Session worker、Radio 串行入口和共享预算。
`ready/close` 注册原生 Future，等待取消/超时与操作截止时间分开；JS 句柄释放后
仍保留原生清理 owner，服务关闭先清理自身配对，父 Session 接管 STOP 和 TX 退休。

源码核对发现原 SDK 会在 NIK 回复尚未发送时先报告配对完成。本批公开 ready
另外等待后续帧的真实 TX 结果及 buffer 回收；失败单独保留，buffer 回收不能代替
发送成功，原生完成时间用于区分延迟轮询与实际超时。PIN 捕获和命令副本清零；
新 setup 不继承旧 peer 的 has_nik 标志，也不因已有缓存而静默关闭 setup。
这些控制路径已有生产实现 fixture，故障运行复现与动态验证仍按用户安排后置。

配对开启的 C5 配置共九个受影响生产单元编译通过，十四实际对象局部链接通过；
关闭配对的 Sync+USD 配置七个单元及十九对象局部链接通过。实际 ROM 核对先发现
pairing 开关未传给 Host 表生成器，补齐 CMake 转发后，仅重新生成两份 ROM 并
重编其消费单元；最终配对类/方法只在开启配置中存在。manifest 61 classes /
591 functions 与覆盖清单 1267 项一致。九份 Python 文件仅 AST，fixture 未导入、
编译或执行。C5 Pairing 句柄结构为 112 B、Service 为 3248 B，均走共享预算；
这不是整机内存峰值证明。证据见 `build/w08-nan-pairing-public-evidence.json`。

本批交付的是由应用协调两端确认的 PIN/PASN 接口。原生 bootstrap 协商请求接收、
有界确认队列、缓存凭据选择/重验证公开流程仍未完成，继续同一 NAN 工作目标。
没有完整镜像、动态/RF/实机/soak 或提交，不提升稳定等级。

2026-09-12：NAN pairing 服务身份、缓存归属与发送回收批次。新增内部启动命令，
提交前核对本地服务、对端服务和显式凭据索引；验证使用有界、清零释放的凭据副本。
NPK 不再退回第一服务/唯一全局密钥，PASN NIRA 只验证本次选定的服务与 NIK。
普通发现的 NIRA 观察保持独立，不构成配对授权。已移除把 ND-PMK 当 NPK 使用的
回退；服务缓存按 service hash/NIK 区分，满时明确失败，不静默覆盖其他服务。
派生 ND-PMK 缓存的服务归属保存在既有共享 control/TX 分配中，两条 SDK 密钥
记录的布局不变；数据连接按自身服务取密钥，服务释放清理自身记录，过期缓存不复用。
已启动但失败或未完成的配对关闭时，清除该服务/对端的派生密钥缓存；未进入原生
初始化的预验证/分配失败不冒用前一次 operation identity，也不清理其缓存。
同一对端有未退休 NDP 时不替换其配对密钥；配对存活期间也不接纳该对端的新 NDP。
重验证不再终止同一 publisher 上其他对端的数据连接。

Auth producer 已在进入 NAN 队列前登记实际 EB，follow-up 分配绑定本次 pairing
identity，两者共享既有 32 槽账本，容量不足时归还未提交 EB 并失败。普通 pairing
close 等待发送 producer、完整 follow-up callback 和真实 recycler；父 Radio shutdown
撤销 PASN 后允许继续 STOP，由父级保留并排空独立 EB，避免在 STOP 前互相等待。
认证请求、PIN 副本、NIK/NPK/plain 临时材料及绑定存储的异常退出清零已补齐。

本批配对开启配置累计 6 个受影响生产单元、配对关闭的 Sync+USD 配置 3 个单元
编译通过，两组局部链接通过。另用实际构建副本中的认证 producer/ioctl/output
对象链接编译后的 wrapper，确认真实 call relocation 已指向入队前的跟踪边界。
C5 DWARF 显示 TX/control 分配从 2544 B 增为 2688 B，按原有预算记账；配对绑定
88 B，同样使用共享 control 预算，清零后释放。没有增加另一套 TX pool。这些结构
大小不代表整机峰值或实际运行时内存归属。

本批按功能合并检查，仅定向编译/局部链接；新增原始 TX 对象校验最初放在已有
Vendor IE/FTM patch 之后，被 hash gate 拒绝，已修正为变更前核验原始对象。
新增生产服务/凭据选择、缓存跨服务隔离/到期/满额、同池 Auth/follow-up、嵌套回收、
旧 ticket 和关闭保留 fixture，仅 AST，未导入、编译或运行。证据与最终编译范围
见 `build/w08-nan-pairing-binding-evidence.json`。公开配对/确认队列及完整 Radio/Future
生命周期仍待实现；本批无完整镜像、刷机、实机/RF、soak 或提交，不提升稳定等级。

2026-09-12：NAN pairing/PASN 原生接入。重新核对确认 `nan_pairing.c` 与
`esp_nan_supplicant.c` 已包含在固定 SDK，旧“依赖额外组件”的判断不成立。
构建副本移除未确认 Auth1 的默认 responder、默认 PIN 和 15 处二进制材料日志，
无效 PIN 日志不再带输入值。初始化在 Wi-Fi 任务实际执行后返回，重复请求不能
替换存活操作；身份不回绕，Auth 定时器不再持可复用的原生地址。初始化的 MAC、
cache、SAE/RSNX 分配及密钥派生/安装失败不再被部分成功状态掩盖。

PASN 与待发 NIK follow-up 的显式存储接入共享预算；失败释放和命令返回清零秘密。
后续帧至多保留一个待调度 context，取消未确认时保留至已派发 callback 退出。
父 Radio 在 STOP/host reset 前撤销配对权限并取消这些原生后缀，成功后不重复执行。
NIK timeout 写入失败状态后才发布观察事件；PASN 密钥建立、完整 pairing 及原生
context 退休分开记录。这仍不是管理帧 buffer/RF 退休证明。

新增不可变 C5 pairing context；12 个受影响生产单元编译及局部链接通过，NAN/PASN
桥接无未解析符号。首次编译的事件参数 const 不匹配已修复，仅复编相关单元；
没有完整镜像构建。新增生产初始化/命令/计时/清理 fixture 仅 AST，未导入、编译或
执行。证据见 `build/w08-nan-pairing-evidence.json`。公开配对 API、用户确认队列、
精确服务及 cached NPK 选择、管理帧完整退休和集中动态/硬件验收仍待完成，未提交。

2026-09-12：USD 独立构建接入。Wi-Fi runtime、Radio owner、Session/Service/Future、
能力与诊断使用 Sync 或 USD 的共同 gate；DataPath、同步 TX pool、netif 与回调
位图仅随 Sync 启用。仅启用 USD 时 open 默认非同步模式；显式请求未编译的模式
在分配与 Radio 修改前拒绝。同步 pool 状态在活动 USD 或 USD-only 构建中为 null，
对应类型、能力上限和 API 文档同步更新。

本批只配置、生成 ROM、定向编译和局部链接。旧 C3 代表 context 缺少新增显式无线
配额，首次配置被准入检查拒绝；保留该 context，另建带配额的不可变 context。
C3 首次编译复现公开构造引用 Sync 专用默认宏的错误，修复后只重编失败单元。
C3 18 单元与 C5 19 单元通过；两组局部链接的 NAN/USD/共享 off-channel 桥接
符号已解析，实际 ROM 的 DataPath 开关也已核对。两份 fixture 仅 AST，新增
USD-only 的模式预验证、分配失败及清理失败保留 owner 用例未运行。证据见
`build/w08-nan-usd-only-evidence.json`。未构建完整镜像；S3/disabled 全矩阵、动态、
实机和 RF 验收继续后置，未提交或提升稳定等级。

2026-09-12：USD 共享传输与公开接入。启动/关闭使用现有 Radio lease，后台轮询
驱动丢失 wake/deadline 的处理和 native 故障关闭；服务终止先写入原生状态。
Action TX 原生结果直接绑定本次 message identity，Future 完成与真实 buffer 回收
分别记录。DPP/USD 复用一份不可回绕 ticket 分配器和同一物理 buffer 账本；后续
帧最多保留一份共享预算副本。服务关闭只排空自身发送，尚未退休的 raw ID 不复用。

公开 `open({mode:"unsynchronized"})`、服务 TTL/channel/channel list/发布 dwell
已接入唯一 v1；同步模式拒绝 USD 字段，USD 拒绝原生不支持的 NDP/security、Vendor、
filter/singleEvent。原 SDK 未传递发现类型、订阅忽略 5 GHz 列表及未实际轮换列表、发送结果被吞掉、
广播仍请求 ACK 的路径已在固定构建副本中修正。相关缺陷来自源码核对，动态复现
与修复运行对比按用户安排后置，不能写成用例通过。

本批只刷新既有不可变 C5 USD context/ROM，十六生产单元定向编译、十六实际对象
局部链接通过；USD/off-channel/CHM helper 无未解析符号。C3 DPP 的共享 ticket
接入单元另行编译通过。订阅轮换/服务调度的后续修订只重编原生 `nan_de.c` 并重做局部链接。八份新增/调整 fixture 仅 AST；manifest 60 classes /
585 functions、覆盖清单 1267 项及 diff whitespace 检查通过。完整矩阵、镜像、
动态/实机/RF 与整机内存峰值未执行；共享 SDK 保持未修改。证据为
`build/w08-nan-usd-public-evidence.json`。剩余 USD-only 注册、pairing/WAPI/Mesh
和既定 Driver/AP/恢复收尾继续实施；未提交、未提升稳定等级。

2026-09-12：USD 原生引擎及 NAN 安全/Vendor 接续修复。源码核对发现 Session
启动 worker 清零临时配置后，服务构造仍从该配置读取组保护策略；现改为读取非秘密
Session 状态，公开 status 也保留该策略。follow-up 的公共捕获已支持 Vendor，SDK
入口仍有旧拒绝分支；现按 body 长度/指针验证后交给实际 driver，保留发送 storage
至原生退休。固定 C5 SD 对象的 follow-up 构帧到 Vendor 编码调用也已核对。

新增不可变 C5 NAN USD context，构建内适配固定 supplicant 的 `esp_nan_usd.c`
和 `nan_de.c`，不修改共享 SDK。启动失败保留 engine/handler 后缀；关闭先撤销
admission，再在 engine mutex 外逐个注销 handler，仅成功步骤出账。定时回调持
不可回绕 identity，关闭后排队的旧回调不能进入复用地址的新 engine；注册定时器
失败进入可查询故障。发布/订阅/更新/取消/发送在 Wi-Fi 任务执行，等待调度时不持
engine mutex；命令在执行时重验 identity。Action/ROC 观察按 callback context
过滤；发现事件 post 不阻塞，SSI 不再打印。该 engine 清理仍不是完整 Radio、
off-channel buffer 或 RX 退休证明，公开 USD API 继续保持未接通。

本批仅配置/生成 ROM、五生产单元定向编译、三个原生对象局部链接及 manifest
一致性检查（60 classes / 585 functions）；USD 自有桥接符号已解析。USD 开启时
新增生命周期状态符号合计 22 bytes，最终镜像对齐及整机峰值未测。新增/扩展的
三份 fixture 仅 AST，覆盖启动失败、关闭后缀、同址旧定时器、identity 耗尽、命令
调度、事件饱和和安全/Vendor 接续；未编译或执行 fixture。证据为
`build/w08-nan-usd-evidence.json`。未全量构建、刷写或进行运行/RF 验收。

2026-09-12：NAN 安全服务与 Vendor 属性。发布/订阅接受 1..4 份 NCS-SK-128
passphrase/PMK 凭据和可协商组数据保护；Session 可启用组管理保护。发布、订阅和
follow-up 支持 3-byte OUI 与 0..255-byte body。输入严格验证并深复制，安全服务
拒绝关闭 NDP 确认；原生退休后清零凭据，关闭的 JS 句柄只保留非秘密状态。

固定 SDK 的安全构建副本去除 17 处二进制安全材料输出；MAC/HMAC 失败和部分
派生结果走失败清理。PMKID 未匹配或 M1 缺失时拒绝入站请求，单帧 NAF 前后及
STOP 清除暂存解析材料。组管理密钥生成/安装失败阻止 Session ready；协商组密钥
缺失、封装或安装失败阻止连接 ready。SDK host 服务和框架捕获均使用 secure-zero。
原始/修复后的加密失败、入站错误凭据、组密钥安装失败、输入修改/关闭清零、OOM、
实际 VM getter/GC 与 ByteSource 用例已补写，七份相关 fixture 仅 AST，未执行。

保留原 C5 NAN context，并新增不可变 C5 security context；只配置/生成 ROM 和定向
编译。两分支十二个生产单元通过，安全审查后的 SDK 修订只复编三个受影响单元；
十七个实际对象局部链接无未解析 NAN 符号。manifest 仍为 60 classes / 585 functions。
C5 Service 基础存储 3240 bytes，安全凭据额外 394 bytes，Vendor body 按长度追加；
message 基础存储 2184 bytes，Vendor body 按长度追加，均使用既有共享预算。
SDK 保持未修改，security-disabled 的既有未使用 helper 警告保留；没有完整镜像、
实机操作、提交或稳定性升级。证据见 `build/w08-nan-security-evidence.json`。
当时 NAN 剩余 USD/配对集成及集中验证；其中“配对需额外组件”的判断已由本表
后续 pairing 原生接入批次纠正。

2026-09-12：NAN 开放 DataPath 公开批次。启用 `dataPath` 的订阅服务通过
`requestDataPath()` 发起连接，发布服务通过 `receiveDataPath()` 取得请求；独立
句柄提供 `ready/respond/status/close/cancel`。最多两个活动连接、八个存活句柄，
入站未显式接受的请求默认拒绝；自己的建立截止时间与 Future 等待超时分开。
对象转换成功后才交付入站 owner，服务/父 Session 关闭接管原生连接清理。

仅刷新现有 C5 NAN 配置及 ROM，十个受影响生产单元编译、十九个实际对象局部链接
通过。首次配置因覆盖摘要未随 manifest 更新而失败，已重新生成；ROM 消费单元
缺少 NAN 声明头导致编译失败，已补 include，仅重编失败单元后通过。
manifest 为 60 classes / 585 functions。C5 DWARF 显示共享 control pool 为
2536 bytes（新增有界 SSI/时间记录），每个 DataPath 为 1192 bytes，均走共享预算。
九份 fixture 仅 AST，未导入、编译或执行；完整镜像、运行与 RF 验收仍 not-run。
证据见 `build/w08-nan-datapath-public-evidence.json`。本批没有刷写、提交或全量构建。

2026-09-12：NDP 数据帧与独立连接回收批次。普通数据/空数据帧在进入
`nan_dp_post_tx` 前取得精确 ticket，沿用 32 槽池；数据最多占 24 槽，为管理帧保留
8 槽。原生 post 返回错误不等于 buffer 已回收，已进入 post/NAF/null-data callback
继续保留槽。回调需核对捕获时的独立连接 identity，已删除连接不能借地址复用影响
新连接。重复 buffer 准入不回收其他 owner 的 storage。

独立 release 沿用现有 ioctl 串行入口，核对原生删除、完整 callback/recycler、
精确 peer timer 清理后，才清除 host preclaim 与账本记录；失败保留原错误及未完成
后缀。清理不访问已释放的动态 node，不停止其他 peer 的建立定时器。全局组播帧
仍由父 Session 持有，不阻止无引用的连接或服务单独退休。SDK 的 8 位协议 ID
回绕后选择非零空闲值，避开原生、保留记录和在途帧；框架 boot identity 仍不复用。

只刷新已有 C5 NAN 配置，四个受影响生产单元编译通过；十三个实际生产对象局部
链接，核对普通/空数据发送、原生 ioctl release 和精确 callback 路由。既有
security-disabled 未使用 helper 警告保留。NAN control pool 从 1224 增至 1480
bytes，新增 256 bytes 用于逐帧身份/种类，仍从共享预算分配；没有增加第二个池。
八份相关 fixture 只作 AST 解析，未导入、编译或执行。没有全量镜像构建、实机操作
或提交。证据为 `build/w08-nan-data-evidence.json`；本批没有接入公开 NDP API，
后续直接完成 Session 请求接纳、连接句柄/Future 和公开 request/response/end。

2026-09-12：NDP 接收和定时器批次。管理帧完整 RX、空数据发送完成及原生建立/空闲
定时器处理器进入既有关闭屏障；冻结 RX 仍回收由 `nan_input` 移交的 EB。沿用 C5
现有 OSI/ETS 分发，三个定时器记录放入 NAN 共享 control pool。ESP_TIMER_TASK 与
Wi-Fi 队列仅携带 boot-unique 数字身份；每次 setfn 创建新身份，disarm/删除先撤销
旧权限。原生删除一个 peer 无权停用其他 peer 的共享建立定时器。删除失败保留
实际 handle，SDK node 被释放后仍可在 STOP 后清理，成功的 stop 不重复执行。
已进入的 timer post/处理器保留 pool，queued 旧身份在新 pool 中也无效。原生池
故障由 Session service 捕获并启动关闭，不依赖可丢的观察队列。

只刷新已有 C5 NAN 配置并编译 SDK、NAN TX、Session、共享 timer dispatcher 四个
生产单元；九个实际对象局部链接确认完整 RX/null-data/timer handler 路由、两个
精确回调地址导出与原有 14 个生命周期调用点。NAN IRAM 调用仅落到自身 IRAM
helper、FreeRTOS 与当前 `CONFIG_ESP_TIMER_IN_IRAM=y` 的 stop/start_once。SDK 的
security-disabled 未使用 helper 警告仍存在；无新的生产编译错误。
六份相关 fixture 及 archive preparer 仅 AST；原生队列/地址复用、跨 peer 删除、
停止/删除失败、进入 post 时关闭、完整回调范围及无观察事件故障关闭用例已补写，
动态运行、security-enabled 配置及 RF 验收仍 not-run。证据为
`build/w08-nan-timer-evidence.json`。无全量构建、镜像链接、设备操作或提交；NDP
普通数据/空数据 buffer 退休、独立连接复用和 Session/Future/公开接口继续实施。

2026-09-12：NDP 操作身份与 checked 提交批次。复用 NAN 共享池内的两个记录，
保留 boot-unique identity、精确请求地址/调用任务、服务/peer/原生 NDL 绑定和
提交/完成/清理的独立状态。原生分配前认领、M1 前发布真实 ID，早到完成不依赖
worker 返回；原始 request 错误穿过 SDK 的通用失败返回。response/end 使用同一
Radio mutation mutex 和精确 token，每次身份不隐式重放提交。仍被数据通路引用
的服务禁止单独取消；host/key 释放延至原生删除返回。

实际对象探针确认普通 `--wrap` 不拦截同一对象内的删除调用，因此在固定 hash 的
构建副本中重定向 14 个分配/删除调用点，并将 6 个 peer 查询绑定到准确服务。
本批仅刷新已有 C5 NAN 配置，编译 SDK、NAN TX、Radio 三个生产单元并做七对象
局部链接，所有调用路由核对通过。未启用 security 的 SDK 留有一处
`nan_ndl_release` 未使用警告；未声称 security-enabled 编译或运行通过。
新增生产 broker/SDK helper 回归覆盖早到完成、提交错误、重复操作、地址隔离和
服务取消边界，五份相关 fixture 仅 AST，未导入、编译或执行。

证据为 `build/w08-nan-operation-evidence.json`。接收/定时器与 Session/Future 尚未
接入，NDP 记录暂不单独复用，不能据本批宣称数据通路公开或安全退休完成。
公开 API/类型未变化，未重复生成 manifest；无全量构建、设备操作或提交。

2026-09-12：NAN 数据通路的管理帧与控制回调批次。固定 C5 datapath 对象的五类
`nan_alloc_action` 分配（请求、响应、确认、安全安装、终止）进入既有 32 槽
SD/NDP 共享池，包含发现服务默认拒绝 NDP 产生的帧。保存精确目的 peer 与 NDP ID，
不把 NDP ID 当成 service ID；完整 `nan_naf_txcb`、应用 NDP 回调和实际 recycler
分别保留退出条件。全局关闭封闭回调和新分配，STOP 与存储回收后才能释放池；
单服务冻结仍允许原生 NDP 完成。beacon/普通数据帧和完整 RX/timer 生命周期不属于
本批账本证明。

SDK 原始路径的请求等待超时、confirm teardown 发送终止后都会提前清掉 host NDL；
当前改为保留到原生终止或 STOP，清理失败保留原错误和 NDL。NDL reset 复用
`forced_memzero`。确认回调改用栈上 metadata，在既有安全检查、密钥安装和 netif
处理后先通知原生 ACCEPTED，再分配可丢弃的观察事件；观察 OOM 不再拆掉连接。
RX 注册/密钥安装失败先保留原始错误，终止失败另报 cleanup fault。请求/确认的
原生通知提供有界接收方可同步复制的借用 metadata/SSI，不导出密钥。

本批仅刷新合法 C5 NAN 配置，编译两个受影响生产单元；补原始密钥错误通知后仅
复编 SDK 单元。七个实际对象的 relocatable 链接验证五类分配与完整 NAF wrapper
解析，IRAM recycler 仅调用实际 FreeRTOS 临界区/上下文检查和自身退休 helper。
首次 IRAM 检查因检查器误列 FreeRTOS 函数名而失败，已核对实际符号后修正检查；
没有对应的生产编译错误。NDP 超时/清理/事件 OOM、完整回调范围、共享容量和旧
buffer 地址复用 fixture 已补写，仅 AST，未导入、编译或运行；安全启用路径、
真实 native RX/timer 与 RF 仍待集中验证。证据为 `build/w08-nan-datapath-evidence.json`。
没有全量构建、完整固件链接、设备操作或提交。NDP 独立操作身份、请求/接纳/终止
公开 API、原生 NDL/RX/timer 退休、USD/security/pairing/Vendor 属性继续实施。

2026-09-12：NAN follow-up 发送接通 `service.send({peerServiceId, peerMac, ssi?,
timeoutMs?})`。输入在 Future capture 中复制，参数完整验证后激活；使用现有 Session
worker、Radio mutex、SD TX pool 和共享预算。最多一个活动发送、八个捕获/结果记录，
boot 内不复用 identity。固定 C5 SDK 将 EB 地址写回 context，新增真实 ioctl→
`nan_send_followup_msg` 包装，在精确 context 指针与 Wi-Fi task 的作用域内绑定分配
ticket；完成使用完整原生 callback 退出，存储使用实际 recycler 返回，两者独立。
未开始提交的取消不发帧；提交后的超时/取消保留存储，不自动重发、不终止无关服务。
公共状态增加发送 identity/等待/清理字段和 messageHandles。原生完成时间与快速状态
读取避免 100 ms 清理调度间隔造成错误超时，未知成功但缺少 tracker 证据会关闭父 Session。

合法 C5 NAN 配置、真实 ROM、七个受影响生产对象通过；完成时间修订后仅复编三个
变化单元。六个生产对象的 relocatable 链接确认 API→ioctl→发送包装→原 native
及完整 callback 的实际解析，IRAM recycler 调用仍只进入临界区与自身 IRAM helper。
类型、manifest（59 classes / 578 functions）和覆盖摘要一致。原生消息、发送账本、
SDK 精确对端/原错误、Future 超时/迟到回收与真实 VM 结果转换 fixture 已补写，
只作 AST，未导入、编译或执行。完整公开 capture/队列/Future runtime、GC/OOM/
竞争、RF 与吞吐仍待集中验证，不能据本批编译提升稳定等级。证据为
`build/w08-nan-message-evidence.json`；没有全量构建、完整固件链接、实机操作或提交。
收尾核对还修正了五项 NAN 覆盖映射：类方法使用 manifest 中的 `.prototype.`
完整路径，确保已有 callable 被诊断摘要正确计入；重新生成后仅复编 Wi-Fi diagnostics。
NAN datapath、USD、安全/配对、Vendor 属性及 Wi-Fi 其他剩余批次继续实施。

2026-09-12：NAN 发现服务批次接通 `session.publish/subscribe`、
`WiFiNanService.status/ready/close/cancel` 和创建时即存在的 `service.events`。
公开构造成功后才激活；实际 service ID 在 Wi-Fi task 返回请求 worker 前完成
SDK host 和 framework identity 绑定，早到事件进入同一有界队列。最多两个原生
服务、八个存活句柄；取消成功后只重试回调/TX 回收后缀，旧 ID 未退休时拒绝新建。
已关闭句柄和队列不再保留父 Session；活动 discovery-only 服务拒绝 NDP 接纳，
关闭后的冻结回调不再处理新请求。消息发送、完整 NDP、USD/security/pairing 仍待实现。

本批合法 C5 NAN 配置、七个受影响生产对象（含更新覆盖摘要的 Wi-Fi diagnostics）和真实 ROM 生成通过；五个生产对象的
relocatable 链接确认 ioctl → create wrapper → 原生 start，以及完整 SD TX wrapper
和五处分配 hook 的真实解析。首次配置因 runtime coverage 未同步失败，更新后通过；
公开构造误用 `JS_NewObjectClass` 在目标编译失败，改为本仓库实际
`JS_NewObjectClassUser` 后仅复编该单元通过。类型、manifest（59 classes / 577
functions）、SDK 覆盖摘要同步；生产 SDK/Session/Future/转换 fixture 已补写，
仅 AST，未导入、编译或执行。完整公开构造/输入捕获/队列转换与 Future runtime、
NDP 拒绝路径及各项 GC/OOM/竞争行为仍需集中运行验证。本批无完整固件链接、
全目标构建矩阵、实机操作或提交。证据为 `build/w08-nan-discovery-evidence.json`。

2026-09-12：NAN 服务实现的原生前置已接入。C5 SD 对象的五处实际 buffer 分配
引用统一进入 32 槽 internal/control 账本；提交时读取所属 service ID，实际 recycler
返回后按 boot 内不复用 ticket 归还槽。未提交/未知归属的帧保守阻止 ID 复用；
关闭先封住新分配，等待完整 SD 回调后 STOP，所有原生 buffer 回收后才释放池和
Radio owner。`status().tx` 和 `serviceCallbacks` 已同步到公开诊断与类型。
固定 SDK 的原生 `nan_sdf_txcb` 在应用 replied 回调返回后仍写 peer；现将该原生
函数的服务分支也纳入同一冻结/退出计数。action/follow-up 的独立完成路径继续
交给 SDK，其完整 operation identity/关闭仍须随消息接口接通，不能冒充已经完成。
publish/subscribe/cancel 的 driver ioctl 改为在 NAN 数据锁之外执行，预占 host
记录保持存活，避免 worker 等待需要该锁的 Wi-Fi task 回调。新增 checked 内部
入口保留 native submit/cancel 原错误；取消成功后保留 ID 冻结，等待真实 TX
退休后才允许恢复。公开服务句柄、早到事件与 host service ID 发布交接、消息及
datapath 仍未接通，不新增占位注册。
本批 C5 NAN 配置、实际 SDK 单元目标对象编译、四个生产对象的 relocatable 链接
通过，确认 common → wrapper → 原 SD 回调及五处分配 hook 均真实解析；这不是
完整固件链接。此前本批 10 项生产语法、TX 目标对象、类型与 ROM 生成记录保留。
SDK 补丁生成首次因 cancel 函数含两处 `ESP_FAIL` 返回而拒绝；已将替换限定到
fail 解锁分支，并重新配置、编译通过。生产回调尾部竞争、SDK 锁边界/原错误、
第 N 次分配失败、旧 ticket/地址复用、关闭失败后缀等 fixture 已补写，仅作 AST，
未导入、编译或执行。证据为 `build/w08-nan-service-evidence.json`；无完整镜像、
实机操作或提交。NAN 其他能力及剩余 Wi-Fi 范围继续实施，集中验收安排保持不变。

2026-09-12：NAN 公开生命周期已接通 `wifi.nan.capabilities/status/open` 和
`WiFiNanSession.status/ready/close/cancel`。完整 JS 句柄构造后才激活原生 Session；
ready/close 注册原生 Future，启动截止时间触发 Session 关闭，单次 ready 等待的
超时/取消仅结束等待，已开始的 close 在等待结束后继续清理。全局状态与 diagnostics
可查询 GC 后保留的原生 owner；capabilities 只报告已实现的同步 discovery 生命周期。
新增类/模块/ROM/源类型/manifest 同用 Wi-Fi + NAN_SYNC gate，独立于 IPv4。
channel 使用 SDK uint8 输入范围，由目标 driver 决定 NAN 信道是否合法，不把
此前 14 的软件限制当作硬件能力。NVS 擦除/持久化、安全与服务等扩展仍未开放。
13 项受影响的 C5 NAN、C3 非 NAN、C5 disabled 生产语法检查通过；NAN ROM 工具
按已有合法 Context 的实际 host 命令生成，并显式补入该 sdkconfig 的 NAN gate。
本配置 ROM 中 116 个 Future 方法未超过现有 128 项 registry；这不是其他配置的
运行证明。类型、manifest（58 classes / 571 functions）和 NAN 文档单个 MQuickJS
示例语法通过。Session/Future 调度及真实 VM 输入、状态/错误转换 GC/OOM fixture
已写入，只作 AST；未导入、编译或执行。公开构造/capture 与完整 Future runtime
集成仍需集中运行时补齐验证。证据为 `build/w08-nan-public-evidence.json`。
本批没有完整构建/链接、实机操作或提交。服务发布/订阅、消息、datapath 的完整
native callback/TX buffer 退休、USD/security、Mesh/WAPI 与阶段验收继续待完成。

2026-09-12：NAN Radio/netif/原生 Session 生命周期已接入。独占 lease 与 operation
覆盖初始化、START/STOP、SDK reset、netif 双屏障退休、observer 退出及旧停机
mode/storage 恢复，完成后才释放；迟到事件和停止屏障超时不重复提交已受理的 STOP。
SDK 启动配置与实际 START 提交分开，原生 ready 需默认 handler/事件屏障及 netif up；
handler 注册/注销原错误和未完成后缀保留。后台 Session 使用共享 worker 和 `wifi.nan`
control 预算，超时/关闭与 worker 引用独立，迟到成功不会清掉已过截止期，runtime
销毁等待其退休。Radio 诊断增加 NAN mode、owner 和事件 bit 4；公共 NAN API 仍未注册。
新增生产 Session 的线程调度/OOM/满队列/timeout/runtime 用例及真实 Radio close
后缀用例，扩展 SDK handler 和拆分启动 fixture；本批仅 AST，未编译或运行。
10 项受影响 C5 NAN/C3 非 NAN/C5 disabled 生产语法检查通过；首次发现 SDK 缺失
attach 原型，按固定 SDK 实际定义补齐内部声明后定向复查。新 Session 使用已配置
框架目标的真实参数检查；本批无完整构建/链接或设备操作。
记录为 `build/w08-nan-lifecycle-evidence.json`。公共服务/消息/datapath、完整 native
callback/buffer 退休、USD/security、未知恢复来源和集中验收继续待完成。

2026-09-12：开始 NAN 原生接入。固定 SDK 的 `nan_app.c` 使用 build-local 适配，
新增无分配、精确身份且等待已进入回调的 native observer；请求/拒绝/终止控制
通知先于事件分配或发布，SDK 观察队列满时不阻塞 native producer。启动/停止有界
等待保留原错误/context；迟到 STOP 后不重复提交停止。补初始化 OOM/重复创建、
短 SSI、NDP response 二次解锁和本文件四处密钥日志的修正。生产源审查发现的问题
已写入 original/prepared 对照 fixture；六项动态用例仅写入并 AST，尚未复现运行。
新增完整不可变 C5 NAN Build Context（保留旧 inventory Context），配置和实际
生成后的 NAN 源定向语法通过；无完整构建/链接、设备操作或提交。证据为
`build/w08-nan-native-evidence.json`。公共 API 尚未接通；Radio/netif、独立操作
身份/退休、预算、USD/security、完整生命周期和实机验收继续保留，不将 native
observer 的结束视作整个 SDK callback 或发送 buffer 已退出。

2026-09-12：诊断观察计数批次完成代码接入。`resetFrameworkCounters()` 成功路径
不分配 JS 对象；清除队列/ingress、连接、可读 CSI/Monitor 及全局 memory manager
观察历史，峰值从当前占用重新开始。保留 owner、在途操作、过滤节奏、身份和故障；
跨 provider 非原子，无法读取的 generation 及重置时间窗口通过 snapshot 明示。
EventQueue 入队/出队/峰值共用原锁；固定 SDK 的非阻塞 data queue 路径已只读核对，
通知和 drop 在锁外，RTOS 动态竞争证明仍待集中执行。新增/扩展生产 helper、
完整 manager、真实 VM 和 CSI store 退休屏障 fixture，7 份 Python 仅 AST，C/VM
用例均未编译或执行。26 项受影响生产源语法、启用/禁用 ROM、类型、manifest
（57 classes / 564 functions）及 SDK 清单检查通过。C5 disabled 旧缓存缺少新
Kconfig 预算宏，只刷新这一份合法配置，保留前 20 项未变输入的检查；无完整构建、
链接、设备操作或提交。证据：`build/w09-counter-evidence.json`。
下一批继续 Wi-Fi 功能缺口，运行/三目标全矩阵/实机验收和 BLE 后长 soak 的节奏不变。

2026-09-11：Build Context 配额校验与两项公开诊断方法已接通。loader/CMake 共用
无 `.env` 依赖的校验器，检查三项显式十进制配额、重复/范围、控制余量、物理 PSRAM
容量与驱动模式、target 及实际 sdkconfig 一致性；Future worker 栈/真实 TCB 的
data 配额下界由配置检查与目标 C 断言共同约束，动态对象继续经过各模块 cap 和
既有共享 allocator 准入。`dumpDriverStats(mask?)` 在稳定 Radio mutation mutex 下
单次调用 SDK，输入先校验，失败保留原 ESP 错误及 stage，不自动启动/重试。
`idfApiCoverage()` 返回从 1,267 项清单和当前映射/manifest 生成的常量统计、输入
hash、逐 header/task/参考配置和未展开条件缺口，当前 capabilities 单独观察；
不从注册推断实现或 RF 通过。覆盖检查和 CMake 拒绝过期生成物及 SDK 输入。
三份新增 fixture 覆盖生产预算校验、生成器分类、实际 Radio dump 与真实 VM
GC/Nth failure；仅 AST，未导入、编译或执行。C3 一次配置更新通过新 CMake 入口，
C3 无 PSRAM/S3 PSRAM 实际预算核对、10 项受影响源/配置语法、启用/禁用 ROM、
类型及 manifest（57 classes / 563 functions）通过。证据为
`build/w09-diagnostics-evidence.json`；发现并修正的新代码数值转换函数名错误亦保留
初次编译日志。无完整构建、链接、设备操作或提交。下一项仍是 queue high-water、
reconnect 计数和只清观察历史的 reset 契约；其余 Wi-Fi API 与集中验收范围不变。

2026-09-11：内部 RTOS 与共享 Future service 预算批次完成。Wi-Fi helper 的锁、
事件组和原生完成队列计入 control，watch ingress 计入 queue。统一内部 RTOS 工厂
预留实际 StaticQueue/StaticSemaphore/StaticEventGroup 与 queue payload，经过
现有 owner/回调排空门槛后先 SDK delete、再实际 free/归还。无线启用构建将 Future
runtime/driver registry/slots/dispatch-ready 队列、EventQueue runtime registry、
通用 public handle/argument/combinator roots 和 worker stack/TCB 统一计入
`wireless.runtime`。这包含共享 receive 与该服务处理的非无线调用；关闭所有无线
feature 的构建不采用无线额度。捕获存储仍按原 driver owner 单独准入。
worker 栈维持原 4096-byte 深度，创建前一次预留全部栈/TCB。尚无已创建任务时失败
可全部释放；已部分创建后保留 queue/stack/TCB 和任务，只重试剩余创建，全部成功
前不接收后台工作；runtime 重启不重新分配 boot worker pool。删除旧的无条件任务
删除回滚 helper 及其独立测试，由实际 initializer + RTOS factory + 完整 manager
联合 fixture 取代。覆盖第 N 次 heap/RTOS/task 创建失败、先删除后 free、控制预留、
部分启动后缀、拒绝提前提交和重启后账本保留；用例仅写入，未运行。
16 项生产语法检查覆盖 C3、S3、C5/no-SoftAP 及 C5 wireless-disabled；类型、manifest、
diff 检查通过，4 份 Python 文件仅 AST。证据为 `build/w09-runtime-budget-evidence.json`。
本批没有重新配置、完整构建/链接、动态测试、设备操作或提交。剩余 Build Context
预算相互约束、完整诊断、其余 Wi-Fi 功能和集中运行/实机验收继续保留。

2026-09-11：共享预算的模块存储与 ByteView/Source 接入批次。40 个模块源文件的
100 处分配入口及跨模块最终释放端改用既有 memory manager，覆盖 Radio 配置/binding、
配网 worker/Session、凭据 profile、扫描/发现结果、FTM/RRM 存储、TWT 控制和发送
记录、CSI/Monitor Frame/Batch/Source、本阶段已有 BLE 原生池/控制对象/数据副本及
ESP-NOW 公开句柄。保留明确 internal/PSRAM 策略和原 secure-zero 释放顺序。
CSI/Monitor 外层 ByteView/ByteSpanSource 与 Source 读取租约通过内部显式 owner
工厂计入 control；BLE/ESP-NOW 数组输入临时副本计入 copy。关闭但仍被读取的 View
保持其 wrapper 和数据预算，最后读取释放后才归还；Source close 仍按既有 busy
语义拒绝。没有新增 JS callable 或 BLE API。
新增实际 ByteView/Source 生产函数与完整 memory manager 联合 fixture，覆盖构造
分配失败、JS 创建/属性失败、control reserve、Source open 失败、读取保留、runtime
账本和最终释放。既有真实 VM 用例同时覆盖 generic/wireless 工厂，snippet fixture
继续使用其原 heap/故障注入边界；全部运行后置，不能作为当前通过结果。
42 项受影响生产源定向语法及类型/manifest/diff 检查通过，5 份 Python 文件仅 AST；
检查发现 C5 FTM 缓存缺少当前
CMake 已声明的 wpa_supplicant 依赖，只重新配置该合法 Build Context，未构建或链接。
最终证据写入 `build/w09-storage-admission-evidence.json`；Wi-Fi helper/ingress 的
动态 RTOS 存储、共享 receive Future/runtime、SDK 不透明对象和完整诊断继续待完成。

2026-09-11：共享预算接入异步存储批次完成。14 个无线 EventQueue 创建入口使用显式
owner 工厂；队列 control、drain/overflow scratch、静态 RTOS mutex 和完整 queue
control/storage 计入 queue，receive capture state 与单份 event buffer 计入 control。
固定 SDK 的 xQueueGenericCreateStatic 返回传入 control 地址，删除 RTOS 对象后再
归还对应 allocation；原生 retain/关闭/最后 owner 的时序保持既有生产路径。
无线 Future driver 声明显式 memory owner，公共参数 roots/句柄在实际分配前准入；
47 处原生 capture state 同步纳入控制额度。三类存储按各自的真实释放时点归还，
公开 Future 结束不提前归还仍被原生操作持有的 storage。Raw TX 的 Session/周期任务/
公开 handle、slot/batch 数组、capture/周期数据及 broker 隔离副本全部接入现有预算。
没有新增 JS callable；manifest 仍为 57 classes / 561 functions。
新增 EventQueue factory/receive/最终 native release 与完整 memory manager 联合
fixture，覆盖第 N 次分配失败、RTOS delete 边界、quota 满后控制准入、overflow 和
非无线队列隔离；使用现有 heap/RTOS/JS 边界，执行真实生产函数，未运行。相关 VM/
Raw TX/Monitor/BLE fixture 的分配工厂边界同步，9 份 fixture 仅 AST。
29 个受影响生产单元在既有合法 C3 DPP、C5 FTM/roaming/TWT、S3 AP WPS 配置下定向
语法检查通过，类型与 manifest 一致；证据为 `build/w09-async-budget-evidence.json`。
本批没有配置矩阵重建、链接、动态用例、设备操作或提交。共享 receive driver 的
generic Future handle/参数 roots，以及 shared runtime slots/queues/workers 仍属于
未完整归属的框架开销；其余 control/Source/BLE 分配与诊断方法继续待完成。

2026-09-11：共享预算首批生产接入完成，沿用原 memory manager 和 48 项 owner 账本，
新增 internal/PSRAM 总额、控制保留与 pool/retiredPool/queue/TX/stack/copy 角色。
payload 与跟踪节点按实际区域在 allocator 前联合预留；节点继续优先 PSRAM，字节归
同一数据角色，不借用 control reserve。分配部分失败在 free 返回后回滚，最终释放
也等待 payload 和节点两个 free 返回；runtime restart 不清掉活跃固定存储预算。
CSI/Monitor 关闭后仍存活的池只改退休分类，保持全部额度。已接入 CSI 池控制/slot/
payload、Monitor Session/池、ESP-NOW RX copy/pool 和 TX queue/staging/tracked/
worker stack，以及既有 BLE scan/subscription pool。没有启动 BLE 新 API。
`sys.status.memory.manager.wireless` 与 Wi-Fi snapshot 接通配额、各角色当前预留、
boot 峰值及饱和拒绝计数；明确不包含完整 allocator/SDK/JS 堆，也未覆盖所有无线
入口。新增纯生产 ledger、完整 memory manager 的 Nth failure/双线程准入/控制余量/
PSRAM placement/owner 容量/allocator 返回/runtime 账本测试，以及实际诊断 converter
移动 GC/OOM fixture；CSI store 夹具补退休通知须持有原生 control pin 的断言。
全部测试只写入，执行后置。C3/S3 两份合法 Build Context 副本刷新配置，新增显式
配额；CMake exit 0，辅助 gdbinit 因未设置 ESP_ROM_ELF_DIR 报错，未用于调试或链接。
12 项定向生产语法（C3、S3 PSRAM、C5、Host）及类型/manifest 核对通过，证据为
`build/w09-budget-evidence.json`。未全量构建、链接、运行夹具或操作设备。
通用队列/Future、其余 Source/control、Raw TX 与其他无线模块分配入口、完整 Build
Context 策略约束和 reset/dump/coverage 仍待完成；W-09 继续 in-progress。

2026-09-11：CSI header 过滤与公共接收信息批次已接通 Candidate。
source/destination/BSSID 使用同次 proven header 的逻辑地址角色；frameTypes/subtypes
接通严格输入、requested、capabilities 和统计。默认不保存 packet 时仍解析 header，
不增加 packet record/bytes 分配；未知地址/sequence 保留 null。移除对 SDK 有条件
初始化 rx_seq 的读取。Frame info 改为公共 signal/channel/addresses/PHY 结构，HT、
VHT、HE FEC/SGI/聚合等按实际字段可用性报告；原有平铺字段在唯一 v1 直接替换。
slot 与本 Session sequence 不回绕，耗尽停止采集并保留旧 owner；close/open 新代
可恢复，boot generation 耗尽仍需重启。修复 full packet native-copy 截断与 Host
validator 不一致；Monitor 同步 driver-report 完整性语义。零长度 CSI 不发布，保留
PHY/secondary 值不会生成已知 layout。13 个受影响生产单元的定向语法检查通过，
覆盖 C3 legacy、C5 HE、Host 辅助模块；类型与 manifest 一致。最终复核只重查两个
新增 guard 的 Host 单元。10 份 Python fixture 仅 AST，未导入、编译或执行；新增
目标 normalizer、过滤、身份耗尽、实际 VM GC/OOM 与 writer→Host 截断用例运行后置。
证据为 `build/w05-csi-metadata-evidence.json`，无完整矩阵、链接或设备操作。共享
控制预算、完整资源诊断、其余功能及集中运行/RF 验收仍属于剩余范围。

2026-09-11：CSI 对应原始包接通 Candidate 生产路径。四个 reviewed RX 来源记录已完成
复制范围，随同次构造器交付一次性 receipt；读取取 proven span 与 driver packet report
较小值，解析可变 MAC header，不使用 hdr+24/payload_len 拼接。packet none/header/full、
required/requireComplete、同槽分配、Frame packetBytes/copyPacketBytes/packetSource、
Batch packetBytes、info.packet、统计、snapshot 和 wire payload 已接通，新增四个公开方法。
默认 none 不分配 packet record/bytes；header 每槽 36 bytes，full 每槽 snapLength，最大
16384。旧 View/Source 和 packet 随原 slot generation 保留，预留字节包含 packet 与
原生 receipt 控制；FCS/保护负载表示保持 unknown。CSI 字符串判等使用完整长度，拒绝
NUL 后缀。新增原生 receipt/packet 和生产 C writer→Host、实际 VM 跨代 packet owner、
GC/Nth OOM 用例，当前仅 AST。受影响生产源、ROM、三目标 archive、类型与 manifest 核对
记录为 `build/w05-csi-packet-evidence.json`；无全量构建、链接、运行测试或设备操作。
此条替代下方历史“公开 packet 尚未接通”状态；完整 CSI metadata/filter、共享预算、
集中运行与 RF 验收继续属于剩余工作。

2026-09-11 进度核对：CSI 对应原始包正在实现。已写入原生 RX 连续复制范围记录、
同次同步 CSI callback 的精确 receipt 桥接，以及固定三目标 SDK archive 的局部
调用点补丁脚本和 CMake 接入。当前桥接尚未由 CSI Session 启用或由采集回调消费，
尚未接通 packet 选项、存储、Frame/Batch/Source 和 wire；本批新增生产代码及
archive 补丁尚未验证，不能算作 packet API 完成。上一批跨代池的语法检查记录
不覆盖这些新增文件。下一功能批次须打通公开采集链路后，再集中做受影响检查。
本次进度核对只读取代码、提交和已有证据并更新本表，没有运行构建、测试或操作设备。

2026-09-11：CSI 跨代池接入生产 open/close、Frame/Batch/View/Source 和诊断。
单个 driver Session 关闭并完成 Radio 清理后，旧数据池按精确 generation 保留；
新 open 在共享 CSI 总槽预算和最多八代限制内分配，不驱逐旧数据。预算先预留，
allocator 全部返回后才归还；代号耗尽不回绕。队列和 reaper 绑定代号，旧关闭回调
不会作用到新 Session。统计在 JS 分配前复制原生值，诊断列出逐代 pool reservation。
新增原生 registry/resource 竞争和第 N 次分配失败用例，实际 VM 增补旧 View/Source
跨代重开；本批只写入，运行继续后置。此项不等于共享 Wi-Fi/BLE 总预算、control
reserve 或 CSI 原始包完成。五项受影响生产源语法检查通过（C3/C5 adapter 与
store，以及 Host store），manifest 57 classes / 557 functions 一致；五份 Python
fixture 仅 AST。已有 associated 设备用例新增预算拒绝、旧视图跨代重开及 GC 后
新会话存活断言，offline 用例同步预算诊断；设备用例未编译/执行。本批无链接、
全量构建、动态测试或设备操作。证据为 `build/w05-csi-store-evidence.json`。

2026-09-11：CSI `source` 改为 associated/promiscuous discriminated object，
associated 不接受 channel；删除旧 conflict/queue 输入，改用
`buffering.poolCapacity/queueCapacity`。pool 按请求容量实际分配，省略 queue
时使用 build 默认与 pool 的较小者；显式越界拒绝。批量上限不超过当前 queue，
Session 内不改变既有 pool/queue 容量。capabilities/effective 使用 `maxCsiBytes`，
类型、API 文档、设备用例同步唯一 v1。生产 parser/result 与原生 allocator 的
GC/Nth failure fixture 已补，实际 batch adapter 也覆盖小队列默认值和消费前拒绝，
运行后置。C3/C5 受影响生产单元语法通过，并单独检查 promiscuous 预处理分支；
该分支检查不代表启用功能的完整 Build Context 构建。三份 Python fixture 仅 AST，
两段 API 文档示例通过 MQuickJS 语法；manifest 一致。记录为
`build/w05-csi-options-evidence.json`，本批无链接、动态用例或设备操作。
[SDK packet span 核对](2026-09-11-w05-csi-packet-span.md)发现三目标构造器固定
payload=hdr+24，仍需完整 RX 来源的可读范围证明；没有提前启用 packet capability
或注册占位方法，correlated packet、跨代预算及集中验收继续保留。

2026-09-11：`wifi.diagnostics.snapshot()` 接通生产模块、注册与类型。复用当前
Wi-Fi/Radio/watch/Raw TX/Action、Vendor IE、FTM/TWT、Enterprise、SmartConfig、
Station/AP WPS、DPP 和全局 managed-memory/EventQueue 账本；feature 不存在时
对应 provider 为 null。Monitor 在既有 registry 中临时 retain 全部 generation，
锁外取 pool 快照并释放引用后才转换 JS，覆盖关闭后的 Frame/View/Source owner。
CSI 新增独立于可重置 Session 的 pool-present 发布位，分配器 clear/free 返回后才
允许 open/runtime init；退休进行中只读冻结计数，pool bytes/free slots 为暂不可读。
该入口无 driver mutation、队列消费或自动清理；顺序采样时间窗不代表全局原子快照。
当前 pool/control 字节口径不包括全部 driver/queue/JS/分配器开销，也没有宣称 W-09
预算已完成。组合及 Monitor/CSI 实际转换函数的 GC/OOM、Monitor 原生保留与引用
耗尽、CSI allocator 返回边界 fixture 已补，执行后置。16 项受影响生产源语法检查
与五种已有配置的 ROM 表核对通过，覆盖 DPP、C5/no-SoftAP、FTM、AP registrar 和
Wi-Fi-disabled gate；manifest 为 57 classes / 557 functions。五份 fixture 仅作
AST，未导入、编译或执行。记录见 `build/w09-diagnostics-evidence.json`。
本批没有链接新镜像；实机、全矩阵和长期 soak 未执行。

2026-09-11：`startAP({allowDisconnect:true,...})` 接入中央配置执行器。顶层严格布尔
输入在所有配置捕获成功后才交付；嵌套 driver 不接受该权限。原生选择在同一 Radio
锁内保留当前 Station mode 与 storage、增加 AP，然后用既有精确 owner 准入。
切换明确断开 Station/AP 客户端，STOP/事件屏障后写入和校验新配置，重建 AP/APSTA，
不自动重连 Station；Station 已存配置不由本入口重写。默认匹配共享重开保持原行为。
同配置的显式请求仍执行完整事务；其他 feature、pending native 和 cleanup 不因权限
被绕过。失败由 stop/runtime 继续清理，不持有 caller 凭据用于重放。保持 Station
连接的新配置激活仍未实现；不能把这个显式断连路径算作该项通过。生产 parser、公开
adapter、Radio 选择和中央执行器 fixture 已更新，运行后置；定向语法/生成物核对
记录为 `build/w02-ap-activation-evidence.json`，无全量构建、实机或 soak。

2026-09-11：`wifi.deauthClient(address)` 已接通生产 Radio/JS/类型与注册。固定三目标
SDK 的 `ieee80211_ioctl` 在 Wi-Fi task 内直接调用 handler，因此 MAC→AID 查询与
定向 deauth 放进同一原生命令；不同于分别从 runtime 发两条命令。严格单播 MAC
预验证、AID 非零/范围检查，返回 false 表示该 MAC 不在当前关联表，true 仅表示 SDK
接受请求。共享 Radio operation 保留精确 AP lease；未知 IPC 保留一份有界堆命令与
fault，迟到回执由 poll/清理收取，不重发。没有 AP restart、全局 deauth 或持久 AID。
SDK 证据与当前契约见[客户端记录](2026-09-08-w02-client-queries.md)；集中运行与 RF
仍 not-run，本批只作生产源定向语法、fixture AST 和生成物核对。

2026-09-11：DPP 的已知 `dpp-restore-start` 故障接入 `session.recover()`。公开调用只
排队，不作同步 SDK mutation；已有 worker 重新检查 operation、所有原 helper lease、
RAM storage、原生退休及精确故障来源。只有 STOP/fence 完成后才清除此来源的原故障，
重新核对旧配置并授权一次 START；恢复内再次 START 失败需要新的显式请求。请求在
pending 期间幂等，计数不回绕；未知来源及 runtime teardown 期间拒绝新请求。未获授权
时停止重复调度相同 START 失败。恢复用于完成关闭，仍由 `close()` 等待，连接不会重连。
实际 Radio 与 Session scheduler fixture 已补请求合并、停止屏障、再次失败、未知来源
拒绝和 teardown 边界；当前只 AST，运行及 RF 验证待 Wi-Fi 集中验收。

2026-09-11：DPP 的合格 PMF-disabled 旧配置已接入关闭恢复。共用原生 STOP 执行器
新增精确 DPP owner 准入，原生 DPP 退休且 Station 断连后才可停止；所有 helper lease
仍留在原 registry。APSTA 增加默认 false 的 `connect(...,{allowApRestart:true})`
许可，缺失许可在 credential/storage 修改前拒绝。停止后复用既有 PMF 恢复 helper，
配置完整读回后启动；已接受的 STOP/START 只重试 event/fence 后缀。配置、省电、
TX power、启用接口 inactive-time 与 home channel 恢复完成后才恢复原 storage。
SDK START 本身失败保留 Radio fault/owner，不自动重发；显式恢复见上一条，未知来源与
不满足 SDK PMF 安全前置的旧状态继续属于剩余范围。后台 STOP/START 改用不进入 JS runtime
的原生事件等待，避免全局 active runtime 被后台线程修改等待期限或调用协作 hook。
Radio 用例直接接入生产 STOP 与 fault/cleanup 实现，补跨 owner 拒绝、迟到 STOP/START、
PMF 部分成功、storage 恢复失败不重放策略，以及 SDK START 失败不自动重发。
Radio/Session 与共享 STOP 用例已同步，动态执行、AP 客户端影响与实机验证仍为 not-run；
本批仅对九个 C3 生产单元作定向语法核对、14 份 fixture 作 AST、生成 ROM 表和
manifest 一致性检查，记录见 `build/w08-dpp-public-evidence.json`，不构建新镜像。

2026-09-11：DPP 显式连接已接通 `connect(index, options)`、共同 Station 连接槽和
原生协商认证核对。接收完成后由用户选配置；强认证缺少 build 支持不降级，取得 IP
后还须核对实际 BSSID/AKM。相同选择可重新转换现有结果，转换失败不释放连接。
关闭先排空 Station 断连与 timer/event fence，再退休 DPP 并恢复原配置、信道、
storage；失败保留 owner，仅重试未完成后缀。新增/更新实际 Radio、Station、Session、
Future、原生认证与 MQuickJS Nth allocation/移动 GC fixture，仅 AST，不执行。
本批只做九个 C3 生产单元的定向语法与 ROM 表/manifest 核对，记录与输入 hash 见
`build/w08-dpp-public-evidence.json`，不产生新固件镜像。PMF-disabled 旧配置恢复与
集中运行/RF 继续待完成；本条替代下方历史“公开连接未接入”的状态。

2026-09-11：DPP 公开捕获层接通原生 Session/Future/watch，类型、文档与 manifest
同步；修复 ROM 表生成缺少 `CONFIG_ESP_WIFI_DPP_SUPPORT` 的 gate，避免源码注册而
实际生成入口缺失。选配置的内部准备函数按收到的 AKM 选择 Connector/SAE/PSK，
缺少 SAE build 能力不自动降级；显式认证必须在该行 AKM 中，不接受密码截断、
不可表示的含 NUL Station SSID 或无效 key/Connector 长度。原生 worker 先释放已
退休捕获结果，再申请新的 native identity 并安装所选 Connector；失败保留准确
后缀，未知 IPC 不释放仍被引用的输入。独立 Network Introduction 不再要求旧
provisioning auth 对象存活，发送使用当前 driver 信道，连接模式不接纳新的配网
认证帧。该内部路径尚未接通 Station/Future 连接，因此未注册公开连接方法。
九个 C3 生产单元（含当前 ROM 表与生成的 SDK DPP 源）定向语法编译通过，零警告；
新增真实 VM 转换 GC/Nth OOM、Future/观察队列、认证选择和原生安装/超时 fixture，
只作 AST，未导入、编译或执行。证据：`build/w08-dpp-public-evidence.json`。
本条覆盖下方历史记录中“公开 JS 注册仍待完成”的描述。

2026-09-11：DPP TX buffer 与 Radio/原生 Session 合并接入。off-channel record 清空、
TX 完成、recycler 入口均不单独释放 owner；精确 recycler 返回后才释放当前 buffer
账本，异常 producer/未知 pool 类型保留故障。C3/S3/C5 固定 SDK 的 HMAC 输出均先
追加 EB 再 `pp_post(5, NULL)`，失败仅重唤醒；后续协议帧复制上限沿用当前 Action
body 的 1476 bytes，并限制为一份，不提前复用 op_id。新增私有 buffer/wake/排队字节
诊断。Radio 保存 home channel 并保留 application/Station/AP 精确 lease；AP 信道
中断需要显式同意。复用 WPS 的 Station 预约槽并区分操作类型，防止交叉释放；原生
Session 在 worker 外保留 JS 独立引用，URI/配置转换前可重读，配置 receipt commit 后
仍保留行供后续显式选择，close 清零。DPP 接收不自动安装或连接任何配置。
阶段用例已补写 TX/recycle、Radio、Session 和共享 helper；仅 AST，运行 `not-run`。
本批局部核对记录为 `build/w08-dpp-session-evidence.json`，全构建矩阵与实机仍后置。
公开 JS 注册、选择连接（含 Connector/hybrid 凭据生命周期）继续待完成；本条覆盖
下方历史记录中“Radio/worker/原生 Session 尚未接通”的状态。

2026-09-11：[DPP 信道超时隔离](2026-09-11-w08-dpp-chm.md)已编码：复用两个
timer，以 deadline wake 和每 arm 数字身份隔离旧 callback/原生消息；native
record reset 先撤销 min/max，DPP poll 处理队列投递失败后保留的到期操作。
原始错误与删除失败 handle 保留，补三目标布局/源码 gate 和私有诊断。四个
C3 生产 translation unit 定向语法编译及三目标 archive 核对通过，四份 fixture
仅 AST；无新构建/ELF/运行结果。全部 TX buffer 退休与公开接入仍待完成。

2026-09-11：[DPP 后台命令与 eloop 重唤醒](2026-09-11-w08-dpp-worker.md)已接入：
SDK 一次性 wake 投递失败后复用原 timer/list 重试；新增 DPP worker 的输入
预验证、初始化/bootstrap、监听、结果 copy/commit、关闭后缀和不明 IPC 保留。
当前 worker 尚无 Radio/Session caller，公开 DPP 仍未注册。channel-manager
旧 timer 隔离、全部 TX buffer 退休和公开接入继续待完成。一次 C3 DPP 增量
构建通过；eloop 修复已链接，worker 已编译入库，三份动态 fixture 仅 AST。

2026-09-11：[DPP TX 原生结果与操作身份](2026-09-11-w08-dpp-tx.md)已接入：
原生发布前捕获、64-bit 调度 ticket、发送/dwell 独立完成、取消后缀和重用准入；
移除默认 TX 事件消费者，DPP TX/ROC 观察零等待。新增生产 TX fixture 并同步
四份已有 fixture；一次 C3 DPP 增量构建与实际 ELF 核对通过，运行继续后置。
原生执行队列/timer 隔离和全部 TX buffer
退休、Radio/worker/公开 Session/Future 与凭据选择连接仍待完成。

2026-09-11：[DPP ROC 原生完成与关闭](2026-09-11-w08-dpp-roc.md)已接入：
提交前数字身份/调度预留、原生 done_cb 有界捕获、物理回收后复用、取消后缀与
RX 切换错误传播；移除 ROC 默认事件消费者。一次 C3 DPP 增量构建通过，ROC
生产路径进入 ELF；五份 fixture 仅 AST。TX 实际帧身份、完整原生队列压力恢复、
Radio/worker/Session、连接选择和公开 API 仍未完成。全矩阵和运行验收继续后置。

2026-09-11：[DPP 原生结果与关闭](2026-09-11-w08-dpp-native.md)已编码：
有界 URI/完整凭据 copy/commit、首终态保留、全部异步任务数字 ticket、精确
driver 取消/静止检查、部分初始化清理和默认事件屏障；内部 native 命令已接入。
延续[输入/凭据事务](2026-09-11-w08-dpp-input.md)，托管接收改为显式选择前不安装
配置，独立 C3 DPP 增量构建及静态产物检查。新增/更新四份 fixture 仅 AST。
同会话 TX/ROC 迟到完成与生产事件队列饱和、Radio/worker/Session、连接选择和
公开 API 仍待完成；尚无 DPP 注册入口，其他 Wi-Fi 与集中运行/实机范围保持不变。

2026-09-11：[AP WPS 公开接口](2026-09-11-w08-wps-ap-session.md)已接入
`wifi.wps.startAP/apStatus` 与 `WiFiWpsAPSession.status/watch/receive/cancel/close`，
含严格参数、PIN/首次注册 MAC copy/commit、Future 与独立有界观察。启用构建
`apRegistrar` 为 true；关闭构建无 AP 类/入口。新增 host ROM 开关传递，产物
检查覆盖实际注册，替代下方历史“公开 AP 未接入”。完整安全/失败恢复、5 GHz
registrar、其余 Wi-Fi 功能以及集中运行/实机验收仍待完成；新增 fixture 仅 AST。
开发中仅检查受影响目标，完整构建矩阵保留到模块/阶段节点。

2026-09-11：[AP Radio/helper 与原生 Session](2026-09-11-w08-wps-ap-session.md)
已接入共同 WPS operation/fence、三 helper lease 保留、独立预留、PIN/注册结果
与后台/runtime 关闭。APSTA 保留现有 Station 连接，当前固定 SDK registrar
RF 声明限定 2.4 GHz。C3 registrar 增量构建通过，新增 fixture 仅 AST。
公开 AP options/Session/Future/watch、5 GHz registrar 和集中运行/实机仍待完成。

2026-09-11：[AP 托管命令、原生退休与 worker](2026-09-11-w08-wps-ap-native.md)
已接入精确命令 identity、旧 AP/Station SDK 命令隔离、停止输入/SDK 析构/
checkpoint/release、客户端 WPS IE 释放和 retained IPC/timer worker。仅做
C3 registrar 增量构建及局部静态检查，运行 fixture 仍仅 AST。Radio/AP lease、
runtime 与公开 Session/Future/watch 尚未接入；全矩阵和运行/实机验收继续保留。
按用户最新安排，开发中按功能批次检查受影响目标，完整构建矩阵放到模块节点。

2026-09-11：[AP 输入存活与 WPS IE 所有权](2026-09-11-w08-wps-ap-input.md)
已接入 driver RX/关联 peer 查找和 semaphore、普通 WPA/Station owner 分流、
有界 WPS IE 解析与替换/失败释放。WPA main 保留 Enterprise 修复并合并编译，
新增独立 no-Enterprise 上下文。完整 native 输入排空/托管 release、Radio/
worker/公开 AP Session 和集中运行/实机仍未完成；表内指针匹配不代表跨关联身份。

2026-09-11：[AP WPS 初始化失败关闭义务](2026-09-11-w08-wps-ap-init-cleanup.md)
已接入修改前 bind、部分 allocation 保留、统一关闭后缀、原始/清理错误分离及
精确 EAP 方法登记。九配置构建与静态产物核对通过，新增用例仅 AST。辅助 AP PIN set/random 没有当前源码 caller/ELF 入口，
reenable 没有注册路径；不再把它们列为当前可达 Session 的阻塞项。完整 native
输入排空/托管 release、Radio/worker/公开 AP Session 和集中运行/实机仍未完成。

2026-09-11：[AP WPS 延迟客户端与 EAP 释放交接](2026-09-11-w08-wps-peer-retire.md)
已接入数字 peer ticket、删除义务、移出 STA 表前的 EAP 转移、Wi-Fi task
析构和父对象 child/producer 检查。九配置生产构建通过；运行用例仅 AST。
AP PIN 辅助 timer、部分初始化失败保留、完整 native 输入排空/托管 release、
Radio/worker/公开 AP Session 继续待完成，不能据此宣称 AP registrar 可用。

2026-09-11：[AP WPS EAPOL 定时器与客户端处理](2026-09-11-w08-wps-eapol.md)
已编码数字 timer ticket、真实客户端表查找/SAE peer lock、native activity 和
本客户端 abort；关闭前缀撤销 EAP timer。延迟删除身份、跨任务 peer 析构、
完整 native 排空/release、Radio/worker/公开 AP Session 继续待完成。
九配置（含独立 C3 no-SAE）构建与静态产物核对通过；新增用例仅 AST，运行及
实机仍未执行。

2026-09-11：[AP WPS 关闭前缀与父对象保留](2026-09-11-w08-wps-ap-close.md)
已编码回调深度保护、逐步清理/失败后缀、关闭后禁止重发广告，以及 AP 退出检查
WPS 关闭错误。托管状态在完成前缀后仍保留，不能据此释放 EAP/native 引用；
完整排空/release、部分初始化失败保留、Radio/worker/公开 AP Session 继续待完成。
八配置构建和静态产物核对通过；新增回归用例仅 AST，运行及实机仍未执行。

2026-09-11：[AP WPS 原生结果 owner](2026-09-11-w08-wps-ap-result.md)
已编码精确事件身份、PIN copy/commit、首终态存储和零等待观察。托管结果在 SDK
detach 后继续保留并阻止复用；绑定后的排空/release 尚未实现。八配置构建及
静态产物核对通过，fixture 仅 AST；Radio/worker/公开 AP registrar 继续待完成。

2026-09-11：[registrar 协商定时器身份](2026-09-11-w08-wps-registrar-timers.md)
已编码每次 arm 不复用的数字 ticket、旧回调隔离和失败保留；PBC/PIN 不再忽略
timer 注册失败，AP start 保留原始错误。八配置构建及静态产物核对通过，fixture 仅 AST。
原生结果 owner、其余回调排空、共享 Radio 和公开 AP registrar 继续待完成。

2026-09-11：[AP registrar 的 EAP 客户端所有权](2026-09-11-w08-wps-registrar-peers.md)
已将握手协议/凭据改为每客户端独立持有，移除 EAP reset 中的全局 disable，
修正客户端、authenticator 和 registrar 的释放顺序。八配置构建及静态产物核对通过；
fixture 仅 AST。原生 timer/回调退休、共享 Radio 和公开 AP registrar 仍未完成。

2026-09-11：[WPS registrar 初始化/凭据边界](2026-09-11-w08-wps-registrar-init.md)
已编码 SDK 初始化错误传播、借用 context 释放顺序、凭据替换/清零及实际
registrar 编译能力修正；原五配置未启用独立 registrar 开关，新建三目标
启用上下文；八配置构建和静态产物核对通过。公开 AP registrar、原生身份/回调退休及完整
生命周期仍未实现；新增 fixture 仅 AST，运行/实机仍待集中执行。

2026-09-11：[WPS 公开 Session/Future/watch](2026-09-11-w08-wps-public.md)
已接入 Station enrollee PBC/PIN 的 start/status/capabilities、PIN/凭据
copy/commit、receive/close Future、同步 cancel 和有界 metadata watch。
替代下方历史“尚无公开 API”；AP registrar、完整安全/失败注入及集中
运行/实机验收仍待完成。五配置生产构建与静态产物核对通过；manifest
55 classes / 538 functions，新增 fixture 仅 AST，运行验收没有提前执行。

2026-09-11：[WPS Station helper 与原生 Session](2026-09-11-w08-wps-session.md)
已接入 helper 预留、DHCP/IP 排空、两阶段关闭、后台调度、PIN/凭据 copy/commit
及 runtime 退出门槛。五配置构建与静态产物核对通过；C5 roaming 原 3 MiB app
实际溢出，已保留原上下文并在独立 4 MiB app 上下文构建通过。Session 按需
768 B，SDK WPS 已进入 ELF，但公开 JS API/watch 尚未注册。运行/实机仍待集中执行。

2026-09-11：[WPS Radio 与 Station 配置恢复](2026-09-11-w08-wps-radio.md)
已接入 helper 精确持有、原生 enable/start 分离、协商 RAM storage、原配置/
信道/存储恢复及 boot event fence；五配置生产构建和静态产物核对通过。
Radio 绑定按需 408 B，boot 新增 12 B；公开 WPS 尚无 caller。Station helper
连接/IP 状态交接、runtime/Session/Future/GC/watch 和集中运行/实机仍待完成。

2026-09-11：[WPS 原始错误与接收端边界](2026-09-11-w08-wps-errors.md)
已接入初始化/扫描/TX 原始错误和阶段、失败撤销、配置失败禁止连接、
RX peer/phase 检查及部分临时秘密清理。五配置生产构建与 archive/ELF/静态
核对通过；动态故障注入和 RF 未运行。Radio/Station 恢复与事件交接、公开
Session/Future/watch 和完整安全/运行验收仍待完成，未宣称 WPS 已公开。

2026-09-11：[WPS retained worker 与原生结果释放](2026-09-11-w08-wps-worker.md)
已编码配置/结果 IPC 存储、timer/native 排空、只读扫描静止检查、callback
revision 与精确 result release。Radio/Station 配置恢复和 boot event 排空、
完整错误/安全边界、公开 Session/Future/watch 与集中运行/实机验收仍未完成。
五配置生产构建、静态契约和 archive/ELF 核对通过；worker 624 B，原生 record
392 B，均按需分配，尚无公开 caller，镜像尺寸未变化。

2026-09-11：[WPS 分步停止与 SDK 状态释放](2026-09-11-w08-wps-cleanup.md)
已编码逐项清理后缀、真实 callback depth、部分初始化失败存储保留和独立
SDK heap 释放。结果 owner 仍保留，不能据此重开；完整 scan/native 排空、
record release、Radio/IPC/Future 和公开 Session 仍未完成。运行验收继续后置。
本批五配置生产构建及静态产物核对通过，record/status 大小和镜像尺寸未增加。

2026-09-11：[WPS 原生结果 owner 与 timer 身份](2026-09-11-w08-wps-native.md)
已接入 native begin/start、PIN/三组凭据 copy/commit、精确 64-bit timer、托管
终态与关闭撤销；托管控制不等待默认观察队列。SDK 返回不代表回调全部退休，
record release/reuse、Radio/IPC/Future 和公开 Session 仍未实现。五配置生产构建
与静态检查通过，阶段运行未执行。

2026-09-11：[WPS 凭据与设备信息边界](2026-09-11-w08-wps-credentials.md)
已接入 build-local SDK：修复 factory format、短凭据残留、配置失败仍进入成功
路径及部分秘密副本释放。公开 WPS Session、可靠原生完成和共享 Radio 接入仍
未实现；五配置生产构建和静态检查通过，新增用例仅登记，阶段运行/实机验收仍后置。

2026-09-11：[SmartConfig 原生 OOM 交付](2026-09-11-w08-smartconfig-oom-status.md)
已接入专用 calloc 观察、原生 owner 错误记录、start/status 检查和凭据 copy/commit
门槛；沿既有 Session/Future 交付原始 OOM 并继续关闭，不等待观察队列。SDK 栈
秘密副本、完整原生失败恢复及阶段运行/实机验收仍未完成。

2026-09-11：[SmartConfig SDK 分配失败保护](2026-09-11-w08-smartconfig-oom.md)
已修复 C3/S3/C5 的两处 calloc 后 NULL 检查前写入，以及 v2 失败 free 后的
`psni_info` 残留。保留 SDK 原失败分支，未执行 OOM 注入；端到端错误诊断、
完整失败恢复与栈秘密副本仍须继续处理，不能写成 SmartConfig 已全部完成。

2026-09-11：[SmartConfig SDK 解码堆副本清理](2026-09-11-w08-smartconfig-heap.md)
已在 build-local archive/adapter 接入专属 secure free，完整分配清零后释放，
保持共享 Wi-Fi allocator/table 与 SDK 不变。五配置生产构建通过；SDK 栈副本
和原生 calloc 失败路径仍须继续处理，集中运行及实机验收仍未执行。

2026-09-11：[SmartConfig 专属观察队列](2026-09-11-w08-smartconfig-watch.md)已接入
`Session.watch()`：metadata 快照、drop-newest、Future 终态门槛、队列/Session
存活及 runtime detach。五种生产构建通过；manifest 为 54 classes / 530 functions。
此项替代下方历史“专属观察未实现”。SmartConfig SDK 内部秘密副本清理，以及
所有 Wi-Fi 阶段运行与实机验收仍未完成。

2026-09-11：[SmartConfig 二进制 custom data](2026-09-11-w08-smartconfig-custom.md)
已从同步原生事件边界接入 credential bundle、Radio/Session copy/commit 和公开
`customDataBytes`。ESPTouch v2 保留精确 0–64 字节（包含零字节），其他协议返回
null。五配置生产构建及静态契约检查通过，新增运行用例仍未执行。此项替代历史
“custom data 未实现”；专属观察、SDK 内部秘密副本清理及集中运行/RF 验收仍待完成。

2026-09-11：[SmartConfig 自动连接与 ACK 交接](2026-09-11-w08-smartconfig-connect.md)
已接入显式 autoConnect：捕获/事件排空后复用 Station helper，取得 IPv4 后发送 ACK，
成功时保留应用连接并退休 Session 原生 owner；交接前关闭/超时只清理本次连接。
五配置生产构建及静态契约检查通过。发现并修复无连接 generation 的失败清理被
轮询饿死问题，回归用例已登记但未执行。此项替代下方历史“自动连接未实现”；
SmartConfig 仍缺专属观察、custom data、SDK 内部秘密清理及集中运行/RF 验收。

2026-09-11：[SmartConfig 公开凭据 Session](2026-09-11-w08-smartconfig-public.md)
已接入 start/status/capabilities、Session receive/close Future、精确身份诊断及凭据
转换成功后 commit。替代下方历史“尚无 JS API”；当前公开的是凭据获取流程，
仍缺专属观察队列、custom data、SDK 内部秘密副本清理及自动连接/手机 ACK 交接。
不会把这些缺口改写成已完成；阶段运行及实机验收继续后置。

2026-09-11：[SmartConfig Radio/runtime Session](2026-09-11-w08-smartconfig-session.md)
已接入精确 operation/全 helper lease 固定、捕获后的信道恢复、后台 worker、
凭据 copy/commit 转移和 core teardown 门槛。仍无公开 JS Session；custom data、
SDK 内部秘密清理和显式自动连接/ACK 交接继续待完成，集中运行/实机测试未执行。
此项替代下方历史“Radio/helper/runtime 未接入”的描述。

2026-09-11：[SmartConfig 定时器与原生协调](2026-09-11-w08-smartconfig-decoder.md)
已串联事件、九个 timer、decoder 和精确 ACK reservation，含保留凭据的
finish_capture 与完整 close；五种生产构建通过。当前仍是内部 worker 入口，
Radio/helper/runtime、JS Session、custom data 和显式自动连接未接入，运行测试
未执行。此项更新下方历史“decoder 协调未接入”的描述，不代表公开 API 完成。

2026-09-11：[SmartConfig 原生事件与凭据 owner](2026-09-11-w08-smartconfig-events.md)
已补 exact identity/generation、单份凭据 copy/commit、关闭清零和 native event
捕获。尚无公开 caller；Radio/decoder 退休、AES/custom data、ACK handoff 与
公开 Session 继续待完成，不能把内部 event boundary 当作新 API 交付。

最新非 TWT 增量：[SmartConfig 原生 ACK 修复](2026-09-10-w08-smartconfig-native.md)
已编码，解决停止/重开共用开关、默认密码日志和 ACK 完成队列阻塞。
公开 Session、解码器/事件退休和凭据交付仍未接入；不把内部修复记作 API 已实现。
另已确认固定 SDK 支持编译启用 WAPI-PSK，却没有任务草案中的独立公开
WAPI enable/disable；该独立控制契约仍待落实，不能用普通 WPA 或私有 init/deinit
冒充。三个目标的 WAPI-enabled 构建与认证 RF 验收仍待完成。

下方长表和增量记录保留实施历史；当前不要把历史的“未公开”描述覆盖后续已接入项。
Monitor、Raw TX、Vendor IE、Action/ROC、FTM、roaming request/watch 和 Enterprise
以及 C5 HE TWT probe、individual Agreement setup/status/close/suspend/resume 均已有 Candidate 公开入口；这些模块的完整行为及阶段运行验收尚未完成。

广播 TWT 的 `broadcasts(options?)` 查询已接入原生任务快照和 Future；
AP 公告/本站加入状态分开表达，不创建 Agreement。
广播 setup/teardown、完整物理故障恢复仍未完成，见
[广播发现记录](2026-09-10-w08-twt-broadcast-discovery.md)。
随后已接入[广播 setup 响应/dwell timer](2026-09-10-w08-btwt-timer.md)：
数字身份、参数副本、stop/delete 失败后缀及原生连接关闭撤销；广播 TX/RX
完成关联、原生结果、Agreement owner/联合回收与公开 setup/teardown 仍未完成。
[广播定时器完成/观察分离](2026-09-10-w08-btwt-event.md)随后已接入：原生
处理返回并尝试清理后保存精确 timer 结果，最后零等待观察。TX/RX 关联、
定时器范围外的托管结果、Agreement owner 和公开 setup/teardown 仍未完成。
[广播 setup TX 身份](2026-09-10-w08-btwt-tx.md)随后已接入 output/callback/
close 路径，复用可选 identity 存储，撤销断连前旧 TX。广播 RX 关联、
托管原生结果、Agreement 和公开 setup/teardown 仍未完成。
[广播 RX 长度与响应关联](2026-09-10-w08-btwt-rx.md)随后已接入：15-byte
广播/PMF 完成副本、当前 timer/dialog 关联及清理后再调用 SDK。RF dialog
重用后的隔离、托管结果、Agreement 和完整回收仍未完成。
[广播 dialog 不回绕分配](2026-09-10-w08-btwt-dialog.md)随后已编码：同
boot 每广播 ID 最多 255 个非零 token，耗尽明确拒绝、重连/runtime restart
不重置。公开限制/错误交付、托管结果、Agreement 与完整回收仍待接入，
该策略的实际对端互操作与运行测试尚未执行。
[稳定 setup 原生结果](2026-09-10-w08-btwt-result.md)随后已接入发送前
预留、TXFAIL scope、response/dwell 结果保留及输出/关闭错误分离；复用
原 timer 池增加 256 B INTERNAL。公开提交身份交接、Agreement owner、
teardown 和联合回收仍未完成，精确 reader 尚无公开 Future caller。
[广播提交与结果持有](2026-09-10-w08-btwt-submit.md)随后补上 boot 配置、
原生 task/精确身份交接及发送前 held。同 ID 在结果持有期间不能重新准入，
断连不清持有者；联合退休/精确释放、Radio/Agreement/Future 公开 caller
仍待完成。worker 提交目前在 archive，未注册占位 API。
[广播 pending 取消/静止检查](2026-09-10-w08-btwt-cancel.md)已接入私有
原生队列，精确撤销 TX/timer/RX，保留 stop/delete 后缀和 held，拒绝把已
建立 Agreement 当作 pending 删除。静止值不代替顺序屏障；联合退休/释放、
teardown、公开 caller 及阶段运行继续待完成。
[广播 pending 联合退休/释放](2026-09-10-w08-btwt-retire.md)随后已补
worker 协调器、timer/native 顺序屏障、boot Radio 事件确认及精确 held
释放；原生路由已接入，worker 仍待公开 owner 调用。已建立协议 teardown、
Radio/Agreement/Future 和集中运行验收仍未完成。

[广播 teardown 原生提交/TX/PM](2026-09-10-w08-btwt-teardown.md)随后已接入：
复用单例、精确 held setup/node 验证、PMF 完成副本及控制先于观察。连接关闭
永久撤销旧 node，pending 回收拒绝尚持有的 teardown；联合回收、失败策略、
Radio/Agreement/Future 公开接入仍未完成，运行验收继续后置。

[广播 teardown 联合回收](2026-09-10-w08-btwt-close.md)随后已编码：精确
关闭结果/TX/PM 检查、三种 revision 和既有 timer/native/event 顺序，释放
teardown 后再撤销 held；保留结果和失败后缀，不自动重发。公开 owner、
setup/Agreement/Future、显式失败恢复与集中运行仍未完成。

[广播 Radio/Agreement/Future 公开接入](2026-09-10-w08-btwt-public.md)随后已编码并通过五种目标构建：
`setupBroadcast`、精确 owner、共享 Future/GC/runtime 清理、公开 dialog 预算和
错误状态已接入 Candidate。上文“公开 caller 未完成”是历史阶段记录；当前仍缺
完整显式物理故障恢复、all-flow 策略及集中运行/RF 验收。

[individual 原连接关闭与清理](2026-09-10-w08-twt-native-close.md)随后已接入：
真实 close-all 永久撤销 request 和 setup/information 数字回调；保留原失败结果，
关闭后仍需 TX/timer/native/event 排空，避免永远等不到 teardown 成功或把旧
连接显示 active。完整物理恢复、all-flow 策略和集中运行仍未完成。

[共享信息定时器关闭授权与 closeAll](2026-09-10-w08-twt-close-all.md)已接入：
精确 request 的关闭意愿允许全部相关 owner 关闭时撤销共享 timer；公开
`closeAll` 原子选择当前 individual/broadcast owner 并等待其退休，超时保持清理。
此项是多 owner 关闭协调，不等于 STOP/deinit/replay 的完整故障恢复，也不代表
新增 all-flow suspend/resume；集中运行与 RF 验收仍后置。

[TWT 共享 Radio 恢复](2026-09-10-w08-twt-recovery.md)已接入 Candidate：
显式 generation/closeAll/allowDisconnect 准入，冻结原 owner，先撤销 probe，
断连/STOP 后保持 driver 初始化直至原 TWT owner 联合退休，再 deinit/配置重放。
公开 Future 超时/取消保留中央清理，迟到 probe 销毁不覆盖后继 owner。
当前支持健康可读 STARTED 来源；未知 handoff、sticky tracking fault、已故障/
不可读来源、all-flow suspend/resume 及集中运行/RF 验收仍未完成。

| 剩余类别 | 当前待完成内容 |
| --- | --- |
| TWT | C5 HE `capabilities/status/probe`、Radio/Future/联合回收已接入 Candidate，probe 保留 AP Beacon/Probe Response 存活语义；setup timer/TX 身份、原生结果、pending 取消/联合释放与 teardown 提交已编码；[teardown TX 身份与 PM 引用](2026-09-10-w08-twt-teardown-tx.md)已通过目标构建/静态检查。[信息定时器参数/数字身份](2026-09-10-w08-twt-information-timer.md)已接入原生路径。[信息 TX 身份与 PM 回收](2026-09-10-w08-twt-information-tx.md)已编码并接入原生回调及关闭检查。[individual Agreement setup/status/close](2026-09-10-w08-twt-individual-public.md)已接入 Radio/Future/GC/runtime，仍需阶段验证。[individual suspend 与信息操作完成关联](2026-09-10-w08-twt-suspend.md)已接入；[显式 resume](2026-09-10-w08-twt-resume.md)已连接精确定时器完成；[广播 setup/Agreement/close](2026-09-10-w08-btwt-public.md)已接入 Candidate；完整物理故障恢复、all-flow 策略及 RF/阶段运行未完成 |
| 安全、漫游与配网 | Enterprise 完整 restart 恢复/共存，剩余 roaming；WAPI、WPS、DPP、SmartConfig |
| 网络拓扑 | 目标/build 支持时的 NAN、Mesh 完整生命周期与消息能力 |
| 基础与 Driver | AP live activation、全局 AP 策略、天线/共享 GPIO 写入、其余控制和故障恢复来源；deauthClient 实现见当前记录，运行仍待验收 |
| 数据与资源 | CSI 对应原始包、完整 PHY/time、跨代资源与控制保留预算、统一 diagnostics |
| 验收 | 所有 Wi-Fi API 完成后集中运行/实机测试；长 soak 留到 BLE API 完成后 |

本轮继续 [TWT 原生请求记录](2026-09-10-w08-twt-lane.md)，没有把纯内部记录
或构建通过标记为 TWT 已可调用。完整范围仍以 W-01～W-12 为准。
随后推进 [SDK 原生快照/setup 准入](2026-09-10-w08-twt-sdk.md)：区分 pending
和 established，保护 native slot/ID/flow 边界；实际 timer/TX 退休仍待完成。
[timer/native queue 顺序 helper](2026-09-10-w08-twt-fence.md)已补 marker 调度、
callback 存储保留与失败后缀清理，完整 SDK quiescence 和公开 TWT 接入仍未完成。
[管理帧 TX 存活观察](2026-09-10-w08-twt-tx.md)已接入实际 C5 output 与 ROM
recycler 函数表路径，保留缓存/同步回收/地址复用边界；probe TX、完整 native
quiescence 和 Radio/公开 API 接入仍待完成，运行与实机用例未执行。
[probe TX 范围](2026-09-10-w08-twt-probe-tx.md)已编码，沿原生 task/帧类型
纳入同一 ledger；新确认普通连接 probe 与 TWT 共用完成 callback 的身份缺口。
完整 probe 隔离和退休仍待完成，公开入口尚未接入。
[probe TX callback 身份隔离](2026-09-10-w08-twt-probe-callback.md)已修复共用
TX 回调路由并加入提前完成保留；RX/timeout 的身份与存储、完整联合退休仍待
完成，公开 TWT 不变为已完成。相关竞争 fixture 仅 AST，运行 not-run。
[probe timeout 参数与身份](2026-09-10-w08-twt-probe-timeout.md)已改用不可复用
数字，旧 callback/队列消息不再借用 node；完整 RX 语义、联合退休、故障恢复
与公开接入继续保留。构建/链接为静态证据，动态用例未执行。
[probe 原生完成与观察队列](2026-09-10-w08-twt-probe-result.md)已接入：
完成结果在 post 前保存，观察改为零等待，避免默认事件队列饱和挡住原生
清理。RX 成功是关联 AP 的 Beacon/Probe Response 存活观察，不是精确 RF
request cookie。完整联合退休、Future/Radio、公开 TWT 和动态验收仍待完成。
[probe timer 错误与清理](2026-09-10-w08-twt-probe-timer-errors.md)已将 create/
start 的 abort 路径改为原始错误记录和原生返回后清理，delete 失败保留同一
handle；本地错误不伪装成 RF timeout。完整联合退休、晚到 post 失败的请求
协调、故障恢复及 Radio/Future/公开 TWT 仍待完成。
[probe 唤醒引用](2026-09-10-w08-twt-probe-wake.md)已接入六个精确 SDK 调用点
及 framework abort，修复保留 timer handle 后 stop 可能再次减少共享 PM
计数的交互缺口。取消、联合退休及公开接入继续保留；动态验收未执行。
[probe 精确取消与存活状态](2026-09-10-w08-twt-probe-cancel.md)已接入私有原生
队列：校验 operation identity，清除数字 timer 权限并重试保留的 handle，
只释放匹配 probe 的 active/PM；保存取消结果并独立观察 TX buffer 存活。
取消成功不作为全部资源排空证明；联合退休、故障恢复及 Radio/Future/公开
TWT 继续保留，新增 fixture 仅 AST，阶段运行未执行。
[probe 原生 owner 与联合回收](2026-09-10-w08-twt-probe-retire.md)已编码：
托管 submit 返回精确身份并阻止提前复用，协调取消、TX 存活、timer/native
顺序及 copied-number event fence，原生 release 再次核对同一快照。私有
submit/确认路径已链接，联合 poll 尚待 Radio/Future caller；公开 TWT 和
其他 agreement 生命周期仍未完成，运行与实机测试继续后置。

[probe Radio/Future 与公开入口](2026-09-10-w08-twt-probe-public.md)已接入：
固定当前关联信道，保留精确 lease，Public Future worker 发布后才释放 storage，
boot cleanup 重试联合退休，runtime 排空进展复用现有 poller。新增 C5/HE 主机
注册表 gate 同步；生产 ELF/ROM 检查不代替运行或 RF 验收。

[iTWT setup 响应/dwell timer 身份](2026-09-10-w08-twt-setup-timer.md)已接入：
两阶段 callback/原生消息携带不可复用数字，消费前核对当前请求/关联，timer
错误保留原始阶段和未删除 handle。实际 SDK timer table 与两个 process 路由
已在 C5 ELF 核对；公开 Agreement、TX/RF 身份及完整退休继续保留，fixture 仅 AST。

[iTWT setup 原生结果与观察队列](2026-09-10-w08-twt-setup-result.md)已补：八槽
结果记录先保存控制数据，观察事件零等待；ID 不复用、冲突保留、提交/观察
错误分离。实际发布链已接入；托管 owner 的预留/读取/释放目前仅在 archive，
仍待公开 Agreement caller 和完整退休，运行验收未执行。

[iTWT setup 调用与身份交接](2026-09-10-w08-twt-setup-submit.md)已编码：复用
公开 SDK preflight/同步 ioctl，使用稳定配置副本、精确 native hook 和一次
身份领取；保留 API/driver/handoff 错误，异常提前返回保留 occupied 存储。
桥接仍在 archive，待 Radio/Agreement caller；没有新增可调用 JS API，运行未执行。

[iTWT setup TX 回调隔离](2026-09-10-w08-twt-setup-tx.md)已接入实际 C5 callback 19：
发送前绑定不复用的 response timer identity，完成时核对当前 pending/phase/临时 ID，
拒绝旧、重复或已回收的 individual TX；bTWT 分支保留。共用 TX ledger 延迟预算
增加 256 B，静态对象尺寸不变。Agreement/RF 关联/完整退休仍待完成，fixture 仅 AST。

[iTWT setup 精确取消](2026-09-10-w08-twt-setup-cancel.md)已接入 native ioctl：
校验结果身份、pending/flow/timer owner，先完成 stop/delete 后清除本请求；
失败保留 handle/pending 并重试后缀。已建立 Agreement 和异常 AP bitmap 不被
当作已取消。worker caller、完整联合退休与 RF 仍待完成，本批不增加资源预算。

[setup 观察事件排空](2026-09-10-w08-twt-setup-event.md)已接入 Radio 常驻
handler：复制 identity/sequence、固定发布中记录、失败或新观察撤销旧证明，
释放需精确 revision 与事件确认。八槽结果账本因对齐增加 64 B 延迟 INTERNAL
预算；完整联合退休、公开 Agreement 和阶段运行仍待完成，fixture 仅 AST。

[pending setup 联合释放](2026-09-10-w08-twt-setup-retire.md)随后已编码：保留
取消 flow 证据，复核 TX/timer/native 状态，串联 TASK/native/event 屏障后原生
释放结果；变化的 revision 或新观察撤销旧证明。执行器尚待 Radio/Agreement
caller，已建立 Agreement、bTWT/信息定时器和完整物理恢复继续保留，运行未执行。

[iTWT teardown 提交/观察](2026-09-10-w08-twt-teardown-submit.md)随后已编码：
单 flow 的精确请求准入、一次尝试、原始提交错误及事件 29 零等待原生捕获。
结果账本增加 64 B 至 576 B；teardown 记录不能走 pending 退休路径。完整
TX/PM/teardown 退休、Radio/Future/Agreement 与阶段运行继续待完成。

## 已有的基础 API 与尚缺的参数

目前 19 个顶层方法已注册：configure、start、stop、status、connect、disconnect、scan、
startAP、stopAP、apClients、getMac、setMac、setPowerSave、setTxPower、setCountry、setChannel、acquireWakeLock、capabilities、watch；另有
wifi.csi.capabilities/open 和既有 CSI Session/Frame/Batch 能力。

| 类别 | 当前实现 | 剩余范围 |
| --- | --- | --- |
| Radio 生命周期 | 精确 lease、跨步骤排他、内部 stop/shutdown/restart；公开 start(mode/storage)/stop(timeoutMs) 和 configure(mode/storage/start/controls)，start 缺省恢复已配置 Station/AP/APSTA；停机配置提交/回滚、三 owner 生命周期准入和启动交接已用于 startAP；Station/AP helper 准备/退休入口、event-task detach/fence 与共用接口执行器 | 配置恢复及 driver.restart；[原生阶段边界](2026-09-09-w07-restart-phases.md)已拆出，[runtime helper/netif executor](2026-09-09-w07-restart-runtime.md)已接入原生阶段和中央清理，[STOP 前功率/信道历史观察](2026-09-09-w07-stop-snapshot.md)已接入，[Radio 写入失效标记](2026-09-09-w07-stop-validity.md)已接入，[合格 STOP 观察的停机捕获](2026-09-09-w07-restart-stopped.md)已接入，[公开 Candidate restart](2026-09-09-w07-public-restart.md)已接入健康 STOP 来源的零 owner 准入和真实执行器；停机写入后/无观察来源与完整故障恢复仍待完成 |
| Station 连接 | password/timeout、BSSID/channel、scan/sort/门限、PMF disabled/optional/required、listenInterval/retry、5 GHz RSSI 偏好、RM/BTM/MBO/FT/OWE/SAE/H2E build gate 与冲突检查；HE/VHT、SAE-PK 与安全 flag；独立连接结果快照、关联 status | [嵌套 driver](2026-09-09-w02-connect-driver.md)已编码；[二进制 SSID](2026-09-09-w02-binary-ssid.md)已接入当前 SDK 可表示的 ByteSource 捕获和字节交付；完整高级认证配置仍待完成；[connect PMF 停机事务](2026-09-09-w02-connect-pmf.md)已编码，待集中验证 |
| 扫描 | SSID/BSSID、单信道/all/channels、主动/被动时间、主信道停留、coex、maxRecords/timeout；secondary/band/cipher/antenna/protocol/country/capability 结果 | 显式 5 GHz 选择要求 manual 非零法规 mask，隐式规则查询缺口待解决；完整阶段验收待执行 |
| SoftAP | startAP 独占冷启动 2.4 GHz；configure 可启动 APSTA、raw SSID/TU/target channel，stopAP(timeoutMs?) 已接入共享等待预算、仅退休 AP 并保留 Station 资源的路径，startAP 可共享重开匹配的已存 AP 配置；status().accessPoint 当前配置/信道/客户端数/MAC 与查询/清理诊断；扩展 auth/cipher/PMF/SAE、beacon/DTIM/CSA、GTK/transition-disable、FTM responder、SAE-EXT/兼容模式/BSS idle；readback 后 start | 不同 AP 配置的 live activation、实际 Station 连续性及 5 GHz/自动信道 RF 验收 |
| AP 客户端 | apClients(options?)，MAC/AID/RSSI/PHY；includeIp 可选本地 DHCP IPv4 查询 | 定向断开；AID/IP 只是后续观察值，实际关联/DHCP 竞争待集中验收 |
| 常用控制 | getMac、stopped/零 owner setMac、16 个独立 wake lock、Station setPowerSave/setTxPower、空闲 Station setCountry、Station/AP setChannel | AP/APSTA 国家配置事务与后续 target-specific 控制 |
| 能力发现 | capabilities 报告实际注册/gate、物理频段/法规快照、Station options 与共享 limits，无隐式启动 | 完整 security/protocol/bandwidth、Build Context identity，随对应 API 扩展 |
| 事件 | watch 有界入口与多订阅、53 个 descriptor，常用事件 typed snapshot，秘密事件 metadata-only | 高级事件 converter/专属秘密交付、逐布局 raw export 审查与集中竞争验收 |
| CSI | 既有 bounded capture/view/batch/source、legacy/HE adapter | W-05 correlated packet、Monitor 共用 broker 与 W-09 退休资源预算；CSI Batch 新 wire/Host parser 和 callback-time 已接入，待集中验收 |

最新恢复审计确认另有 [inactive time 持久化诊断与恢复缺口](2026-09-09-w07-inactive-persistence.md)：
公开 setter 已补 FLASH 写入可能性；[原启用接口的阈值捕获/STOP 历史/START 后重放](2026-09-09-w07-restart-inactive.md)已接入。
[同代隐藏阈值历史与重新启用时恢复](2026-09-09-w07-inactive-history.md)已接入 AP 关闭、setter、STOP 与 checkpoint。
[停用接口的原生 pending 转交与下一次激活恢复](2026-09-09-w07-inactive-deferred.md)已接入。
未知隐藏来源、停机后写入、无 STOP 历史及完整故障清理协调仍待处理；当前公开 Candidate restart 对这些来源明确拒绝，未缩减完整恢复目标。
[RSSI 一次性请求语义与诊断](2026-09-09-w07-rssi-request.md)已编码：不自动重放/重新请求，保留带 generation 的请求历史；运行验收未执行。
[合格 STOP 的原子零 owner 准入与 runtime/AP 转交](2026-09-09-w07-restart-admission.md)已编码；同锁解析 mode、检查 registry 并占用生命周期。[公开绑定/超时/状态/本次错误详情](2026-09-09-w07-public-restart.md)随后已接入；其余恢复来源与完整验收仍未完成。
[停机后 storage/RSSI 写入](2026-09-09-w07-stopped-controls.md)已在固定 SDK C3/S3/C5 放宽：保留原 RF 历史，冻结最新已接受 storage；失败 storage 仍须显式修复。其他写入/未知来源/故障恢复仍待完成。

## 完全未注册的顶层方法与模块

02 目标中的顶层 deauthClient 已追加实现，见本表当前记录。configure 已注册，实际完整范围与限制见[公开配置记录](2026-09-08-w02-public-configure.md)。
已注册方法仍有上表中的参数/行为缺口。

| 任务 | 尚未提供的公开能力 | 主要前提 |
| --- | --- | --- |
| W-02 | [startAP 嵌套 driver](2026-09-09-w02-ap-driver.md)已接入；[直接 connect PMF 关闭](2026-09-09-w02-connect-pmf.md)已接入启动前事务与连接前核对；[configure PMF 三态](2026-09-09-w02-config-pmf.md)已接入 Station/AP 原始配置；[AP PMF disabled](2026-09-09-w02-ap-pmf-disabled.md)已接入 startAP、读回和重开匹配；基础方法剩余参数、事件 broker 其余高级转换/raw（[五类状态 snapshot](2026-09-09-w02-watch-values.md)、[TWT 七类 snapshot](2026-09-09-w02-watch-twt.md)与[neighbor report snapshot](2026-09-09-w02-watch-neighbor.md)已编码）、共享 AP 生命周期；已注册 configure/APSTA 的集中验收 | 配置提交/回滚、共享 mode/channel 准入、逐事件字段与秘密交付 |
| W-03 | 单帧 wifi.monitor、Frame bytes/copyBytes/source、Session 生命周期/状态已注册；Batch receive/info/bytes/source/close 已注册；统一 wire 与 Host JSONL 已编码；[能力/队列/owner/命名过滤诊断](2026-09-10-w03-monitor-diagnostics.md)已接入；[HE-layout HT-SIG](2026-09-10-w03-monitor-ht-signal.md)已接入；[VHT SIG](2026-09-10-w06-vht-signal.md)已在 CSI/Monitor 共用；[HE SIG/统一 PHY snapshot](2026-09-10-w06-he-signal.md)已接入，完整 PHY/time 与跨代资源诊断尚缺；configure 支持停止后的完整替换和 Radio lease 交接 | 公共接口已接入原生池/queue/capture/Session；GC/OOM/关闭和 RF 集中验收待执行 |
| W-04A/B | wifi.rawTx one-shot/capabilities/open 和 Session send/enqueue/enqueueBatch/flush/startPeriodic/status/stats/close、周期对象 status/stop/close 已注册；[Station 临时速率租约](2026-09-09-w04-raw-tx-rate-lease.md)及停止后恢复已编码；[AP/APSTA 停止态临时 rate](2026-09-10-w04-apsta-rate.md)已接入，已故障/不可读来源恢复尚缺；已接入 [物理终止证据向 owner 传递](2026-09-09-w04-raw-tx-termination.md)和[公开 Raw TX recover/临时速率前值重放](2026-09-10-w04-raw-recovery-public.md)，可显式恢复可信健康 STARTED 来源；[可信速率写入记录](2026-09-09-w07-tx-rate.md)已接入 driver API | 已编码 [MAC validator 与无指针 callback snapshot](2026-09-09-w04-raw-tx-input.md)、[单 in-flight broker 与超时/注销隔离](2026-09-09-w04-raw-tx-broker.md)及 [Radio 发送/退休与通道保留](2026-09-09-w04-raw-tx-radio.md)，已接入 [ByteSource/Future、公开 one-shot 和固定清理槽](2026-09-09-w04-raw-tx-public.md)；完整故障来源恢复及集中竞争/RF 验收尚缺；B 已编码 [FIFO/整批准入/flush 账本](2026-09-09-w04-raw-tx-queue.md)，已将 [共享发送仲裁](2026-09-09-w04-raw-tx-lane.md) 接入 one-shot/退休槽，并连接 [原生 Session/worker/关闭](2026-09-09-w04-raw-tx-session.md)；已补 [逐次结果 watcher](2026-09-09-w04-raw-tx-results.md)，已接入 [公开 Session/queue/flush/send Future](2026-09-09-w04-raw-tx-session-public.md)、关闭后的缓存状态及全局 Session 诊断；已补 [原生周期调度账本](2026-09-09-w04-raw-tx-periodic-ledger.md)，已连接 [原生周期 owner/Session/自治 timer](2026-09-09-w04-raw-tx-periodic-job.md)，已接入 [公开周期/Future/全局诊断](2026-09-09-w04-raw-tx-periodic-public.md)；周期精度、GC/OOM、竞争和 RF 集中验收仍待执行；集中验收先 A 后 B |
| W-05/06 | CSI 关联原始包与完整 PHY/time 验收；CSI Frame/Batch wire、sampleSource、Monitor Batch wire、共享 Host decoder 和 Monitor JSONL/PCAPNG/显式时钟锚点已编码 | target capture 证据、writer/parser 原子替换 v1、时钟回绕契约 |
| W-07 | [显式 PMF disable](2026-09-09-w07-pmf-control.md)已注册并接入 disabled 前值恢复；[restart 启动前配置核对](2026-09-09-w07-restart-prestart.md)与[存储策略最终提交](2026-09-09-w07-restart-storage-commit.md)已接入，START/最终核对期间保持 RAM；[AP-only CSA 与随后 Station 启动](2026-09-09-w07-ap-activation.md)已接入内部恢复，[原生 restart 阶段](2026-09-09-w07-restart-phases.md)已拆分以允许 helper 在 STOP/重建/重放之间退休和接入，[内部 runtime executor](2026-09-09-w07-restart-runtime.md)已接入，[STOP 前功率/信道观察](2026-09-09-w07-stop-snapshot.md)已接入真实关闭，[跟踪 Radio 写入的失效边界](2026-09-09-w07-stop-validity.md)已接入，[合格 STOP 记录的捕获与临时 Station 准备](2026-09-09-w07-restart-stopped.md)已接入，健康 STOP 来源的公开 Candidate 准入已接入，其余停止状态恢复和集中验证仍待完成；[天线/GPIO 读取](2026-09-09-w07-antenna-observations.md)已注册，写入仍待共享 PHY/GPIO 所有权协调；[显式 setMode](2026-09-09-w07-mode-selection.md)已注册（stopped/零 owner、读回/回滚，NAN 待专属生命周期）；[显式 setStorage](2026-09-09-w07-storage-selection.md)已注册（stopped/零 owner、未知状态与显式修复）；固定 SDK clearFastConnect 为已确认 stub，不注册伪清理；wifi.driver.configureTxRate/txRateStatus 已注册，stopped/零 owner 写入、同代已知前值回滚和速率故障修复已编码；[协议/带宽四个 getter](2026-09-09-w07-phy-readback.md)及[四个停止后 setter](2026-09-09-w07-phy-write.md)已注册；[九个状态 getter](2026-09-09-w07-driver-observations.md)已覆盖 band/bandMode、power save/TX power、RSSI/AID/PHY、TSF/inactive time；[inactive time/RSSI 阈值 setter](2026-09-09-w07-connection-controls.md)已接入；[Station-only band/bandMode setter](2026-09-09-w07-band-control.md)已注册；[mode/country/current/home channel 读回](2026-09-09-w07-state-readback.md)及[事件 mask 控制](2026-09-09-w07-event-mask.md)已接入；[动态 CS/11b/coexistence 控制](2026-09-09-w07-policy-controls.md)已编码，当前 C5 coexistence power gate 关闭；[connectionless interval/ESP-NOW 精确恢复](2026-09-09-w07-interval-integration.md)已接入公开 setter、Radio 基准写入、ESP-NOW 四条路径和状态诊断；[无 getter 策略记录](2026-09-09-w07-policy-record.md)及[内部 pre-/post-start 策略重放](2026-09-09-w07-policy-replay.md)及[Station/AP 凭据 checkpoint](2026-09-09-w07-restart-configs.md)及[国家/MAC/省电/事件 mask checkpoint](2026-09-09-w07-restart-globals.md)及[启动后 TX power 精确恢复](2026-09-09-w07-restart-tx-power.md)已编码（stopped driver 无提前快照时拒绝）及[interval 原值/revision 恢复](2026-09-09-w07-restart-interval.md)及[Station/AP TX rate 恢复](2026-09-09-w07-restart-rate.md)及[PHY 全字段恢复主体](2026-09-09-w07-restart-phy.md)已编码（已接入[重建端 Station/AUTO/STOP 准备](2026-09-09-w07-band-prepare.md)及[SDK 内部重启事件屏障](2026-09-09-w07-band-cycle-events.md)，[捕获端单频准备与部分快照保留](2026-09-09-w07-band-capture.md)已编码，[原 band mode 与 Station channel 恢复](2026-09-09-w07-band-restore.md)已编码，[AP/APSTA 启动信道协调](2026-09-09-w07-ap-activation.md)已编码，读回不匹配不能交接 owner）；共享 AP/global policy、完整配置重放、其余 getter/setter、antenna/完整 restart 恢复尚缺 | 每项状态/owner/readback/restore 契约，不能绕过 Radio mutation |
| W-08 | [vendorIe set/clear/status Candidate](2026-09-09-w08-vendor-ie.md) 及[预启动交接](2026-09-09-w08-vendor-prestart.md)已接入；[Vendor IE watch](2026-09-09-w08-vendor-watch.md) 已接入；[Action/ROC 原生记录与取消保护](2026-09-09-w08-action-native.md)及[Radio/终态排空](2026-09-09-w08-action-radio.md)已编码，[公开 send Future/status/capabilities 与 runtime 清理](2026-09-09-w08-action-public.md)已接入 Candidate；[ROC Session/open/wait/close 与 runtime 清理](2026-09-09-w08-roc-public.md)已接入 Candidate；[SDK 独立退休证明](2026-09-10-w08-action-quiescence.md)补缺失/歧义终态回收；[Action/ROC 内部物理恢复阶段](2026-09-10-w08-action-physical-recovery.md)已编码，[恢复 checkpoint](2026-09-10-w08-recovery-checkpoint.md)已补不切频段的完整 PHY 与 home channel 保存；[恢复 helper 退休与中央失败清理](2026-09-10-w08-recovery-helper.md)已接入，[runtime 分阶段协调与公开 recover](2026-09-10-w08-recovery-public.md)已接入可读来源的准入、原 owner 排空和成功重放；不可读/已故障来源及完整运行验收尚缺；[FTM SDK 报告释放修复与独立启用构建](2026-09-10-w08-ftm-sdk-report.md)已完成前置工作，[FTM 原生 Radio 会话/报告领取/退休屏障](2026-09-10-w08-ftm-radio.md)已接入，[原生 Session/报告存储/runtime 清理](2026-09-10-w08-ftm-session.md)已接入，[公开 FTM 参数/报告/Session/Future](2026-09-10-w08-ftm-public.md)已接入 Candidate；[responder offset/记录/重放](2026-09-10-w08-ftm-offset.md)已接入；[FTM 原生物理恢复](2026-09-10-w08-ftm-physical-recovery.md)已编码，[共享 runtime/公开 FTM recover](2026-09-10-w08-ftm-recovery-public.md)已接入可信健康 STARTED 来源，已故障/不可读来源和完整运行验收仍待完成；TWT/roaming/enterprise/WAPI/WPS/DPP/SmartConfig/NAN/Mesh 未完 | 逐模块 typed options/events/results、timeout/ownership、target 编译 gate；草案中的 object 仍 contract-pending |
| W-09 | 新模块所需 active/retired/control-reserve 预算和可诊断关闭 | 复用现有资源约束；已有 CSI 单池限制不能冒充多代预算完成 |
| 诊断与交付 | wifi.diagnostics、W-10 集成扩展、W-11 全矩阵/RF、W-12 最终契约冻结 | 现有 status.radio 仅覆盖部分诊断；代码、构建、运行验收分别记账 |

ESP-NOW 已有 API 和其 Radio 接入不等于新增 Raw TX，已有 BLE 也不等于 03 的新
BLE 生命周期/拆分完成。没有因为暂未实现就把高级目标改成 target-unsupported。

deauthClient 已将 MAC 查 AID 与定向断开放入同一 Wi-Fi task 命令，固定 SDK 的
任务内直接执行路径与验证边界见[客户端查询记录](2026-09-08-w02-client-queries.md)。
没有使用全局断开或 AP 重启；框架 mutex 与无线端关联任务的保护范围分别记录。

## 本轮继续实现：扫描时间契约

正式 v1 用 mode/activeMinMs/activeMaxMs/passiveMs 替换 passive/dwellMs，不保留
旧参数别名。所有仓库内 Wi-Fi CSI 扫描调用与 API 示例同步更新；不自动改写设备
workspace。跨模式参数、未知 mode、NUL 后缀、非整数与越界在 SDK 操作前拒绝。

| 字段 | 范围/默认 | 适用模式 |
| --- | --- | --- |
| mode | active / passive，默认 active | 所有扫描 |
| activeMinMs | 整数 0–1500，默认 0，不得大于 activeMaxMs | active |
| activeMaxMs | 整数 1–1500，默认 120 | active |
| passiveMs | 整数 1–1500，默认 360 | passive |
| channel | 数字单信道，或 all；省略为 all | 所有扫描 |

时间为每信道时长；timeoutMs 仍为整个操作 deadline。默认值来自固定 SDK
esp_wifi_types_generic.h 的 WIFI_ACTIVE_SCAN_* / WIFI_PASSIVE_SCAN_DEFAULT_TIME，
explicit max/passive 0 不作为“默认”的别名。capture 显式保存默认值，不改变
driver 全局扫描配置。主信道停留与原生 filter storage 沿用前批实现。

字符串 enum 共用比较 helper 改为长度与内容同时匹配；因此 connect 的 enum
字段同样拒绝 NUL 后缀。该严格输入变化与 scan capture GC/OOM、min/max 边界、
mode 冲突、真实 RF 时间与原生取消竞争一起登记为阶段测试 not-run。

## 后续顺序与证据边界

扫描列表与结果字段已接入，见[当前边界](2026-09-08-w02-scan-records.md)。MAC/唤醒锁/能力发现也已接入，见[实际契约与待验证项](2026-09-08-w02-mac-wake-capabilities.md)。后续已新增 [watch](2026-09-08-w02-watch.md) 与 [Station 参数](2026-09-08-w02-station-options.md)。后续已接入 [AP 参数](2026-09-08-w02-ap-options.md)。已接入[连接结果快照](2026-09-08-w02-connect-result.md)。已实现[原生配置事务核心](2026-09-08-w02-config-transaction.md)并接入 startAP；已接入[生命周期交接](2026-09-08-w02-lifecycle-handoff.md)，已接入[helper 协调基础与失败处理](2026-09-08-w02-helper-coordination.md)；已接入[netif 事件串行化](2026-09-08-w02-netif-retirement.md)；[SDK 延迟 IP timer 地址复用](2026-09-08-w02-netif-timer.md)已有构建修补、待运行验证；已接入[AP/STA 中央 runtime 清理](2026-09-08-w02-central-cleanup.md)；已接入[Radio 启停事件边界](2026-09-08-w02-radio-event-boundary.md)；已接入[共用接口配置执行器](2026-09-08-w02-configuration-executor.md)及公共 stop 的中央清理转交；[完整字段映射/生成及 5 GHz RSSI 偏好](2026-09-08-w02-config-schema.md)已接入；[Station PHY/SAE-PK capture](2026-09-08-w02-station-phy-security.md)已接入；[AP 扩展 capture/读回](2026-09-08-w02-ap-extensions.md)已接入；[内部 raw capture](2026-09-08-w02-driver-capture.md)已加入 ByteSource/TU/PMF 解析与生成字段清单；[停机复合控制事务](2026-09-08-w02-config-controls.md)已补国家/协议/带宽/省电快照与回滚，[启动后 TX power](2026-09-08-w02-start-controls.md)已加入读回/恢复及 owner 发布前隔离；[正常 AP/APSTA 统一 stop](2026-09-08-w02-unified-stop.md)已接入三 owner 退休及 stop-only 重试；[内部授权 Station 断连](2026-09-08-w02-config-disconnect.md)已接入 token/epoch/fence 与失败后缀；[外层 capture](2026-09-08-w02-outer-capture.md)已覆盖复合 controls、缺省标记与严格 NUL 校验；[缺省/授权与捕获执行交接](2026-09-08-w02-config-selection.md)已接入原子 selection admission、运行 AP 显式停机权限与 Station 严格读回；[公开 configure](2026-09-08-w02-public-configure.md)已接入绑定/类型/本次错误记录、storage 状态和能力发现；完整高级认证与不同 AP 配置的 live activation 仍待完成；[保留 Station 的 AP 关闭原生核心](2026-09-08-w02-ap-stop-native.md)已编码，[公开 stopAP 的 helper/netif/runtime 接入](2026-09-08-w02-ap-stop-public.md)已编码，采用同 token 的部分关闭与断连排空后的整体失败接管；事件隔离与 Station 连续性待运行验收，[客户端 IP 查询](2026-09-08-w02-client-ip.md)已追加 includeIp 输入、按 MAC 的本地 DHCP 查询及缺失 null 契约，[共享 AP 重开](2026-09-08-w02-ap-reopen.md)已接入已存配置匹配、AP_START 屏障、实际信道与同 token 失败清理，[start options](2026-09-08-w02-start-options.md)已接入 mode/storage 缺省恢复、运行幂等与 AP 广播前校验，[stop timeout](2026-09-08-w02-stop-timeout.md) 已接入共享等待预算和超时后缀保留，[AP 状态](2026-09-08-w02-ap-status.md) 已接入非秘密字段、精确 binary SSID、实际信道和关闭/读取失败诊断，[stopAP timeout](2026-09-08-w02-stop-ap-timeout.md) 已复用共享等待预算，并修正 AP 启动结果/当前状态的同名类型冲突，已开始 [W-03 有界 RX header parser](2026-09-08-w03-rx-header.md)，已补 [target span 与原生过滤](2026-09-08-w03-rx-target.md)，已补 [registry 分发保留与关闭](2026-09-08-w03-rx-registry.md)，已补 [driver 事务、共享 enable 和 CSI 清理](2026-09-08-w03-promiscuous-driver.md)，已补 [RX 需求并集与 Radio 准入/激活/排空事务](2026-09-08-w03-rx-radio.md)，已补 [Monitor 原生帧池与引用生命周期](2026-09-08-w03-monitor-resources.md)，已补 [EventQueue bridge 与上下文寿命](2026-09-09-w03-monitor-queue.md)，已补 [capture Radio 生命周期](2026-09-09-w03-monitor-capture.md)，已补 [Session owner/reaper 与 runtime 边界](2026-09-09-w03-monitor-session.md)，已补 [原子参数捕获](2026-09-09-w03-monitor-options.md)，已接入 [公开单帧 Monitor 与原始字节 Source](2026-09-09-w03-monitor-public.md)，已接入 [停止后的完整配置替换](2026-09-09-w03-monitor-configure.md)，已接入 [Batch 接收与原生所有权](2026-09-09-w03-monitor-batch.md)，已补 [统一 wire envelope 基础](2026-09-09-w06-rx-wire-layout.md)和 [256-byte metadata encoder](2026-09-09-w06-rx-wire-metadata.md)，已补 [Monitor 快照 adapter](2026-09-09-w06-monitor-wire-snapshot.md)，已接入 [CSI Batch wire/Host parser 与 callback-time](2026-09-09-w06-csi-wire.md)；已接入 [Monitor Batch wire Source 与 Host JSONL](2026-09-09-w06-monitor-source.md)；已接入 [CSI 单帧统一 Source 与参数关闭边界](2026-09-09-w05-csi-frame-source.md)；已接入 [Monitor PCAPNG/显式时钟锚点](2026-09-09-w06-monitor-pcapng.md)；correlated packet、完整 PHY/time 及导出运行验收仍待完成。
按用户安排，Wi-Fi API 完成后统一阶段测试，相关测试完成后再进入 BLE。Wi-Fi 完成后执行实机功能测试；长时间 soak 放到 BLE API 也完成后。
原 W-04A/B 测试门槛按最新顺序安排：先完成 A 的 completion 隔离实现，再编写
B；集中阶段测试先验证 A，再验证 B。全部新模块保留实际 Candidate/待验证状态，
不把下层“已编码”写成竞争验收通过，也不因为后置测试删掉相关验收要求。

本轮仅执行必要 C5 编译、MQuickJS 语法及生成物一致性检查；新功能的 Host/
故障注入/硬件测试均未执行。未刷写、串口操作、提交、推送或更新父仓库 gitlink。

历史扫描批次结果：C5 编译通过，MQuickJS syntax 59 sources / 47 snippets 通过，
API manifest 43 classes / 390 functions、feature 文档 27 项与 recorded SDK map
结构一致性检查通过。编译/语法日志为 `build/w02-scan-timing-c5-build.txt`、
`build/w02-scan-timing-check-js.txt`；hash 与实际验证范围见
`build/w02-scan-timing-evidence.json`，不覆盖全套测试或其他 target。

后续已接入 setCountry/setChannel，当前 owner/读回/CSA 限制及未验证项见
[Radio 控制增量](2026-09-08-w02-radio-controls.md)。

停机恢复增量：[事件 mask](2026-09-09-w07-stopped-mask.md) 在固定 SDK C3/S3/C5 保留 STOP 观察，restart 捕获最新 mask；失败回滚仍受故障准入约束。实现为 Candidate，运行测试 not-run，其他恢复来源与 W-08 新操作 API 仍待完成。

FTM/Action 共享恢复已接入后，源码审查确认 [runtime teardown 的先后依赖](2026-09-10-w08-ftm-recovery-public.md)：Future/原 owner 排空门槛可能挡住已准入恢复的中央清理。[销毁顺序修复](2026-09-10-w08-recovery-teardown.md)已接入，新增真实 core/Future/cleanup 调度用例仅做 AST 检查；完整运行与 runtime restart 验收仍待 Wi-Fi 阶段测试。

[Raw TX 原生物理恢复](2026-09-10-w04-raw-recovery-native.md)已编码：精确 owner、STOP、callback/SDK task 屏障、deinit 和原 owner 退休前保留 generation。[公共 recover、临时速率前值 checkpoint/重放](2026-09-10-w04-raw-recovery-public.md)已接入共享 runtime/Future 与销毁清理；完整故障来源仍待实现，运行验收未执行。

[STA/AP 共用启动前速率事务](2026-09-10-w04-rate-transaction.md)已接入现有 Radio 借用/恢复路径；AP 的配置验证、helper 生命周期和公开临时 rate 已由后续 [AP/APSTA 接入](2026-09-10-w04-apsta-rate.md)完成编码，运行验收未执行。

[AP 临时速率原生生命周期](2026-09-10-w04-ap-rate-native.md)已编码；runtime/AP helper、公开 Session 和停止态 APSTA 协调已由后续 [AP/APSTA 接入](2026-09-10-w04-apsta-rate.md)完成编码，运行验收未执行。

[AP-only 临时速率公开 Session 与 helper 接入](2026-09-10-w04-ap-rate-session.md)已编码，含正常关闭、取消保留与物理恢复后的原 owner 退休；[停止态 APSTA](2026-09-10-w04-apsta-rate.md)随后已接入；故障来源与运行验收继续保留。

[GI/LTF/DCM 元数据](2026-09-10-w06-guard-interval.md)已连接 CSI/Monitor JS、
共同 wire writer 与 Host parser，包含 SU/ER-SU 特殊 DCM/STBC 编码修正。
完整 PHY（NSS/RU/每用户/puncturing/legacy bitrate）、correlated packet、
时钟/RF/导出资格及其余 Wi-Fi API 继续保留；不将字段编码等同 W-06 完成。

[Roaming 能力/BTM Query](2026-09-10-w08-roaming-btm.md)已接入 Candidate，
含 supplicant 原生任务串行化、Radio/helper 准入、16 项 typed candidate、提交前
输入/结果分配及 SDK 原始错误。Neighbor Report 专属 request/watch、完整漫游
事件与生命周期/RF 验证继续保留，不能据此把 W-08 roaming 标记为完整实现。

[Neighbor Report SDK 前置修复](2026-09-10-w08-rrm-sdk.md)已编码并通过 C5
启用/关闭构建：修复 token 255 比较和 timeout 注册失败遗留 callback，加入精确
callback/context 取消与退休查询。公开 Request/Future、identity registry、Radio
长期保护和 runtime cleanup 尚未接入；新增回归 fixture 仅 AST，运行 not-run。

后续 [Neighbor Report 原生请求](2026-09-10-w08-rrm-request.md)已接入单活动
registry、boot identity、报告容量/引用、Radio 长期 reservation、SDK 精确退休
和 runtime 排空。公开 Request/Future、IE 解析、观察排序和 RF token 回绕/reset
歧义仍待完成；C5 启用/关闭构建通过，新增竞争 fixture 未执行。

[Neighbor Report 公开 Request/Future](2026-09-10-w08-neighbor-public.md)已接入
Candidate：requestNeighborReport/status、Request status/receive/cancel/close、
共享 IE decoder、Future waiter 与后续观察发布已编码。专属 roaming watch、
RF token 回绕/reset 歧义、完整 Future/GC/竞争和实机验收仍保留。

[Roaming watch](2026-09-10-w08-roaming-watch.md) 已接入 Candidate：复用 Wi-Fi
broker、共享订阅/Neighbor pool、严格限定事件和 typed payload；RRM 关闭时不接受
Neighbor Report 选择。完整漫游/RF token 隔离与其余高级模块仍待完成，运行测试
仍集中后置。本项替代上文各历史批次“专属 watch 未接入”的状态。

[Enterprise 原生凭据 profile](2026-09-10-w08-enterprise-profile.md) 已编码，
具有独立 native 副本、引用、预算和整个分配块清零；JS/Radio/SDK 安装与退休、
SDK 内部秘密副本清零及公开企业认证 API 仍待完成。未注册新的占位接口。

[Enterprise SDK 秘密副本修补](2026-09-10-w08-eap-secrets.md)已编码并纳入
C5 构建静态库，覆盖 global/SM 副本清零、setter OOM 保留与 PAC 旧分配回收。
SDK 可靠退休/事务、Radio 与 JS/public enterprise 接入及阶段运行验证继续保留。

[Enterprise worker 退休保护](2026-09-10-w08-eap-lifecycle.md)已编码并通过
C5 构建，新增独立退出确认、失败保留、初始化清理及原生资源快照。C5 driver
在 callback 前先改变状态字节，完整安装/失败回滚/driver ownership、Radio/JS
与公开 enterprise API 仍待完成；阶段运行测试继续后置。

[Enterprise EAP control/失败清理](2026-09-10-w08-eap-control.md)已编码：Wi-Fi
task 串行启停、API lock 失败处理、driver/callback/method 步骤、后缀清理重试、
意外 callback 接管的有界隔离及 deattach 错误传播。完整 profile 安装/回滚、
Radio owner/runtime、JS/public enterprise 和其余 Wi-Fi 范围仍待完成；新增
生产路径 fixture 仅 AST，C5 启用/关闭构建不替代运行或 RF 证明。

[Enterprise profile 安装/借用退休](2026-09-10-w08-eap-install.md)已编码：原生
独立引用和精确 identity、SDK 全字段安装/enable、失败清理与保留、旧 clear 拒绝、
PEM/DER 长度及显式 FAST provisioning。Radio owner/runtime、JS/public enterprise
仍待完成；动态用例继续后置，构建不作为认证通过证据。

[Enterprise Radio/runtime 清理](2026-09-10-w08-eap-radio.md)已编码：独立借用
记录固定 helper leases，SDK 借用未退休时阻止生命周期变更；正常 scan/connect
保留操作槽，退出沿现有断连/复用屏障清理。公开 enterprise 配置/启停策略、
JS/Future/GC、完整共存与其余 Wi-Fi 范围仍待完成；运行测试继续后置。

[Enterprise JS 凭据捕获](2026-09-10-w08-eap-capture.md)已编码：严格方法/字段、
GC roots、ByteView 租用和同一预算内 profile 构造/校验/失败清零。公开配置 owner、
重入保护、启停/Future、stop/restart 集成与注册仍待完成；真实 VM fixture 仅 AST，
运行测试继续后置。

[Enterprise 配置 owner/修订号](2026-09-10-w08-eap-config.md)已编码并接入
runtime 开关：过期捕获不能覆盖新配置，控制按完整 token 收尾，SDK 借用未退休
时保留配置，退出后释放秘密。公开入口/Future、stop/restart 与完整共存仍待完成；
新增配置竞争 fixture 仅 AST，运行测试继续后置。

[Enterprise 公共 API/Future](2026-09-10-w08-eap-public.md) 已接入 Candidate：
capabilities/configure/status 与 enable/disable/clear、精确修订号收据、忙态非阻塞
观察、worker 原生引用保留和失败清理重试。正式 v1 类型/注册/文档已同步。
此项替代历史批次的“公开入口待实现”；stop/restart、完整共存/认证仍待完成。
运行测试继续后置；TWT、其余 roaming、WAPI/WPS/DPP/SmartConfig/NAN/Mesh、
完整恢复/预算/CSI 和诊断等清单范围保持不变。

[Enterprise stop 事务](2026-09-10-w08-eap-stop.md) 已接入现有公开 stop：精确
lifecycle 先准入，SDK 退休后释放 helper leases，配置保留；失败保持控制引用，
SDK/AP-release 成功前缀不会被后续 netif/driver 重试重复，runtime 可接管收尾。
Station 已连接时仍拒绝 stop。完整 restart 恢复策略与共存仍待完成，阶段运行
和实机测试继续后置；其余清单范围不变。

[TWT SDK/输入捕获](2026-09-10-w08-twt-capture.md) 已编码，含 iTWT/bTWT 严格
参数、GC roots、64-bit 时间换算、10 ms 最小睡眠与 SDK 最大睡眠约束；记录
flow ID 改写、无 cookie 路径和 suspend/resume 区别。该捕获批次未注册公开 TWT；
后续 probe 公开接入见当前摘要，完整 Agreement、共存和阶段验证继续待完成。

[iTWT teardown TX 身份与 PM 引用](2026-09-10-w08-twt-teardown-tx.md)已连接原生 callback 18、
现有 TX 账本及两个精确 SDK PM 调用点；output 返回和 recycler 均携带身份，
缓冲地址复用不作为旧请求完成。完整 timer/native/event 联合退休、信息定时器、
Radio/Future/Agreement 和物理恢复仍待完成；本批新增 fixture 仅 AST，运行未执行。

[iTWT information timer 参数与身份](2026-09-10-w08-twt-information-timer.md)已接入现有 OSI/native
分发：异步队列携带数字，参数在取消或原生消费时精确释放，stop/delete 失败
保留后缀；单 flow/all-flow 核对不同请求集合。信息 TX 身份、完整联合退休、
Radio/Future/Agreement 与阶段运行仍待完成，新增 fixture 仅 AST。

[单 flow teardown 本地联合回收](2026-09-10-w08-twt-teardown-retire.md)已扩展现有执行器：
核对 TX/setup/information/teardown 四个 revision，沿用 TASK/native/event 顺序，
结果先释放、TX 后释放并保留失败后缀。共享 information timer 不取消其他 flow。
公开 Radio/Agreement caller、信息 TX 身份、RF/bTWT 与物理恢复继续待完成。

[information 提交失败/观察队列](2026-09-10-w08-twt-information-submit.md)已编码无 output 的 PM 引用
回滚（archive-only，待 suspend caller）；event 31 的归一化零等待观察已链接，
避免观察队列饱和阻塞原生清理。
信息 TX 身份、结果/Future 关联、公开 Agreement suspend/恢复与 RF 仍待完成。


</details>
