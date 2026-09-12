# Wi-Fi NAN

`wifi.nan` 提供 NAN discovery 的 Session、服务发布/订阅与发现事件。启用 Wi-Fi，且
`CONFIG_ESP_WIFI_NAN_SYNC_ENABLE` 或 `CONFIG_ESP_WIFI_NAN_USD_ENABLE` 至少一个启用时注册，不要求 IPv4。
固定 SDK 的 C3/S3/C5 三目标中，仅 C5 声明 `SOC_WIFI_NAN_SUPPORT` 并提供
NAN-Sync driver 实现；C3/S3 不能通过手动设置宏获得该能力。
USD 可独立启用，不要求目标支持 NAN-Sync。仅启用 USD 时不注册 DataPath 类及
`requestDataPath/receiveDataPath` 方法；能力快照中对应能力为 false，上限为 0。
当前为 **Candidate**；follow-up 消息收发、Vendor 属性、开放及 NCS-SK-128 安全
数据连接已接通。启用 NAN USD 时可通过 `open({mode:"unsynchronized"})` 使用非同步发现；显式 PIN 配对、bootstrap 请求/接收和缓存重验证已接通。接收普通对端 follow-up 使用服务事件队列。

## 接口

| 方法 | 结果与作用 |
| --- | --- |
| `wifi.nan.capabilities()` | 返回 `wifi-nan/1`、Candidate、synchronized/unsynchronized 能力、独占 Radio 及 Session 上限 |
| `wifi.nan.status()` | 活动 Session 快照、存活句柄数、worker 数和 runtime 关闭状态 |
| `session.getServiceInfo(service)` | Sync 原生服务的 ID/name/peerCount 快照；未找到返回 null |
| `session.getPeerInfo(peerMac, service?)` | Sync 原生对端及服务匹配的 NDP 元数据；未找到返回 null |
| `session.getPeerRecords(service)` | 一次加锁读取服务信息与最多 15 个对端；未找到服务返回 null |
| `wifi.nan.open(options?)` | 完整构造 `WiFiNanSession` 后激活后台启动；不等待 RF 启动 |
| `session.status()` | 新的 `WiFiNanStatus` 快照；关闭后仍可查询 |
| `session.ready(options?)` | Future-capable；等待 native ready，返回状态快照 |
| `session.close(options?)` | Future-capable；请求关闭并等待原生清理与 Radio 释放，返回 `undefined` |
| `session.cancel()` | 立即请求关闭，不等待原生清理 |
| `session.publish(options)` / `session.subscribe(options)` | 在 ready Session 中构造 `WiFiNanService`，然后后台提交原生服务 |
| `service.status()` / `service.ready(options?)` | 当前快照 / Future-capable 等待创建完成 |
| `service.close(options?)` / `service.cancel()` | 等待实际取消与回收 / 立即请求取消 |
| `service.requestDataPath(options)` | 从启用数据连接的订阅服务发起请求，返回独立连接句柄 |
| `service.receiveDataPath(options?)` | 从启用数据连接的发布服务取得入站连接句柄；等待超时返回 null |
| `path.ready(options?)` / `path.respond(options)` | 等待建立 / 显式接受或拒绝入站请求 |
| `path.status()` / `path.close(options?)` / `path.cancel()` | 状态 / 等待关闭 / 请求关闭 |
| `service.events` | 创建时即建立的 `EventQueue<WiFiNanServiceEvent>`；使用已有 `receive/stats/close` |
| `service.send(options)` | Future-capable；向已发现的指定对端发送 follow-up，返回 `WiFiNanSendResult` |

`WiFiNanSession` 不可直接构造。每次原生 Session 使用 boot 内不复用的 identity；
同时至多一个活动 Session、四个存活 Session。已关闭但仍由 JS/Future 引用的句柄
计入上限；最后引用释放后归还其共享 control 预算。超过上限或 identity 耗尽会失败。

## Sync 原生缓存查询

上述三个查询为同步读取，不发起扫描、连接、发现请求或 Future。要求当前 Session
ready 且未请求关闭；Radio 精确 token 校验、原生服务变更与父 Session 关闭串行。
查询过程中开始的关闭可能排在快照之后；已经调用 `cancel()` 的 Session 拒绝新查询。
运行时销毁、启动失败或已退休的 Session 同样拒绝查询。

`service` 为整数 ID 1..255 或非空服务名（最多 255 UTF-8 bytes，禁止 NUL）；
ID 选择的是**当前原生缓存记录**，不是 `WiFiNanService` 的 boot-unique identity，
不能授权修改旧句柄或证明服务仍存活。`peerMac` 为恰好六个字节的 ByteSource，
必须是非零单播 NMI。`getPeerInfo` 可省略 `service`，此时返回 SDK 原生服务槽和
对端链表顺序中的第一个匹配；有多个匹配时用服务选择器或列表进一步区分。

