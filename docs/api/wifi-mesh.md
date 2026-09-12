# Wi-Fi Mesh

`wifi.mesh` 使用唯一 `wifi-mesh/1` 契约，当前为 Candidate。注册要求 Wi-Fi、
目标 `SOC_WIFI_MESH_SUPPORT`、SoftAP、BSD TCP/IP 和 IPv4，且为固定 SDK 支持的
C3/C5/S3 本地 Wi-Fi driver；ESP Host Wi-Fi 不注册。C5 的双频能力不代表 Mesh
支持 5 GHz，本接口使用 SDK 的 2.4 GHz Mesh 信道范围。

## 接口

| 方法 | 行为 |
| --- | --- |
| `wifi.mesh.capabilities()` | 版本、状态、排他 Radio 及固定资源上限 |
| `wifi.mesh.open(options)` | 完整构造 Session 后激活后台启动，返回 `WiFiMeshSession` |
| `session.status()` | 新的非秘密状态快照，关闭后仍可查询 |
| `session.ready(options?)` | Future-capable；等待原生启动完成，返回状态 |
| `session.close(options?)` | Future-capable；请求关闭，等待原生退休、原 Radio 配置恢复及 owner 释放 |
| `session.cancel()` | 立即请求关闭，返回 `undefined`，不等待原生清理 |
| `session.recover(options?)` | Future-capable；显式请求一次已知恢复失败的重试，不重新创建已退休的 Mesh |
| `session.send(options)` | Future-capable；发送数据，返回独立命令 identity 和 SDK 提交结果 |
| `session.receive(options?)` | Future-capable；返回带独立 ByteView 的消息，等待超时返回 `null` |
| `session.watch({capacity?})` | 创建唯一观察队列，返回 `EventQueue<WiFiMeshEvent>` |
| `session.routingTable(options?)` / `groups(options?)` | Future-capable；返回复制的六字节地址数组 |
| `session.addGroups({addresses,timeoutMs?})` / `removeGroups(...)` | Future-capable；添加/删除组地址 |
| `session.setToDSState({reachable,timeoutMs?})` | Future-capable；原生执行时要求实际 root 角色 |
| `session.connect(options?)` / `disconnect(options?)` | Future-capable；请求 Mesh 连接/断连，保留 Session |
| `session.flushUpstream(options?)` | Future-capable；显式丢弃原生待发 upstream 队列 |
| `session.setParent(options)` | Future-capable；关闭自组织后，按明确配置选择 router 或 Mesh parent |
| `session.scan(options?)` | Future-capable；关闭自组织后执行手动扫描，返回带独立 identity 的扫描状态 |
| `session.receiveScan({scanId,timeoutMs?})` | Future-capable；读取一条 AP/Mesh IE，完整转换后消费，读完返回 null |
| `session.flushScan({scanId,timeoutMs?})` | Future-capable；丢弃指定扫描列表并释放扫描占用 |

不可直接构造 `WiFiMeshSession`。同时最多一个活动 Session、四个存活 Session 句柄，
每个 Session 一个待提交/执行中的命令；最多八个存活命令句柄。路由表上限 1000，
组地址上限 64。已关闭但仍被 JS/Future 引用的句柄仍占预算；identity 在 boot 内不
回绕复用。所有 Future-capable 方法可通过既有 `Future.call` 异步等待。

## 启动与参数

`open` 要求完整普通 options 对象；拒绝未知字段、隐式类型转换、小数和溢出。
ByteSource 在调用捕获阶段复制，后续修改 JS 输入不改变已捕获的配置/命令。

