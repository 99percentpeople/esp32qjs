# ESP32QJS 第三阶段：BLE 重构与新功能实施文档

- **状态**：Proposed / 分阶段实施任务书；不代表代码已经实现或通过硬件验收
- **目标仓库**：`99percentpeople/esp32qjs`
- **历史审查基线**：`9a74f1197d53863e079c30f8559ccd5b6cd60b42`
- **第一阶段实施基线**：firmware `e1b861c`；[实际证据与剩余边界](investigations/2026-09-07-wireless-core.md)
- **ESP-IDF 基线**：`v6.1` / `fff9895c82d744c7237be8847347bdd1b07c6643`
- **BLE Host**：ESP-NimBLE
- **主要范围**：GAP、Legacy/Extended/Periodic Advertising、扫描、连接、GATT Client/Server、安全、隐私、Bond、L2CAP CoC，以及可选 ISO/方向测量能力
- **版本策略**：继续使用唯一开发版本 **v1**；允许破坏性修改；不保留旧别名、兼容 reader 或 v2 命名空间
- **建议仓库路径**：`docs/plans/03_esp32qjs_ble_refactor_and_features.md`
- **文档日期**：2026-09-07
- **前置文档**：[第一阶段：现有问题修复](01_esp32qjs_existing_fixes.md)
- **并行文档**：[第二阶段：Wi-Fi 重构与新功能](02_esp32qjs_wifi_refactor_and_features.md)
- **已核验的 ESP-NimBLE 基线**：IDF v6.1 指向 `139cada0ae932957fa06ba37d17e3c9c2c95c773`；实施时记录实际 checkout

> 本文是用于开发拆分、接口评审和测试验收的目标 BLE 规范；明确标记 contract-pending 的子功能必须先完成其子契约，不得直接作为当前已实现 API 发布。
> 它不把 ESP-NimBLE C 函数机械地逐个改名后暴露给 JavaScript，而是在不丢失
> 公共能力的前提下，将同步 callback、mbuf、连接句柄和 Controller 状态转换成
> ESP32QJS 已有的 `Future`、`EventQueue`、`ByteView`、`ByteSpanSource`、有界 native
> pool 和 generation-checked handle。

## 0. 实施入口与本轮决策

本文以用户 BLE 原设计为主体，保留其四角色、Adapter/Connection/Scanner/Advertiser 所有权、完整 GATT、Security、L2CAP、Periodic 和可选高级能力范围。第 1～34 节保留原章节组织，并在有问题的条款处就地修订；附录 C～E 给出工单依赖、子契约阻塞和来源。

**本轮任务是先稳定拆分，再逐步扩展能力，不是重新写一个与现有 Future/queue/ByteView 无关的 BLE 封装。** 现有原生行为的修复先按第一份文档完成；不要在机械拆分提交里同时改变 API、安全政策和超时处理。

### 0.1 实施前提

用户最新安排：先完成全部 Wi-Fi API，再集中执行 Wi-Fi 阶段测试；相关测试完成后才开始本文件 BLE 新功能。Wi-Fi 实现期间仅做必要编译和生成物一致性检查。Wi-Fi 完成后先做实机功能测试；长时间 soak 延至 BLE API 也完成后。内存/吞吐与共存仍分项登记，不把待测项写为通过。见[集中验收账本](investigations/2026-09-07-wifi-refactor-start.md)。第一阶段新增 timeout 取消失败保留原生存储的修复已通过 Host/构建，设备验证 not-run；本阶段新功能尚未开始。

**启动前提（不是当前结果声明）**：01 的 F-CORE 必须已通过，并确认当前分支相对实施基线的变化。根据用户当前实施顺序，BLE 新功能还须等待 Wi-Fi API 完成及其相关阶段测试完成；共用的 native 生命周期、预算和生成物修改仍须协调，最终完成混合负载验收。

本文的任务/额外字段/政策均是本轮设计要求，不是基线已经实现。所有 task 默认 planned，所有未经执行的测试仍为 not-run。

### 0.2 本轮修订决策

| 原设计问题 | 正式决策 | 工单 |
| --- | --- | --- |
| 多 Scanner 缺少物理扫描协调 | 单物理扫描 procedure，多逻辑 Scanner 仅共享相同规范化硬件参数；冲突直接拒绝 | B-03 |
| 队列 drop-newest 可能丢控制完成/入站连接 | 原生完成状态先更新；增加 `adapter.connections()` 作为有界连接表的恢复入口 | B-02 |
| 重复订阅关闭时盲写 CCCD=0 | 每 connection/gattGeneration/valueHandle 仅一个 Stream owner；重复订阅明确拒绝 | B-06 |
| event-only 写成功但 JS 消息可能丢失 | 新增 `server.receive()` 主业务队列；先预留槽再接受 Write Request；watch 是可丢观察副本 | B-07 |
| Prepared/可靠写事务没有公共 hook 证明 | 单独验证公开 Host 的 staging/commit 能力；不提前承诺多属性原子提交 | B-07 |
| Repeat Pairing `request` 等待 JS 与同步 callback 不符 | 删除该值；提供 reject/显式 replace，以及“先拒绝、应用批准、删除 Bond、重新配对”流程 | B-08 |
| autoPair 在返回 Connection 前要求交互 | 删除 `autoPair`；先 connect 返回，再显式 pair 并消费请求 | B-08 |
| Indication callback status=0 被误当确认 | 区分提交与最终 confirmation，仅按固定 NimBLE 的确认终态完成 Future | B-07 |
| 打开前能力与 Controller query 混淆 | build/effective 两阶段；打开前未知项为 null，不擅自初始化 Host | B-02 |
| close 在 Host 停止前释放回调可见池 | 先停止 source/取得原生终止屏障，再释放控制资源；数据 lease 单独延迟回收 | B-10 |

这些变更属于未冻结 v1 的直接替换，不增加兼容别名。原设计使用 `object` 或未展开类型的 PAwR/ISO/CTE 等部分继续保留完整目标，但在对应子契约被审查前不进入正式 public registration。

## 文档导航

- 第 1～4 节：目标、完整性、命名和公共运行时规则
- 第 5～9 节：模块、能力探测、Adapter、扫描和广播
- 第 10～14 节：连接、链路控制、GATT Client、通知和 GATT Server
- 第 15～18 节：安全、隐私、Bond、Filter Accept List 与 L2CAP CoC
- 第 19～22 节：Periodic Advertising、PAwR、ISO、方向测量和 BLE Mesh 边界
- 第 23～27 节：错误模型、并发、内存、Kconfig 和文件拆分
- 第 28～34 节：NimBLE 覆盖矩阵、实施工单、测试、迁移、示例和完成标准
- 附录 A～E：原有能力、固定来源、提交依赖、子契约阻塞与修订追溯

---

## 1. 核心决策

1. **保留简单入口。** 基础入口仍为 `ble.capabilities()` 和 `ble.open()`。打开后，
   常用操作直接使用 `adapter.scan()`、`adapter.advertise()`、`adapter.connect()`、
   `adapter.server()` 和 `adapter.close()`；不创建 `ble.host.adapter.gap.scan()` 之类
   的深层路径。
2. **一个 JavaScript Runtime 只拥有一个 NimBLE Host Adapter。** 多扫描器、多广播
   Set、多连接和多 L2CAP Channel 都是 Adapter 的子资源，但 Host 初始化、同步、
   Store、GAP callback 和关闭过程只有一个 owner。
3. **完整能力通过 typed mapping 实现。** ESP-IDF 6.1 中公开、文档化、当前构建可用
   的 ESP-NimBLE Host/Controller 能力必须映射为类型安全的 JS API，或在机器可检查
   的清单中标记为 `framework-owned`、`build-time`、`target-unsupported` 或
   `separate-stack`。
4. **不提供任意字符串式 `ble.nimble.call(name, args)`。** Raw HCI 或 untyped C ABI
   会绕过内存、线程、连接 generation、mbuf 和安全边界，不进入公共 v1。
5. **NimBLE callback 永不直接调用 JavaScript。** callback 只允许复制到固定 pool、
   更新原子状态、投递固定大小事件以及唤醒已注册 Future。
6. **重复输入必须有界。** Scan Report、Notification、Indication、GATT Server Write、
   Pairing Request、L2CAP SDU、Periodic Report 和 ISO SDU 均使用有界队列与明确
   overflow 统计。
7. **GATT Server Database 在 Host 启动前静态注册。** 运行期间允许修改缓存值、
   订阅状态和发送 Notify/Indicate，但不允许任意增删 Service/Characteristic，除非
   完整关闭并重开 Adapter。
8. **JS 不参与同步 GATT access callback。** 远端读请求必须读取 native cache；远端
   写请求由 native policy 同步接受或拒绝，再投递事件。不能在 NimBLE Host task 内
   等待 JS 决策。
9. **高级能力显式 feature-gated。** Extended Advertising、Periodic Advertising、
   PAwR、L2CAP CoC、ISO、方向测量、LE Power Control 等只有在 Controller、Host、
   Kconfig 和 target 同时支持时才出现。
10. **产品 Profile 不进入核心 native API。** HID、Heart Rate、Battery、ANCS、Nordic
    UART、自定义 provisioning 等由官方 JS Library 或应用实现；native 层只提供通用
    GAP/GATT/L2CAP/ISO primitive。
11. **不静默降级。** 请求 2M PHY、Coded PHY、Extended Advertising、MITM、Secure
    Connections、Periodic Sync 或 L2CAP CoC 时，如果实际构建不支持，必须返回明确
    错误，不能改用 1M、Legacy、无 MITM 或 GATT 替代。
12. **v1 可直接替换。** 当前 Host API 未冻结，旧的 legacy-only Scan Report、单一
    Advertiser 假设或不完整 role 类型可直接改成新的 v1，不维护兼容 façade。

---

## 2. “完整 BLE 能力”的定义

### 2.1 纳入核心范围

核心 `ble` 模块必须覆盖以下公开能力族：

- NimBLE Host 初始化、同步、重置、Store 和身份地址；
- Central、Peripheral、Observer、Broadcaster 四种角色；
- Legacy Advertising 和 Legacy Scanning；
- Extended Advertising、Extended Scanning、多 Advertising Set；
- Periodic Advertising 与 Periodic Advertising Sync；
- 连接创建、取消、断开、Connection Parameter Update；
- RSSI、PHY Update、Data Length Update、MTU Exchange；
- Filter Accept List、地址解析、RPA、Identity Address 和 Privacy；
- GATT Service、Characteristic、Descriptor Discovery；
- GATT Read、Long Read、Read Multiple；
- GATT Write Request、Write Command、Long Write、Reliable Write；
- Notification、Indication 和 CCCD 生命周期；
- 静态 native-cached GATT Server；
- Pairing、Bonding、LE Secure Connections、MITM、Passkey、Numeric Comparison、OOB；
- Bond Store 查询和删除；
- L2CAP Connection-Oriented Channel；
- Host/Controller 能力、限制和错误的结构化暴露。

### 2.2 可选高级范围

以下能力进入同一份覆盖清单，但按 target/build gate 暴露：

- Periodic Advertising with Responses（PAwR）；
- Periodic Advertising Sync Transfer（PAST）；
- LE Isochronous Channels：CIS/CIG、BIS/BIG；
- Direction Finding：CTE、AoA/AoD IQ Report；
- LE Power Control、Path Loss Monitoring、Connection Subrating；
- Controller/Host 统计和诊断事件。

### 2.3 单独栈或单独模块

以下能力不混入通用 `ble` Adapter，但必须在映射表中明确归类：

- **ESP-BLE-MESH**：单独顶层模块 `bleMesh`，与通用 `ble.open()` 的 Host ownership
  和配置可能互斥；需要独立设计文档。
- **LE Audio Profile 层**：BAP、CAP、TMAP、MCP 等属于 Profile/Codec/Audio pipeline，
  可建立在可选 `ble.iso` primitive 上，但不属于通用 BLE v1。
- **Bluetooth Classic**：当前方案使用 ESP-NimBLE，只定义 BLE；Classic/Bluedroid
  不属于本模块。
- **产品 GATT Profile**：作为 JS Library，不复制进固件核心。

### 2.4 明确排除

- NimBLE private header、内部 struct layout 和未文档化 Controller command；
- 任意 Raw HCI passthrough；
- 直接向 JS 暴露 `os_mbuf *`、C callback 或裸 `conn_handle`；
- 在 callback 中分配 JSValue、运行脚本或执行文件/网络 I/O；
- 无上限的 Scan/Notification/ISO buffering；
- 自动重连、业务级 provisioning、Profile-specific policy；
- 未经能力检查就根据芯片名称猜测 PHY、Advertising Set 或 ISO 支持。

### 2.5 可机器验证的完整性契约

```text
docs/nimble-api-map.json
scripts/generate_nimble_api_map.py
tests/python/test_nimble_api_coverage.py
```

覆盖表使用与 Wi-Fi 文档相同的 schema/disposition/implementation/contract/validation 概念；不要复用一个 `mapped` 同时表示目标映射和完成实现。

```json
{
  "schema": 1,
  "symbol": "ble_gap_ext_adv_configure",
  "header": "host/ble_gap.h",
  "disposition": "mapped",
  "jsPath": "BLEAdapter.advertise",
  "feature": "bleExtendedAdvertising",
  "implementation": "planned",
  "contract": "review-required",
  "stateRequirements": ["adapter-ready", "advertising-set-owned"],
  "fieldCoverage": [],
  "ownershipContract": "B-04",
  "completionContract": "B-04",
  "validation": {"host": "not-run", "build": "not-run", "hardware": "not-run"},
  "evidence": []
}
```

允许 `disposition`：mapped、framework-owned、build-time、target-unsupported、separate-stack、removed-or-deprecated、private-excluded。

CI 约束：public symbol 不能未分类；已实现映射必须有 callable、registration、typed fields 和测试；Host/Controller 能力差异逐 build 记录；callbacks、mbuf ownership、同步返回、取消/终止、字段单位和结构条件分支都属于覆盖内容。

NimBLE 公开存在但不属于 JS 能力面的函数，可经审查标为 framework-owned/build-time；不能用这两个分类隐藏实际尚未实现的功能。Raw HCI 即使包含公共底层入口，也按本设计排除理由记录，不能误写为“上游该入口一定是 private”。

目标 API 先留在本文。正式 `.d.ts`、manifest 和 runtime feature 只随真实可用实现更新。

---

## 3. 最终公共命名结构

```ts
interface BLEModule {
  capabilities(): BLECapabilities;
  open(options?: BLEOpenOptions): BLEAdapter;

  /** 官方纯 JS Advertising Data 编解码工具。 */
  readonly ad: BLEAdvertisingDataCodec;
}

declare const ble: BLEModule;
```

基础使用：

```js
var adapter = ble.open({
  roles: ["central", "observer"],
  deviceName: "ESP32QJS"
});

var scanner = adapter.scan({ active: true });
var connection = adapter.connect("12:34:56:78:9a:bc");
```

命名约束：

- 静态调用不超过 `ble.method()`；
- Adapter 操作使用 `adapter.method()`；
- Connection、Scanner、Advertiser 等 handle 不再套静态 namespace；
- 可选高级入口允许 `adapter.iso` 和 `adapter.directionFinding` 一层属性；
- 不创建 `ble.gap.central.connection.connect()`；
- 不把 `ble` 改成大量全局无 owner 的方法。

---

## 4. 公共运行时规则

### 4.1 能力探测

- `sys.info.features.ble` 决定 `ble` 是否存在；
- `ble.capabilities()` 在 Host 未打开时可调用；
- `adapter.capabilities()` 返回打开后的有效能力和资源上限；
- capability 对象为 detached snapshot，不包含 native pointer；
- target 不支持的字段返回 `false`、`0` 或不出现在 optional namespace，而不是伪造。

### 4.2 Future

- `ble.open()`、`adapter.connect()`、GATT operation、pair、MTU、PHY、Data Length、
  L2CAP open 和 Indication confirmation 使用 native Future；
- 直接调用协作式等待；显式并发使用 `Future.call()`；
- timeout 先结束公开等待，再按逐操作契约取消/清理；无法安全取消的 GATT/安全过程可能需要断开本连接，必须显式报告，不影响无关连接；
- timeout 不降低安全要求、不切换地址策略、不自动重连；
- 每个 Connection 的 ATT procedure 共享一个 FIFO lane；
- GAP start/stop/configure 使用全局 GAP lane；
- 每条 Indication 在对应连接上使用 confirmation lane。

### 4.3 EventQueue

每类高频 source 具备：

```ts
interface BLEQueueStats {
  capacity: number;
  depth: number;
  highWater: number;
  published: number;
  delivered: number;
  droppedNewest: number;
  closed: boolean;
}
```

高频数据与观察副本默认 overflow 为 `"drop-newest"`；Producer 不阻塞 NimBLE Host task。控制完成先更新 native operation/Future，不能依赖公开 EventQueue 入队成功。Pairing input、入站连接和 event-only 写入采用有界原生登记/预留容量/拒绝或过期策略，而不是成功接纳后无声丢弃。具体见第 7、16、17、24 节。

### 4.4 二进制所有权