`getServiceInfo` 返回 `{serviceId, name, peerCount}`。
`getPeerRecords` 额外返回 `peers` 数组，列表和数量在同一原生数据锁中捕获。
现有空服务返回 `peerCount:0, peers:[]`；服务不存在返回 null。
`getPeerInfo` 和 `peers` 中的每项为：

| 字段 | 含义 |
| --- | --- |
| `serviceId` / `peerServiceId` | 本机和对端原生服务 ID |
| `peerType` | `"publish"` 或 `"subscribe"` |
| `peerMac` | 对端 NMI，六字节普通数组 |
| `ndpId` | 服务归属匹配的原生 NDL 记录 ID，否则 null；不是建立成功或 IP 可达的证明 |
| `peerDataMac` | 有对应 NDP 记录时的 NDI，六字节普通数组；否则 null |

原生 SDK 的单对端查询只按 NMI 寻找 NDL，列表查询在 NDL 属于另一服务时还可能
保留调用方旧的 NDP 字段。框架统一使用清零且核对服务归属的复制路径；订阅方再
核对现有受管理 NDP 账本，避免同一 publisher/NMI 在多个本机服务之间串用。
查询不输出密钥、凭据、原生指针或 SSI，也不消费发现事件。JS 分配/GC 失败不会
修改原生缓存；临时元数据使用有界栈空间，ByteSource 临时复制沿用共享资源预算。
原生链表超出 15 项、数量不一致或无效记录返回错误，不返回部分列表。

`capabilities().peerQueries` 表示是否编入 Sync 查询；对应
`maxPeerRecordsPerService` 为 15 或 0。仅 USD 构建不注册这些方法；Sync+USD
构建中的 USD Session 调用会以 `WIFI_NAN_FAILED` / `ESP_ERR_NOT_SUPPORTED`
拒绝，不用 Sync 空缓存冒充 USD 查询。其余原生失败使用现有 `WiFiNanError`，
保留 `operation`、`details.espCode/espName` 和 Session 状态；参数错误为 TypeError。

## 启动参数

`open()` 只接受下表字段的普通 options 对象；缺省或 `undefined` 使用默认值。
数字必须是范围内的有限整数，不隐式转换字符串、布尔值或小数。未知字段拒绝。

| 字段 | 单位、范围与默认值 |
| --- | --- |
| `channel` | SDK channel 字段，1..255，默认 6；实际 NAN 信道及监管限制由目标 driver 校验 |
| `masterPreference` | 0..255，默认 2 |
| `scanTimeSeconds` | 秒，0..255，默认 3 |
| `warmUpSeconds` | 秒，0..65535，默认 5 |
| `randomizeMac` | boolean，默认 true |
| `groupManagementProtection` | boolean，默认 false；需要编译 NAN security，Session 级 IGTK/BIGTK 策略，同时为安全服务声明组数据保护支持 |
| `timeoutMs` | 激活起的启动截止时间，1..120000 ms，默认 10000 |

启动要求健康、已停止或未初始化的 Radio，且没有 Station/AP、ESP-NOW、CSI、
Monitor、wake lock 或其他原生 operation owner。NAN 不隐式关闭这些资源。
`open()` 的参数和 JS 对象构造在激活前完成；worker 再串行核对 Radio 条件。
原生准入或启动失败通过 `ready()` 错误及状态快照报告。

NAN 使用临时 RAM storage。此 API 不开放擦除或持久化 NAN 凭据的选项。
关闭先封闭新的 SD/NDP buffer 分配和服务回调接纳，等待已进入的 service-match、
replied、receive、NDP 应用回调、原生 SD 服务/NAF 收发、空数据帧完成及定时器处理函数退出，再等待 STOP、实际 buffer 回收、定时器清理、SDK reset、
netif 退休、observer 退出，恢复进入前的停机
mode/storage，最后释放精确的 Radio lease。清理失败保留原错误、存储和未完成
后缀；已经受理的 STOP 不会因为等待超时而重复提交。

`ready` 表示同步 discovery 已完成 native START、默认 handler/事件屏障及 netif
up 检查。它不表示已发现对端，也不表示 datapath 已连接或 IPv6 已可通信。
`warmUpSeconds` 是 SDK 协议配置，不延长 `open.timeoutMs`。

## USD 非同步发现

启用 `CONFIG_ESP_WIFI_NAN_USD_ENABLE` 时支持
`open({mode:"unsynchronized", timeoutMs:10000})`，无需同时启用 NAN-Sync。
启用 NAN-Sync 时 `mode` 默认 `"synchronized"`，仅启用 USD 时默认 `"unsynchronized"`；
显式请求未编译的模式会在原生分配或 Radio 修改前拒绝。
`capabilities().synchronized/unsynchronized` 报告构建能力，
Session status 的 `mode` 报告实际模式。USD 启动只接受 `mode` 与 `timeoutMs`，
使用 Station MAC、独占 Radio 和临时 RAM storage，不创建 NAN netif。
`wifi.nan.status().tx` 仅描述同步模式的 buffer pool；活动 USD Session 或仅启用
USD 的构建返回 null。USD 的发送结果和存储退休由自身操作状态及共享传输账本记录。