| 字段 | 范围与默认值 |
| --- | --- |
| `meshId` | 必需，非零六字节 ByteSource |
| `routerSsid` / `routerPassword` | SSID 必需、1..32 UTF-8 字节；password 缺省为空，否则为空或 8..64 字节；禁止嵌入 NUL |
| `routerBssid` | 可选六字节 ByteSource，缺省全零交给 SDK 选择 |
| `apPassword` / `apAuthentication` | password 必需；认证默认 `wpa2-psk`，另支持 `open`、`wpa-psk`、`wpa-wpa2-psk`；open 密码必须为空，其余 8..64 字节 |
| `encryptIE` / `ieKey` | 加密选择必需；true 时要求 8..64 可打印 ASCII 字节的显式 key；false 时禁止 key |
| `channel` | 0..14，默认 0 查找；实际合法信道仍由 driver 和监管配置决定 |
| `topology` / `type` | 默认 `tree` / `idle`；另支持 `chain`，类型为 `root`、`node`、`leaf`、`station` |
| `maxLayer` / `capacity` | 默认 6 / 32；tree 层数 1..25、chain 1..1000，容量 1..1000 |
| `receiveQueue` | SDK 队列设置 16..128，默认 16；框架另保留 self/ToDS 两个 1500 字节接收槽 |
| `sendBlockMs` | SDK 发送阻塞设置，1..60000 ms，默认 1000；不等同于 Future 超时 |
| `maxConnections` / `nonMeshConnections` | 默认 4 / 0；分别 1..10 / 0..9，总数不超过 10 |
| `allowChannelSwitch` / `allowRouterSwitch` | boolean，默认 false；分别允许 SDK 改变配置中的信道/路由器 |
| `votePercentage` | 启动前投票阈值，(0,1]，默认 0.9 |
| `fixedRoot` / `selfOrganized` / `powerSave` | boolean，默认 false / true / false |
| `allowApRestart` | 默认 false；原配置含 AP/APSTA 时必须显式允许恢复验收短暂启动原 AP |
| `timeoutMs` | 从激活算起的启动期限，1..120000 ms，默认 30000 |

worker 仅接纳健康、零 owner 的 STOP Radio 或冷初始化；不会隐式关闭 Station、AP、
ESP-NOW、CSI、Monitor、NAN 或其他 owner。Mesh 全生命周期持有中央排他 lease。
使用临时 RAM 配置，保存并恢复既有 STA/AP 配置和已知 Radio 策略。C5 先切换至
2.4 GHz，再使用本次 Mesh 密码创建临时 AP。`ready` 仅表示 SDK 启动和 Radio 交接
完成；parent 关联、角色、IP 和 Internet 可达性分别查询。

`status` 的 `parentKnown/parentConnected` 来自原生控制事件，`root/nodes/routes`
仅在 `snapshotValid` 时有效，否则为 null。`type` 另要求已经观察到 parent 连接；
`txPending/rxPending` 为原生发送/接收队列计数，无有效快照时为 null。所有 SDK
getter 顺序采样，不能把这些字段当作整机同一时刻的原子快照。`routerBssid` 在有效
快照中给出原生路由器 BSSID，`queryError` 保留 getter 错误。`ipReady/ipv4` 是本地
IPv4 状态，与 `toDSReachable` 分开。root 和直连路由器的 `station` 角色可启动 DHCP；
Mesh node/leaf 不启动 DHCP。管理在同一 worker 上进行，可能等待正在执行的 SDK
发送/扫描返回；断连捕获或无效快照会使公开 IP readiness 立即失效，Wi-Fi 自身的
断连处理不依赖 JS 观察队列。`reservedBytes` 只含 Session
及 Radio/native owner 的框架存储，完整共享预算查看 `wifi.diagnostics`；SDK workers、
队列及整机峰值尚待阶段测量。

## 收发与控制结果

`send` 必需 `data`（1..1472 字节），默认 `destination:"root"`、`protocol:"binary"`、
`reliable:true`。协议另支持 `http/json/mqtt`。目的地址字段按下表严格匹配，不能
夹带不适用的 `address/ipv4/port/dropOnRootChange`。

| destination | 地址与约束 |
| --- | --- |
| `root` | 无地址，必须 reliable |
| `peer` / `group` | 非零六字节 `address` |
| `fromDS` | 非零六字节 `address`，SDK 执行时要求 root |
| `broadcast` | 无地址，使用 SDK 广播目的地址 |
| `toDS` | 四字节、非零 `ipv4` 和 1..65535 `port`；可选 `dropOnRootChange` |

普通命令结果为 `{identity,bytes,dispatched,returned,completedAtUs}`，时间是单调
微秒。发送成功说明 SDK 返回成功并接纳数据；不表示对端应用已收到，可靠发送也
不承诺端到端确认。connect/disconnect 成功表示请求已返回，实际关联状态用 status
观察。组操作接收 1..64 个唯一、非零六字节地址，不承诺 SDK 复合调用失败后的
逐项原子回滚，失败后可用 `groups()` 核对实际值。

`receive` 默认读 self，`toDS:true` 使用 root 的 ToDS 接收槽。两条 lane 各只允许
一个等待者。消息含 sequence、from、toDS、可选 IPv4/port、原生 protocol/service/
flags 和 `data:ByteView`。sequence 属于本 Session 的原生 identity，不是 RF 序号。
JS 对象和 ByteView 全部转换成功后才消费原生接收槽；转换失败、等待取消或超时
留下原消息可供重试。返回的 ByteView 是独立副本，可在 Session 关闭后读取；使用
完毕调用 `message.data.close()` 归还共享预算。关闭会丢弃尚未交付的原生接收槽。