- Scan Data、Notification、GATT Read、Server Write、L2CAP 和 ISO 数据使用 `ByteView`；
- 从 pool 返回的 view 必须显式 `close()`；
- `copy()` 或 `copyData()` 返回独立 owned storage；
- `ByteSpanSource` 可用于 RPC/USB/文件流式转发；
- callback 返回后 NimBLE 的 mbuf/data pointer 立即失效，因此必须在 callback 内复制或
  将 mbuf 所有权按受审查的方式转移给 native owner；不得保存裸指针。

### 4.5 Handle generation

所有公开 handle 至少绑定：

```text
adapterGeneration
resourceGeneration
nativeHandle
closed/faulted state
```

从 Adapter closing 开始，旧操作 handle 拒绝新操作；完全关闭/重新打开后，旧 Scanner、Advertiser、Connection、Notification Stream、L2CAP 和 ISO 操作 handle 报 `BLE_STALE_HANDLE`。GATT snapshot 是 detached 值对象，可继续读取，但其 generation 不能用于新连接。

已经交付且持有独立 native storage lease 的 ByteView/Source 不属于失效的操作 handle；它们可以在 Adapter 关闭后按数据契约读取，直到自身 close。不得为了维持数据有效而保留整个 NimBLE Host。

---

## 5. `ble.capabilities()`

```ts
interface BLECapabilities {
  readonly apiVersion: "ble/1";
  readonly probePhase: "build" | "effective";
  readonly target: string;
  readonly idfVersion: string;
  readonly host: "esp-nimble";
  readonly hostVersion: string | null;
  readonly controllerVersion: string | null;

  readonly roles: {
    central: boolean | null;
    peripheral: boolean | null;
    observer: boolean | null;
    broadcaster: boolean | null;
  };

  readonly addressing: {
    publicAddress: boolean | null;
    randomStatic: boolean | null;
    resolvablePrivate: boolean | null;
    nonResolvablePrivate: boolean | null;
    privacy: boolean | null;
    filterAcceptList: boolean | null;
  };

  readonly scanning: {
    legacy: boolean | null;
    extended: boolean | null;
    active: boolean | null;
    passive: boolean | null;
    duplicateFiltering: boolean | null;
    codedPhy: boolean | null;
  };

  readonly advertising: {
    legacy: boolean | null;
    extended: boolean | null;
    periodic: boolean | null;
    pawr: boolean | null;
    directed: boolean | null;
    anonymous: boolean | null;
    scanRequestNotifications: boolean | null;
  };

  readonly connection: {
    phy1M: boolean | null;
    phy2M: boolean | null;
    phyCoded: boolean | null;
    dataLengthExtension: boolean | null;
    parameterUpdate: boolean | null;
    channelSelectionAlgorithm2: boolean | null;
    powerControl: boolean | null;
    pathLossMonitoring: boolean | null;
    subrating: boolean | null;
  };

  readonly gatt: {
    client: boolean | null;
    server: boolean | null;
    longRead: boolean | null;
    readMultiple: boolean | null;
    longWrite: boolean | null;
    reliableWrite: boolean | null;
    serverPreparedWrites: boolean | null;
    serverAtomicReliableWrite: boolean | null;
    notifications: boolean | null;
    indications: boolean | null;
    serviceChanged: boolean | null;
  };

  readonly security: {
    encryption: boolean | null;
    bonding: boolean | null;
    mitm: boolean | null;
    secureConnections: boolean | null;
    passkey: boolean | null;
    numericComparison: boolean | null;
    oob: boolean | null;
    persistentStore: boolean | null;
  };

  readonly l2cap: {
    coc: boolean | null;
    enhancedCoc: boolean | null;
  };

  readonly periodic: {
    sync: boolean | null;
    syncTransfer: boolean | null;
    advertiserList: boolean | null;
  };

  readonly iso: {
    cis: boolean | null;
    bis: boolean | null;
    framed: boolean | null;
    unframed: boolean | null;
  };

  readonly directionFinding: {
    connectionCte: boolean | null;
    connectionlessCte: boolean | null;
    angleOfArrival: boolean | null;
    angleOfDeparture: boolean | null;
  };

  readonly limits: {
    maxConnections: number | null;
    maxMtu: number | null;
    maxAttributeBytes: number | null;
    maxServices: number | null;
    maxCharacteristics: number | null;
    maxDescriptors: number | null;
    maxAdvertisingSets: number | null;
    maxLegacyAdvertisingBytes: number | null;
    maxExtendedAdvertisingBytes: number | null;
    maxPeriodicAdvertisingBytes: number | null;
    maxPeriodicSyncs: number | null;
    maxFilterAcceptListEntries: number | null;
    maxBonds: number | null;
    maxL2capChannels: number | null;
    maxL2capMtu: number | null;
    maxIsoStreams: number | null;
  };
}
```

要求（本轮修订）：

- `ble.capabilities()` 不为了查询而初始化/启动 Controller 或 Host。probePhase=build 时，编译明确不存在的能力为 false/0；必须在线查询且尚未确定的能力或上限为 null。
- `adapter.capabilities()` 在 Host 同步且必要 Controller 查询完成后返回 probePhase=effective；已查询能力用有效布尔/上限，不把请求值当实际结果。
- 请求开启的 feature 必须在 open/对应操作前确定可用；必要 query 失败返回结构化错误，不猜 true。未实现功能不能因为 Host 宏存在就宣布可用。
- null 不是 unsupported，也不是无限容量；调用者先取得有效能力。只凭芯片名字不能确定 Extended/Periodic/ISO/CTE。
- `maxMtu` 为能力上限，不等于某连接的当前 MTU；effective 上限取 Host/Controller/build 与本框架实际分配能力的交集。
- GATT `reliableWrite` 表示客户端过程；`serverPreparedWrites` 与 `serverAtomicReliableWrite` 单独报告，避免把客户端能力误用为服务端多属性原子性。
- 本轮给能力字段增加 null，只用于上述两阶段探测；对应已准备好的 effective 能力不能无理由返回未知。

---

## 6. `ble.open()` 与 Adapter 生命周期

### 6.1 打开参数

```ts
type BLERole = "central" | "peripheral" | "observer" | "broadcaster";

type BLEIoCapability =
  | "display-only"
  | "display-yes-no"
  | "keyboard-only"
  | "no-input-no-output"
  | "keyboard-display";

type BLEOwnAddressPolicy =
  | "public"
  | "random-static"
  | "rpa-public-identity"
  | "rpa-random-identity"
  | "non-resolvable-random";

interface BLEPrivacyOptions {
  enabled: boolean;
  rpaTimeoutSeconds?: number;
  persistIdentity?: boolean;
}

interface BLESecurityOptions {
  bonding?: boolean;
  mitm?: boolean;
  secureConnections?: "required" | "preferred" | "disabled";
  allowLegacyPairing?: boolean;
  ioCapability?: BLEIoCapability;
  minKeySize?: number;
  maxKeySize?: number;
  repeatPairing?: "reject" | "replace";
  store?: "nvs" | "ram";
  keyDistribution?: {
    distributeEncryptionKey?: boolean;
    distributeIdentityKey?: boolean;
    distributeSigningKey?: boolean;
    requestEncryptionKey?: boolean;
    requestIdentityKey?: boolean;
    requestSigningKey?: boolean;
  };
}

interface BLEOpenOptions {
  roles?: BLERole[];
  deviceName?: string;
  appearance?: number;
  ownAddress?: BLEOwnAddressPolicy;
  privacy?: BLEPrivacyOptions;
  preferredMtu?: number;
  connectionLimit?: number;
  security?: BLESecurityOptions;
  gattServer?: BLEGattServerDefinition;
}
```

### 6.2 打开行为

`ble.open()` 必须依次：

1. 验证参数、feature、role、Kconfig 和打开前可确定的 target 条件；在线才能确定的能力不在此猜测；
2. 获取 BLE Controller/Host owner；
3. 初始化 NimBLE port；
4. 配置 Store、Security Manager、GAP Device Name、Appearance 和 GATT Database；
5. 启动 Host task；
6. 等待 `on_sync`；
7. 完成必要的 Controller 能力/上限查询，核验已请求功能；推导或创建 Identity Address；
8. 仅在所需能力、资源和身份均有效后返回 `BLEAdapter`。

任何阶段失败必须逆序回滚。若 Host stop/deinit 失败，保留准确资源后缀，Adapter
进入 cleanup-only faulted state，后续 `close()` 重试，不允许重新 `open()` 覆盖。

### 6.3 Adapter 状态

```ts
interface BLEAdapterStatus {
  generation: number;
  state: "opening" | "ready" | "resetting" | "closing" | "cleanup-pending" | "faulted" | "closed";
  synchronized: boolean;
  roles: BLERole[];
  identityAddress: BLEAddress | null;
  ownAddressPolicy: BLEOwnAddressPolicy;
  privacyEnabled: boolean;
  activeScanners: number;
  activeAdvertisers: number;
  activePeriodicSyncs: number;
  connections: number;
  l2capChannels: number;
  isoStreams: number;
  lastResetReason: number | null;
  lastError: NativeError | null;
}
```

---

## 7. `BLEAdapter`

```ts
class BLEAdapter {
  private constructor();

  capabilities(): BLECapabilities;
  status(): BLEAdapterStatus;
  watch(options?: BLEAdapterWatchOptions): EventQueue<BLEAdapterEvent>;

  scan(options?: BLEScanOptions): BLEScanner;
  advertise(options: BLEAdvertiseOptions): BLEAdvertiser;
  connect(peer: BLEPeerInput, options?: BLEConnectOptions): BLEConnection;

  syncPeriodic(options: BLEPeriodicSyncOptions): BLEPeriodicSync;
  listenL2cap(options: BLEL2capListenOptions): BLEL2capServer;

  server(): BLEGattServer | null;
  connections(): BLEConnection[];

  filterAcceptList(): BLEAddress[];
  setFilterAcceptList(peers: ArrayLike<BLEAddressInput>): BLEAddress[];
  clearFilterAcceptList(): boolean;

  bonds(): BLEBondInfo[];
  removeBond(peer: BLEAddressInput): boolean;
  clearBonds(): number;

  close(): boolean;

  readonly iso?: BLEIsoAPI;
  readonly directionFinding?: BLEDirectionFindingAPI;
}
```

### 7.1 Adapter 事件

```ts
type BLEAdapterEvent =
  | { type: "host-reset"; reason: number; timestampUs: number }
  | { type: "host-synchronized"; timestampUs: number }
  | { type: "identity-changed"; address: BLEAddress; timestampUs: number }
  | { type: "store-error"; operation: string; code: number; timestampUs: number }
  | { type: "resource-warning"; resource: string; used: number; limit: number };
```

Adapter watch 不复制 Connection 的高频 Notification，也不替代各资源自己的队列。

---

### 7.2 入站连接不能因事件丢失而失去 owner（本轮补充）

Adapter 的有界 connection registry 是连接的事实来源。NimBLE 连接成功时先登记原生 slot/generation 和必要控制状态，再投递 Advertiser 观察事件。JS wrapper 只在 runtime task 创建；不同观察入口得到的 wrapper 不能制造第二个独立 native connection owner。

`adapter.connections()` 返回当前连接的可操作 wrapper 快照，作为 Advertiser `connected` 事件丢失后的恢复入口。关闭 Advertiser 不关闭已建立连接；关闭 Adapter 会结束所有属于它的连接。

若无法为新入站连接分配必要原生状态，按明确拒绝/终止路径结束该连接并计数，不能把连接留在 Controller 内但不纳入 registry。maxConnections 为全 Adapter 总量，不是每个 Advertiser 各有一份上限。

---

## 8. 公共地址与 Peer 类型

```ts
type BLEAddressType =
  | "public"
  | "random"
  | "public-identity"
  | "random-identity";

interface BLEAddress {
  address: string;
  type: BLEAddressType;
}

interface BLEPeerIdentity {
  overTheAir: BLEAddress;
  identity: BLEAddress | null;
  resolved: boolean;
}

type BLEAddressInput = string | BLEAddress;
type BLEPeerInput = BLEAddressInput | BLEPeerIdentity;
```

规则：

- 字符串默认不猜地址类型；只有 `capabilities().addressing` 和 scan report 能提供
  正确类型。对 `connect("xx:...")`，默认使用 `"public"` 仅作为显式 API 约定，并在
  文档中警告随机地址设备应传对象。
- 地址统一输出小写冒号格式；
- RPA 解析后同时保留 OTA 地址和 Identity；
- Bond、Filter Accept List 和连接缓存使用 Identity，不使用可能变化的 RPA 作为键。

---

## 9. 扫描 API

### 9.1 打开扫描

```ts
type BLEPhy = "1m" | "2m" | "coded";

type BLEScanDuplicatePolicy =
  | "none"
  | "address"
  | "address-and-data"
  | "controller-default";

interface BLEScanPhyOptions {
  phy: "1m" | "coded";
  intervalMs: number;
  windowMs: number;
  active?: boolean;
}

interface BLEScanOptions {
  mode?: "auto" | "legacy" | "extended";
  active?: boolean;
  intervalMs?: number;
  windowMs?: number;
  phys?: BLEScanPhyOptions[];
  durationMs?: number;
  limited?: boolean;
  filterPolicy?: "all" | "accept-list-only";
  duplicates?: BLEScanDuplicatePolicy;
  reassembly?: "none" | "complete";
  maximumReassembledBytes?: number;
  minimumRssi?: number;
  capacity?: number;
  overflow?: "drop-newest";
}
```

`adapter.scan()` 立即启动扫描并返回 Scanner。Extended mode 的 1M/Coded 参数必须
分别验证。`reassembly: "complete"` 在 native 层使用有界表，按 advertiser、SID 和
事件属性合并 fragment；表满时丢弃新 reassembly 并计数，不进行 heap 无限增长。

### 9.1A 多逻辑 Scanner 的物理协调（本轮决策）

一个 Adapter 只有一个物理 discovery procedure。多个 Scanner 只是对其结果的有界订阅者，不是同时向 Controller 启动多个互不协调的扫描过程。

first Scanner 固定规范化硬件参数：legacy/extended、各 PHY 的 active/interval/window、own-address、accept-list policy/revision 和需要的硬件 duplicate 配置。后续 Scanner 仅在硬件参数兼容时加入；不兼容返回 `BLE_SCAN_CONFIG_CONFLICT` 并保持现有扫描不变。不自动取 interval 最小值/window 最大值，不停止旧 Scanner 来满足新请求。

RSSI、JS queue capacity、native subscriber filter 和逻辑 duration 可在各 Scanner 独立实施。一个 Scanner duration 到期只结束自己；最后一个 subscriber 退出时才取消物理扫描。`configure()` 仅可作用于 stopped 的逻辑 Scanner；restart 时重新申请相同规则的共享 procedure。

address/address-and-data 去重由有界 subscriber table 实现时，要确保硬件过滤没有提前丢掉它所需的数据；controller-default 的有效语义明确报告。表满、过期、AD fragment 超限都独立计数，不增长无限表。共享重组要么每 subscriber 有独立预算，要么使用受审查的共享 immutable payload，不能相互覆盖。

Extended fragment 的 key/终止/expiry/截断规则必须按固定 Host 报告定义审查；anonymous/缺地址报告不能按假 MAC 合并。重组时间窗口有上限，不把两个广播 event 的片段拼成一条虚构 report。

### 9.2 Scan Report

```ts
type BLEAdvertisingDataStatus = "complete" | "incomplete" | "truncated";

interface BLEScanReport {
  sequence: number;
  timestampUs: number;
  peer: BLEPeerIdentity;
  directAddress: BLEAddress | null;
  rssi: number;
  txPower: number | null;

  event: {
    connectable: boolean;
    scannable: boolean;
    directed: boolean;
    scanResponse: boolean;
    legacy: boolean;
  };

  primaryPhy: "1m" | "coded" | null;
  secondaryPhy: "1m" | "2m" | "coded" | null;
  sid: number | null;
  periodicIntervalMs: number | null;
  dataStatus: BLEAdvertisingDataStatus;
  data: ByteView;
}
```

保留 raw Advertising Data。结构化 AD 解析由 `ble.ad.decode()` 完成，不在 callback
中遍历所有 AD element。

### 9.3 Scanner Handle

```ts
interface BLEScannerStats {
  callbacks: number;
  accepted: number;
  deliveredReports: number;
  deliveredBatches: number;
  filteredRssi: number;
  filteredDuplicate: number;
  droppedPoolFull: number;
  droppedQueueFull: number;
  droppedReassemblyFull: number;
  droppedReassemblyTooLarge: number;
  invalidReports: number;
  queue: BLEQueueStats;
}

class BLEScanner {
  private constructor();

  status(): {
    state: "running" | "stopped" | "stopping" | "faulted" | "closed";
    requested: BLEScanOptions;
    effective: object;
  };

  receive(timeoutMs?: number): BLEScanReport | null;
  receiveBatch(options?: BLEReceiveBatchOptions): BLEScanBatch | null;
  stats(): BLEScannerStats;

  stop(): boolean;
  configure(options: BLEScanOptions): boolean;
  start(): boolean;
  close(): boolean;
}
```

`configure()` 仅在 stopped 状态可用。Batch 持有多个 pool lease，并提供
`info(index)`、`data(index)`、`source()` 和 `close()`。