USD 服务沿用 `publish/subscribe/ready/events/send/close`，另支持：

| 字段 | 语义 |
| --- | --- |
| `ttlSeconds` | 0..2147483647 秒，默认 60；0 表示发布一次或订阅至首次匹配 |
| `channel` | 默认 6；实际目标及监管限制仍由 driver 校验 |
| `channels` | 1..42 个不重复的信道，5 GHz 需要目标支持；省略时使用默认信道 |
| `dwell` | 仅发布：`nMin/nMax/mMin/mMax`，1..255，默认最小 5/最大 10；按 100 TU 计量（1 TU = 1024 微秒） |

发布的 unsolicited/solicited、订阅的 active/passive 均传递到原生引擎。
USD 不支持此 SDK 的 matching filter、singleEvent、Vendor 或 NDP/security/pairing；
相关非空/启用参数会拒绝，不会静默退化为开放连接。上述 USD 字段在同步模式下拒绝。

发送 Future 依据本次操作的原生完成时间结束，队列饱和不会丢失控制完成；完成后仍
保留正在发送或等待原 buffer 回收的原生存储。至多一份后续帧副本等待旧 buffer 退休，
使用既有无线 control 预算。服务停止需退休属于它的发送记录，其他服务不被取消。
Session 关闭先封闭引擎，再排空 Action/ROC、实际 buffer 和回调/定时器，最后 STOP
与恢复原 mode/storage。已完成的清理步骤不重做，失败后缀保留供继续等待。
新增实现为 Candidate；本批尚未进行动态或 RF 验收。

## 等待、关闭和 GC

`ready()` 与 `close()` 的 options 只有 `timeoutMs`，范围 1..120000 ms，默认
10000。直接调用等待结果；异步组合使用已有的 `Future.call()`。

| 操作 | 超时或取消的实际影响 |
| --- | --- |
| `open.timeoutMs` 到期 | Session 标记 `timedOut`，停止交付 ready，开始原生关闭；迟到 START 成功仍须关闭 |
| `ready()` 等待到期/其 Future 被取消 | 只结束本次等待；不修改 Session 的启动截止时间，也不请求停止 discovery |
| `close()` 等待到期/已启动的 Future 被取消 | 等待结束，原生清理继续；用 `status()` 检查或再次 `close()` 等待 |
| `close` Future 在 start 前被取消 | 不请求原生关闭 |
| `session.cancel()` | 请求原生关闭，后续 `ready()` 失败；可接着 `close()` 等待退休 |
| 最后一个 JS/Future 引用释放 | registry 请求关闭并保留 worker/observer 所需原生存储 |

Future 结束可释放 JS roots，仍被原生引用的 Session 和 Radio binding 不会提前
释放。`ready()` 结果转换 OOM 不消费 ready 状态；持有 Session 可重试等待。
关闭成功可重复调用。runtime teardown 同样等待原生退休；不能将仍有未知 driver
故障或清理失败的 runtime restart 宣称为恢复手段。

## 发布、订阅与事件

每个 Session 最多同时保留两个原生服务；全 runtime 最多八个服务句柄，包括关闭
后仍被 JS、Future 或事件队列引用的句柄。服务的 framework identity 在 boot 内不复用。
SDK 的 `serviceId` 只用于诊断和协议字段，不能替代 framework identity。

| 创建字段 | 契约 |
| --- | --- |
| `name` | 必填非空字符串，最多 255 UTF-8 bytes，无嵌入 NUL |
| `type` | publish：默认 `unsolicited`，也可 `solicited`；subscribe：默认 `active`，也可 `passive` |
| `matchingFilter` | SDK 逗号分隔过滤字符串，最多 255 UTF-8 bytes，默认空，无嵌入 NUL |
| `singleEvent` | boolean，默认 false；采用 SDK 单次 match/replied 语义 |
| `ssi` | 可选 ByteSource，最多 512 bytes；启动前复制，调用后可释放输入 |
| `timeoutMs` | 创建截止时间，1..120000 ms，默认 10000；到期请求取消，迟到成功仍需回收 |
| `dataPath` | boolean，默认 false；启用数据连接，入站请求必须在 10 秒内显式应答 |
| `security` | 可选 NCS-SK-128 凭据对象；与 pairing 均省略时为开放服务，启用安全后不降级到开放连接 |
| `vendor` | 可选 `{oui, body}`；OUI 必须为 3 bytes，body 为 0..255 bytes，均为 ByteSource |
| `queueCapacity` | 1..16，默认 4；满时丢弃最新观察，服务控制和关闭不依赖入队成功 |