## 超时、关闭与恢复

各等待方法的 `timeoutMs` 均为 1..120000 ms，默认 30000。receive 到期返回 null；
其他到期抛 `WIFI_MESH_TIMEOUT`。取消 ready/receive 等待不关闭 Session；取消已经
请求的 close/recover 等待也不停止原生清理。启动期限是 open 自己的期限，独立于
ready 等待期限。

发送/控制取消会禁止尚未提交的命令；已经提交的 SDK 调用不能抢占，其数据和父
Session 引用保留至真正返回。超时并不表示发送或控制没有生效。错误 details 给出
`operationIdentity/dispatched/returned/nativeError/waitTimedOut`，返回后的 SDK 错误
与等待超时分别保留。JS roots 可随 Future 结束释放，原生 job storage 仍由 worker
持有。status 同时保留启动、清理错误和原始阶段，不暴露凭据。

Session 关闭先拒绝新命令；正在执行的调用返回后，继续原生退休、物理 STOP、
netif detach/fence、原配置恢复和精确 owner 释放。已完成前缀不重复执行。原来的
冷 Radio 回到未初始化；原健康 STOP Radio 恢复原配置后保持 STOP。AP 恢复验收
允许的短暂启动已由 allowApRestart 明确授权。

已知配置重放/START 失败保留冻结快照，`recover` 请求一次显式物理恢复，不重新
启动 Mesh。未知 init/deinit 或无法证明原生清理完成的错误保留可诊断状态，
`restartRequired:true` 要求设备重启；runtime restart 不构成恢复保证。runtime
销毁也等待存活原生工作真正退休，不提前释放 worker 引用的数据。

## 观察队列与验证状态

每个 Session 至多一个 watch，capacity 为 1..16、默认 8；全局最多四个存活 watch
存储。队列和 context 的预算保留到实际存储销毁。观察包含固定 SDK event id、
sequence、字段 bit mask 及白名单 MAC/channel/layer/reason/tableSize/tableChange/
value/duty。不存在的字段为 null；位分别为 1/2/4/8/16/32/64。

原生控制更新先于八槽观察 ring 和 SDK post。ring 或 EventQueue 满时丢观察，
不会阻塞断连、发送返回或关闭。事件转换失败也不影响原生控制；watch 不是可靠
事务日志，Session 开始关闭后观察队列会请求关闭。

Host/VM、GC/OOM、竞争、完整三目标及 disabled 构建和实机/RF 验证待 Wi-Fi API
全部完成后集中执行；长 soak 延至 BLE。

## 手动 parent 与扫描

这些方法要求执行时原生自组织已关闭，可使用 `setSelfOrganized({enabled:false})`。
Mesh 继续持有排他 Radio；扫描通过现有 worker 调用阻塞 SDK 扫描，不依赖观察事件
决定完成。只有 SDK 明确返回成功后才开放读取；未确认终止的扫描保留输入和占用，
需要关闭 Session 并完成物理 STOP，不能重用扫描槽或切回自组织。

`setParent` 参数为：

| 字段 | 契约 |
| --- | --- |
| `ssid` | 必需，string 或 ByteSource，1..32 字节且不含 NUL；ByteSource 可表示非 UTF-8 SSID |
| `password` | 缺省为空；否则为空或 8..64 字节，不含 NUL；非 root parent 的开放/加密选择须与本 Session 的 AP 认证设置一致 |
| `bssid` | 可选，非零单播六字节 ByteSource，须与 SSID 对应同一 parent |
| `channel` | 必需，1..14，仍受当前合法信道限制 |
| `meshId` | 可选，非零六字节 ByteSource；缺省沿用当前 Mesh ID |
| `type` / `layer` | 必需，root 的层数为 1；node 大于 1 且小于 maxLayer；leaf 为 1..maxLayer；加入后 SDK 可能改变层数 |
| `timeoutMs` | 1..120000，默认 30000 |

原生 setter 可能改变 mode、断开旧 parent 并发起新关联；返回成功只代表请求返回。
指定 router 时使用 root/layer=1；SDK 不支持在 setParent 中指定 station。
完整 Station driver options 不会透传给此方法：SDK root 路径会重建 router 配置，
不能把其他 Station 安全字段宣称为此入口支持的参数。输入密码在原生调用返回后清零。

`scan` 可省略 options，参数如下：