---

## 10. Advertising Data 编解码

`ble.ad` 是随构建发布的官方纯 JS Library；底层 native 仍接收 raw bytes。Library 的选择、打包和预编译由产品/构建端生成不可变 Build Context，框架不解析 Library/Board/Agent manifest。提供 ble.ad 的 profile 必须确实包含其 bundle，不用文档声明代替 JS 实现。

```ts
interface BLEAdvertisingElement {
  type: number;
  name: string | null;
  data: ByteView;
}

interface BLEAdvertisingDataCodec {
  decode(data: ByteSource, options?: { strict?: boolean }): BLEAdvertisingElement[];
  encode(elements: ArrayLike<BLEAdvertisingElementInput>): ByteView;

  flags(value: number): BLEAdvertisingElementInput;
  completeName(value: string): BLEAdvertisingElementInput;
  shortName(value: string): BLEAdvertisingElementInput;
  serviceUuids(values: string[], complete?: boolean): BLEAdvertisingElementInput;
  serviceData(uuid: string, data: ByteSource): BLEAdvertisingElementInput;
  manufacturerData(companyId: number, data: ByteSource): BLEAdvertisingElementInput;
}
```

规则：

- `encode()` 检查单 element 长度和总长度；
- Legacy 31-byte 限制由 Advertiser 再次检查；
- decode 结果中的每个 ByteView 必须关闭；需要独立副本时使用框架现有明确的复制接口，不使用未在声明中定义的 copy option；
- 不内置厂商私有格式解释。

---

## 11. Advertising API

### 11.1 创建并启动

```ts
interface BLEAdvertiseOptions {
  mode?: "auto" | "legacy" | "extended";
  connectable?: boolean;
  scannable?: boolean;
  directed?: BLEAddressInput;
  highDutyDirected?: boolean;
  anonymous?: boolean;

  intervalMinMs?: number;
  intervalMaxMs?: number;
  channels?: Array<37 | 38 | 39>;
  filterPolicy?: "all" | "scan-accept-list" | "connect-accept-list" | "both-accept-list";

  ownAddress?: BLEOwnAddressPolicy;
  primaryPhy?: "1m" | "coded";
  secondaryPhy?: BLEPhy;
  sid?: number;
  txPowerDbm?: number | "auto";
  scanRequestNotifications?: boolean;

  data?: ByteSource;
  scanResponse?: ByteSource;
  durationMs?: number;
  maxEvents?: number;

  eventCapacity?: number;
}
```

`adapter.advertise()` 分配一个 Advertising Set、配置 data 并立即启动。Legacy 模式
限制 31 bytes；Extended 模式使用 capability 中的最大长度。

### 11.2 Advertiser 事件

```ts
type BLEAdvertiserEvent =
  | { type: "connected"; connection: BLEConnection; timestampUs: number }
  | { type: "scan-request"; peer: BLEPeerIdentity; timestampUs: number }
  | { type: "complete"; reason: "duration" | "max-events" | "stopped" | "error"; code: number }
  | { type: "terminated"; code: number; completedEvents: number };
```

### 11.3 Advertiser Handle

```ts
class BLEAdvertiser {
  private constructor();

  status(): BLEAdvertiserStatus;
  receive(timeoutMs?: number): BLEAdvertiserEvent | null;
  stats(): BLEAdvertiserStats;

  setData(data: ByteSource): number;
  setScanResponse(data: ByteSource): number;
  configure(options: BLEAdvertiseOptions): boolean;

  start(options?: { durationMs?: number; maxEvents?: number }): boolean;
  stop(): boolean;

  configurePeriodic(options: BLEPeriodicAdvertiseOptions): BLEPeriodicAdvertiser;
  close(): boolean;
}
```

Data update 不在 NimBLE callback 中进行。输入 ByteSource 在方法返回前复制或按明确 native ownership retain；调用者可依契约释放输入。连接归 Adapter registry 管理；关闭 Advertiser 不自动断开已有连接。

`mode:"auto"` 是显式选择规则，不是无条件降级：参数本身要求 Extended/2M/Coded/SID/超长 data 时，只能选择支持该组合的模式，否则报错。显式 mode 不可更改。connectable、scannable、anonymous、directed、PHY、Periodic 的组合按公开规范验证，不因每个字段单独支持就认定组合可用。

---

## 12. 连接 API

### 12.1 建立连接

```ts
interface BLEConnectionParameters {
  intervalMinMs: number;
  intervalMaxMs: number;
  latency: number;
  supervisionTimeoutMs: number;
  minCeLengthMs?: number;
  maxCeLengthMs?: number;
}

interface BLEConnectOptions {
  ownAddress?: BLEOwnAddressPolicy;
  timeoutMs?: number;
  filterPolicy?: "peer" | "accept-list";
  phy?: "1m" | "coded" | "auto";
  scanIntervalMs?: number;
  scanWindowMs?: number;
  parameters?: BLEConnectionParameters;

  autoExchangeMtu?: boolean;
  autoDataLength?: { txOctets?: number; txTimeUs?: number } | false;
  autoPhy?: BLEPhy[] | false;
}
```

`adapter.connect()` 成功后返回 Connection。失败/timeout 时取消本次 GAP connect procedure，并处理“取消同时连接成功”的竞态；无可交付 owner 的新连接必须终止。释放 operation 需要可靠的原生完成/停止屏障，不能只看一瞬间 callback active=0。

本轮删除 `autoPair`。连接先返回，调用者再显式发起 `connection.pair()`；需要用户输入时，必须并发运行该 Future 与 Pairing event 消费者。Cooperative yield 不会执行当前同步语句后面尚未到达的 JS 代码。

`autoExchangeMtu/autoDataLength/autoPhy` 仍是显式的连接后步骤；使用一个总 deadline 并报告失败 stage。若 connect 已建链但后续必需步骤失败，清理该次新连接，不能返回错误后遗留无 owner 的连接；cleanup 尚未完成时返回对应 pending 状态。

### 12.2 连接状态

```ts
interface BLEConnectionStatus {
  generation: number;
  state: "connecting" | "connected" | "disconnecting" | "faulted" | "closed";
  role: "central" | "peripheral";
  peer: BLEPeerIdentity;
  localAddress: BLEAddress;
  connectionHandle: number; // diagnostic only; not a reusable public identity

  parameters: BLEConnectionParameters;
  mtu: number;
  txPhy: BLEPhy | null;
  rxPhy: BLEPhy | null;
  dataLength: {
    txOctets: number | null;
    txTimeUs: number | null;
    rxOctets: number | null;
    rxTimeUs: number | null;
  };

  security: BLESecurityState;
  rssi: number | null;
  gattGeneration: number;
  activeNotificationStreams: number;
  activeL2capChannels: number;
  lastError: NativeError | null;
}
```

### 12.3 Connection 事件

```ts
type BLEConnectionEvent =
  | { type: "disconnected"; reason: number; timestampUs: number }
  | { type: "parameters-changed"; parameters: BLEConnectionParameters }
  | { type: "mtu-changed"; mtu: number }
  | { type: "phy-changed"; txPhy: BLEPhy; rxPhy: BLEPhy; status: number }
  | { type: "data-length-changed"; txOctets: number; rxOctets: number; txTimeUs: number; rxTimeUs: number }
  | { type: "encryption-changed"; security: BLESecurityState }
  | { type: "identity-resolved"; peer: BLEPeerIdentity }
  | BLEPairingRequestEvent;
```

### 12.4 Connection Handle

```ts
class BLEConnection {
  private constructor();

  status(): BLEConnectionStatus;
  receive(timeoutMs?: number): BLEConnectionEvent | null;
  stats(): BLEConnectionStats;

  updateParameters(parameters: BLEConnectionParameters, timeoutMs?: number): BLEConnectionStatus;
  setPhy(options: BLESetPhyOptions): BLEConnectionStatus;
  setDataLength(options: BLEDataLengthOptions): BLEConnectionStatus;
  exchangeMtu(timeoutMs?: number): number;
  readRssi(): number;

  pair(options?: BLEPairOptions): BLESecurityState;
  respondPairing(requestId: number, response: BLEPairingResponse): BLEPairingResponseResult;

  discover(options?: BLEGattDiscoverOptions): BLEGattDatabaseSnapshot;
  discoverServices(options?: BLEGattServiceDiscoverOptions): BLEGattServiceRecord[];
  discoverCharacteristics(options: BLEGattCharacteristicDiscoverOptions): BLEGattCharacteristicRecord[];
  discoverDescriptors(options: BLEGattDescriptorDiscoverOptions): BLEGattDescriptorRecord[];

  readHandle(handle: number, options?: BLEGattReadOptions): ByteView;
  readLongHandle(handle: number, options?: BLEGattLongReadOptions): ByteView;
  readMultiple(handles: ArrayLike<number>, options?: BLEGattReadOptions): ByteView;

  writeHandle(handle: number, data: ByteSource, options?: BLEGattWriteOptions): BLEGattWriteResult;
  writeLongHandle(handle: number, data: ByteSource, options?: BLEGattLongWriteOptions): BLEGattWriteResult;
  writeReliable(writes: ArrayLike<BLEReliableWritePart>, options?: { timeoutMs?: number }): BLEGattWriteResult;

  subscribeHandle(handle: number, options?: BLESubscribeOptions): BLENotificationStream;
  openL2cap(options: BLEL2capOpenOptions): BLEL2capChannel;

  close(reason?: number): boolean;
}
```

---

## 13. Link Layer 控制

### 13.1 PHY

```ts
interface BLESetPhyOptions {
  tx: BLEPhy[];
  rx: BLEPhy[];
  codedPreference?: "s2" | "s8" | "none";
  timeoutMs?: number;
}
```

- 2M/Coded capability必须检查；
- PHY update complete event 之前 Future 不完成；
- 对端拒绝或 Controller 不支持时返回错误，不把 requested 当 effective。

### 13.2 Data Length Extension

```ts
interface BLEDataLengthOptions {
  txOctets: number;
  txTimeUs?: number;
  timeoutMs?: number;
}
```

状态必须返回实际 tx/rx octets 和 time。Data Length 不等于 ATT MTU。

### 13.3 Connection Parameters

- Central 可直接发起 update；
- Peripheral 根据 Host/Controller 路径发送 L2CAP or LL request；
- 参数单位在 JS 中统一为毫秒，native 转换时进行可逆范围检查；
- 不接受会使 supervision timeout 违反规范关系的参数。

### 13.4 RSSI、TX Power、Path Loss、Subrating

可选能力直接放在 Connection 上：

```ts
connection.readRssi();
connection.readRemoteTxPower({ phy: "1m" });
connection.enablePathLossMonitoring(options);
connection.requestSubrate(options);
```

这些方法只有在 `capabilities().connection` 对应字段为 true 时存在或成功。

---

## 14. GATT Client

### 14.1 Discovery Snapshot

```ts
interface BLEGattServiceRecord {
  startHandle: number;
  endHandle: number;
  uuid: string;
  primary: boolean;
}

interface BLEGattCharacteristicRecord {
  definitionHandle: number;
  valueHandle: number;
  endHandle: number;
  uuid: string;
  properties: string[];
  serviceStartHandle: number;
}

interface BLEGattDescriptorRecord {
  handle: number;
  uuid: string;
  characteristicValueHandle: number;
}

interface BLEGattDatabaseSnapshot {
  generation: number;
  complete: boolean;
  services: BLEGattServiceRecord[];
  characteristics: BLEGattCharacteristicRecord[];
  descriptors: BLEGattDescriptorRecord[];
}
```

Snapshot 是值对象，不为每个 attribute 创建 native handle。Connection 的 Service
Changed、重新发现或重连会增加 `gattGeneration`；传入旧 snapshot 得到的 handle 时，
框架仍可操作当前连接上的数值 handle，但使用 generation-aware façade 时必须报 cache stale。不能声称一个孤立的 number 自带来源 generation；官方 JS helper 必须绑定 connection/gattGeneration。

### 14.2 Read

```ts
interface BLEGattReadOptions {
  timeoutMs?: number;
  maxBytes?: number;
}

interface BLEGattLongReadOptions extends BLEGattReadOptions {
  offset?: number;
}
```

- Read 结果合并 mbuf chain 后返回 ByteView；
- `maxBytes` 是硬限制，超出报 `BLE_GATT_VALUE_TOO_LARGE`，不静默截断；
- Long Read 每次 blob response 后检查取消和 deadline；
- ATT error 保留 request opcode、handle、offset 和 error code。

### 14.3 Write

```ts
type BLEGattWriteMode = "request" | "command";

interface BLEGattWriteOptions {
  mode?: BLEGattWriteMode;
  timeoutMs?: number;
}

interface BLEGattLongWriteOptions {
  offset?: number;
  timeoutMs?: number;
}

interface BLEReliableWritePart {
  handle: number;
  offset: number;
  data: ByteSource;
}

interface BLEGattWriteResult {
  handle: number;
  bytes: number;
  mode: "request" | "command" | "long" | "reliable";
  confirmed: boolean;
}
```

Write Command 的 `confirmed` 为 false；它只表示 Host 接受发送，不表示远端 attribute
处理成功。Reliable Write 必须比较 Prepare Write echo，任何不一致时 Execute Cancel。

### 14.4 ATT 并发

每条连接只允许一个主动 GATT procedure lane：

```text
Discovery
Read / Long Read / Read Multiple
Write Request / Long Write / Reliable Write
CCCD Write
MTU Exchange
```

Write Command 可通过单独有界 enqueue lane 优化，但不得破坏发送顺序和连接关闭语义。

---

## 15. Notification 与 Indication

### 15.1 Subscribe

```ts
interface BLESubscribeOptions {
  notify?: boolean;
  indicate?: boolean;
  cccdHandle?: number;
  capacity?: number;
  overflow?: "drop-newest";
  timeoutMs?: number;
}
```

当没有显式 `cccdHandle` 时，必须从当前 discovery snapshot 查找；没有 snapshot 或
匹配不唯一时报错，不猜 `valueHandle + 1`。

### 15.1A 唯一 CCCD owner（本轮决策）

v1 对同一 `(adapterGeneration, connectionGeneration, gattGeneration, valueHandle)` 仅允许一个 Notification Stream。重复订阅返回 `BLE_SUBSCRIPTION_EXISTS`，不创建会互相写 CCCD 的多个 owner。不同 characteristic 或不同 connection 可以并行订阅。

启用时先准备 RX route、队列/pool 和 owner，再执行 CCCD Write；写入完成与立即收到 notification 的竞态必须可处理。启用失败准确回滚。CCCD 由此 stream 持有期间，其他直接 `writeHandle` 对同一 CCCD 的冲突写入必须拒绝或通过同一 owner 协议，不能绕过。

Service Changed、断连和重连使旧订阅 generation 失效；对旧 gattGeneration 不向可能已重新分配的 handle 盲写 0。Bond 恢复的订阅状态同样进入原生登记，不能假定所有 CCCD 都由当前 JS stream 首次写入。

### 15.2 Stream

```ts
interface BLENotification {
  sequence: number;
  timestampUs: number;
  valueHandle: number;
  indication: boolean;
  data: ByteView;
}

class BLENotificationStream {
  private constructor();

  receive(timeoutMs?: number): BLENotification | null;
  receiveBatch(options?: BLEReceiveBatchOptions): BLENotificationBatch | null;
  stats(): BLENotificationStats;
  close(): boolean;
}
```

关闭顺序：

1. 停止向队列 admission；
2. 若连接/gattGeneration 仍有效且该 stream 仍独占其 CCCD owner，写 0 禁用；否则不向旧 handle 发送写入；
3. 等待活动 callback 退出；
4. 丢弃队列并归还 pool slot；
5. 释放 stream handle。

Indication ACK 由 NimBLE Host 处理；接收端 Stream 不让 JS 决定是否确认。若 JS 接收队列已满而 payload 被丢弃，对端仍可能收到协议确认，因此不能把 Indication ACK 描述为“JS 业务已处理”。要求业务可靠性时由上层协议另行确认。

---

## 16. GATT Server

### 16.1 静态定义

```ts
type BLEGattProperty =
  | "broadcast"
  | "read"
  | "write-without-response"
  | "write"
  | "notify"
  | "indicate"
  | "authenticated-signed-write"
  | "extended-properties";

type BLEGattAccessLevel =
  | "open"
  | "encrypted"
  | "authenticated"
  | "secure-connections"
  | "deny";

interface BLEGattPermissions {
  read?: BLEGattAccessLevel;
  write?: BLEGattAccessLevel;
  minKeySize?: number;
}

interface BLEGattDescriptorDefinition {
  id: string;
  uuid: string;
  permissions?: BLEGattPermissions;
  maxLength?: number;
  value?: ByteSource;
  writePolicy?: "cache-and-event" | "event-only" | "deny";
}

interface BLEGattCharacteristicDefinition {
  id: string;
  uuid: string;
  properties: BLEGattProperty[];
  permissions?: BLEGattPermissions;
  maxLength: number;
  value?: ByteSource;
  variableLength?: boolean;
  readPolicy?: "cached" | "deny";
  writePolicy?: "cache-and-event" | "event-only" | "deny";
  descriptors?: BLEGattDescriptorDefinition[];
}

interface BLEGattServiceDefinition {
  id: string;
  uuid: string;
  primary?: boolean;
  includes?: string[];
  characteristics: BLEGattCharacteristicDefinition[];
}

interface BLEGattServerDefinition {
  services: BLEGattServiceDefinition[];
  eventCapacity?: number;
  overflow?: "drop-newest";
}
```