默认服务仅用于 discovery，不授予数据通路接纳权限。只有显式 `dataPath:true`
才允许请求或接收入站连接。未启用的活动服务收到 NDP 请求会发送拒绝，拒绝提交
失败会保留原错误并关闭父 Session；不会退回自动接受。配对接受下述显式 PIN 与缓存重验证契约；USD 不接受同步模式字段。
关闭期间已冻结的服务不再处理新的 NDP indication；对端不能将无响应视作接纳。

驱动创建成功后先在 Wi-Fi 任务内绑定 SDK host service ID 和框架 identity，再返回
请求 worker。事件队列在激活前创建，因此 `publish/subscribe` 返回前到达的事件也
可保留。`service.events.receive()` 返回 `match`、`replied` 或 `message`：包含
framework identity、单服务 sequence、双方 service ID、6 字节 `peerMac` 和复制的
`ssi` 数组；接收 SSI 最多 2048 bytes。match 另有 SSI version、datapath/security/FSD/
GAS/NDPE 字段，其他事件对应字段为 null。收到 match 不等于认证或数据通路成功。
`events.stats().dropped` 记录队列丢弃；`service.status().droppedBeforeQueue` 记录进入
队列前、已能定位所属服务的 SSI 长度/并发/观察存储拒绝。无法定位服务的截断事件头
不会计入某个服务。关闭事件队列只停止观察，不取消服务。

服务 ready/close 的等待参数、Future 取消规则与 Session 相同。ready 超时只结束等待，
创建截止时间仍独立生效。服务取消成功后保留原生 ID 和记录，直到完整回调、SDK
取消及实际 TX recycler 返回均完成；已完成的取消不重发。此期间新服务激活拒绝，
避免原生自动分配到旧 ID。创建失败无法证明局部回收时关闭父 Session，保留原生资源
到整体 STOP/回收完成。父 Session 关闭同时关闭其服务；服务 JS finalizer 请求取消。
已退休的服务句柄和事件队列仅保留自身快照，不再延长父 Session 的 Radio 所有权。

## 服务安全与 Vendor 属性

`security.credentials` 必须是含 1..4 项的数组；同时启用 pairing 时最多 3 项，原生第四槽留给 PASN cipher 声明。每项的 `cipher` 省略或为
`"ncs-sk-128"`，并且只提供 `passphrase` 或 `pmk` 之一：passphrase 是无嵌入 NUL
的 8..63 UTF-8 bytes 字符串；PMK 是恰好 32 bytes 的 ByteSource。空列表、其他
cipher、两个秘密同时提供、未知字段和非整数 byte 均在 driver 提交前拒绝。
固定 SDK 的 256-bit 枚举不是可用的加密实现，不作能力声明。

使用前检查 `wifi.nan.capabilities().security` 与 `securityReason`。编译需要
`CONFIG_ESP_WIFI_NAN_SECURITY` 及其 SDK 依赖；未启用时服务创建明确失败。
安全服务的 DataPath 必须保留 `confirmRequired:true`，不会通过关闭确认绕过完整
握手。协议认证、MIC 与密钥安装沿用 SDK；`ready` 在其成功完成后才交付。

`security.groupDataProtection` 默认 false，声明可协商的组数据保护能力，是否安装
组密钥还取决于对端能力。Session 的 `groupManagementProtection:true` 会为安全
服务同时声明组管理与组数据保护。status 的这两个字段报告声明策略，不能当成
某个对端已经协商组密钥的证明。

参数复制到共享 control 预算，调用后可释放或修改输入。服务关闭等待原生退休，
随后清零框架凭据，即使关闭的 JS 句柄仍存活；清理失败保留原生所需存储。异常
构造和最后引用释放也清零复制材料。status 只输出是否安全、凭据数量、组保护策略
和 Vendor body 长度；不输出 passphrase、PMK、派生密钥。SDK 构建副本关闭二进制
安全材料日志，MAC/加密派生失败不能进入成功结果。
入站 PMKID 未匹配或 M1 缺失时明确拒绝；每次完整 NAF 接收前后清除暂存解析材料。
组管理密钥生成/安装失败阻止 Session ready；已协商组密钥缺失、封装失败或安装
失败阻止连接 ready，并保留错误进入关闭流程，不退回只有单播保护的成功结果。

`vendor` 可用于 publish、subscribe 和 同步模式的 `service.send()`，添加到相应发送帧；
OUI 与 body 在提交前复制，并随原生发送/服务 owner 保留到退休。不将 Vendor body
加入 status，也不承诺 SDK 未提供的 Vendor 接收事件。Vendor 属性独立于 SSI。

固定 SDK 已包含 NAN pairing/PASN 实现，不需要另一套 Wi-Fi Aware 组件。
显式 PIN 配对、bootstrap 交互和缓存重验证通过下述 Service/Pairing API 使用。
运行/RF 验收待执行，不暴露凭据持久化或擦除选项。`session.status().groupManagementProtection`
保留本 Session 的组管理保护策略，启动临时配置清零后仍适用于后续创建的服务。
USD 通过前述 mode 使用同一 Session/Service 接口，启动/关闭走独立原生分支；不支持本节的安全/Vendor 参数。