| 字段 | 范围与默认值 |
| --- | --- |
| `ssid` / `bssid` | 可选过滤；SSID 与 setParent 字节规则相同，BSSID 为非零单播六字节 ByteSource |
| `channel` / `channels` | channel 默认 0，扫描所有允许的 2.4 GHz 信道；否则 1..14。channels 为 1..14 个唯一信道，与非零 channel 互斥；明确跳过 5 GHz |
| `showHidden` / `mode` | 默认 true / passive；mode 另支持 active |
| `activeMinMs` / `activeMaxMs` | active 才能使用；默认 0 / 120，范围 0..1500 / 1..1500，min 不大于 max |
| `passiveMs` | passive 才能使用；默认 360，范围 1..1500 |
| `homeChannelDwellMs` | 默认 30，范围 30..150 |
| `coexistenceBackgroundScan` | 默认 false，传给 SDK 的后台扫描选项，不代表跨模块共存已验收 |
| `timeoutMs` | 1..120000，默认 30000；等待期限，不取消已提交的原生扫描 |

返回 `{identity,running,completed,uncertain,retained,total,remaining,error}`，同一状态
也在 `status().scan` 中可见，没有扫描时为 null。等待超时后若扫描已经提交，可从
status 获取其 identity；completed 只代表原生扫描完成，error 还可能记录随后数量
读取失败。total 是本次 SDK 列表大小；remaining 包括已从 SDK 取出、仍等待 JS
完整转换的那一条。扫描 identity 在 boot 内不复用。

`receiveScan` 和 `flushScan` 必须提供对应 `scanId`。扫描占用存在时允许状态及配置/
路由等只读查询，但拒绝新的扫描和其他控制修改；即使结果已读完，也须显式 flush
后再 setParent 或恢复自组织。扫描终止已确认后，可 flush 丢弃发生读取错误的列表。
flush 成功还要求原生列表数量读回为零，否则保留扫描占用以便诊断和显式重试。

每次 receiveScan 返回 `{scanId,sequence,accessPoint,bssidBytes,association,meshIE}`，
其中 accessPoint 复用普通 Wi-Fi 扫描记录字段。association 只解码固定 SDK 的 tree/
chain IE，包含 Mesh ID、角色/层数、容量、RSSI、root/vote 和 ToDS 字段；未知或没有
Mesh IE 时为 null，不构成对 parent 身份或安全性的认证。meshIE 是 SDK 返回的原始
IE 独立 ByteView，长度上限 257 字节，也由 capabilities.maxScanIEBytes 暴露。

先分配有界原生记录，再执行破坏性的 SDK 读取。原生记录保留至 JS 对象及 ByteView
全部转换成功并以精确 scanId/sequence 提交；转换失败、取消或等待超时可以重读同一条，
不会默默跳过 AP。没有剩余记录立即返回 null。返回的 meshIE 不受 flush 或 Session
关闭影响，使用完调用 `record.meshIE.close()`；未交付记录在成功 flush 或关闭退休时归还预算。

## 配置、拓扑与电源控制

下列方法均为 Future-capable，复用同一个 Session 命令槽。参数可带 timeoutMs，
缺省 30000。除只读查询外，成功返回 WiFiMeshCommandResult，表示 SDK 调用已返回；
网络收敛、角色改变或信道切换完成须继续观察 status/watch。取消和等待超时不撤销
已提交修改。层数、容量、拓扑、AP 密码/认证/连接上限、RX 队列、PS 开关和投票
阈值属于启动配置；修改这些设置须关闭后以新 open 参数创建 Session。