### 16.2 同步 access、业务接纳与缓存规则（本轮修订）

NimBLE GATT access callback 必须同步返回。JS 不参与这个同步决定；所有安全级别、长度、offset、缓存和接纳能力都提前配置好。不能在 Host task 等待 JS、Future、文件或网络。

#### 16.2.1 两种写策略的成功含义不同

| 策略 | Write Request 返回成功的条件 | 队列满时 | 成功不代表什么 |
| --- | --- | --- | --- |
| `cache-and-event` | 安全/长度/offset 校验通过，native cache 已按该次写的提交规则更新 | cache 可成功更新，观察事件允许丢弃并统计；应用应可重新读取当前缓存 | 不代表 JS 已收到逐条通知或执行了业务 |
| `event-only` | 校验通过，主业务 inbox 已预留并提交 payload 与事件槽 | 拒绝该次 Request，返回对应、经过上游映射确认的 ATT 错误；不能先成功再丢弃 | 不代表 JS 已执行业务或状态已持久化 |
| `deny` | 不接受 | 立即按权限错误拒绝 | 不产生成功写入事件 |

这里的 inbox 是 `BLEGattServerDefinition.eventCapacity` 控制的**主接收队列**，由 `ble.open({gattServer})` 一并构造，调用 `server.receive()` 消费。它与 `server.watch()` 的观察订阅分离：watch 可以满、关闭或无人订阅，不改变 event-only 的业务接纳结果；观察副本不能被当成第二条业务执行队列。

event-only 主队列容量耗尽时使用 `reject-before-accept` 的原生语义；原设计的 `overflow:"drop-newest"` 仅描述可丢观察事件，不覆盖必须交付的 event-only 请求。这是固定行为，不再增加含糊的“成功但尽力交付”选项。

接纳路径为：

```text
校验连接 generation、安全、属性权限、数据长度/offset
    → 预留本次写需要的 payload + 主 inbox token
    → 将输入复制到 owned slot
    → 在同一 admission 协议内提交 token
    → 返回 ATT success
```

“预留 token”和“commit token”必须与 close 协调，不能在检查队列非满后，另一个线程先关闭队列，随后仍错误返回成功。预留成功不等于无限保证：Adapter/连接主动关闭后未消费的请求会被取消并计数；断电后的可靠执行需要业务层 request ID、持久化或 ACK 协议，不属于 GATT 原生成功。

对于 **Write Command**，协议没有对应的写响应。框架仍必须先校验/预留再交付，但容量不足时只能按原生支持路径拒收/丢弃并统计，不能声称向远端返回了 ATT Write Response。对命令需要可靠交付的业务，应采用 Request 或业务流控。禁止在背压时擅自断开所有连接。

建议分开统计 `writeRequestsAccepted`、`writeRequestsRejectedCapacity`、`writeCommandsDroppedCapacity`、`writeEventsDropped`、`writeCommandsCancelledOnClose`，避免一个 `droppedNewest` 掩盖完全不同的语义。

#### 16.2.2 cache、ByteView 与并发读

`readPolicy:"cached"` 只读取已提交的 native cache；`readPolicy:"deny"` 返回权限错误。`setValue()` 先验证并准备新值，再在短临界区替换可见值/长度/revision；所有失败都不能留下半写状态。

`value()` 返回的是有明确所有权的读取快照，而不是指向仍会被 Host callback 原地修改的裸缓存。可以复制或保留不可变的 cache version；采用版本保留时，旧版本同样计入退休存储预算。不能让另一个核心写 cache 时，JS 从同一地址读到混合版本。

长度较大的分配、复制、NVS 和 JS 转换不放在全局 critical section 内。小型最终状态切换可受锁保护；所有涉及 callback 可见指针的发布和回收都遵循第 24 节。

#### 16.2.3 Prepared / Long / Reliable Write 的条件性支持

**客户端 `writeReliable()` 的存在不证明服务端具备多属性原子事务。** 原附件给出了 `prepared`、`committed` 字段，但没有证明固定 ESP-NimBLE 的公开 access 接口在所有路径中提供 prepare/execute/cancel 的完整事务边界。因此 B-07 必须先做独立的 Host 行为验证。[R-BLE] [U-GATT]

验证项包括：公开 callback 能否识别普通写与准备写、是否有可靠 offset、Host 何时组合 mbuf、Execute Cancel 是否交给应用、多个属性是否可在任何 cache 修改前完成统一校验。只能使用公开 API，不读取 private ATT transaction struct，也不修改 vendored Host 来伪装标准能力。

若公开路径可以实现，服务端至少必须保证：

1. staging 由连接和事务 generation 拥有；限制 fragment 数量、累计字节、属性数和 deadline。Prepare 阶段不改变已提交 cache，也不交付 `committed:true` 的业务事件。
2. Execute Commit 之前校验全部 staging 数据，并为必须交付的最终业务消息预留容量。某项校验/资源分配失败时不报告整体成功。
3. Execute Cancel、超时、断连、Host reset、Adapter close 都释放 staging，不投递虚构的成功提交。
4. 声称多属性原子提交时，全部属性必须经过同一个可验证的提交协调点，不能按属性逐个 callback 写缓存后才在最后一个属性失败。
5. event-only 的事务提交可以使用一个有界 transaction envelope；不允许先向 JS 发布前半部分，再因队列满丢掉后半部分。

如果固定 Host 的公共 hook 无法提供上述某项保证，分别将 `serverPreparedWrites` 或 `serverAtomicReliableWrite` 保持未实现/不可用并附原因；保留已验证的普通写与客户端 Reliable Write。**实现尚未完成应记 contract-pending，不应随意改标 target-unsupported。** 不把“字段存在”“对端发送成功”或单属性测试通过作为多属性事务的证据。

公开 write event 只描述真实已接受/提交的数据。未经验证不能随意把 `prepared=true` 填进普通写事件；若未来提供 staging 诊断流，必须与业务提交事件分开。

---

### 16.3 Server 事件

```ts
type BLEGattServerEvent =
  | {
      type: "write";
      connection: BLEConnection;
      characteristicId: string | null;
      descriptorId: string | null;
      handle: number;
      offset: number;
      prepared: boolean; // true only when the audited Host path identifies it
      committed: boolean; // public write events report accepted/committed data, not speculative fragments
      data: ByteView;
      timestampUs: number;
    }
  | {
      type: "subscribe";
      connection: BLEConnection;
      characteristicId: string;
      notify: boolean;
      indicate: boolean;
      reason: string;
      timestampUs: number;
    };
```

### 16.4 Server Handle

```ts
class BLEGattServer {
  private constructor();

  status(): BLEGattServerStatus;
  receive(timeoutMs?: number): BLEGattServerEvent | null;
  watch(options?: { capacity?: number }): EventQueue<BLEGattServerEvent>;

  characteristic(id: string): BLELocalCharacteristic;
  descriptor(id: string): BLELocalDescriptor;

  serviceChanged(options?: {
    connection?: BLEConnection;
    startHandle?: number;
    endHandle?: number;
    timeoutMs?: number;
  }): boolean;
}

class BLELocalCharacteristic {
  private constructor();

  info(): object;
  value(): ByteView;
  setValue(data: ByteSource): number;
  subscribers(): Array<{
    connection: BLEConnection;
    notify: boolean;
    indicate: boolean;
  }>;

  notify(options?: {
    connection?: BLEConnection;
    data?: ByteSource;
  }): number;

  indicate(options: {
    connection: BLEConnection;
    data?: ByteSource;
    timeoutMs?: number;
  }): boolean;
}
```

Notify broadcast 只发给已订阅 notify 的连接。Indicate 必须指定连接，且每连接同一时刻只允许一个未确认 indication。

固定 NimBLE 的 notify-tx 事件中，`status=0` 表示命令发送状态，`BLE_HS_EDONE` 才表示 indication confirmation。[U-GAP] Indicate Future 不能在 status=0 时提前完成。timeout/disconnect 清理只影响对应连接/操作；late confirmation 不得完成下一条 indication。Notify 返回接纳的发送数，不代表远端应用已收到或处理。

---

## 17. Security、Pairing 与 Bond

### 17.1 安全状态

```ts
interface BLESecurityState {
  encrypted: boolean;
  authenticated: boolean;
  bonded: boolean;
  secureConnections: boolean;
  keySize: number | null;
}
```

### 17.2 Pairing

```ts
interface BLEPairOptions {
  timeoutMs?: number;
}

interface BLEPairingResponseResult {
  requestId: number;
  submitted: boolean;
}

type BLEPairingAction =
  | "display-passkey"
  | "input-passkey"
  | "numeric-comparison"
  | "oob"
  | "none";

interface BLEPairingRequestEvent {
  type: "pairing-request";
  requestId: number;
  action: BLEPairingAction;
  passkey: number | null;
  number: number | null;
  expiresAtUs: number;
}

type BLEPairingResponse =
  | { accept: false }
  | { accept: true }
  | { accept: true; passkey: number }
  | { accept: true; match: boolean }
  | { accept: true; oob: ByteSource };
```

- Pairing request 必须在 deadline 前响应；
- Request ID 与 Adapter/Connection generation 绑定；
- 超时默认拒绝；
- Passkey、LTK、IRK、CSRK 不写日志、不进入 status/error；
- `pair()` Future 可在等待用户输入时 cooperative yield，但事件消费者必须作为另一可运行任务/Future 已经启动；不能同步调用 pair 后才去 receive 同一次请求。
- `respondPairing()` 只表示输入已提交给原生流程，返回 BLEPairingResponseResult；最终是否配对成功由 pair Future/安全终态决定。
- Pairing request 从专用有界表按 requestId 保存到响应或过期。`connection.receive()` 必须优先检查仍待交付的有效输入请求，而不只读取可能已经丢通知的观察队列；同一请求不重复交付，JS 转换失败时保留可重试的原生交付状态。普通观察队列满不让该请求失去访问入口；无法接纳新请求时明确拒绝，而不是无限等待。

### 17.3 Bond Store

```ts
interface BLEBondInfo {
  peerIdentity: BLEAddress;
  ourIdentity: BLEAddress;
  authenticated: boolean;
  secureConnections: boolean;
  keySize: number;
  hasPeerIdentityKey: boolean;
  hasPeerSigningKey: boolean;
}
```

`bonds()` 不返回密钥材料。删除 Bond 时同时更新 Host Store、Resolving List 和缓存；
如果删除过程部分失败，保留 cleanup state 并可重试。

### 17.4 Repeat Pairing：同步决定与两阶段重新配对

固定 IDF v6.1 使用的 ESP-NimBLE 在 `BLE_GAP_EVENT_REPEAT_PAIRING` 回调返回时要求同步选择：删除冲突 Bond 后返回 `BLE_GAP_REPEAT_PAIRING_RETRY`，或返回 `BLE_GAP_REPEAT_PAIRING_IGNORE`。它不是可以保留 callback、稍后由 JS 恢复的普通 Passkey/Numeric Comparison 请求。[U-GAP]

因此正式 v1 **删除 `repeatPairing:"request"`**。传入该值直接按选项枚举错误拒绝，不保持旧别名、不自动解释为 replace。

| 配置 | 原生行为 | 必须满足的条件 |
| --- | --- | --- |
| `reject`（默认） | 按 Host 的 IGNORE 路径结束本次重复配对，可投递无秘密的被阻止观察事件 | 不删旧 Bond，不降低安全条件 |
| `replace` | 仅在显式允许时，按公开 Store API 删除冲突 Bond，确认成功后返回 RETRY | 固定 Host/Store 的同步路径可审查、可完成，错误不得伪装成功 |

`replace` 是应用预先授予的替换政策，不是收到事件后临时等待用户的决定。其 Store 调用上下文、持久化行为和最大阻塞风险必须单独评审；不能在 callback 内等待 JS、跨 lane worker 或需要该 Host 继续运行才能完成的 Future。若选用的 Store 无法安全实现同步删除，拒绝这个配置并说明原因，采用下述两阶段工作流。不得为了“继续配对”擦除整个 NVS 或清除无关 peer 的 Bond。

#### 17.4.1 需要用户确认时的正式流程

```text
收到重复配对 → native 拒绝/忽略当前尝试，保留旧 Bond
          → 应用得知 peer 需要重新授权（观察事件或应用流程）
          → 用户实际确认该 peer
          → 在正常 runtime/Store mutation 流程执行 adapter.removeBond(peerIdentity)
          → 删除结果确认；部分失败则处理 cleanup，不宣称成功
          → 显式发起新 pairing，或让对端重新发起
```

这是**一次新的配对过程**，不是恢复已经返回 IGNORE 的旧 callback。不保证同一连接上的任意对端都会自动重试；应用按有效连接状态选择重新发起或明确重新连接。不做隐式重连。

本轮不新增一个伪装可恢复原请求的 JS 方法。用户批准必须对应稳定 peer identity、当前连接/Adapter generation 和应用可见身份；不能只按会变化的 RPA 或过期 request ID 删除密钥。

### 17.5 交互配对的完成与响应通道

普通输入型 Pairing request 与 Repeat Pairing 分开实现。只有公开 Security Manager 明确支持的输入动作才允许后续 `respondPairing()`。

原生 request table 保存 requestId、Adapter/Connection generation、action、期限和响应状态；请求只能响应一次。过期、重复响应、错误 action、旧 connection 或不匹配的输入格式都拒绝。响应方法不得排在正在等待该响应的 pair Future 后面形成 lane 死锁；使用专门的安全输入通道。

`respondPairing()` 返回 `{requestId, submitted}`，只表示输入提交结果。`pair()` 的最终结果来自真实安全终态，不从“用户点击同意”或 `ble_sm_inject_io` 接纳输入推断已经 encrypted/bonded。

请求满载或无法提供要求的 IO 时必须 fail closed。Numeric Comparison 的 `match:true` 只能来自真实用户确认；不能在示例或无屏设备上自动接受。展示数字应进入明确的配对 UI，而不是默认记录到持久日志/遥测。

### 17.6 安全组合和 Store 的不可逆部分

`secureConnections:"required"`、MITM、IO capability、`allowLegacyPairing`、key size 和 key distribution 必须作为组合验证；矛盾选项不能静默降级。协议允许协商的参数应同时报告 requested 和 effective；不能把合法协商称为实现自动降低安全条件，也不能把未达到要求的终态当成功。

Bond 查询不输出 LTK/IRK/CSRK。删除 Bond 时按固定 public API 协调 Store、地址解析状态与框架缓存，并记录准确的已完成步骤。如果前一步已经不可逆地删除密钥，后一步失败，返回 partial/cleanup 状态，不声称可以无损回滚原 Bond。不要缓存秘密副本来“方便回滚”而没有独立的生命周期和清零规则。

原生秘密存储按框架安全分配/清零规则处理；不能声称 JS string、上游栈内部副本或闪存历史页均已被框架即时物理擦除。NVS 故障、容量不足、掉电恢复和重复配对的安全行为必须有专门测试。

---

## 18. Privacy 与 Filter Accept List

### 18.1 Privacy

- Identity 地址在 Adapter 生命周期内固定；
- RPA timeout 由 open options 设置；
- 解析到 Bond identity 后 report/connection 同时返回 OTA 地址和 identity；
- Privacy 关闭时不得仍以 RPA 形式发送；
- `persistIdentity` 决定 random-static identity 是否进入 NVS；
- 不向 JS 暴露 IRK 原始值。

### 18.2 Filter Accept List

```js
adapter.setFilterAcceptList([
  { address: "12:34:56:78:9a:bc", type: "public" }
]);
```

- 修改必须走 GAP resource lane；
- 正在使用 accept-list policy 的 scan/advertising/connect operation 时，若 Controller
  不允许更新则返回 busy，不隐式停止资源；
- 返回 effective list；
- 条目超过 Controller limit 时报 RangeError。

---

## 19. L2CAP Connection-Oriented Channels

### 19.1 Listener

```ts
interface BLEL2capListenOptions {
  psm: number;
  mtu: number;
  initialCredits?: number;
  security?: BLEGattAccessLevel;
  acceptCapacity?: number;
}

class BLEL2capServer {
  private constructor();

  status(): object;
  accept(timeoutMs?: number): BLEL2capChannel | null;
  stats(): object;
  close(): boolean;
}
```

### 19.2 Outbound Channel

```ts
interface BLEL2capOpenOptions {
  psm: number;
  mtu: number;
  initialCredits?: number;
  timeoutMs?: number;
  receiveCapacity?: number;
}

interface BLEL2capSdu {
  sequence: number;
  timestampUs: number;
  data: ByteView;
}

class BLEL2capChannel {
  private constructor();

  status(): {
    state: "connecting" | "open" | "closing" | "closed" | "faulted";
    localCid: number;
    remoteCid: number;
    psm: number;
    localMtu: number;
    peerMtu: number;
    localMps: number;
    peerMps: number;
    txCredits: number | null;
    rxCredits: number | null;
  };

  send(data: ByteSource, options?: { timeoutMs?: number }): number;
  enqueue(data: ByteSource): boolean;
  flush(timeoutMs?: number): boolean;
  receive(timeoutMs?: number): BLEL2capSdu | null;
  stats(): object;
  close(): boolean;
}
```