## 显式 PIN 配对

需要 Wi-Fi、NAN Sync、安全和 `CONFIG_ESP_WIFI_NAN_PAIRING`，当前为 Candidate。
`capabilities().pinPairing` 表示本契约是否已编译；关闭 pairing 时不注册
`preparePairing/requestPairing/receivePairing/pairingCredentials` 或 `WiFiNanPairing`。USD 不支持该配置。

发布与订阅均显式传入 `pairing: true`，启用 setup、缓存重验证和 RAM 内 NIK/NPK 缓存。
框架会声明 `securityRequired` 并追加原生 NCS-PK-PASN-128 cipher 描述；不需要虚构
静态密码或 PMK。可另提供最多三份 NCS-SK-128 静态凭据。`credentialCount` 仅统计
这些显式静态凭据，`pairingEnabled` 记录配对策略。已有缓存不会静默关闭 setup，
服务也不会自动接受认证帧。发布方声明 PIN display，订阅方声明 PIN keypad。

| 方法 | 行为 |
| --- | --- |
| `service.preparePairing({peerServiceId, peerMac, timeoutMs?})` | 返回未确认的独立句柄；订阅方为 initiator，发布方为 responder。要求服务 ready、精确发现对端 ID 和六字节非零单播 NMI |
| `pairing.confirm({accept:true, pin})` | 仅接受六位 ASCII 数字字符串，保留前导零；复制后排队提交，不等待配对完成 |
| `pairing.confirm({accept:false})` | 拒绝本次操作，禁止携带 PIN；不启动认证 |
| `pairing.status()` | 返回自身操作、原生状态及清理快照，不含 PIN、NIK、NPK 或 ND-PMK |
| `pairing.ready(options?)` | 支持 Future，等待协议配对和必要后续帧完成/回收，返回状态 |
| `pairing.close(options?)` | 支持 Future；成功表示本操作原生后缀及实际发送 buffer 退休 |
| `pairing.cancel()` | 请求关闭，立即返回 |

准备时的 `timeoutMs` 为整个确认和配对的截止时间，1..120000 ms，默认 30000。
无人确认时到期关闭，不使用默认 PIN。`confirm` 是一次性决定；完整参数验证后才
提交，重复确认或关闭后的确认失败。应用应在取得真实用户确认后调用 accept。
此低层入口不发送 bootstrap 协商帧：应用协调两端，先确认 responder，核对其
`nativeActive` 后再确认 initiator，两端使用用户确认的同一个 PIN。原生 responder
另有 10 秒 SDK 建立超时；整体 timeoutMs 不会延长该原生超时。

`ready/close` 的等待超时默认为 10000 ms，可在 1..120000 ms 内指定。取消或超时
只结束该次等待；`ready` 不撤销已经提交的确认，独立操作截止时间仍有效；已经开始的
`close` 继续清理。需要终止认证时调用 cancel/close。等待结束可释放 JS roots，
仍被原生引用的状态由 Session registry 持有。

同时最多一个活动配对、八个存活句柄，释放旧句柄不能影响后来操作。成功配对后
调用 close 归还 lane，再为该 peer 建立数据连接；成功的派生密钥缓存按服务保留，
服务关闭清除自身派生记录。配对失败或取消会清除本次已启动但未成功操作的派生缓存。
同一 peer 有未退休 NDP 时拒绝替换其配对密钥。

`authenticated`、`paired` 是原生里程碑；`trafficPending` 表示必要发送仍未完成，
不能把 `paired` 单独当作 `ready`。原生接收 NIK 后可能仍需回复自身 NIK；ready
会等待该回复的真实 TX 结果和回收，失败保留原错误。关闭服务先关闭自身配对；
父 Session 关闭撤销权限后继续物理 STOP，再统一排空发送，避免 STOP 前互相等待。
`nativeRetired` 单独只说明 PASN context；close 成功才证明本次完整清理。
全局 status 的 `pairingHandles/activePairings` 分别统计存活句柄与活动 lane。

竞争、GC/OOM、两端互通和 RF 验收后置，具体编译范围见实施记录。

### PIN bootstrap 请求与接收

`capabilities().pinBootstrap` 表示本流程已编译。双方服务均须启用 `pairing:true`。

| 方法 | 行为 |
| --- | --- |
| `subscriber.requestPairing({peerServiceId, peerMac, timeoutMs?})` | 返回未确认 Pairing 并排队发送 PIN bootstrap 请求；参数范围同 preparePairing，不接受 credentialId |
| `publisher.receivePairing(options?)` | 支持 Future，交付一个未确认 Pairing；等待超时返回 null，默认等待 10000 ms，范围 1..120000 ms |

发布方接收后，取得真实用户确认并调用 `confirm({accept:true,pin})`，原生 responder
启动后才发送接受响应。订阅方也须取得用户确认、输入相同 PIN；只有本地确认、对端
接受和请求帧回收均满足时才启动 initiator。`peerAccepted` 只表示协商响应，不能代替
用户确认、认证或 `ready`。无默认 PIN，不提供未经确认的 opportunistic 配对。