| 方法 | 参数与真实行为 |
| --- | --- |
| `configuration({includeSecrets?,timeoutMs?}?)` | 顺序读取原生配置、容量/连接上限、固定根、自组织、PS、投票阈值、关联期限和根修复延迟；不是原子快照 |
| `setRouter({ssid,password?,bssid?,allowRouterSwitch?,timeoutMs?})` | 完整替换 Mesh router 配置；SSID 1..32 UTF-8 字节，密码空或 8..64 字节；省略密码表示空，省略 BSSID 为 SDK 自动选择 |
| `setMeshId({address,timeoutMs?})` | 动态修改网络 ID，非零六字节 ByteSource |
| `setType({type,timeoutMs?})` | `idle/root/node/leaf/station`；原生决定当前状态是否允许 |
| `setSelfOrganized({enabled,selectParent?,timeoutMs?})` | selectParent 默认 false；只能在 enabled=true 使用，true 可使当前 root 放弃角色并重新寻找 parent |
| `setFixedRoot({enabled,timeoutMs?})` | 修改固定根设置；同网设备须使用相同设置，启用后由应用指定 root |
| `setRootConflicts({enabled,timeoutMs?})` | 允许/禁止同网多个 root |
| `setAssociationExpiry({seconds,timeoutMs?})` | 10..2147483 秒；超期无数据的 child 可被解除关联。SDK 建议加密网络使用更长时间，如 30 秒 |
| `setRootHealingDelay({milliseconds,timeoutMs?})` | 0..2147483647 ms；根修复启动延迟 |
| `setIEEncryption({enabled,key?,timeoutMs?})` | true 必须提供 8..64 可打印 ASCII 字节 key；false 禁止 key，停用 IE 加密 |
| `waiveRoot({attempts?,percentage?,timeoutMs?}?)` | 执行时要求实际 root；投票尝试 15..2147483647，默认 15；阈值 (0,1]、默认 0.9。没有更好候选者时可能保留当前 root |
| `switchChannel({channel,beaconCount?,routerBssid?,timeoutMs?})` | 实际 root 才可请求整网 CSA；channel 1..14，beaconCount 1..255、默认 15；可提供新路由器非零六字节 BSSID |
| `setDeviceDuty({duty,type,timeoutMs?})` | duty 1..100；type=`request` 使用网络 duty，`demand` 要求本设备 duty；100 停止省电 |
| `setNetworkDuty({duty,durationMinutes,timeoutMs?})` | 整网 duty 1..100，持续分钟数 1..2147483647；只有实际 root 可用 -1 表示持续至新策略接管 |
| `signalDuty({forwardCount,timeoutMs?})` | 转发 duty signaling，次数 0..254；SDK 命令把次数 + 1 存在单字节字段 |
| `subnet({address,timeoutMs?})` | 读取关联 child 的子网地址，返回 number[][]、上限 1000；数量/列表分步读取，拓扑变化时可失败 |
| `hasGroup({address,timeoutMs?})` | 查询本机是否属于该 group，返回 boolean |
| `upstreamCapacity({address,timeoutMs?})` | 本机或关联 child 的原生上行容量，返回 `{available,lastSequence}`；available 保留 SDK 有符号结果 |
| `powerStatus(options?)` | 顺序读取 `{enabled,active,deviceDuty,deviceType,networkDuty,durationMinutes,networkType,appliedRule,runningDuty}`；type/rule 为 SDK 数字值 |
| `tsfTime(options?)` | SDK 有符号微秒；超出 JS 安全整数范围时报错，不舍入 |

configuration 返回原始 routerSsidBytes 和严格 UTF-8 的 routerSsid（无法解码时 null），
地址均为复制的 number[]。apAuthentication 为原生数字枚举，sendBlockMs 是该 Session
成功设置的值，其他可读策略由 SDK getter 取得。默认 secretsIncluded=false，
routerPassword/apPassword/ieKey 为 null；只有显式 includeSecrets=true 才读取/返回秘密。
返回的 JS 字符串由调用者管理。status、watch、错误详情不包含凭据。

IE key 先写入，随后启用加密；第二步失败不回滚已写入 key。错误详情的 controlStage
和 completedSteps 记录失败位置及已返回成功的步骤。configuration 的 encryptIE 与
ieKeyLength 反映本 Session 已确认的原生设置；停用加密不声称擦除了 SDK 内部 key。
输入秘密在 setter 返回后清零；配置查询的临时秘密在失败或默认隐藏路径立即清零，
显式读回的副本在命令释放时清零。两类命令的原生存储都保留到实际 SDK 返回。

固定 SDK 的 esp_mesh_waive_root 明确要求 is_rc_specified=false，未实现指定替代 root；
esp_mesh_set_network_duty_cycle 的参数契约只支持 ENTIRE。公开入口因此没有指定候选
root 或 UPLINK 参数，未知字段会被拒绝。依据为固定 esp_mesh.h 中对应声明及注释。

固定 C5 SDK 二进制还确认：关联过期 setter 在 ioctl 分配失败时仍可返回 ESP_OK，
且丢弃 ioctl 返回码。本入口因此在 setter 返回后读取实际过期值；不匹配时报告
WIFI_MESH_FAILED / controlStage=mesh-association-expiry-readback，completedSteps=1。
这表示修改尚未确认，不表示修改必定未发生，不自动重试。相关 SDK 指令与 hash
保留在本批 build/w08-mesh-controls-evidence.json；其他目标运行证明仍在阶段验收。