实现要求：

- L2CAP SDU 可能跨多个 mbuf fragment，native 层按 negotiated MTU 重组；
- 重组使用固定 slot 或明确上限；
- Credits/receive-ready 由 native 按真实可用 buffer 和 Host API 管理；应用仍持有全部接收 lease 时不得无条件补足 credit 水位。credit 不简单等于 SDU slot 数，预算必须包含协商 MTU/MPS、PDU 分片和已授出的在途接收责任；
- peer 关闭、连接断开和 Adapter 关闭必须唤醒所有 pending send/receive；
- L2CAP 数据不经过 GATT procedure lane。
- Host 未公开可靠的 credit 数值/控制入口时，status 相应字段返回 null，配置仅保留有证据可实现的语义；不得调用 private struct/Raw HCI 来凑字段。
- Listener 在原生层预先验证 PSM、安全和容量并决定 accept/reject；不能在同步 callback 中等 JS 调用 accept()。已接纳 Channel 进入有界原生表，队列满不能让 Channel 失去 owner。
- `send()`/enqueue/flush 说明 Host 接纳或发送推进，不承诺远端业务处理；peer close、credit stall timeout 和 Adapter close 必须有明确终态。

---

## 20. Periodic Advertising 与 Periodic Sync

### 20.1 Periodic Advertiser

```ts
interface BLEPeriodicAdvertiseOptions {
  intervalMinMs: number;
  intervalMaxMs: number;
  includeTxPower?: boolean;
  data?: ByteSource;
}

class BLEPeriodicAdvertiser {
  private constructor();

  status(): object;
  setData(data: ByteSource): number;
  start(): boolean;
  stop(): boolean;
  close(): boolean;
}
```

必须依附于 non-connectable、non-scannable Extended Advertising Set，并保持 Set 的
SID/Address 一致性。

### 20.2 Periodic Sync

```ts
interface BLEPeriodicSyncOptions {
  peer: BLEAddressInput;
  sid: number;
  skip?: number;
  timeoutMs: number;
  usePeriodicAdvertiserList?: boolean;
  reportCapacity?: number;
}

interface BLEPeriodicReport {
  sequence: number;
  timestampUs: number;
  txPower: number | null;
  rssi: number;
  dataStatus: BLEAdvertisingDataStatus;
  data: ByteView;
}

class BLEPeriodicSync {
  private constructor();

  status(): object;
  receive(timeoutMs?: number): BLEPeriodicReport | null;
  transfer(connection: BLEConnection, options?: object): boolean;
  stats(): object;
  close(): boolean;
}
```

### 20.3 Periodic Advertiser List

Adapter 提供：

```ts
adapter.setPeriodicAdvertiserList(entries);
adapter.periodicAdvertiserList();
adapter.clearPeriodicAdvertiserList();
```

仅在 Controller 支持时出现。

---

## 21. PAwR

PAwR 是 Periodic Advertising 的高级可选能力，不能硬塞进普通 Advertiser event。

建议 API：

```ts
interface BLEPawrAdvertiser {
  setSubeventData(subevents: ArrayLike<BLEPawrSubeventData>): boolean;
  receiveResponse(timeoutMs?: number): BLEPawrResponse | null;
  start(): boolean;
  stop(): boolean;
  close(): boolean;
}

interface BLEPawrSync {
  setResponseData(options: BLEPawrResponseData): boolean;
  receive(timeoutMs?: number): BLEPawrSubeventReport | null;
  close(): boolean;
}
```

PAwR callback 高频且时间窗口严格：

- subevent data 必须预先放入 native buffer；
- JS 不能在即将发送的 Controller deadline 内生成 response；
- JS 只能为未来 event 预装数据；
- 错过窗口计入 `missedDeadline`，不延迟发送到下一个未声明窗口。

---

## 22. ISO、方向测量和 Mesh 边界

### 22.1 可选 `adapter.iso`

```ts
interface BLEIsoAPI {
  capabilities(): BLEIsoCapabilities;
  createCig(options: BLECigOptions): BLECig;
  createBig(advertiser: BLEPeriodicAdvertiser, options: BLEBigOptions): BLEBigBroadcaster;
  syncBig(periodicSync: BLEPeriodicSync, options: BLEBigSyncOptions): BLEBigSync;
}

interface BLEIsoSdu {
  sequence: number;
  timestampUs: number;
  packetStatus: "valid" | "possibly-invalid" | "lost";
  data: ByteView;
}

class BLEIsoStream {
  send(data: ByteSource, options?: { timestampUs?: number; timeoutMs?: number }): boolean;
  receive(timeoutMs?: number): BLEIsoSdu | null;
  stats(): object;
  close(): boolean;
}
```

ISO 必须使用专用 Controller buffer accounting，不复用普通 Notification pool。本文只
定义 transport primitive，不定义 LC3 codec 或 LE Audio Profile。

### 22.2 可选 `adapter.directionFinding`

```ts
interface BLEDirectionFindingAPI {
  configureConnectionCte(connection: BLEConnection, options: object): BLECteSession;
  configurePeriodicCte(advertiser: BLEPeriodicAdvertiser, options: object): BLECteTransmitter;
  syncCte(periodicSync: BLEPeriodicSync, options: object): BLEIqReportStream;
}
```

IQ sample 使用 pool-backed ByteView/typed packed format，并携带 antenna ID、slot
Duration、RSSI、channel index、packet status 和 timestamp。框架不在固件内计算角度。

### 22.3 BLE Mesh

`ESP-BLE-MESH` 不放进 `BLEAdapter`：

```text
ble          通用 GAP/GATT/L2CAP/ISO Host API
bleMesh      独立 Mesh Provisioning/Model API
```

原因：Mesh 拥有独立 provisioning、key store、model、bearer、event 和 Host 配置，
将其伪装成一个 GATT Profile 会破坏资源 owner。映射表将相关 API 标为
`separate-stack`。

---

## 23. 错误模型

所有可恢复 native operational failure 使用：

```ts
interface BLEError extends NativeError {
  code: BLEErrorCode;
  operation: string;
  details: {
    hostCode?: number;
    hciStatus?: number;
    attError?: number;
    smError?: number;
    connectionHandle?: number;
    attributeHandle?: number;
    advertisingInstance?: number;
    stage?: string;
  };
}
```

建议错误码：

```text
BLE_NOT_COMPILED
BLE_NOT_SUPPORTED
BLE_ALREADY_OPEN
BLE_NOT_OPEN
BLE_HOST_SYNC_TIMEOUT
BLE_HOST_RESET
BLE_ROLE_NOT_COMPILED
BLE_RESOURCE_EXHAUSTED
BLE_BUSY
BLE_STALE_HANDLE
BLE_INVALID_STATE
BLE_SCAN_FAILED
BLE_ADVERTISE_FAILED
BLE_ADVERTISING_SET_LIMIT
BLE_CONNECT_TIMEOUT
BLE_CONNECT_FAILED
BLE_DISCONNECTED
BLE_PHY_UNSUPPORTED
BLE_DATA_LENGTH_UNSUPPORTED
BLE_GATT_BUSY
BLE_GATT_CACHE_STALE
BLE_GATT_ATT_ERROR
BLE_GATT_VALUE_TOO_LARGE
BLE_GATT_WRITE_FAILED
BLE_SUBSCRIBE_FAILED
BLE_INDICATION_TIMEOUT
BLE_SECURITY_REQUIRED
BLE_PAIRING_FAILED
BLE_PAIRING_EXPIRED
BLE_BOND_STORE_ERROR
BLE_PRIVACY_ERROR
BLE_L2CAP_CONNECT_FAILED
BLE_L2CAP_CLOSED
BLE_L2CAP_MTU_EXCEEDED
BLE_PERIODIC_SYNC_FAILED
BLE_ISO_ERROR
BLE_QUEUE_FULL
BLE_CLEANUP_PENDING
BLE_DRIVER_ERROR
```

参数类型、未知字段、整数范围和本地 schema 错误仍使用 `TypeError`/`RangeError`，
不包装成 BLEError。

---

## 24. 并发、终态与三层生命周期

本节替换原设计中“等待 callback active=0 后就释放队列，再停止 Host”的笼统顺序。基线已经有 Host stop/deinit 与 callback 引用保护，重构必须保留并验证这些机制。[R-CONCURRENCY]

### 24.1 唯一 owner 与 lane

一个 Runtime 只有一个 NimBLE Host owner。控制器句柄、Advertising instance、Connection slot、requestId 都是可重用数值；只有与 Adapter/resource generation 和原生对象身份共同使用时才构成有效路由。

| lane / 管理域 | 负责内容 | 不得发生 |
| --- | --- | --- |
| Host lifecycle | open、sync、reset、stop/deinit、cleanup retry | 旧 Host 未可靠停止就覆盖 owner 或 reopen |
| GAP control | scan manager、advertise set、connect/cancel 的控制变更 | 持有全局 lane 等待整个扫描存续期，阻塞 stop 或其他允许并行的资源 |
| Per-connection ATT | discovery/read/write/CCCD/MTU 的主动 procedure | 同一 legacy ATT bearer 上重叠发起不兼容过程 |
| Per-connection indication | 一个待确认 indication 及其 timeout/late-event 隔离 | 一条连接的确认误完成另一连接/下一次发送 |
| Pairing input | 对既有 requestId 注入有效用户输入 | 排在等待输入的 pair operation 后造成自锁 |
| Store / privacy mutation | Bond、identity、filter/resolve 状态变更 | 和 GAP policy 修改发生无保护竞争，或 callback 等待自身依赖的 worker |
| Per-channel L2CAP | 有界发送、接收容量与 Host receive-ready | 无实际空间仍无限授予接收责任 |
| Per-stream ISO | 已配置时限内的发送和专用 buffer accounting | 与普通通知共用无上限队列 |

lane 控制提交顺序，不等于持有 mutex 等到无线操作完成。需要长期 operation reservation 的路径将 reservation 与控制命令锁分离。对端断连、Host reset、timeout、cancel 必须能结束对应 reservation；每条排队队列都有上限和 deadline 语义。

锁顺序、字段 writer/reader 和 callback-visible 生命周期写入 `docs/wireless-concurrency.md` 的 ownership 表。共享字段使用 C11 atomic 或明确锁域，不用 `volatile` 替代同步。driver/NimBLE 调用、等待、JS 转换不发生在全局 critical section 内。

### 24.2 callback entry 与 storage 的不同生命期

callback 只能：读取受保护的原生状态，复制或依法转移 mbuf 到受控 owner，更新原生完成状态，发布固定事件，唤醒已存在的 Future。不创建 JSValue、不读取 JS heap、不调用 JS、不做无界分配、不等待应用业务逻辑。

必须区分四个条件：

```text
公开 Future 已超时
callback 此刻 active=0
上游已保证该操作不会再产生 callback
已有用户 ByteView/Source 已全部释放
```

它们不是同一件事。callback 的第一个动作若要增加计数，承载这个计数的 context 本身就必须仍然有效；不能把可被并发释放的对象指针当作保护入口。

采用稳定的 Runtime/Host broker context 和有界操作槽；在 callback 进入时先通过仍存活的 registry 获取引用并验证 generation。取消后的 operation 只有在取得与固定 Host 公共 API 相匹配的最终完成/停止屏障后才可重用。关闭状态仍允许处理必要的终态，但拒绝新的普通数据 admission。

`adapterGeneration`、`connectionGeneration`、`gattGeneration` 和 operation sequence 不混用。给新操作换一个 generation 并不能凭空让上游不带 request cookie 的迟到事件带上正确身份。存在歧义时保持 tombstone/quarantine，或者执行该资源范围内受审查的终止，不把旧事件分配给“当前 active operation”。

### 24.3 控制状态与观察事件分离

| 数据类型 | 原生事实来源 | 公开投递规则 |
| --- | --- | --- |
| connect、disconnect、GATT 完成、PHY/MTU/加密状态 | 已分配的 operation/connection scalar state | 先更新事实和 Future，再投递观察副本 |
| incoming connection | Adapter connection registry | connected 事件可丢，但 `connections()` 能恢复访问 |
| pairing input request | 有界 security request table | 保存到响应/过期；无法接纳则拒绝 |
| event-only GATT Write Request | 预先分配的 Server 主业务 inbox | 容量不足在协议接纳之前拒绝 |
| Scan、Notification、Periodic、可选 IQ/ISO 数据 | 各自的有界 pool/queue | 明确 drop/invalid/lost 统计，不伪造零丢包 |
| diagnostics/watch | detached 原生快照/观察副本 | 允许丢失，有 counter 与状态查询补充 |

资源接纳时就必须为该资源的终态保留足够原生状态，不等收到 disconnect 才临时分配“可靠事件”。若采用控制 mailbox，大小必须与已接纳资源上限推导一致，并支持合并可重建的状态快照；不能用另一个会静默溢出的队列冒充可靠控制面。

### 24.4 Adapter 关闭顺序

1. 将生命周期改为 closing，禁止新子资源和普通操作；保留 status、close retry 与必要的原生完成通道。
2. 关闭数据 admission，停止/取消扫描、广播、Periodic 和正在建立的连接；按资源层级结束 ISO/L2CAP/活动连接。任何失败记录确切 stage 和仍可能回调的资源。
3. 处理“取消同时成功”、pending Future、queued ATT/发送和 Pairing request；对没有交付给应用但已建立的资源执行终止，不能遗留孤儿连接。
4. 按固定 port 的公开流程停止 Host，并等待确实不会再访问 callback registry/pool 的屏障；再完成 port/controller owner 的 deinit。**active count 一次为零不能替代这一步。**
5. 已停止的 source 可提前 discard 其队列，但底层 queue/pool/control storage 不得在 callback 仍可能到达时物理释放；drop、Future destruction、conversion failure 共用 exactly-once release 路径。
6. 全部必须的停止/注销/deinit 成功且引用安全后，释放 callback-visible 控制资源、queue retain、operation storage 和 lane。
7. 公开 handle 进入 closed。仍被已交付 ByteView/Source 持有的不可变数据进入 retired storage；最后一个数据 lease 释放后再回收。

具体 Host/Controller API 的调用顺序与所有权以固定 port 为准，不额外调用不属于当前 owner 的 controller deinit。每一个可能失败的阶段保存 completion bit/准确资源后缀，close retry 只继续未完成步骤，不重复 free 已释放资源。

deadline 到期但物理清理未完成时，返回 `BLE_CLEANUP_PENDING`，状态保持 cleanup-pending/closing；禁止 reopen。不得假装 close 成功，也不得通过无期限阻塞 JS/finalizer 来掩盖问题。清理 worker 使用独立的原生 retain，不能依赖已经销毁的 JSContext；需要 JS 转换的最终动作只交给仍有效的 runtime task。

### 24.5 操作关闭与已交付数据分离

**停止 Host 不能等待应用未来才会执行的 `view.close()`。** 数据 lease 只维持存储有效，不维持无线连接、controller 或完整旧 Adapter 对象。

```text
操作层：拒绝新请求 → 结束原生操作 → 操作 handle closed
控制层：可靠终止屏障 → callback/queue/Future storage 可回收
数据层：已交付 immutable bytes → 最后 view/source close → 退休池回收
```

公开 close 成功表示该 handle 不再占用应被停止的无线/控制资源；不表示历史已交付字节必然已经释放。retired pool/旧 cache version 使用全局配额，持续保留旧 view 后 reopen 最终应明确返回资源耗尽，而不是无界分配。

数据保留不得依赖旧 JS object 地址。Runtime 销毁时，旧 JS 引用本身不再可用；仍存在的跨模块 native source retain 按其真实生命周期清理，不能访问已失效 JS heap。

### 24.6 逐操作 timeout 与取消契约

下表是实现要求；每一行要在固定 Host 下补上具体 API/终态证据。现有 BLE 某些已开始 GATT/安全过程在超时后终止连接，不能在机械拆分中随意删除这项保护。[R-BLE]

| 操作 | 公开等待结束后 | 可复用条件 |
| --- | --- | --- |
| 尚未发起的排队操作 | 取消排队、释放自身输入和预算 | 确认未向 Host 提交 |
| Scan/Advertiser stop | 停止该 owner 或释放逻辑 subscriber；不影响其他共享者 | 物理 source 的停止规则已满足 |
| Connect | 取消；若同时建链则终止这次未交付连接 | final connect/cancel/terminate 路径和 registry 结清 |
| ATT read/write/discovery/MTU | 优先按公开 API 取消；无法安全停止时终止本连接并报告副作用 | 不再收到旧 procedure callback，或本连接已可靠终止 |
| PHY/DLE/参数更新 | 保留原生实际状态与 late completion 路由，不把 requested 当成功 | 对应过程终态确认；不能简单让下一操作接管其完成事件 |
| Pairing | 拒绝/过期当前输入、按 SM 原生路径结束；必要时终止本连接 | 旧 requestId 与安全操作失效、late event 不混入新流程 |
| Indication | 等待确认终态；超时按 Host 要求处理该连接/确认 lane | 旧 confirmation 不会命中新 indication |
| L2CAP send/open | 取消本 channel 内的排队/建立动作；已交给 Host 的 mbuf 按实际 owner 清理 | peer close/失败/Host 接管状态明确，不重复释放 |
| Adapter close | 可返回 cleanup-pending，由可重试原生 owner 继续清理 | 全部关键控制资源可靠结束后才允许 reopen |