入站操作截止时间从收到请求起计算，固定 30000 ms；receive 的等待期限独立，领取
请求不会延长其寿命。无人确认时关闭；可发送拒绝响应时尽力发送，发送通道被占用或
父级关闭时可直接丢弃。发布方确认后仍受上述原生 10 秒建立超时约束，因此订阅方
应及时确认。显式拒绝、取消或超时不会使其他连接获得授权。

Session 只保留一个待领取请求，且与一个活动配对共用准入边界；忙时的新请求丢弃。
`maxPendingPairingRequests` 为 1；Service status 的 `pairingRequestPending` 和饱和计数
`pairingRequestsDropped` 可观察未交付和丢弃情况。接收不占用普通 events 队列容量，
该队列满不会阻止确认、关闭或原生完成。JS 结果构造失败、领取等待取消时释放领取
预约，尚未到期的原生请求可再次领取；成功构造结果后才转交句柄。

SDK 的 bootstrap 接收回调没有 operation cookie。每个 Service 最多记录八个不同的
`(peerServiceId, peerMac)`，在操作激活时占用；同一组合在该 Service 生命周期内不再
用于 bootstrap，新组合满额明确失败。`maxBootstrapPeersPerService` 为 8。关闭并
重新创建服务可开始新的服务生命周期，但这不证明无线网络中的旧帧已消失。NPBA
本身未经认证；最终仍须显式本地同意和新的 PASN 认证。收到 COMEBACK 响应会以
not-supported 结束本次操作；当前 SDK 回调没有足够信息恢复其 cookie/delay 流程。

Pairing status 追加 `bootstrap/incoming/delivered`、`bootstrapAttempted/bootstrapSent/
bootstrapTxPending`、`peerResponded/peerAccepted`。发送成功与 buffer 回收分别跟踪，
`ready` 等待协商帧和认证后续帧完成；`close` 等待本次实际回收。等待超时、迟到响应
和释放 JS 句柄均不能提前复用仍被原生引用的发送存储。

### 缓存凭据与重验证

`capabilities().cachedVerification` 表示该流程已编译；`maxCachedPairings` 为 2，
是整个 NAN Session 共用的缓存上限。关闭 pairing 时不注册 `pairingCredentials`。

- `service.pairingCredentials(options?)` 支持 Future，返回同服务 hash 的已完成且未到期
  凭据数组。每项仅有 `credentialId`、最后成功认证的 `peerMac`、设备单调时钟的
  `expiresAtUs`（无有限寿命时为 null），不返回 NIK/NPK/ND-PMK。数组为空表示没有
  可用凭据。等待超时抛出 timeout；取消或超时只结束本次读取等待。
- `preparePairing({peerServiceId, peerMac, credentialId, timeoutMs?})` 创建未确认的
  重验证操作。`credentialId` 必须来自本地缓存；MAC 可以是重新发现后的随机地址，
  原生阶段仍须通过所选 NIK 的 NIRA 和 NPK/PASN 校验。状态 `mode` 为
  `verification`；省略 credentialId 则是 `pin`。
- 重验证使用 `confirm({accept:true})`，禁止携带 PIN；PIN 模式仍要求六位数字字符串。
  两种模式均可显式拒绝，均不默认同意。应用先确认 responder 并观察 nativeActive，
  再确认 initiator；重验证继续使用手动协调的 preparePairing，本节 credentialId 不用于 PIN bootstrap。
- 缓存只在协议完成、实际 TX 成功和 buffer 回收后提交。中途失败、到期或发送失败
  不覆盖此前有效凭据。成功后 `ready()` 返回提交后的 credentialId；密钥刷新会换新
  ID，旧 ID 在原生提交前被拒绝。ID 不回绕，耗尽明确失败。
- 重验证不延长原有 NIK 寿命。过期条目不可读取、识别或用于认证；新配对可回收其槽。
  两个有效槽都被其他凭据占用时明确失败，不自动淘汰。凭据只在当前 Session 的 RAM
  中有效；服务关闭后可由同 Session、相同服务名重新读取，Session 关闭后全部失效。

上述 API 已接通生产代码；集中运行/GC/OOM/竞争、实机和 RF 验证仍按 Wi-Fi 阶段统一安排。

## 消息发送

`service.send({peerServiceId, peerMac, ssi?, vendor?, timeoutMs?})` 要求服务已 ready；同步模式还要求 SDK
保存该对端的服务记录。USD 使用调用者提供的对端地址/ID，并拒绝 vendor。`peerServiceId` 是发现事件中的对端 ID，范围 1..255；
`peerMac` 为恰好六字节的 ByteSource，必须是非零单播 NAN interface MAC。
不支持零值通配选择。`ssi` 为可选 ByteSource，0..2048 bytes，默认空；输入在
Future capture 时完成复制，随后可以修改或释放。`vendor` 契约见上节；未知字段拒绝。
`timeoutMs` 为 1..120000 ms，默认 10000。

每个 Session 同时只接纳一条原生消息，全 runtime 最多保留八个捕获/发送记录。
identity 在 boot 内不复用；SDK 的 context 是可复用的 buffer 地址，不作为公开
身份。实际 follow-up 分配关联精确 identity/ticket；完整原生 TX 回调退出后写入
完成状态，实际 recycler 返回后才归还 buffer 所有权。成功结果中的
`txSubmitted/txCompleted/txSucceeded` 为 true；`bufferRetired` 可能仍为 false。
TX 成功不代表对端应用已处理消息，不提供自动重发或去重。
`submitStarted` 表示 worker 已进入提交阶段，`txSubmitted` 表示 SDK 已成功返回；
提交期间等待到期时，后者可能仍为 false。完成时间按原生回调退出时记录，公共等待
直接读取完成状态，不依赖后台清理 worker 的重试间隔。

| 情况 | 行为 |
| --- | --- |
| Future 在 start 前取消 | 不提交原生发送，释放捕获数据 |
| 已入队、原生提交尚未开始时超时/取消 | 标记结束，worker 丢弃该消息 |
| 提交后超时/取消 | 结束公共等待；不撤回已发射或待发射的帧，保留原生记录直至实际回收 |
| TX 完成但尚未回收 | 可返回成功；继续保留原生记录并拒绝下一条发送 |
| 服务关闭 | 取消公共发送等待，清理服务；仍等待所属原生帧退休 |
| Session 关闭/runtime teardown | STOP、完整回调和实际 buffer 回收后，才释放发送及服务记录 |

`service.status()` 的 `sendIdentity`、`sendPending`、`sendCleanupPending` 区分
等待发送和保留清理。`wifi.nan.status().messageHandles` 包括 Future 保留的历史结果。
底层未回收的发送不会因为等待超时而被重试；需要终止整个 discovery 时可关闭父
Session。结果转换 OOM 也不会重新提交已完成的发送。错误详情保留发送 identity、
TX/回收标记及原错误，不输出 SSI 内容。数据连接使用服务的安全策略；配对仍待实现。

## 状态与错误

状态为 `opening`、`ready`、`closing` 或 `closed`。`identity` 标识本 Session；
`operation`/`radioGeneration` 是最近完成的 native 快照，关闭后可作为历史诊断。
worker 在 SDK 中执行时，native 阶段快照可能仍是上一份，须结合 `workerBusy`。

`error`/`stage` 保留启动或原生运行的主错误；`cleanupError`/`cleanupStage` 反映最近一次清理
或 worker 排队失败。`startAttempted`、`startAccepted`、`stopped`、`nativeReset`、
`netifRetired`、`observerRetired`、`modeRestored` 和 `storageRestored` 区分具体进度。
未进入相应阶段的标记可以为 false，即使 Session 已无资源可清理。
`reservedBytes` 统计 Session 与其保留的 Radio control 存储，不含 SDK heap 或
单独记账的 Future 存储。完整字段见源类型 `WiFiNanStatus`。

`wifi.nan.status().tx` 返回当前 SD/NDP buffer 跟踪池的 `capacity`、`tracked`、
`unidentified`、`submissions`、`rejected`、`reservedBytes`、`closing`、
`identityExhausted` 和 `error`。固定上限为 32 个 buffer 跟踪槽；这不是服务数或
SDK 队列容量。跟踪原生 service-discovery 和 datapath 对象的管理帧分配，
包括 NDP 默认拒绝、请求、响应、确认、安全安装及终止帧，以及进入原生数据队列的
普通/空数据帧；SDK 原帧仍归 SDK 所有。数据帧最多占 24 槽，另为管理帧保留
    8 槽。beacon 不计入此跟踪数。
跟踪池在 NAN 启动时从共享 control 预算分配到 internal memory，原生 recycler
返回后才释放相应槽；全部 buffer、定时器句柄与已进入回调退休后才能释放池。
`reservedBytes` 还包含池内的操作和定时器控制记录，不包含 SDK 定时器内部堆分配。
地址复用不替代精确 ticket。
`unidentified` 包括尚未完成构造或到达 TX 提交点的帧，以及没有单一连接 ID 的
组播数据。未确认服务身份的管理帧会保守地延迟 service ID 复用；组播数据属于
Session，不阻止已经完成原生回收的单连接或服务退休。ticket 在 boot 内不回绕，
耗尽后拒绝新 buffer。

全局 `serviceCallbacks` 统计服务发现/接收、NDP 应用回调、原生 SD 发送回调的服务处理范围
和完整 NDP 管理帧收发、空数据帧发送完成及原生定时器处理范围，包括应用回调返回
之后的 peer 写入/重试；嵌套调用会分别计数。
单服务冻结仍允许在途 NDP 完成；全局关闭封住 NDP 回调入口，等待已进入回调退出再 STOP。
独立 follow-up 完成通知继续保留。全局 Session、TX 和 callback 字段依次采样，不是跨任务原子快照。