所有结果/错误区分 public timeout、operation outcome unknown、connection terminated 和 cleanup pending。不要用同一个 `BLE_TIMEOUT` 掩盖不同后续可用性。

### 24.7 Host reset

Host reset 立即阻止旧 generation 的新操作并完成/取消相关 pending Future。旧连接、订阅和 GATT cache 不能仅因 Host 再次同步而自动变成有效。

Host 由其公开流程重新同步与应用自动重连是不同事情。框架可如实报告 reset/synchronized，但不重建业务连接、不重新配对、不无声替换 identity。只有新的 generation 和明确的资源登记才可接纳后续操作；旧 view 继续按数据层规则处理。

---

## 25. 内存、背压与复用现有资源框架

### 25.1 原生资源域

保持原方案的独立 pool 方向：scan report、notification、server write、pairing request、advertiser/connection control、periodic report、L2CAP SDU、ISO SDU。高流量扫描不能抢走已接纳连接必须的控制状态。

不是每增加一个 C 文件就创建一个新的全局 allocator。复用现有 framework memory ownership 和 resource helper，明确 allocation owner、capability、release path 和总预算；若新增 helper，必须服务多个真实调用点并以生产路径测试。

### 25.2 总预算而不只是单队列上限

```text
BLE 总预留 = Host/Controller 已知配置成本
           + Adapter/control/request 固定资源
           + 所有启用 data pool 的 payload + metadata
           + GATT cache / staging / discovery upper bounds
           + 排队 operation 输入副本
           + retired ByteView/Source/cache-version storage
           + 经测量确定的安全余量
```

未知或由上游动态使用的开销不能填成零。记录每种 Build Context 的编译估算与上板 peak/current；Wi-Fi、CSI、ESP-NOW、TLS 与 BLE 使用第二份文档规定的统一预算口径。设备无 PSRAM 时，advanced feature 不能只因 SOC 宏为 true 就忽略 SRAM 总量。

建立 low-memory、standard、capture/throughput 等配置方案时，具体值由构建上下文和硬件证据确定，不在 runtime 根据板名猜测。本文不给未经测量的“所有芯片通用默认槽数”。

### 25.3 分配和生命周期规则

callback-visible metadata、控制表和 queue storage 默认 internal RAM。大 payload 放 PSRAM 需明确 capability 与 callback copy 性能验证；不把“CPU 可访问”视为已证明适合所有 callback/缓存状态。

option parsing、长度乘加、预留预算、构造 pool 和注册前置状态都在更改无线状态前完成；失败逆序回滚。对 upstream 已取得 ownership 的 mbuf，不按普通借用指针再次 free；对借用 mbuf，不在 callback 后保留其地址。每个 NimBLE 调用都要记录成功、失败、取消时的 mbuf ownership。

`queueCapacity <= poolCapacity` 只是最小关系；还要为在途 callback、已交付 view、Batch、重组和 pending conversion 计算最大占用。slot 饱和时按该 source 的声明拒绝/丢弃，而不是借用别的 source 的控制保留区。

### 25.4 L2CAP、Periodic 与 ISO 的专项背压

L2CAP credit/receive-ready 与真实可接收字节和已授出责任绑定。应用长时间保留全部 SDU view 时，native 不能继续无限补充接收能力。MTU/MPS/PDU 分片边界按固定 Host 处理；无法从公开 API 取得的内部 credit 数值不伪造。

Periodic fragment 重组使用有界 key/期限/长度，不占用其他 connection 的数据池。PAwR 为未来窗口预装数据；错过窗口按原方案记录 missedDeadline，不在同步 callback 等 JS。ISO 保留专用 buffer accounting 和 loss/status，不能套用“普通通知无论如何重试”的策略。

### 25.5 统计与泄漏验收

至少记录 current internal/PSRAM free、largest block、历史 minimum-free、active operation/resource 数、每 pool free/leased/queued、retired bytes、allocation rejects 和 callback refs。

预热后在**相同静止状态**比较 current heap 与 owner/slot 账本；所有测试 view 释放后必须恢复定义基线，允许的长期缓存须有明确上限和解释。历史 minimum-free 是压力指标，本来只能保持或下降，不能据此直接判断泄漏。largest block 单独观察碎片趋势，不假设一次回收必然恢复完全相同的地址布局。

---

## 26. Kconfig 设计

```text
CONFIG_ESP32_MQUICKJS_FEATURE_BLE
CONFIG_ESP32_MQUICKJS_BLE_EXTENDED_ADVERTISING
CONFIG_ESP32_MQUICKJS_BLE_PERIODIC_ADVERTISING
CONFIG_ESP32_MQUICKJS_BLE_PAWR
CONFIG_ESP32_MQUICKJS_BLE_L2CAP_COC
CONFIG_ESP32_MQUICKJS_BLE_PRIVACY
CONFIG_ESP32_MQUICKJS_BLE_BONDING
CONFIG_ESP32_MQUICKJS_BLE_ISO
CONFIG_ESP32_MQUICKJS_BLE_DIRECTION_FINDING

CONFIG_ESP32_MQUICKJS_BLE_MAX_CONNECTIONS
CONFIG_ESP32_MQUICKJS_BLE_MAX_ADVERTISING_SETS
CONFIG_ESP32_MQUICKJS_BLE_MAX_PERIODIC_SYNCS
CONFIG_ESP32_MQUICKJS_BLE_MAX_L2CAP_CHANNELS
CONFIG_ESP32_MQUICKJS_BLE_MAX_ISO_STREAMS

CONFIG_ESP32_MQUICKJS_BLE_SCAN_POOL_CAPACITY
CONFIG_ESP32_MQUICKJS_BLE_SCAN_MAX_DATA_BYTES
CONFIG_ESP32_MQUICKJS_BLE_NOTIFICATION_POOL_CAPACITY
CONFIG_ESP32_MQUICKJS_BLE_NOTIFICATION_MAX_BYTES
CONFIG_ESP32_MQUICKJS_BLE_SERVER_EVENT_POOL_CAPACITY
CONFIG_ESP32_MQUICKJS_BLE_L2CAP_POOL_CAPACITY
CONFIG_ESP32_MQUICKJS_BLE_L2CAP_MAX_SDU_BYTES
CONFIG_ESP32_MQUICKJS_BLE_ISO_POOL_CAPACITY
CONFIG_ESP32_MQUICKJS_BLE_ISO_MAX_SDU_BYTES

CONFIG_ESP32_MQUICKJS_BLE_MAX_SERVICES
CONFIG_ESP32_MQUICKJS_BLE_MAX_CHARACTERISTICS
CONFIG_ESP32_MQUICKJS_BLE_MAX_DESCRIPTORS
CONFIG_ESP32_MQUICKJS_BLE_MAX_ATTRIBUTE_BYTES
CONFIG_ESP32_MQUICKJS_BLE_HOST_TASK_STACK_SIZE
```

依赖规则：

- 所有 feature 依赖 `SOC_BLE_SUPPORTED`、Controller 和 NimBLE Host；
- Extended/Periodic/PAwR/ISO/Direction Finding 同时依赖对应 Controller feature；
- `BLE_MAX_CONNECTIONS` 不超过 Controller/NimBLE 编译上限；
- queue capacity 不超过 pool capacity；
- `maxAttributeBytes`、L2CAP MTU、Scan max data 和 ISO SDU 均有硬上限；
- Wi-Fi + BLE 的正式 Build Context 显式启用经验证的 coexistence 配置并由构建检查校验；不在运行中猜板卡配置，不保证所有负载下无性能影响。

---

## 27. Native 文件拆分

用户原方案记录基线 BLE 核心集中在约 292 KB 的 `esp32_mquickjs_ble.c` 中。拆分以职责/owner 和实际依赖为准，不把文件大小当质量指标；完整扩展前先做可验证的机械拆分：

```text
components/esp32_mquickjs/src/modules/ble/
  esp32_mquickjs_ble_module.c
  esp32_mquickjs_ble_runtime.c
  esp32_mquickjs_ble_capabilities.c
  esp32_mquickjs_ble_gap.c
  esp32_mquickjs_ble_scan_manager.c
  esp32_mquickjs_ble_scan.c
  esp32_mquickjs_ble_advertise.c
  esp32_mquickjs_ble_connection.c
  esp32_mquickjs_ble_link.c
  esp32_mquickjs_ble_gatt_client.c
  esp32_mquickjs_ble_subscription.c
  esp32_mquickjs_ble_gatt_server.c
  esp32_mquickjs_ble_security.c
  esp32_mquickjs_ble_store.c
  esp32_mquickjs_ble_periodic.c
  esp32_mquickjs_ble_l2cap.c
  esp32_mquickjs_ble_iso.c
  esp32_mquickjs_ble_direction_finding.c
  esp32_mquickjs_ble_resources.c
  esp32_mquickjs_ble_control_mailbox.c
  esp32_mquickjs_ble_errors.c
```

内部头文件：

```text
components/esp32_mquickjs/internal/
  esp32_mquickjs_ble_runtime.h
  esp32_mquickjs_ble_gap.h
  esp32_mquickjs_ble_connection.h
  esp32_mquickjs_ble_resources.h
  esp32_mquickjs_ble_gatt_database.h
  esp32_mquickjs_ble_security.h
```

拆分原则：

- 不先改 public behavior，只做可验证的机械拆分；
- 全局 `s_ble` 拆成一个明确 runtime owner，而不是每文件复制 singleton；
- callback registry 和 generation 仍有唯一来源；
- 每次拆分后运行 host C test、Python architecture test 和三 target build。

---

## 28. ESP-NimBLE API 覆盖映射

### 28.1 Header allowlist

至少扫描：

```text
host/ble_gap.h
host/ble_gatt.h
host/ble_gattc.h
host/ble_gatts.h
host/ble_hs.h
host/ble_l2cap.h
host/ble_sm.h
host/ble_store.h
host/ble_uuid.h
services/gap/ble_svc_gap.h
services/gatt/ble_svc_gatt.h
ESP-IDF NimBLE port/controller public headers
ESP-IDF ISO public headers（feature enabled 时）
```

### 28.2 目标映射示例

本表仅说明 disposition，不表示 implementation 已完成。实际 implementation/contract/validation 分别记录在 API map 中。

| ESP-NimBLE 能力 | JS API | 状态 |
|---|---|---|
| Host init/sync/stop | `ble.open()` / `adapter.close()` | framework-owned |
| Legacy discovery | `adapter.scan({mode:"legacy"})` | mapped |
| Extended discovery | `adapter.scan({mode:"extended"})` | mapped |
| Legacy advertising | `adapter.advertise({mode:"legacy"})` | mapped |
| Extended advertising set | `adapter.advertise({mode:"extended"})` | mapped |
| Periodic advertising | `advertiser.configurePeriodic()` | mapped |
| Connect/cancel/terminate | `adapter.connect()` / `connection.close()` | mapped |
| Connection update | `connection.updateParameters()` | mapped |
| PHY update | `connection.setPhy()` | mapped |
| Data length update | `connection.setDataLength()` | mapped |
| GATT discovery | `connection.discover*()` | mapped |
| GATT read/write | `connection.read*()` / `write*()` | mapped |
| GATT Server registration | `ble.open({gattServer})` | framework-owned |
| Security initiate/inject IO | `connection.pair()` / `respondPairing()` | mapped |
| Store iterate/delete | `adapter.bonds()` / `removeBond()` | mapped |
| L2CAP CoC | `listenL2cap()` / `openL2cap()` | mapped |
| mbuf allocation/free | ByteView/pool owner | framework-owned |
| Raw HCI command | 无；按本设计的 ownership/security 排除 | private-excluded（项目分类，不断言所有上游 HCI API 都是 private） |
| BLE Mesh | `bleMesh` | separate-stack |

### 28.3 覆盖测试

`test_nimble_api_coverage.py` 必须验证：

- allowlist header 中所有函数有条目；
- `mapped + implemented` 条目对应 TypeScript callable 和 native registration；planned 条目不得伪装已注册；
- `framework-owned` 有设计说明和 owner test；
- target/build 宏变化不会留下错误能力声明；
- private header 不进入 allowlist；
- ESP-IDF 升级时新增函数导致 CI 明确失败，直到完成评审。

---

## 29. 实施工单与提交顺序

所有任务默认 planned；“目标映射完成”“代码实现”“Host 测试通过”“三目标编译”“硬件通过”分别记录，不能互相替代。任务中的建议新文件名可按现有模块组织调整，但必须保留可追溯 owner 和测试入口。

### 29.1 工单总表

| ID | 任务 | 前置 | 主要验收 |
| --- | --- | --- | --- |
| B-00 | 固定 Host/Controller、能力与 API inventory | F-CORE、工作分支对比 | 每项 capability 有来源，planned 不进入正式 runtime surface |
| B-01 | 现有 BLE 机械拆分 | B-00 | 不改变公共行为，基线测试无回归，唯一 runtime owner |
| B-02 | 两阶段能力、地址、错误、连接 registry | B-01 | 打开前未知不猜测；入站事件丢失不产生孤儿连接 |
| B-03 | Scan manager、统一报告、重组/去重 | B-02、B-10 公共契约 | 多逻辑 Scanner 兼容共享/冲突拒绝，last owner stop |
| B-04 | Legacy/Extended 多 Advertising Set | B-02、B-10 公共契约 | instance/generation 隔离、组合验证、输入所有权 |
| B-05 | 链路控制与完整 GATT Client | B-02 | effective PHY/DLE/MTU；ATT FIFO、Reliable echo/Cancel |
| B-06 | Notification/CCCD 唯一 owner | B-05 | 先准备 RX 再 enable，重复订阅拒绝，旧 cache 不盲写 0 |
| B-07 | GATT Server cache、业务接纳、Indication 与事务验证 | B-02、B-05 | event-only 背压、确认终态、权限；事务能力有独立证据 |
| B-08 | Security、Privacy、Bond、Filter Accept List | B-02、B-05 | 显式用户确认、同步重复配对规则、secret 与持久化测试 |
| B-09 | L2CAP、Periodic 与 PAST | B-04、B-05、B-10 | credit/容量一致，channel/sync 生命周期、对端验证 |
| B-10 | 生命周期、控制 mailbox 与总内存预算 | B-01 后即开始 | callback 屏障、retired storage、cleanup retry；贯穿全部任务 |
| B-11 | PAwR、ISO、CTE、Power/Path Loss/Subrating | 各前置 transport/能力已验证 | 每项单独完成子契约、feature gate、公开接口和硬件证据 |
| B-12 | JS AD Library、迁移、类型/文档/生成物 | 随各能力提交 | 所有正式声明对应实现，示例正确关闭数据，ES5-like 检查 |
| B-13 | 完整编译、设备、共存与冻结评审 | 待发布能力对应工单 | C3/S3/C5 按实际 capability 验证，未执行项不冒充通过 |

B-10 和 B-12 是贯穿任务，不是等功能全部写完再处理内存和文档。高级能力可以独立关闭，不阻塞已经完成验证的核心能力交付；最终声称“完整”时仍需清单无遗漏、已启用目标能力逐项完成。

### B-00：基线与覆盖清单

读取当前 commit、IDF tag/checkout、NimBLE submodule SHA、sdkconfig 和 Build Context。保存现有 BLE API、错误、超时副作用、测试结果及功能 gate，建立本轮 before/after 列表。

先从实际 include/compile context 解析 public inventory，再与已审查 mapping 对照。原附件列出的头文件名是起始 allowlist，不能假定所有版本都具有独立的同名头文件。字段、条件宏、事件、同步返回值、单位、mbuf ownership 和取消边界都需要条目。

**交付**：`docs/nimble-api-map.json`、生成/测试脚本、固定来源记录；API inventory 检查和每种 build 的能力抽样测试。发现 public 入口未分类时 CI 失败，但 planned 项不伪造 native registration。

### B-01：机械拆分

按第 27 节将 module registration、runtime lifecycle、GAP、scan、advertise、connection、GATT、security/store、resources/errors 拆开。先移动代码并建立内部接口，不在同一提交改 role 枚举、订阅语义、queue policy、timeout 或公开返回类型。

`s_ble` 可以作为唯一 owner 内部对象被重命名/封装，不能每个文件复制 singleton。callback registry、generation 分配、Host port ownership 和操作资源计数只能有一个来源。避免为了跨文件访问而公开所有内部字段；优先小型明确接口。

**交付/验收**：每次可审查的移动提交运行现有 Host C、Python 架构检查与实际三目标 build。文件行数不是验收指标。原来依赖源码字符串位置的架构测试可更新定位，但必须保留行为级测试，不能通过删除断言掩盖退化。

### B-02：能力、身份、错误与连接登记