NDP 建立/空闲定时器在两个异步队列中仅携带不复用的数值身份，停用后旧通知不再
获得原生对象访问权。停止/删除失败保留实际句柄，关闭只重试未完成后缀。
定时器或原生池故障由 Session service 读取并开始关闭，不依赖观察事件入队。
单连接回收在 Wi-Fi 原生任务中核对该连接的删除、buffer/callback 退休与
精确定时器清理，失败保留记录。

## 数据连接

发布和订阅时显式设置 `dataPath: true`。默认的发现服务继续拒绝数据连接；
数据连接沿用所属 Service 的安全凭据和策略，不支持逐连接注入不同密钥；未配置
安全时为开放连接。配对仍待接入，不能通过关闭确认或省略密钥匹配降低安全服务的要求。
每个 Session 最多同时占用两个连接槽，包含等待应答、正在建立和正在清理的连接；
整个 runtime 最多保留八个连接控制对象，已关闭但尚未释放的 JS/Future 句柄也计入。

订阅服务调用 `requestDataPath({ peerServiceId, peerMac, confirmRequired?, timeoutMs? })`。
对端 ID 为发现事件中的发布 ID，范围 1..255；MAC 是六字节 ByteSource，必须是
非零单播地址。`confirmRequired` 默认 true；`timeoutMs` 为建立截止时间，默认
10000，范围 1..120000 ms。方法返回句柄时尚未保证原生提交或连接成功；使用
`path.ready()` 等待确认。SDK 仍会核对该订阅服务实际发现的对应发布记录。

发布服务用 `receiveDataPath({ timeoutMs? })` 接收入站句柄；它支持 `Future.call`，
等待超时返回 null。请求先保存在原生有界记录，再由共享 worker 建立控制对象，
不依赖 `service.events` 或默认观察队列。转换失败/取消等待会归还本次接收预约，
下次可再次接收；已成功转交的同一请求不会被第二个 Future 再次取得。

入站句柄调用 `respond({ accept: true|false, ssi?, timeoutMs? })`，`accept` 必须是
显式 boolean，SSI 至多 512 bytes。每个连接只接受一次应答。接受时等待原生确认，
拒绝时等待本连接清理完成，再返回状态。无人接收、未应答或超过从原生认领起的
10 秒期限默认拒绝；原生协议计时器也可能提前终止请求。共享容量不足或控制对象
分配失败同样拒绝，不把清理责任放进可丢队列。

`ready/respond/close` 支持 `Future.call`。它们的等待超时/取消结束本次等待，
已提交操作和连接自身的截止时间继续有效；要结束连接，调用 `cancel()` 或 `close()`。
`close()` 成功表示原生删除、完整发送回调、实际 buffer、定时器和 host 记录全部
退休。构造失败发生在激活前；finalizer 请求关闭，原生 registry 独立保留引用。
服务关闭先结束所属连接，父 Session 关闭及 runtime teardown 接管全部连接。
原生终止失败或无法证明局部清理时会关闭父 Session，影响该 Session 内其他连接；
原始错误和关闭进度保留在连接及 Session 状态中。

`status()` 返回独立 identity、所属 Session/service、原生 ID、方向、状态、NMI/NDI、
IPv6 identifier、最近捕获的 SSI、提交/终止/回收标记及主错误/清理错误。
`connected` 表示收到原生连接确认，不等同于对端 IPv6 流量或应用协议验证通过。
`wifi.nan.status()` 的 `dataPathHandles/activeDataPaths` 分别统计保留对象与活动槽。
完整字段见源类型 `WiFiNanDataPathStatus`。

错误 `WIFI_NAN_FAILED`、`WIFI_NAN_TIMEOUT`、`WIFI_NAN_CLOSED` 的 `details`
包含状态以及原始 `espCode`、`espName`、`waitTimedOut`。`timedOut` 与
`waitTimedOut` 分别指启动截止时间和单次等待，不能互换。参数错误为 TypeError，
JS 分配失败保留 VM OOM。状态、错误和能力结果不包含凭据。

## 示例

在已停止且没有其他 owner 的 Radio 上运行：

```js
var discovery = wifi.nan.open({ channel: 6, timeoutMs: 10000 });
try {
    var state = discovery.ready({ timeoutMs: 10000 });
    print(state.state);
} finally {
    discovery.close({ timeoutMs: 10000 });
}
```

本接口的集中 VM/调度/GC/OOM、完整目标构建矩阵和实机 RF 验收尚未执行。
服务、消息与 DataPath 已接入各自身份和回调/存储退休流程，并通过当前 C5
受影响生产单元编译及局部链接；这些结果不证明运行、对端互通或 IPv6 数据通信。
原生 observer 退出也不等于所有 TX buffer 已回收，仍须分别验证关闭屏障。