实现 build/effective 两阶段能力；控制器查询未完成时不声称 true/精确 limit。统一四角色、`BLEAddress`、OTA 与 identity、structured errors。公开 number connectionHandle 仅用于诊断，不作为跨 generation 的资源 token。

新增有界 native connection registry 和 `adapter.connections()`。连接在 native 层登记后才投递观察事件；wrapper conversion 失败、watch 满和 Advertiser close 都不能让 active connection 脱离 Adapter 管理。Host reset 后旧资源立即失效，不能复活旧 GATT cache。

**测试**：未打开查询不启动 Host；query failure；capacity 超限；incoming event dropped 后可查询/关闭；raw handle 数值重用；角色/地址类型错误；snapshot 不引用 native 可变数据。

### B-03：扫描重构与 Extended Scan

新增 `scan_manager`，一个物理 discovery，多个逻辑 subscriber。将 hardware tuple 规范化并进行兼容判断；不兼容申请原子拒绝，现有 scan 状态不改变。

保留 raw AD 数据并补 PHY/SID/dataStatus/txPower/periodic metadata。去重和 fragment reassembly 使用有界表，定义 key、fragment complete/truncated、expiry、最大长度与丢弃原因。订阅者软件过滤不能补救硬件过早过滤的数据，duplicate policy 要与物理设置一致。

**测试**：两相同/不同参数 subscriber；一个到期不停止其他；stop/configure/start；anonymous report；缺片、超大、重组表满；retain report 后 close/reopen；与连接/广播并存但不越过 Controller 限制。

### B-04：广播与多 Set

以 Advertising instance + generation 管理 Set。每个 Set 的 data、scan response、timing、PHY、own address 和 Periodic child 有明确 owner。审查 public API 对 mbuf 的成功/失败所有权，配置失败精确回滚，不在关闭后提前复用仍可能报告事件的 instance。

`auto` 的选择规则必须确定；参数要求 Extended 时不得退回 Legacy。Legacy 31-byte 限制、Extended 各组合和有效最大值按能力校验。connectable incoming Connection 归 Adapter，不归 Advertiser。

**测试**：多 Set 上限；data update 失败；关闭 A 不影响 B；旧 instance late event；connectable/scannable/anonymous/directed/Periodic 无效组合；输入 ByteView 在调用返回后关闭；全流程失败清理。

### B-05：Connection / GATT Client 完整化

为 updateParameters、PHY、DLE、MTU、RSSI 建立 requested/effective 与对应完成判据。不能仅提交成功就填入目标 PHY；允许协商的结果如实返回，安全/明确必需条件不满足时报错。

扩展 granular discovery、Long Read、Read Multiple、Write Request/Command、Long/Reliable Write。每连接 ATT procedure FIFO、有界排队和输入 owner；read maxBytes 是硬上限；Reliable Write 逐段验证 Prepare echo 并按公共流程 Execute Cancel。若 peer 已执行但终态丢失，只能报告结果未知，不能承诺自动重试一定不会重复写。

Service Changed/重连更新 gattGeneration。JS helper 可以绑定 snapshot generation，单独 number handle 不伪装有来源检查。超时按第 24.6 节处理并报告是否终止连接。

**测试**：相邻连接 lane 隔离、MTU/PHY 协商、超限值、ATT error context、中途断连、echo mismatch、取消同时完成、输入 GC、旧 cache 和 late callback。

### B-06：订阅与 CCCD 所有权

为同一连接/gattGeneration/valueHandle 建立唯一订阅 owner；没有明确 cccdHandle 时从有效 discovery 查找，不能猜 valueHandle+1。重复订阅 `BLE_SUBSCRIPTION_EXISTS`。

先分配 pool/route 再启用 CCCD，启用失败正确撤销。直接 handle 写入与当前 CCCD owner 冲突时拒绝。close 只有在当前 owner 和 generation 均有效时禁用，否则只清理本地资源。Notification/Indication 的 JS 队列可以丢数据，但必须保留计数和协议确认的真实含义。

**测试**：enable callback 立即数据；重复订阅；close 同时 Service Changed；另一个连接同 handle 不冲突；队列满的 indication 不假称 JS 已处理；旧订阅重连后不能禁用新订阅。

### B-07：Server 与 Indication

先落实普通 cached read/write、权限、`server.receive()` 主 inbox 和 `server.watch()` 观察分流，再做 Prepared Write 公共 hook 验证。不要等高级事务实现后才修普通 event-only 接纳。

Request 容量不足必须在成功之前拒绝；Command 不能虚构 ATT response。缓存版本不能在被 JS view 读取时原地无保护改变。Indication Future 等最终确认，status=0 只作发送状态；同一连接只有一个未确认项。

**专项 spike**：用固定 NimBLE 的实际调用路径验证 prepare/offset/execute/cancel/多属性边界。输出明确能力结论和测试；不能实现的保证不暴露。此 spike 不是用 private hook 绕过设计的授权。

**测试**：主队列/观察队列分别满；无 watch 时 event-only 正常入主 inbox；主队列关闭竞争；权限/长度/offset；cache 快照；命令丢弃；取消 staging；第二属性失败不错误声称全事务提交；indication submitted/confirmed/timeout/late confirmation。

### B-08：Security / Privacy / Store

实现显式 pair + 输入响应通道，删去 autoPair 和 repeatPairing request。IO action、超时、action-specific response、request generation、SC/MITM/key size 必须验证，UI 未确认不能自动 `match:true`。

Privacy/identity/Filter Accept List 统一走持有者与原生限制检查；正在使用列表的资源不被隐式停止。Store 查询不回显密钥；删除精确 peer、部分失败与掉电恢复有独立结果；repeat replace 仅在公开同步路径经过审查时开放。

OOB 数据的格式、长度、方向和 SC/legacy 差异由固定 Host 的公开定义补齐，不能保留一个任意 ByteSource 就宣称支持全部 OOB。

**测试**：真实 UI action；拒绝/过期/重复输入；没有用户输入时安全失败；要求 SC 但 peer 不支持；Bond 恢复与损坏；RPA/identity；replace 与二阶段重配；秘密不进日志/错误/RPC inspection；无自动全库删除。

### B-09：L2CAP / Periodic / PAST

L2CAP listener 先有 native accept budget；outbound channel 独立 lane；每个 mbuf/PDU/SDU 责任与 credit 管理有公开依据。只报告能权威取得的状态，不能用 private struct 读取 credit。peer lost 和 Adapter close 唤醒全部 pending send/receive。

Periodic advertiser 依附有效 Extended Set；关闭父 Set 先结束 child。Periodic Sync、List、Transfer 分别检查 Controller/Host 功能。List mutate 与正在使用的 sync 不能互相无声覆盖。延迟 report 不能投递到复用的 sync handle。

**测试**：双向多 SDU、保留所有 view 时容量约束、credit stall、peer close、listener 满；Periodic report fragmentation、sync lost、父子关闭、PAST 不支持明确拒绝。

### B-10：贯穿式并发和内存工作

在 B-01 后先写出 callback entry、control mailbox、Future detach、retired storage 与关闭状态机测试，所有后续模块接入同一机制。维护生产路径故障注入，不只用平行实现的“测试专用状态机”。

每次新资源加入都更新全局预算和 ownership 表：initial reserve、per-operation bytes、inflight quota、retained bytes、exactly-once release、native worker retain。测试 Host stop/deinit 每一步失败，确保精确后缀继续清理。

**验收**：队列饱和不阻止控制终态；active=0 后仍来的旧事件安全；旧 view 不阻塞控制 close；总预算达到限制时有明确拒绝；GC/runtime reset 不导致 JSContext UAF；跨 Wi-Fi/TLS 的混合预算记录。

### B-11：可选高级能力

PAwR、ISO、CTE/Direction Finding、LE Power Control/Path Loss/Subrating 分别开工单，不在公共 `object options` 尚未展开时一次性注册所有方法。

每个子功能先给出：public API/Controller feature 证据、typed options/result/event、单位/限制、同步窗口、预装 buffer、取消和父子关闭、峰值预算、硬件对端。PAwR/ISO 的时限不能依赖 JS 即时执行；CTE IQ 数据不冒充已计算角度。

**验收**：启用能力才有相应正式 namespace/method 和测试；目标缺失与实现未完成分开记录。不据名称推断 C3/S3/C5 全部支持这些高级能力。BLE Mesh、Classic、LE Audio profile 继续维持原方案的 separate-stack/profile 边界。

### B-12：API、AD Library、示例和一次性迁移

随每项 implemented 能力同步 `.d.ts`、native registration、API manifest、feature catalog、`docs/api/docs.json` 和 API 文档。不要先把完整目标声明发布为设备实际能力。

`ble.ad` 的 encode/decode 是官方 JS Library，由构建侧选择并装入不可变 Build Context。样例必须兼容 vendored ES5-like 语法，通过仓库 checker；Node 成功不是设备语法证明。所有取得的 ByteView/Source/Batch 和控制 handle 都展示正常及异常关闭。

删除 superseded public 字段/alias，不保留 autoPair/request 兼容分支。不要改动用户 workspace 中无关脚本；迁移影响、需要同时更新的 Hub/Host consumer 在 PR 中列明。

### B-13：集成与冻结评审

按第 30 节执行 feature-enabled/disabled build、设备/对端、500 次生命周期、至少一小时并按风险延长的混合负载。Wi-Fi 时间戳跨回绕测试引用第二份文档独立验收，不能用 BLE 一小时测试替代。

记录每项 PASS / FAIL / NOT-RUN / NOT-APPLICABLE 及原因。必须有硬件但当前无对端的用例标 not-run/hardware-pending，不能填 skip 后视作通过。用 feature stability 表保持与真实证据一致，不凭功能列表齐全提升稳定等级。

### 29.2 推荐提交边界

```text
保存基线和 API inventory
 → 机械拆分（不改行为）
 → 公共生命周期/控制状态/预算
 → 两阶段能力 + identity + connection registry
 → Scan / Advertise / Connection-GATT 分别扩展
 → CCCD / Server / Security 分别落实契约
 → L2CAP / Periodic
 → 高级可选模块逐项实现
 → 汇合 Wi-Fi 分支，完成组合与硬件验收
```

每个提交附 task ID、before/after 公共契约、源 API 证据、真实测试命令与结果。测试命令复用第一份文档和仓库 `AGENTS.md`；不发明不存在的 CLI 参数，不用破坏性擦除 workspace 作为默认测试步骤。[R-AGENTS]

---

## 30. 测试计划

### 30.1 Host C tests

必须覆盖：

- pool acquire/publish/drop/reuse exactly once；
- queue full、pool full、oversize；
- generation stale；
- callback active count；
- open 每个阶段 allocation/host error rollback；
- close 每个 stop/deinit suffix retention；
- GATT snapshot construction rollback；
- reliable write echo mismatch；
- pairing request expiry；
- L2CAP reassembly/credit；
- ISO deadline 和 drop path；
- Advertising Set limit；
- scan fragment reassembly bounds；
- 多逻辑 Scanner 的参数冲突与 last-subscriber stop；
- 观察队列饱和时控制 Future 完成和 incoming connection registry 可恢复；
- CCCD 重复订阅/关闭与 Service Changed 竞态；
- event-only 写入在 queue/pool 满时先拒绝、不错误返回成功；
- Prepared Write cancel/echo mismatch/multi-attribute failure；
- indication status=0 不完成、最终 confirmation 才完成；
- adapter close 后已交付 ByteView 可读，retired budget 耗尽时新 open 被拒绝。

### 30.2 Python architecture tests

新增：

```text
tests/python/test_ble_api_architecture.py
tests/python/test_ble_callback_ownership.py
tests/python/test_ble_teardown_invariants.py
tests/python/test_ble_gatt_schema.py
tests/python/test_ble_capability_manifest.py
tests/python/test_nimble_api_coverage.py
```

检查：

- callback 中不调用 JS API；
- callback 中不做 unbounded malloc；
- close 顺序和 worker cleanup；
- Kconfig 上限关系；
- d.ts、文档、manifest、registration 一致；
- optional feature disabled build 不残留对象；
- 密钥字段不进入 status/error/log。

### 30.3 Device JS tests

核心场景：

1. Legacy active/passive scan；
2. Extended scan 1M/Coded；
3. Legacy connectable advertising；
4. 多 Extended Advertising Set；
5. Central connect/disconnect；
6. Peripheral incoming connection；
7. Service/Characteristic/Descriptor discovery；
8. Read/Long Read/Read Multiple；
9. Write Request/Command/Long/Reliable；
10. Notify/Indicate；
11. MTU/PHY/Data Length/Connection Parameter；
12. Just Works、Passkey、Numeric Comparison；
13. Bond 重启恢复、删除、Repeat Pairing；
14. Privacy/RPA resolution；
15. Filter Accept List；
16. L2CAP CoC 双向大 SDU；
17. Periodic Advertising/Sync；
18. Peer loss、突然断电、连接 timeout；
19. Adapter close/reopen；
20. Queue saturation、GC、runtime restart。

### 30.4 Hardware 矩阵

| 本端 | 对端 | 重点 |
|---|---|---|
| ESP32-S3 | Raspberry Pi 5 / BlueZ | Central/Peripheral/GATT/Security |
| ESP32-S3 | ESP32-C5 | 双向 GATT、PHY、共存 |
| ESP32-C3 | ESP32-S3 | 基础 BLE、Bond、低内存 profile |
| ESP32-C5 | 支持 Extended/Periodic 的对端 | Extended Scan/Adv、Periodic、Coded PHY |
| ESP32QJS | Android/iOS | 常见 Central/Peripheral 兼容性 |
| 双 ESP32QJS | 双 ESP32QJS | L2CAP、Periodic、压力和错误注入 |

每项记录：firmware SHA、target、sdkconfig、对端版本、天线、距离、PHY、测试命令和
内存前后快照。

### 30.5 生命周期与内存验收

- `ble.open()` / `close()` 500 次；
- Scanner start/stop/configure 500 次；
- Advertiser create/start/stop/close 500 次；
- Connection connect/disconnect 500 次；
- Notification Stream subscribe/close 500 次；
- L2CAP Channel open/close 500 次；
- 比较预热后同等静止状态的 current internal/PSRAM free、largest block、active/retired owner；minimum free 只用于历史压力观察，不要求其不下降；
- 所有测试持有的 view/source 释放后，pool/free-slot 与预算账本回到定义基线；显式 retained 数据单独计账；
- callback active count 最终为 0；
- 无旧 generation event 被新 Adapter 接收。

### 30.6 共存测试

组合：

```text
BLE scan + Wi-Fi STA traffic
BLE advertising + ESP-NOW queued TX
BLE GATT notifications + HTTPS/TLS
BLE multiple connections + CSI capture
BLE L2CAP bulk + Wi-Fi monitor/raw TX
```

验收不是要求零延迟，而是：

- 无死锁、panic、watchdog；
- 无隐式安全/PHY降级；
- 丢包有统计；
- 关闭可恢复；
- 内存不泄漏；
- Wi‑Fi/BLE ownership 状态一致。

---

## 31. 破坏性迁移

### 31.1 保留的主要形状

```text
ble.capabilities()
ble.open()
adapter.scan()
adapter.advertise()
adapter.connect()
adapter.server()
adapter.bonds()
adapter.close()
connection.discover()
connection.readHandle()
connection.writeHandle()
connection.subscribeHandle()
```

### 31.2 直接替换的内容

- `BLERole` 从仅 central/peripheral 改为四角色；
- Legacy-only Scan Report 替换为统一 Legacy/Extended Report；
- Advertising 支持多个 Set 和 Extended/Periodic；
- Address 输出替换为 OTA + identity 模型；
- Connection status 增加 PHY、DLE、完整 Security；
- GATT 操作改为完整 read/write family；
- Queue stats 统一；
- 错误码与 details 统一；
- 不保留旧字段 alias。
- 删除 `autoPair` 与 `repeatPairing:"request"`；交互配对改为连接返回后的显式过程。
- `respondPairing()` 返回输入提交结果，不把输入提交当最终配对成功。
- 新增 `adapter.connections()` 与 `server.receive()`，分别解决入站资源可恢复和业务写入接纳。
- `ble.capabilities()` 区分 build/effective，打开前未知能力/上限为 null。
- 同一 characteristic 的 Stream owner 唯一，重复 subscribe 明确失败。

### 31.3 删除或拒绝

- callback-style JS API；
- 隐式 fallback；
- 直接暴露 conn handle 作为唯一 identity；
- 动态 JS GATT read callback；
- 未关闭 ByteView 的示例；
- 根据 board 名称判断 BLE 5.x feature 的代码。

---

## 32. 使用示例

以下采用目标 v1 API，不代表基线已支持；设备代码使用项目 ES5-like 方言。真实凭据、GATT handle、对端和用户 UI 由应用提供。清理中的 cleanup-pending 必须由正式应用处理；示例不表示任何错误后资源都已经物理释放。

### 32.1 扫描并解析 Advertising Data

```js
var adapter = ble.open({ roles: ["observer"] });
var scanner = adapter.scan({
  mode: "auto",
  active: true,
  durationMs: 10000,
  minimumRssi: -85,
  capacity: 32
});

try {
  for (;;) {
    var report = scanner.receive(1000);
    if (report === null) {
      if (scanner.status().state !== "running") break;
      continue;
    }

    try {
      var elements = ble.ad.decode(report.data);
      try {
        print(report.peer.overTheAir.address, report.rssi,
              JSON.stringify(elements.map(function (item) {
                return { type: item.type, name: item.name, length: item.data.length };
              })));
      } finally {
        for (var i = 0; i < elements.length; i++) elements[i].data.close();
      }
    } finally {
      report.data.close();
    }
  }
} finally {
  scanner.close();
  adapter.close();
}
```

### 32.2 Central GATT Client

```js
var adapter = ble.open({
  roles: ["central"],
  preferredMtu: 247,
  security: {
    bonding: true,
    secureConnections: "preferred"
  }
});

try {
  var connection = adapter.connect({
    address: "12:34:56:78:9a:bc",
    type: "public"
  }, {
    timeoutMs: 10000,
    autoExchangeMtu: true
  });

  try {
    var db = connection.discover();
    var value = connection.readHandle(0x0025, { maxBytes: 512 });
    try {
      print(value.length);
    } finally {
      value.close();
    }

    connection.writeHandle(0x0028, new Uint8Array([1, 2, 3]), {
      mode: "request"
    });
  } finally {
    connection.close();
  }
} finally {
  adapter.close();
}
```

### 32.3 Peripheral GATT Server 与明确资源关闭

本示例展示 cached write 与观察业务；需要逐条命令交付时将目标 characteristic 改为 event-only 并遵守第 16.2 节。为了聚焦生命周期，示例属性允许普通访问；真实产品按需求配置加密/认证，并加入完整的用户配对流程。

```js
var adapter = null;
var advertiser = null;
var advertisingData = null;

try {
    adapter = ble.open({
        roles: ["peripheral", "broadcaster"],
        deviceName: "ESP32QJS Sensor",
        gattServer: {
            eventCapacity: 8,
            services: [{
                id: "sensor",
                uuid: "12345678-1234-5678-1234-56789abcdef0",
                characteristics: [{
                    id: "value",
                    uuid: "12345678-1234-5678-1234-56789abcdef1",
                    properties: ["read", "write", "notify"],
                    permissions: { read: "open", write: "open" },
                    maxLength: 64,
                    value: new Uint8Array([0]),
                    readPolicy: "cached",
                    writePolicy: "cache-and-event"
                }]
            }]
        }
    });

    advertisingData = ble.ad.encode([
        ble.ad.flags(0x06),
        ble.ad.completeName("ESP32QJS Sensor")
    ]);
    advertiser = adapter.advertise({
        mode: "legacy",
        connectable: true,
        scannable: true,
        data: advertisingData
    });
    // advertise 返回前已按输入契约复制/retain，释放调用者的 ByteView。
    advertisingData.close();
    advertisingData = null;

    var server = adapter.server();
    var characteristic = server.characteristic("value");
    // 有限演示循环；正式应用以自己的退出条件结束。
    for (var i = 0; i < 60; i++) {
        var event = server.receive(1000);
        if (event && event.type === "write") {
            try {
                print("write bytes:", event.data.length);
                characteristic.notify();
            } finally {
                event.data.close();
            }
        }
    }
} finally {
    // 清理错误不应导致后面的 parent close 完全跳过。
    try {
        if (advertisingData !== null) advertisingData.close();
    } finally {
        try {
            if (advertiser !== null) advertiser.close();
        } finally {
            if (adapter !== null) adapter.close();
        }
    }
}
```

示例的 `finally` 只保证尝试关闭所有取得的资源；返回 cleanup-pending 时，正式应用仍需按第 24 节处理剩余原生清理，不能立即强行重新打开。

### 32.4 Numeric Comparison：真实确认后才提交

下面是输入响应 helper，不是完整 UI。应用必须已经并发运行 pair Future 与事件消费者，并从可信本地 UI 获得当前 request 的实际确认结果；不能把第三个参数固定为 true。

```js
function respondToNumericComparison(connection, event, confirmedByUser) {
    if (!event || event.type !== "pairing-request" ||
        event.action !== "numeric-comparison") {
        throw new TypeError("numeric-comparison request required");
    }
    if (typeof confirmedByUser !== "boolean") {
        throw new TypeError("explicit user decision required");
    }
    if (!confirmedByUser) {
        return connection.respondPairing(event.requestId, { accept: false });
    }
    return connection.respondPairing(event.requestId, {
        accept: true,
        match: true
    });
}
```

UI 对照 `event.number`，同时绑定当前连接和 requestId；不把配对数字写入默认持久日志。helper 的返回只是 submitted 结果，最终安全状态以 pair Future/Connection 状态事件为准。仅仅读取到了事件不构成用户授权。

### 32.5 L2CAP CoC

```js
var channel = connection.openL2cap({
  psm: 0x0081,
  mtu: 512,
  receiveCapacity: 8
});

try {
  channel.send(payload);
  var sdu = channel.receive(1000);
  if (sdu) {
    try {
      print(sdu.data.length);
    } finally {
      sdu.data.close();
    }
  }
} finally {
  channel.close();
}
```

---

## 33. 文档与生成物清单

必须同步更新：

```text
docs/api/ble.md
docs/plans/03_esp32qjs_ble_refactor_and_features.md
docs/wireless-concurrency.md
docs/api-stability-plan.md
docs/feature-stability.json
docs/nimble-api-map.json

types/esp32qjs-c-api.d.ts
types/esp32qjs-js-api.d.ts
api-manifest.json
components/esp32_mquickjs/runtime-features.json

scripts/generate_api_manifest.py
scripts/generate_feature_docs.py
scripts/generate_nimble_api_map.py
scripts/remote.py

managed JS BLE advertising-data library
hardware-lab BLE evidence schema
```

文档生成检查必须确认：

- public method 名称一致；
- optional namespace 与 feature 一致；
- Kconfig limit 与 capability 字段一致；
- 所有 ByteView 示例有 close；
- deprecated/removed API 不出现在示例；
- NimBLE API map 没有未分类 public symbol。

---

## 34. Definition of Done

BLE v1 只有满足以下条件才可标记为 Frozen Candidate：

1. `ble.capabilities()` 对 C3/S3/C5 的实际构建返回正确 target-gated 数据；
2. Central、Peripheral、Observer、Broadcaster 四角色均有设备证据；
3. Legacy Scan/Advertising、Connection、GATT Client/Server、安全和 Bond 完整通过；
4. Extended/Periodic/L2CAP 等已启用 feature 有对应硬件证据；
5. 所有 callback 不调用 JS、不做无界分配；
6. 所有重复输入有有界 pool、queue、drop counter 和 deterministic close；
7. 500 次 open/close 和资源 lifecycle 后，释放全部测试 lease 的 current heap/pool/owner 账本回到定义基线；
8. Peer loss、timeout、取消、Host reset、NimBLE stop/deinit failure 均可恢复；
9. d.ts、native registration、API manifest、文档和 NimBLE map 完全一致；
10. feature disabled build 不暴露残留对象；
11. 密钥和 Pairing secret 不进入日志、status、snapshot 或 error；
12. Wi‑Fi/ESP-NOW/BLE 共存压力下无死锁、panic 或未统计的数据丢失；
13. 当前实现中所有 superseded BLE API 和示例已删除，不保留 v1 兼容壳。

---

## 附录 A：当前实现到新设计的差距

当前基线已经具备：

- ESP-NimBLE Central/Peripheral/Observer/Broadcaster 基础；
- Legacy Scan 和 raw Advertising Data；
- Legacy Advertising 和 incoming connection；
- GATT discovery、read/write/subscribe；
- MTU、RSSI、Pairing、Bond；
- 静态 native-cached GATT Server；
- 有界 EventQueue、ByteView、generation handle 和可靠 teardown 基础。

主要待补：

```text
Extended Scan / Advertising
Multiple Advertising Sets
Periodic Advertising / Sync / PAwR
完整地址 identity / privacy / filter accept list
PHY / Data Length / 完整 connection parameter API
Long Read / Read Multiple / Long & Reliable Write
更完整 GATT Server permissions 与 prepared write
L2CAP CoC
可选 ISO / Direction Finding
NimBLE public API 机器覆盖映射
更广泛多连接和长期共存硬件证据
```

因此实施策略应当是保留当前安全的 owner/queue/generation 基础，先机械拆分，再扩展
能力；不建议为了追求“完整”而直接将 NimBLE callback 和 C struct 映射成 JS object。

## 附录 B：参考基线

- ESP32QJS：提交 `9a74f1197d53863e079c30f8559ccd5b6cd60b42`
- 当前 BLE 文档：`docs/api/ble.md`
- 当前 BLE 实现：`components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c`
- 当前无线并发约束：`docs/wireless-concurrency.md`
- ESP-IDF：tag `v6.1`
- ESP-NimBLE public GAP/GATT/L2CAP/Security headers
- ESP-IDF Extended/Periodic Advertising、L2CAP CoC、PHY、ISO 和 CTE 示例



## 附录 C：三份文档的边界与合并依赖

| 范围 | 主文档/工单 | 本 BLE 文档的关系 |
| --- | --- | --- |
| 已有 BLE 超时断连、callback retention 和 Host teardown 复现 | 第一份 F-03/F-05 | 先按现状修复/加回归，B-01 不改变其语义 |
| 已有 CSI/ESP-NOW 所有权 | 第一份 F-06/F-07 | 不复制一套不同风格的 Future/queue 基础设施 |
| 可重复 Wi-Fi Driver 生命周期与共享信道 | 第二份 W-01 | BLE 不调用 Wi-Fi driver setters 或直接取得独立 Radio owner |
| 公共预算与共存 | 第二份 W-09/W-10、第三份 B-10/B-13 | 统一预算口径，真实负载衡量，不保证硬件完全并行无性能损失 |
| CCCD、GATT Server 接纳、配对政策 | 第三份 B-06/B-07/B-08 | 不混入 Wi-Fi 重构，也不误报为当前所有路径均存在 bug |
| manifest、类型和 docs | 各文档相应工单 | 公共生成物合并后重生成、校验，不各自覆盖另一分支的 feature |

机械拆分、公共机制和功能扩展应分提交。若并行开发，先共同合并 owner/状态/错误/预算内部接口，再让功能分支依赖这个固定提交；不要最后通过复制文件解决不同 owner 实现的冲突。

## 附录 D：开始实现前必须展开的子契约

原设计部分类型只给出名称或 `object`。这些内容继续属于总体功能范围，但不足以据此声称“可直接实施的完整 API”。对应任务必须补齐子契约并通过审查，再注册公开能力。

| 子契约 | 未经证明不能承诺的内容 | 必须补齐的证据/定义 |
| --- | --- | --- |
| Server Prepared / Reliable Write | 多属性原子提交、可观察全部 prepare/execute/cancel | 固定 public Host hook、事务边界、失败/取消测试、独立 capability |
| OOB | 任意 ByteSource 同时覆盖 legacy/SC 与所有方向 | typed action、长度、编码、所有权、期限和安全策略 |
| L2CAP credit / enhanced CoC | JS 能读所有内部 credit 数值，任意配置均可实现 | public API、协商 MTU/MPS、接收责任/可用容量、null 状态含义 |
| Periodic/PAST | List、Sync、Transfer 可任意同时修改 | 具体 option/event schema、Controller 组合限制、父子资源生命周期 |
| PAwR | JS 能实时响应 Controller deadline | subevent/response/window 类型、未来数据预装、过期和 missedDeadline |
| ISO CIG/CIS/BIG/BIS | 宏存在就有端到端 transport、所有 codec 已可用 | Controller/Host/框架可用性、group/stream API、专用 buffer、时间戳/丢失状态 |
| Direction Finding/CTE | 芯片名字推出天线/IQ/角度能力 | public API、target/硬件天线配置、IQ 编码/元数据；不自动计算角度 |
| Remote TX Power/Path Loss/Subrating | 原示例中的泛型 options 就是完整实现 | 精确类型/单位、事件/完成规则、capability 和对端测试 |
| 全量辅助类型 | 所有 `*Stats`、`*Options` 已完整定义 | 复用公共类型或逐项展开，生成检查不能输出未定义占位类型 |

`not-implemented`、`contract-pending`、`not-compiled`、`controller-unsupported` 和 `hardware-pending` 分开记录。不能为了让覆盖表全绿，把尚未开发的公开能力全部归为 target-unsupported；也不能为满足“完整”标题而注册只会抛 unsupported 的空 namespace。

### D.1 每个子契约的审查模板

```text
Task / feature / fixed-source SHA
公开 API、结构、事件与条件宏
JS options/result/event 的完整类型与单位
状态前置、组合限制、requested 与 effective
输入/mbuf/pool/ByteView 所有权与 copy/retain 时点
完成、取消、超时、迟到回调和父子关闭
同步 callback 的立即决定与业务接纳规则
资源/队列/重组/在途/retired 的硬上限
错误、秘密字段和诊断边界
Host、build、hardware 测试及当前执行状态
```

## 附录 E：来源、修订追溯与完成声明

**S-B**：用户附件 `esp32qjs_ble_api_v1_development_design(1).md`。本文件保留其章节主体、基础 API、GAP/GATT/Security/Privacy/Periodic/L2CAP/高级能力范围与 separate-stack 边界。

**S-W**：用户附件 `esp32qjs_wireless_api_v1_complete_wifi_design(1).md`。跨无线内存、共存和唯一 v1 方向参考该附件；具体 Wi-Fi 实施以本轮第二份文档为准。

**本轮新增实施决策**：多逻辑扫描器兼容策略、唯一 CCCD owner、Server 主业务 inbox、重复配对两阶段工作流、删除 autoPair、两阶段 capability、连接 registry、Indication 终态、关闭与退休数据预算，以及 B-00～B-13 工单。它们是本轮设计要求，不代表基线已有实现。

| 审查项 | 来源/证据 | 本轮处理 |
| --- | --- | --- |
| ES5-like、不可变 Build Context、唯一 v1 | [R-AGENTS] | B-00/B-12、示例与构建约束 |
| callback 不进 JS、Host stop/deinit、原生引用保留 | [R-CONCURRENCY] | 第 24～25 节、B-10；保留现有正确机制 |
| 基线 BLE 超时可能终止连接 | [R-BLE] 的 `ble_future_on_timeout` | 不假称超时永远无连接副作用；分操作定义 |
| Repeat Pairing callback 同步返回 | [U-GAP] 的 repeat_pairing 注释 | 第 17.4 节，删除无法直接实现的 request 等待语义 |
| Indication notify-tx submitted / confirmed | [U-GAP] 的 notify_tx 注释 | 最终确认前不完成 indicate Future |
| Server 事务公共 hook 是否足够 | 原附件未给出证明 | B-07 spike 与独立 capability，未验证不承诺 |
| BLE 当前成熟度 | [R-STABILITY] | 保留 Candidate/待补证据区分，不因本文生成提升等级 |

实施者应逐项打开实际固定版本头文件与公开注释核对；普通参考链接或函数名称不是能力支持的充分证明。所有引用使用审查基线，不表示本轮重新核验了任意后续 main 分支提交。

**本文仅完成文档设计与任务拆分。未执行固件实现、三目标编译、设备测试、RF 测试或上板生命周期验收。** 真正完成后按工单填写证据，不能把 unchecked 条目自动改为通过。

[R-AGENTS]: https://github.com/99percentpeople/esp32qjs/blob/9a74f1197d53863e079c30f8559ccd5b6cd60b42/AGENTS.md
[R-CONCURRENCY]: https://github.com/99percentpeople/esp32qjs/blob/9a74f1197d53863e079c30f8559ccd5b6cd60b42/docs/wireless-concurrency.md
[R-BLE]: https://github.com/99percentpeople/esp32qjs/blob/9a74f1197d53863e079c30f8559ccd5b6cd60b42/components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c
[R-STABILITY]: https://github.com/99percentpeople/esp32qjs/blob/9a74f1197d53863e079c30f8559ccd5b6cd60b42/docs/feature-stability.json
[U-GAP]: https://github.com/espressif/esp-nimble/blob/139cada0ae932957fa06ba37d17e3c9c2c95c773/nimble/host/include/host/ble_gap.h
[U-GATT]: https://github.com/espressif/esp-nimble/blob/139cada0ae932957fa06ba37d17e3c9c2c95c773/nimble/host/include/host/ble_gatt.h


## 第一阶段实施交接（2026-09-07）

本轮只实施 01 的现有行为修复。本文件的新能力仍为 planned，未展开接口继续保持
`contract-pending`，不加入 native registration、正式类型或 manifest。当前 Radio
仍是 boot-scoped once 启动、单固定信道 owner；新 Driver 生命周期和多 owner 属于
W-01。CSI 仍是单个未释放 pool，旧 View/Source 存活期间禁止 reopen，多代预算留给
W-09/B-10。BLE 的 operation cookie、native quarantine 与 Host 清理后缀应在 B-01
机械拆分时保留；新的 Monitor、Raw TX、CSI wire、BLE 高级功能和安全政策在各自
任务中评审，不能用本次 Host 测试替代 RF/对端验收或提升 feature 稳定等级。
