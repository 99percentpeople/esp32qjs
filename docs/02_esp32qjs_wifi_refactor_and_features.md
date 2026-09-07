# ESP32QJS 第二阶段：Wi-Fi 重构与新功能实施文档

- **状态**：W-00 in-progress（覆盖清单与检查工具）；其余任务 planned；不是已通过硬件验收的报告
- **目标仓库**：`99percentpeople/esp32qjs`
- **历史审查基线**：`9a74f1197d53863e079c30f8559ccd5b6cd60b42`
- **第一阶段实施基线**：firmware `e1b861c`；[实际证据与剩余边界](investigations/2026-09-07-wireless-core.md)
- **ESP-IDF 基线**：`v6.1` / `fff9895c82d744c7237be8847347bdd1b07c6643`
- **主要范围**：Wi-Fi Driver、STA/AP、Raw RX/TX、CSI、Wi-Fi 高级能力与共享 Radio；ESP-NOW 只做底座对齐，BLE 功能扩展另见第三份文档
- **版本策略**：继续使用唯一开发版本 **v1**；允许破坏性修改；不增加兼容别名、旧 reader 或 v2 命名空间
- **建议仓库路径**：`docs/plans/02_esp32qjs_wifi_refactor_and_features.md`
- **文档日期**：2026-09-07
- **前置文档**：[第一阶段：现有问题修复](01_esp32qjs_existing_fixes.md)
- **并行文档**：[第三阶段：BLE 重构与新功能](03_esp32qjs_ble_refactor_and_features.md)

> 本文替换上一版设计。上一版提出的 `wifi.station.connect()`、
> `wifi.accessPoint.start()`、`wifi.radio.setTxPower()` 不再实施。常用 Wi-Fi
> API 保持在 `wifi` 顶层；专业能力最多进入一层命名空间。

## 0. 实施入口与本轮修订

本文保留用户 Wi-Fi 原方案的浅层命名空间、能力覆盖范围、详细 API 与二进制布局，并把上一轮评审意见落实为实施约束。**目标不缩水，但“目标 API 已设计”“代码已实现”“硬件已验证”必须分开。**

第 1～35 节沿用原设计的章节组织，相关条款已就地修订；第 36～38 节提供任务依赖、契约阻塞项和来源追溯。正文中的 TypeScript 是目标公共契约，不是可以提前全部注册到当前固件的已支持 API 清单。

### 0.1 前置条件

第一份文档的 F-CORE 已完成本轮收尾，W-01 可以开始，尚未实施新生命周期；见[最终证据及测试边界](investigations/2026-09-08-fcore-closeout.md)。连接交接/入站队列、原生 handle 复用交错、payload 转换及构造失败已有生产实现回归。W-00 符号/字段覆盖工具已实现，见[本轮收尾与第二阶段进度](investigations/2026-09-07-wifi-refactor-start.md)。长时间验收按用户安排统一移到全部功能完成后，短竞争/错误注入仍随实现执行。若现有无线硬件验证仍未完成，可以继续受控开发，但所有相关 feature 保留其真实的 Hardware pending/Candidate 状态。

共享 Future、ByteView、EventQueue、generation 和内存预算器只扩展一套。Wi-Fi 与 BLE 分支不得各自复制底层实现。框架继续消费不可变 Build Context，不解析 Board、Library、Agent 或产品 workspace manifest。

### 0.2 本轮明确替换的设计条款

| 原设计位置 | 本文决策 | 任务 |
| --- | --- | --- |
| 第 4 节所有队列统一 drop-newest | 控制完成先进入原生状态；数据/观察事件可丢；请求必须有 admission | W-02 |
| 第 25 节 Radio 从 once 模型直接扩展 | 先实现可重复生命周期、精确 lease 集合与串行 mutation | W-01 |
| 第 25.4 节复制 tx_info + generation 关联 | 只复制无指针快照；无请求 cookie 时必须隔离迟到 completion | W-04 |
| 第 26 节等待 retained leases 后 close | 操作关闭和存储回收分离；保留视图不阻塞公开 close | W-09 |
| 第 6.3 节未知事件 raw fallback | 未经逐事件审查不复制 raw bytes；秘密事件仅走专属交付 | W-02 |
| 第 7.3 节 scan 参数子集 | 补 channel bitmap/coexistence 字段映射与冲突检查 | W-02 |
| 第 11 节 CSI packet 完整性 | 只承诺可证明的可读跨度；不预设 payload 密文或完整 FCS | W-05 |
| 第 12/31 节时间与一小时测试 | 有时钟映射/回绕契约及 callback-time 降级标志；增加回绕场景 | W-06 |
| 第 2/30 节先把目标声明写进正式 manifest | inventory 先行，正式 registration/类型随实现提交 | W-00/W-12 |

这些是**本轮建议的契约修订**，不能引用为基线现有行为。基线仍为本页固定提交，不等同于生成文档时主分支最新状态。

### 0.3 交付边界

第一批交付为 W-01～W-06：可靠 Radio、基础 Wi-Fi、Monitor、Raw TX one-shot、CSI 对应包及 Host parser。Native TX queue/batch/periodic 要在 one-shot 的 timeout 隔离通过后开启。

后续完成 W-07/W-08 的完整 Driver 与高级能力。WAPI、FTM/TWT、NAN/Mesh 等原方案使用 `object` 或未展开辅助类型的地方，保留为**目标接口草案 / contract-pending**；实施该模块前必须完成字段、单位、状态、owner、错误和清理表。不得把这样的草案直接复制进正式 `.d.ts` 后宣称完整覆盖。

## 文档导航

- 第 1～4 节：目标、完整性定义、命名规则和公共运行时规则
- 第 5～8 节：浅层 Wi-Fi 基础 API、完整 Driver 控制和公共数据类型
- 第 9～12 节：Monitor、Raw TX、CSI 与二进制 v1 协议
- 第 13～22 节：Action/ROC、FTM、TWT、Vendor IE、Roaming、安全、配网、NAN、Mesh、诊断
- 第 23～27 节：BLE/ESP-NOW 对齐、共享 Radio 状态机、内存并发、ESP-IDF 覆盖机制
- 第 28～35 节：Kconfig、文件清单、实施、测试、迁移、示例、完成标准和参考
- 第 36～38 节：提交依赖、未完成的子契约及来源追溯

---

## 1. 核心决策

1. **基础调用必须浅。** `wifi.connect()`、`wifi.disconnect()`、`wifi.scan()`、
   `wifi.startAP()`、`wifi.stopAP()`、`wifi.setPowerSave()`、
   `wifi.setTxPower()`、`wifi.setCountry()` 和 `wifi.setChannel()` 直接位于
   `wifi` 顶层。
2. **高级功能最多一层。** 采用 `wifi.rawTx.send()`、`wifi.monitor.open()`、
   `wifi.csi.open()`、`wifi.ftm.start()`、`wifi.twt.setupIndividual()` 等形式；
   不出现 `wifi.radio.monitor.open()` 或 `wifi.station.enterprise.configure()`。
3. **JS 获得完整的 ESP-IDF Wi-Fi 功能覆盖。** 所有 ESP-IDF 6.1 中公开、
   文档化、非 private 的 Wi-Fi operational capability，必须有类型安全的 JS
   等价接口，或在机器可检查的映射表中明确标记为 build-time / framework-owned。
4. **不提供任意字符串式 `wifi.idf.call(name, args)`。** 完整性通过审查过的
   typed binding、能力探测和 CI 映射实现，而不是绕过类型、生命周期和安全边界。
5. **驱动 callback 不直接暴露给 JS。** Promiscuous、TX done、CSI、Vendor IE、
   FTM、TWT、DPP、WPS 等 callback/event 全部转成有界 EventQueue 或 session handle。
6. **共享 Radio 是唯一事实来源。** STA、SoftAP、ESP-NOW、Monitor、Raw TX、CSI、
   Action/ROC、NAN 和 Mesh 不得各自隐式改 mode、band 或 channel。
7. **Raw RX 和 Raw TX 均纳入 v1。** `wifi.monitor` 负责接收，`wifi.rawTx` 负责发送；
   CSI 可同时保存触发 CSI 的对应 802.11 header/payload。
8. **协议名继续为 v1。** `wifi/1`、`wifi-monitor/1`、`wifi-raw-tx/1`、
   `wifi-csi/1`、`esp32qjs-monitor/1` 和 `esp32qjs-csi/1` 均保持版本 1；
   当前未冻结布局可直接替换。
9. **完整能力不等于暴露 private ABI。** `esp_wifi_internal_*`、ROM symbol、
   未文档化结构、直接 Wi-Fi task hook 和 private crypto entry point 不属于公共 API。
10. **硬件验证是冻结条件。** Host test、编译成功和源码存在不能替代 C3/S3/C5
    的 RF、共存、长时间运行和内存证据。

---

## 2. “完整 ESP-IDF Wi-Fi 能力”的定义

### 2.1 纳入范围

完整覆盖以下公开能力族：

- Driver start/stop、mode、storage、STA、SoftAP、APSTA 和扫描；
- country、channel、band、protocol、bandwidth、MAC、TX power、power save；
- 连接元数据、RSSI threshold、inactive time、TSF、AID、协商 PHY；
- Wi-Fi 事件、promiscuous RX、raw 802.11 TX、TX completion；
- Action frame、Remain-on-Channel、Vendor IE、CSI、FTM、TWT；
- WPA2/WPA3 Enterprise、WAPI、WPS、DPP、SmartConfig；
- 802.11k RRM、802.11v WNM/BTM 和 802.11r 连接配置；
- NAN 与 ESP-WIFI-MESH（仅在 target/build 支持时）；
- 多天线配置对应的 ESP-IDF 6.x `esp_phy` 公共替代 API；
- 结构化诊断、事件和错误映射。

### 2.2 允许不做一对一函数暴露的三类 API

以下 C API 不直接按原签名暴露，但功能不能丢失：

1. **Driver init/deinit。** `wifi_init_config_t` 中 RX/TX buffer 数量、Wi-Fi task
   core、AMPDU window 等属于 build profile，不能在 JS 运行中安全重配。JS 通过
   `wifi.start()`、`wifi.stop()` 和独占的 `wifi.driver.restart()` 获得安全生命周期控制。
2. **Callback registration。** 注册函数由 framework broker 独占，JS 获得
   EventQueue/session，而不是 C function pointer。
3. **Driver-owned scan/result buffers。** `scan_get_ap_num/records/clear` 等被
   `wifi.scan()` 的资源 owner 吸收，保证成功、失败和取消都释放 driver 内存。

### 2.3 明确排除

- `esp_wifi_internal_*`、`esp_wifi_driver.h` private 接口和 ROM/ABI symbol；
- 自定义 PHY preamble、PLCP、基带 IQ 发射或 SDR 功能；
- 驱动没有上报的帧、跨多个信道同时监听、零丢包承诺；
- 与 Wi-Fi 无关的 TCP/IP policy，仍归 `net`、`socket`、`http` 等模块。

### 2.4 可机器验证的覆盖契约

新增 inventory、审查过的 typed mapping 和 CI；这些文件描述覆盖状态，而不是通过扫描函数名称自动证明行为正确。

```text
docs/idf-wifi-api-map.json
scripts/generate_idf_wifi_api_map.py
tests/python/test_idf_wifi_api_coverage.py
```

映射表建议结构（本轮修订；示例状态为 planned）：

```json
{
  "schema": 1,
  "symbol": "esp_wifi_80211_tx",
  "header": "esp_wifi.h",
  "disposition": "mapped",
  "jsPath": "wifi.rawTx.send",
  "feature": "wifiRawTx",
  "implementation": "planned",
  "contract": "review-required",
  "buildContexts": [],
  "fieldCoverage": [],
  "stateRequirements": ["started", "valid-interface", "radio-lease-held"],
  "ownershipContract": "W-04",
  "completionContract": "W-04",
  "validation": {"host": "not-run", "build": "not-run", "hardware": "not-run"},
  "evidence": []
}
```

`disposition` 沿用原设计的分类含义：`mapped`、`framework-owned`、`build-time`、`removed-or-deprecated`、`private-excluded`、`target-unsupported`。`implementation` 独立记录 `planned / in-progress / implemented`；contract 独立记录 `review-required / contract-pending / reviewed`。

要求：

- 每个 allowlist public symbol 必须有条目；新增且未分类时 CI 失败。
- `mapped + planned` 不能出现在当前已实现 capability/manifest 中。
- `mapped + implemented` 必须有实际注册、类型声明、字段覆盖和 owner/completion 测试。
- `framework-owned` 必须说明功能如何被安全实现，不能用于掩盖尚未实现的功能。
- `target-unsupported` 必须有固定 target/build/公共 capability 的证据；“还没写”不是 unsupported。
- 同一 symbol 在不同目标/条件编译下的参数和结构字段分别追踪；不能只检查名字存在。
- generator 用实际头文件、预处理条件和构建输入提取 inventory；JS 类型、字段转换、秘密策略由 checked mapping 审查，不由正则猜测。
- 参数结构、事件变长布局、单位、默认值、错误、取消和状态限制属于覆盖的一部分。
- 硬件 evidence 带 firmware/IDF SHA、Build Context hash、目标、对端和命令。

正式声明随可用实现生成；目标设计文档不作为“当前全部 callable”的证据。

---

## 3. 最终命名空间

```ts
interface WiFiModule {
  readonly DEFAULT_TIMEOUT_MS: number;

  capabilities(): WiFiCapabilities;
  status(): WiFiStatus;
  watch(options?: WiFiWatchOptions): EventQueue<WiFiEvent>;

  start(options?: WiFiStartOptions): WiFiStatus;
  stop(options?: WiFiStopOptions): WiFiStatus;
  configure(options: WiFiConfigureOptions): WiFiStatus;

  connect(ssid: string, options?: WiFiConnectOptions): WiFiConnectResult;
  disconnect(timeoutMs?: number): WiFiStatus;
  scan(options?: WiFiScanOptions): WiFiScanRecord[];

  startAP(options: WiFiAccessPointOptions): WiFiAccessPointStatus;
  stopAP(timeoutMs?: number): WiFiStatus;
  apClients(options?: WiFiAPClientsOptions): WiFiAPClient[];
  deauthClient(address: MacAddress, options?: WiFiDeauthOptions): boolean;

  setPowerSave(mode: WiFiPowerSaveMode): WiFiPowerSaveMode;
  setTxPower(dbm: number): number;
  setCountry(code: string, options?: WiFiCountryOptions): WiFiCountryStatus;
  setChannel(channel: number, options?: WiFiSetChannelOptions): WiFiChannelStatus;
  getMac(iface: WiFiInterface): MacAddress;
  setMac(iface: WiFiInterface, address: MacAddress): MacAddress;
  acquireWakeLock(): WiFiWakeLock;

  readonly driver: WiFiDriverAPI;
  readonly monitor?: WiFiMonitorAPI;
  readonly rawTx?: WiFiRawTxAPI;
  readonly csi?: WiFiCsiAPI;
  readonly action?: WiFiActionAPI;
  readonly ftm?: WiFiFtmAPI;
  readonly twt?: WiFiTwtAPI;
  readonly vendorIe?: WiFiVendorIeAPI;
  readonly roaming?: WiFiRoamingAPI;
  readonly enterprise?: WiFiEnterpriseAPI;
  readonly wapi?: WiFiWapiAPI;
  readonly wps?: WiFiWpsAPI;
  readonly dpp?: WiFiDppAPI;
  readonly smartConfig?: WiFiSmartConfigAPI;
  readonly nan?: WiFiNanAPI;
  readonly mesh?: WiFiMeshAPI;
  readonly diagnostics: WiFiDiagnosticsAPI;
}

declare const wifi: WiFiModule;
```

命名约束：

- 常用调用不超过 `wifi.method()`；
- 高级调用不超过 `wifi.feature.method()`；
- session/handle 返回后使用 `session.method()`，不继续拼接静态 namespace；
- 不创建 `wifi.station`、`wifi.accessPoint`、`wifi.radio`；
- `espNow` 保持独立顶层模块，不改成 `wifi.espNow`。

---

## 4. 全局运行时规则

### 4.1 能力探测

- `sys.info.features` 决定命名空间是否存在；
- `wifi.capabilities()` 提供摘要；
- `wifi.driver.capabilities()` 提供逐 operation 和 target 约束；
- 不得根据 board 名称推断 5 GHz、HE、TWT、FTM、NAN、Mesh 或天线能力；
- unsupported option 必须拒绝，不能静默降级。

### 4.2 Future 和并发

- 一次性原生操作保持 cooperative direct-call；
- 显式并发使用 `Future.call()`；
- 同一资源 lane FIFO；
- timeout 不隐式重试、换信道、降安全或修改其他 owner；清理本次未完成 connect 可撤销本次连接，但必须在该操作契约中明确，不能断开无关连接；
- 取消 scan、FTM、ROC、WPS、DPP、SmartConfig 时必须调用对应 driver stop/end API。

### 4.3 二进制和所有权

- 大数据使用 `ByteView`、`ByteSpanSource` 或 `Stream`；
- callback 中不创建 JSValue，不调用 JS，不做按帧可变分配；
- callback 数据必须立即复制到固定 pool；
- retained view/source 增加数据 storage lease，不要求为了保留数据继续占用 Radio/driver；
- `close()` 是主生命周期，finalizer 只兜底。

### 4.4 EventQueue：三类不同的交付保证

| 类别 | 包括 | 保证 |
| --- | --- | --- |
| 数据/观察 | Monitor、CSI、Vendor IE、公开 watch 副本 | 可 drop-newest；逐原因计数；exactly-once 返回 slot |
| 原生控制完成 | TX done、scan done、connect/disconnect、取消/关闭完成 | 必须先更新 native operation/state 并唤醒 Future，不依赖公开队列成功 |
| 需应用处理的请求/凭据 | 配网结果、NAN datapath request 等 | 有界保留、admission、过期/拒绝/恢复策略；不能确认后无声丢弃 |

TX admission 默认 `reject-newest`。公开 `wifi.watch()` 与 session 可以各自收到 normalized snapshot，但 watch 副本丢失不得阻止 session 终止。不能通过一个无限增长的“可靠队列”解决溢出；可靠的是已接纳 operation 的原生终态与有界资源登记。

订阅者耗尽、队列满、pool 满、malformed、closing 等独立统计。全局观察队列不接收秘密 payload，也不持有 driver pointer。

### 4.5 错误模型

```ts
interface NativeError extends Error {
  code: string;
  operation: string;
  details: Record<string, unknown>;
}
```

- 参数类型和范围错误使用 `TypeError`/`RangeError`；
- driver 运行错误使用上述结构；
- error/status/log 不得包含 Wi-Fi 密码、EAP password、private key、PMK/LMK；
- `details` 可包含 `espCode`、`espName`、stage、interface、channel、generation；
- 不允许仅返回 `false` 丢失 ESP-IDF 错误上下文，除非方法语义本身是查询布尔值。

---

## 5. 公共数据类型

```ts
type MacAddress = string;
type WiFiInterface = "station" | "access-point" | "nan";
type WiFiMode = "none" | "station" | "ap" | "apsta" | "nan";
type WiFiBand = "2.4GHz" | "5GHz";
type WiFiBandMode = "2.4GHz-only" | "5GHz-only" | "auto";
type WiFiSecondaryChannel = "none" | "above" | "below";
type WiFiPowerSaveMode = "none" | "minimum" | "maximum";
type WiFiTimestampAccuracy = "normal" | "power-save-dependent" | "callback-time";
type WiFiPhyFormat =
  | "legacy" | "ht" | "vht"
  | "he-su" | "he-mu" | "he-er-su" | "he-tb"
  | "unknown";

type WiFiProtocol = "11b" | "11g" | "11n" | "11a" | "11ac" | "11ax" | "lr";
type WiFiPacketType = "management" | "control" | "data" | "misc" | "unknown";
type WiFiFcsState = "unknown" | "absent" | "present-valid" | "present-invalid";
```

### 5.1 公共接收元数据

```ts
interface WiFiRxInfo {
  sequence: number;
  timestampUs: number;
  timestampAccuracy: WiFiTimestampAccuracy;
  rxSequence: number | null;
  radioGeneration: number;

  signal: {
    rssi: number;
    noiseFloor: number | null;
    antenna: number | null;
  };

  channel: {
    band: WiFiBand | null;
    primary: number;
    secondary: WiFiSecondaryChannel;
  };

  phy: {
    format: WiFiPhyFormat;
    bandwidthMHz: 20 | 40 | 80 | 160 | null;
    mcs: number | null;
    legacyRate: number | null;
    stbc: boolean | null;
    shortGuardInterval: boolean | null;
    fecCoding: "bcc" | "ldpc" | null;
    aggregation: boolean | null;
    ampduCount: number | null;
    smoothingRecommended: boolean | null;
    sounding: boolean | null;
  };

  addresses: {
    source: MacAddress | null;
    destination: MacAddress | null;
    transmitter: MacAddress | null;
    receiver: MacAddress | null;
    bssid: MacAddress | null;
  };

  packet: WiFiPacketInfo | null;
}

interface WiFiPacketInfo {
  type: WiFiPacketType;
  subtype: number;
  subtypeName: string | null;
  frameControl: number | null;
  durationId: number | null;
  sequenceControl: number | null;
  qosControl: number | null;
  flags: {
    toDs: boolean | null;
    fromDs: boolean | null;
    moreFragments: boolean | null;
    retry: boolean | null;
    powerManagement: boolean | null;
    moreData: boolean | null;
    protected: boolean | null;
    order: boolean | null;
  };
  capture: {
    mode: "header" | "full";
    headerLength: number;
    payloadLength: number;
    driverLength: number;
    capturedLength: number;
    payloadCapturedLength: number;
    truncated: boolean;
    fcs: WiFiFcsState;
    pointerLayoutValid: boolean;
    parseValid: boolean;
  };
}
```

原始 bytes 始终是权威数据。解析字段用于过滤和诊断；解析失败不修改原始 bytes。

---

## 6. `wifi.capabilities()`、`status()` 与事件

### 6.1 能力对象

```ts
interface WiFiCapabilities {
  apiVersion: "wifi/1";
  target: string;
  idfVersion: string;
  buildProfile: string;

  modes: WiFiMode[];
  interfaces: WiFiInterface[];
  bands: Array<{
    band: WiFiBand;
    channels: number[] | null;
    protocols: WiFiProtocol[];
    bandwidthsMHz: number[];
  }>;

  security: {
    authModes: string[];
    pmf: boolean;
    sae: boolean;
    owe: boolean;
    enterprise: boolean;
    wapi: boolean;
    wps: boolean;
    dpp: boolean;
  };

  features: {
    station: boolean;
    accessPoint: boolean;
    apsta: boolean;
    scan: boolean;
    promiscuous: boolean;
    rawTx: boolean;
    csi: boolean;
    actionTx: boolean;
    remainOnChannel: boolean;
    vendorIe: boolean;
    ftmInitiator: boolean;
    ftmResponder: boolean;
    individualTwt: boolean;
    broadcastTwt: boolean;
    rrm: boolean;
    wnm: boolean;
    smartConfig: boolean;
    nan: boolean;
    mesh: boolean;
    multipleAntennas: boolean;
  };

  limits: {
    maxSoftApClients: number;
    maxScanRecords: number;
    rawTxMinBytes: number;
    rawTxMaxBytes: number;
    minimumTxPowerDbm: number;
    maximumTxPowerDbm: number;
  };

  namespaces: string[];
}
```

`channels: null` 仅表示当前 IDF/target 不能权威枚举，绝不表示法规无限制。

### 6.2 状态对象保持常用字段顶层

```ts
interface WiFiStatus {
  generation: number;
  initialized: boolean;
  started: boolean;
  driverState: "uninitialized" | "initializing" | "stopped" | "starting" | "started" | "stopping" | "restarting" | "cleanup-pending" | "faulted";
  mode: WiFiMode;
  storage: "ram" | "flash";

  scanning: boolean;
  connecting: boolean;
  connected: boolean;
  ssid: string | null;
  bssid: MacAddress | null;
  rssi: number | null;
  channel: number | null;
  secondaryChannel: WiFiSecondaryChannel;
  band: WiFiBand | null;

  powerSave: WiFiPowerSaveMode | null;
  maxTxPowerDbm: number | null;
  country: WiFiCountryStatus | null;
  bandMode: WiFiBandMode | null;

  accessPoint: WiFiAccessPointStatus | null;

  radio: {
    channelGeneration: number;
    fixedChannel: null | {
      channel: number;
      secondaryChannel: WiFiSecondaryChannel;
      owners: string[];
    };
    clients: {
      application: number;
      station: number;
      accessPoint: number;
      espNow: number;
      monitor: number;
      rawTx: number;
      csi: number;
      action: number;
      nan: number;
      mesh: number;
    };
    promiscuousUsers: string[];
  };

  lastDisconnectReason: number | null;
  lastDisconnectReasonName: string | null;
  droppedDriverEvents: number;
  lastError: NativeErrorSnapshot | null;
}
```

IP、DNS、route 和 interface readiness 继续由 `net.status()` / `net.watch()` 提供。

### 6.3 全量 Wi-Fi 事件

```ts
interface WiFiWatchOptions {
  events?: string[] | "all";
  capacity?: number;
  overflow?: "drop-newest";
  includeRawEventData?: boolean;
}

interface WiFiEvent {
  sequence: number;
  timestampUs: number;
  id: number;
  name: string;
  category:
    | "station" | "access-point" | "scan" | "ftm" | "twt"
    | "wps" | "dpp" | "nan" | "mesh" | "radio" | "unknown";
  data: Record<string, unknown>;
  rawEventData?: ByteView;
  rawEventSchema?: string;
  rawEventDataUnavailableReason?: "not-reviewed" | "sensitive" | "unsupported-layout";
}
```

要求（本轮修订）：

- 固定 IDF 的公开 `WIFI_EVENT_*` 都有 inventory 与稳定 name；已实现事件必须有审查过的 converter。
- 小型状态事件转换为 typed snapshot；未知事件可以只输出 id/name/category，不读取未知 payload。
- `includeRawEventData` 不是结构体透传开关。每种事件必须登记长度获取规则、最大字节数、变长/指针处理、秘密级别、销毁路径和 schema。
- 不能对变长事件统一执行 `memcpy(sizeof(event_struct))`；更不能把包含指针的结构直接导出。
- `rawEventData` 仅对审查通过且非敏感的布局提供。事件对象不是隐式资源 owner；已交付 `rawEventData` 由调用者显式 `close()`，队列关闭仅释放尚未交付的条目。
- DPP/WPS/SmartConfig 凭据只由专属 session/credential owner 交付；global watch、日志、诊断、RPC inspection 只接收脱敏状态。
- raw export 被禁止/未实现时，返回明确的 unavailable reason，不把空 bytes 伪装成原始事件。
- DPP 事件契约以固定 IDF 的 Wi-Fi events 为准，不创建旧 callback 兼容接口。

事件 descriptor 建议包含：`eventId`、`schema`、`lengthRule`、`maxBytes`、`pointerPolicy`、`secretPolicy`、`deliveryClass`、`copyFn`、`dropFn`。这些是内部审查字段，不是公共 ABI。

---

## 7. 顶层基础 Wi-Fi API

### 7.1 Driver start/stop/configure

```ts
interface WiFiStartOptions {
  mode?: "station" | "ap" | "apsta";
  storage?: "ram" | "flash";
}

interface WiFiStopOptions {
  timeoutMs?: number;
}

interface WiFiConfigureOptions {
  storage?: "ram" | "flash";
  mode?: "station" | "ap" | "apsta";
  country?: string | WiFiCountryDetails;
  station?: WiFiStationDriverConfig;
  accessPoint?: WiFiAccessPointDriverConfig;
  protocols?: Partial<Record<WiFiInterface, WiFiProtocolConfig>>;
  bandwidths?: Partial<Record<WiFiInterface, WiFiBandwidthConfig>>;
  txPowerDbm?: number;
  powerSave?: WiFiPowerSaveMode;
  start?: boolean;
  allowDisconnect?: boolean;
}
```

`wifi.configure()` 是复合事务，不是隐藏兼容层：先验证和分配，随后按 IDF 要求
stop/configure/start；默认不允许断开现有连接。无法回滚的 driver side effect 必须在
返回值和错误 `details.stage` 中说明。

### 7.2 Station 连接

```ts
interface WiFiConnectOptions {
  password?: string;
  bssid?: MacAddress;
  channel?: number;
  scanMethod?: "fast" | "all-channel";
  sortMethod?: "signal" | "security";
  minimumRssi?: number;
  minimumAuthMode?: string;
  pmf?: "disabled" | "optional" | "required";
  listenInterval?: number;
  failureRetryCount?: number;
  rmEnabled?: boolean;
  btmEnabled?: boolean;
  mboEnabled?: boolean;
  ftEnabled?: boolean;
  oweEnabled?: boolean;
  saePwe?: "hunting-and-pecking" | "hash-to-element" | "both";
  saeH2eIdentifier?: string;
  timeoutMs?: number;
  driver?: Partial<WiFiStationDriverConfig>;
}

interface WiFiConnectResult {
  connected: true;
  ssid: string;
  bssid: MacAddress;
  channel: number;
  rssi: number | null;
  negotiatedPhy: string | null;
  aid: number | null;
  elapsedMs: number;
}
```

调用保持：

```js
wifi.connect("Office WiFi", { password: "..." });
wifi.disconnect();
```

连接参数中的 target-specific 字段由 `driver` 对象承载；字段清单来自生成的
IDF config schema，unsupported 字段直接拒绝。

### 7.3 扫描

```ts
interface WiFiScanOptions {
  channel?: "all" | number;
  channels?: { ghz2?: number[]; ghz5?: number[] };
  coexistenceBackgroundScan?: boolean;
  ssid?: string;
  bssid?: MacAddress;
  showHidden?: boolean;
  mode?: "active" | "passive";
  activeMinMs?: number;
  activeMaxMs?: number;
  passiveMs?: number;
  homeChannelDwellMs?: number;
  maxRecords?: number;
  timeoutMs?: number;
}

interface WiFiScanRecord {
  ssid: string;
  hidden: boolean;
  bssid: MacAddress;
  rssi: number;
  channel: number;
  secondaryChannel: WiFiSecondaryChannel;
  band: WiFiBand | null;
  authMode: string;
  pairwiseCipher: string | null;
  groupCipher: string | null;
  antenna: number | null;
  protocols: WiFiProtocol[];
  country: WiFiCountryStatus | null;
  capabilities: {
    wps: boolean | null;
    ftmResponder: boolean | null;
    ftmInitiator: boolean | null;
    he: boolean | null;
    vht: boolean | null;
  };
}
```

`wifi.scan()` owner 必须在 success、timeout、cancel、conversion failure 时按固定 IDF 的资源规则获取/释放扫描结果，禁止泄漏 driver scan buffer；清理仍需等待原生扫描结果不再被 driver 使用。

本轮补充：`channels` 映射 target 支持的 scan channel bitmap，`coexistenceBackgroundScan` 映射对应公开字段，不因顶层已经有 `scan()` 就把这两个字段视为已覆盖。numeric `channel` 与 `channels` 互斥；空 bitmap、bit0 bypass、band 不支持、法规不允许、重复信道及未知字段均明确校验。构建不支持时拒绝，不悄悄变为全信道扫描。

Scan 属于 off-home-channel 活动，不能仅因为最终会回到原信道就忽略 fixed-channel lease。存在不允许扫描中断的 CSI/Monitor/ESP-NOW 等 owner 时先报 conflict；connected STA 的普通扫描按 IDF 和已声明 coexistence policy 执行，记录有效参数。

### 7.4 SoftAP/APSTA

```ts
interface WiFiAccessPointOptions {
  ssid: string;
  password?: string;
  channel?: number;
  hidden?: boolean;
  authMode?: string;
  maxConnections?: number;
  beaconIntervalMs?: number;
  pairwiseCipher?: string;
  pmf?: "disabled" | "optional" | "required";
  ftmResponder?: boolean;
  saePwe?: "hunting-and-pecking" | "hash-to-element" | "both";
  driver?: Partial<WiFiAccessPointDriverConfig>;
}

interface WiFiAccessPointStatus {
  started: boolean;
  ssid: string;
  hidden: boolean;
  channel: number;
  authMode: string;
  maxConnections: number;
  clientCount: number;
  mac: MacAddress;
}

interface WiFiAPClient {
  address: MacAddress;
  aid: number | null;
  rssi: number | null;
  phy: WiFiProtocol[];
  ip?: string | null;
}
```

基础调用：

```js
wifi.startAP({ ssid: "ESP32QJS", password: "12345678", channel: 6 });
print(JSON.stringify(wifi.apClients()));
wifi.deauthClient("aa:bb:cc:dd:ee:ff");
wifi.stopAP();
```

APSTA 只有一个物理信道。STA 已连接时，AP 必须使用相同信道；框架不能静默断开
STA 或隐式改变 ESP-NOW/CSI/Monitor 的固定信道。

### 7.5 常用 Radio 设置

```ts
interface WiFiCountryOptions {
  policy?: "auto" | "manual";
  ieee80211d?: boolean;
}

interface WiFiSetChannelOptions {
  secondaryChannel?: WiFiSecondaryChannel;
}

interface WiFiWakeLock {
  readonly acquired: boolean;
  close(): void;
}
```

基础调用：

```js
wifi.setCountry("TW", { policy: "manual" });
wifi.setTxPower(18);
wifi.setPowerSave("none");
wifi.setChannel(6);
```

`WiFiWakeLock` 映射 force-wakeup acquire/release，必须 generation-checked、幂等关闭，
不能让调用者漏掉 release。

---

## 8. `wifi.driver`：完整高级 Driver 控制

`wifi.driver` 不是任意 ABI escape hatch，而是对 ESP-IDF 公开 Wi-Fi Driver 控制面的 typed、状态安全映射。**所有 setter 与顶层 Wi-Fi 共用 Radio mutation lane 和 lease 检查**；不允许 Driver namespace 成为绕过资源管理的第二条路径。

```ts
interface WiFiDriverAPI {
  capabilities(): WiFiDriverCapabilities;
  status(): WiFiDriverStatus;

  getMode(): WiFiMode;
  setMode(mode: WiFiMode): WiFiMode;
  setStorage(storage: "ram" | "flash"): "ram" | "flash";

  getInterfaceConfig(
    iface: WiFiInterface,
    options?: { includeSecrets?: boolean }
  ): WiFiStationDriverConfig | WiFiAccessPointDriverConfig;
  setInterfaceConfig(
    iface: WiFiInterface,
    config: WiFiStationDriverConfig | WiFiAccessPointDriverConfig
  ): object;

  getProtocol(iface: WiFiInterface): WiFiProtocol[];
  setProtocol(iface: WiFiInterface, protocols: WiFiProtocol[]): WiFiProtocol[];
  getProtocols(iface: WiFiInterface): WiFiProtocolConfig;
  setProtocols(iface: WiFiInterface, config: WiFiProtocolConfig): WiFiProtocolConfig;

  getBandwidth(iface: WiFiInterface): number;
  setBandwidth(iface: WiFiInterface, mhz: 20 | 40): number;
  getBandwidths(iface: WiFiInterface): WiFiBandwidthConfig;
  setBandwidths(iface: WiFiInterface, config: WiFiBandwidthConfig): WiFiBandwidthConfig;

  getBand(): WiFiBand;
  setBand(band: WiFiBand): WiFiBand;
  getBandMode(): WiFiBandMode;
  setBandMode(mode: WiFiBandMode): WiFiBandMode;

  getChannel(): WiFiChannelStatus;
  getHomeChannel(): WiFiChannelStatus;
  getCountry(): WiFiCountryStatus;
  setCountryDetails(details: WiFiCountryDetails): WiFiCountryStatus;

  getPowerSave(): WiFiPowerSaveMode;
  getTxPower(): number;
  getRssi(): number;
  getAid(): number;
  getNegotiatedPhy(): string;
  getTsfTime(iface: "station" | "access-point"): number;

  setInactiveTime(iface: "station" | "access-point", seconds: number): number;
  getInactiveTime(iface: "station" | "access-point"): number;
  setRssiThreshold(dbm: number): number;
  setDynamicCarrierSense(enabled: boolean): boolean;

  configure11bRate(iface: "station" | "access-point", disabled: boolean): boolean;
  configureTxRate(iface: "station" | "access-point", config: WiFiTxRateConfig): object;
  disablePmf(iface: "station" | "access-point"): boolean;

  setConnectionlessWakeInterval(milliseconds: number): number;
  setCoexistencePowerManagement(enabled: boolean): boolean;

  getEventMask(): number;
  setEventMask(mask: number): number;

  getAntenna(): WiFiAntennaConfig;
  setAntenna(config: WiFiAntennaConfig): WiFiAntennaConfig;
  getAntennaGpio(): WiFiAntennaGpioConfig;
  setAntennaGpio(config: WiFiAntennaGpioConfig): WiFiAntennaGpioConfig;

  clearFastConnect(): boolean;
  restore(): boolean;
  restart(options?: WiFiDriverRestartOptions): WiFiStatus;
}
```

### 8.0 生命周期与破坏性操作补充

`WiFiDriverRestartOptions` 在本轮定义为 `{ timeoutMs?: number }`；restart 只在 STA/AP/ESP-NOW/CSI/Monitor/Raw TX/其他子 owner 均退出、没有 cleanup-pending 时执行。没有 `force` 或 `requireExclusive:false` 绕过路径。

`restore()`、setMode/setBand/setMac 等各有前置状态；不符合时失败，不能替调用者停止旧资源。`wifi.configure({allowDisconnect:true})` 只授权其文档列明的 Wi-Fi 配置事务，不能越权关闭 ESP-NOW/采集 session。

`setEventMask()` 不得禁用框架完成 Future/维护生命周期必需的事件；请求掩盖保留控制事件时拒绝。观察过滤应优先使用 `wifi.watch({events})`。

rate/bandwidth 等配置的恢复需要已知前值和独占 mutation 权。没有 public getter 且未能从框架唯一写入记录获得可信前值时，不提供会伪造恢复的临时 lease。

### 8.1 完整 config schema

`WiFiStationDriverConfig` 和 `WiFiAccessPointDriverConfig` 必须覆盖当前 target 的
`wifi_sta_config_t` / `wifi_ap_config_t` 所有公开字段。不要手写一个永久不更新的
最小子集；由脚本从 IDF 6.1 header + checked mapping 生成 declaration fragment。字段适用条件、单位、bitfield、固定数组、秘密读回和读写方向必须人工审查；生成工具不直接把 C 内存布局当作 JS schema。

```ts
interface WiFiDriverCapabilities {
  apiVersion: "wifi-driver/1";
  target: string;
  idfVersion: string;
  operations: Array<{
    jsPath: string;
    idfSymbols: string[];
    available: boolean;
    stateRequirements: string[];
  }>;
  configSchemas: {
    stationFields: string[];
    accessPointFields: string[];
    scanFields: string[];
    txRateFields: string[];
  };
}
```

### 8.2 状态限制必须原样保留

- IDF 要求 start 前调用的配置，不允许 framework 在后台悄悄 restart；
- 调用者可显式使用 `wifi.configure()` 或 `wifi.driver.restart()`；
- `setMac()` 仅在对应 interface disabled 时成功；
- connected STA 上 application sequence raw TX 必须拒绝；
- 5 GHz、11ac、11ax、TWT、NAN 等均 target-gated；
- security 相关配置不能因 unsupported 自动退回较弱模式。

### 8.3 Secret readback

- 默认 `includeSecrets=false`；password/private material 返回 redacted；
- `includeSecrets=true` 只在 `CONFIG_ESP32_MQUICKJS_WIFI_ALLOW_SECRET_READBACK=y` 时存在；
- 读取结果不能进入 status、error、log、RPC inspection 或 generated snapshot；
- 生产 profile 默认关闭。

---

## 9. `wifi.monitor` 原始 802.11 帧捕获 API

### 9.1 能力

```ts
interface WiFiMonitorCapabilities {
  apiVersion: "wifi-monitor/1";
  target: string;
  idfVersion: string;
  frameTypes: WiFiPacketType[];
  supports: {
    fixedChannel: boolean;
    typeFilter: boolean;
    subtypeFilter: boolean;
    sourceMacFilter: boolean;
    destinationMacFilter: boolean;
    bssidFilter: boolean;
    rssiFilter: boolean;
    nativeDecimation: boolean;
    nativeRateLimit: boolean;
    batch: boolean;
    wireSource: boolean;
    hostPcapngConverter: boolean;
  };
  limits: {
    maxFrameBytes: number;
    maxPoolCapacity: number;
    maxQueueCapacity: number;
    maxBatchFrames: number;
  };
}
```

### 9.2 打开 session

```ts
interface WiFiMonitorOpenOptions {
  channel?: "current" | number;

  filter?: {
    types?: WiFiPacketType[];
    subtypes?: number[];
    sourceMac?: MacAddress | MacAddress[];
    destinationMac?: MacAddress | MacAddress[];
    bssid?: MacAddress | MacAddress[];
    minimumRssi?: number;
    sampleEvery?: number;
    maximumRateHz?: number;
    validOnly?: boolean;
  };

  capture?: {
    snapLength?: number;
    requireComplete?: boolean;
  };

  buffering?: {
    poolCapacity?: number;
    queueCapacity?: number;
    overflow?: "drop-newest";
  };

  powerSavePolicy?: "preserve" | "require-none";
}

interface WiFiMonitorAPI {
  capabilities(): WiFiMonitorCapabilities;
  open(options?: WiFiMonitorOpenOptions): WiFiMonitorSession;
}
```

语义：

- `channel: "current"` 跟随共享 Radio 当前信道。
- numeric channel 通过 promiscuous broker 申请固定信道。
- STA/AP 已绑定其他信道时 numeric open 失败。
- `requireComplete: true` 时，超过 `snapLength` 的帧整体丢弃并计数；否则交付截断帧并设置 `truncated=true`。
- 捕获的是 ESP‑IDF promiscuous callback 提供的 MAC 帧，不是 RF IQ 数据。

### 9.3 Session 与 Frame

```ts
interface WiFiMonitorStatus {
  generation: number;
  state: "running" | "stopped" | "stopping" | "closing" | "cleanup-pending" | "closed" | "faulted";
  requested: WiFiMonitorOpenOptions;
  effective: {
    channel: number;
    secondaryChannel: WiFiSecondaryChannel;
    radioGeneration: number;
    snapLength: number;
    poolCapacity: number;
    queueCapacity: number;
    timestampAccuracy: WiFiTimestampAccuracy;
  };
  lastError: NativeErrorSnapshot | null;
}

interface WiFiMonitorStats {
  callbacks: number;
  accepted: number;
  deliveredFrames: number;
  deliveredBatches: number;
  filteredType: number;
  filteredSubtype: number;
  filteredMac: number;
  filteredRssi: number;
  filteredDecimation: number;
  filteredRateLimit: number;
  invalidCallbackData: number;
  invalidHeader: number;
  truncatedFrames: number;
  droppedRequiredComplete: number;
  droppedPoolFull: number;
  droppedQueueFull: number;
  droppedClosing: number;
  receivedBytes: number;
  capturedBytes: number;
  leasedFrames: number;
  freePoolSlots: number;
  queue: EventQueueStats;
}

interface WiFiMonitorFrame {
  readonly info: WiFiRxInfo;
  bytes(): ByteView;
  copyBytes(): ByteView;
  source(): ByteSpanSource;
  close(): void;
}

interface WiFiMonitorBatch {
  readonly frameCount: number;
  info(index: number): WiFiRxInfo;
  bytes(index: number): ByteView;
  source(options: { format: "esp32qjs-monitor/1" }): ByteSpanSource;
  close(): void;
}

interface WiFiMonitorSession {
  status(): WiFiMonitorStatus;
  stats(): WiFiMonitorStats;
  receive(timeoutMs?: number): WiFiMonitorFrame | null;
  receiveBatch(options?: WiFiReceiveBatchOptions): WiFiMonitorBatch | null;
  stop(): WiFiMonitorStatus;
  configure(options: WiFiMonitorOpenOptions): WiFiMonitorStatus;
  start(): WiFiMonitorStatus;
  close(): void;
}
```

Monitor native source 不直接生成 PCAPNG，避免在设备端伪造 wall-clock、Radiotap 或 target 不可靠字段。设备输出唯一的 `esp32qjs-monitor/1`；host 工具再按明确的时钟锚点和可用 PHY 元数据转换成 PCAPNG。

### 9.4 `esp32qjs-monitor/1` wire format

该格式与 CSI v1 共享同一个 256-byte RX metadata record，但 CSI 相关字段必须写为零、`unknown`，segment count 必须为 0。

Batch header 仍为 32 字节：

| Offset | Type | 含义 |
| ---: | --- | --- |
| 0 | `u8[8]` | ASCII `E32QMON1` |
| 8 | `u16` | version，固定 1 |
| 10 | `u16` | header bytes，固定 32 |
| 12 | `u32` | frame count |
| 16 | `u16` | directory record bytes，固定 24 |
| 18 | `u16` | metadata record bytes，固定 256 |
| 20 | `u32` | flags；v1 bit 0 表示 canonical layout |
| 24 | `u32` | total bytes |
| 28 | `u32` | reserved 0 |

每帧 24-byte directory：

| Offset | Type | 含义 |
| ---: | --- | --- |
| 0 | `u32` | metadata absolute offset |
| 4 | `u32` | packet absolute offset |
| 8 | `u32` | captured packet length |
| 12 | `u32` | driver payload length |
| 16 | `u32` | driver packet length |
| 20 | `u16` | MAC header length |
| 22 | `u16` | 与 CSI directory 相同的 packet record flags |

Canonical ordering：header、全部 directory、全部 metadata、按 frame index 排列的 packet bytes；每个 packet section 4-byte 对齐，padding 为零。

新增 host 工具：

```text
scripts/esp32qjs_monitor.py
```

职责：严格解析 `esp32qjs-monitor/1`、输出 JSONL 摘要，并在用户提供 UTC boot anchor 或可接受相对时间时转换 PCAPNG。设备端不承担 PCAPNG wall-clock 策略。

---

---

## 10. `wifi.rawTx` 原始 802.11 发包 API

### 10.1 设计边界

`wifi.rawTx` 映射 ESP-IDF `esp_wifi_80211_tx()` 与 TX done callback。v1 只承诺
ESP-IDF 公开支持的 frame：Beacon、Probe Request、Probe Response、Action 和
non-QoS Data。不得宣称支持 encrypted frame、QoS Data、任意 Control frame、PHY
preamble 或 RF IQ。

```ts
interface WiFiRawTxCapabilities {
  apiVersion: "wifi-raw-tx/1";
  target: string;
  idfVersion: string;
  interfaces: Array<"station" | "access-point">;
  frameTypes: {
    beacon: boolean;
    probeRequest: boolean;
    probeResponse: boolean;
    action: boolean;
    nonQosData: boolean;
    qosData: false;
    encryptedData: false;
    arbitraryControl: false;
  };
  supports: {
    driverSequence: true;
    applicationSequence: true;
    txDoneCallback: boolean;
    fixedChannel: boolean;
    nativeQueue: boolean;
    batchAdmission: boolean;
    periodicTx: boolean;
    rateLease: boolean;
  };
  limits: {
    minimumFrameBytes: 24;
    maximumFrameBytes: 1500;
    maximumQueueCapacity: number;
    maximumBatchFrames: number;
    minimumPeriodicIntervalUs: number;
  };
}

interface WiFiRawTxAPI {
  capabilities(): WiFiRawTxCapabilities;
  send(frame: ByteSource, options?: WiFiRawTxSendOptions): WiFiRawTxResult;
  open(options?: WiFiRawTxOpenOptions): WiFiRawTxSession;
}
```

### 10.2 One-shot 发送

```ts
interface WiFiRawTxSendOptions {
  interface?: "station" | "access-point";
  channel?: "current" | number;
  sequenceControl?: "driver" | "application";
  validation?: "strict" | "basic";
  timeoutMs?: number;
}

interface WiFiRawTxResult {
  sequence: number;
  interface: "station" | "access-point";
  channel: number;
  frameType: "beacon" | "probe-request" | "probe-response" | "action" | "non-qos-data";
  byteLength: number;
  submittedAtUs: number;
  completedAtUs: number | null;
  driverAccepted: boolean;
  driverCompleted: boolean;
  driverStatus: "success" | "failed" | "unknown";
  rate: string | null;
}
```

```js
var result = wifi.rawTx.send(frameBytes, {
  interface: "station",
  sequenceControl: "driver",
  timeoutMs: 1000
});
```

TX completion 是 MAC/driver 结果，不是业务 ACK；返回对象不得使用
`acknowledged: true` 之类误导字段。

### 10.3 Session、队列和周期发包

```ts
interface WiFiRawTxOpenOptions {
  interface?: "station" | "access-point";
  channel?: "current" | number;
  sequenceControl?: "driver" | "application";
  validation?: "strict" | "basic";
  rate?: WiFiTxRateConfig;
  queue?: {
    capacityPackets?: number;
    overflow?: "reject-newest" | "drop-oldest-batch";
  };
}

interface WiFiRawTxSession {
  send(frame: ByteSource, options?: { timeoutMs?: number }): WiFiRawTxResult;
  enqueue(frame: ByteSource): WiFiRawTxAdmission;
  enqueueBatch(frames: ArrayLike<ByteSource>): WiFiRawTxBatchAdmission;
  flush(timeoutMs?: number): WiFiRawTxFlushResult;
  startPeriodic(options: WiFiRawPeriodicTxOptions): WiFiRawPeriodicTx;
  status(): WiFiRawTxStatus;
  stats(): WiFiRawTxStats;
  close(): void;
}

interface WiFiRawPeriodicTxOptions {
  frame: ByteSource;
  intervalUs: number;
  count?: number;             // 0 = continuous
  startDelayUs?: number;
  busyPolicy?: "skip" | "stop";
  stopOnError?: boolean;
}

interface WiFiRawPeriodicTx {
  status(): {
    state: "running" | "stopped" | "faulted" | "closed";
    scheduled: number;
    submitted: number;
    completed: number;
    failed: number;
    skippedBusy: number;
  };
  stop(): void;
  close(): void;
}
```

周期发送必须在 native worker/timer 中调度，不使用 JS `setInterval()` 作为高频 CSI
激励源。任一时刻只允许一个 native raw TX in-flight；前一包未完成时按 busyPolicy
处理，不能无限积压。

### 10.4 严格验证

`validation: "strict"` 默认检查：

- 长度 24～1500 字节；
- protocol version、type/subtype 和可变 MAC header length；
- Protected bit 必须为 0；
- QoS Data 拒绝；
- frame type 必须位于 capability allowlist；
- connected STA/AP 上必须使用 driver sequence；
- STA→AP、AP→STA 的 ToDS/FromDS 与接口关系；
- connected path 上 driver 明确禁止的 PM/MoreData/Retry 组合；
- 输入必须是纯 802.11 MAC frame，不含 Radiotap、PCAP record header；
- FCS 是否接受由 target capability 明确报告，未知时默认不接受调用者追加 FCS。

### 10.5 Radio 与 rate lease

- numeric channel 使用共享 fixed-channel lease；
- 与 connected STA、SoftAP、ESP-NOW、Monitor、CSI、NAN/Mesh 冲突时无副作用失败；
- session rate/bandwidth 是 interface-global side effect，必须获取 rate lease；
- close 时恢复之前配置；恢复失败进入 cleanup-pending，不可直接复用 lane；
- rawTx 不占有 promiscuous callback，但可与同信道 monitor/CSI 共存。

### 10.6 主动 CSI 拓扑

CSI 是接收数据包后形成的信道估计。推荐两台设备：

```text
TX ESP32: wifi.rawTx / espNow -> fixed-rate packets
RX ESP32: wifi.csi -> CSI + correlated 802.11 packet
```

不增加 `wifi.csi.send()`；发送和接收职责保持分离。

---

## 11. `wifi.csi` 完整 v1 API

### 11.1 能力对象

保留当前 legacy/HE target adapter 思路，增加 packet capture 能力：

```ts
interface WiFiCsiCapabilities {
  apiVersion: "wifi-csi/1";
  wireFormat: "esp32qjs-csi/1";
  target: string;
  idfVersion: string;

  configSchema: "wifi-csi-legacy/1" | "wifi-csi-he/1";
  sources: ("associated" | "promiscuous")[];
  phyFormats: WiFiPhyFormat[];
  sampleEncodings: (
    | "signed-int8"
    | "signed-int12-le"
    | "signed-int12-packed"
  )[];

  packetCapture: {
    supported: boolean;
    header: boolean;
    full: boolean;
    maxHeaderBytes: number;
    maxPacketBytes: number;
    fcsStateReliable: boolean;
    frameControlParsing: boolean;
  };

  radio: {
    country: string | null;
    policy: "auto" | "manual" | null;
    bands: Array<{
      band: WiFiBand;
      allowedChannels: number[] | null;
    }>;
  };

  limits: {
    maxCsiBytes: number;
    maxPacketBytes: number;
    maxPoolCapacity: number;
    maxQueueCapacity: number;
    maxBatchFrames: number;
    scaleMinimum: number | null;
    scaleMaximum: number | null;
    shiftMinimum: number | null;
    shiftMaximum: number | null;
  };

  supports: {
    fixedChannel: boolean;
    promiscuous: boolean;
    sourceMacFilter: boolean;
    destinationMacFilter: boolean;
    bssidFilter: boolean;
    frameTypeFilter: boolean;
    rssiFilter: boolean;
    nativeDecimation: boolean;
    nativeRateLimit: boolean;
    vht: boolean;
    he: boolean;
    heStbcSelection: boolean;
    manualScaling: boolean;
    lltfBitMode: boolean;
    layout: true;
  };
}
```

### 11.2 Capture 配置

legacy 与 HE 配置继续直接映射 ESP‑IDF target 能力：

```ts
interface WiFiCsiLegacyCaptureV1 {
  schema: "wifi-csi-legacy/1";
  lltf?: boolean;
  htLtf?: boolean;
  stbcHtLtf2?: boolean;
  ltfMerge?: boolean;
  adjacentSubcarrierFilter?: boolean;
  scale?: "auto" | { shiftBits: number };
  dumpAck?: boolean;
}

interface WiFiCsiHeCaptureV1 {
  schema: "wifi-csi-he/1";
  enableLegacy?: boolean;
  forceLegacyLtf?: boolean;
  ht20?: boolean;
  ht40?: boolean;
  vht?: boolean;
  heSu?: boolean;
  heMu?: boolean;
  heDcm?: boolean;
  heBeamformed?: boolean;
  heStbcLtf?: "first" | "second" | "alternate";
  valueScale?: number;
  dumpAck?: boolean;
  lltfBits?: 8 | 12;
}
```

不合并成一个“万能”配置对象，避免不同 target 上同名字段出现不透明降级。

### 11.3 Open options

```ts
type WiFiCsiSource =
  | { mode: "associated" }
  | { mode: "promiscuous"; channel?: "current" | number };

interface WiFiCsiPacketCaptureOptions {
  content?: "none" | "header" | "full";
  snapLength?: number;
  required?: boolean;
  requireComplete?: boolean;
}

interface WiFiCsiOpenOptions {
  source?: WiFiCsiSource;
  capture: WiFiCsiLegacyCaptureV1 | WiFiCsiHeCaptureV1;

  packet?: WiFiCsiPacketCaptureOptions;

  filter?: {
    sourceMac?: MacAddress | MacAddress[];
    destinationMac?: MacAddress | MacAddress[];
    bssid?: MacAddress | MacAddress[];
    frameTypes?: WiFiPacketType[];
    frameSubtypes?: number[];
    minimumRssi?: number;
    sampleEvery?: number;
    maximumRateHz?: number;
    validOnly?: boolean;
  };

  buffering?: {
    poolCapacity?: number;
    queueCapacity?: number;
    overflow?: "drop-newest";
  };

  powerSavePolicy?: "preserve" | "require-none";
}
```

#### 11.3.1 破坏性调整

- 删除无信息量的 `conflict: "fail"` 字段；冲突行为固定为 fail。
- `source` 改为 discriminated object，避免 `associated + numeric channel` 这类无意义组合。
- `queue` 改名为 `buffering`，并公开 `poolCapacity` 与 `queueCapacity`。
- `maxFrameBytes` 改名为 `maxCsiBytes`，避免与 802.11 packet 长度混淆。
- 默认 `packet.content` 为 `"none"`，因此不开启对应包捕获时内存行为与当前版本接近。

#### 11.3.2 Packet capture 语义

- `content: "none"`：只采集 CSI。
- `content: "header"`：复制完整可验证的 802.11 MAC header，不复制 body。
- `content: "full"`：先复制完整 header，再复制 driver 提供的 payload，最多 `snapLength` 字节。
- `required: true`：driver 没有提供有效 `hdr/payload` 关系时，整条 CSI observation 丢弃。
- `requireComplete: true`：包无法完整放入 `snapLength` 时整条 observation 丢弃；隐含 `required: true` 和 `content: "full"`。
- 未启用 `requireComplete` 时允许 packet 截断，但 CSI 样本不得截断；CSI 超出 `maxCsiBytes` 时整条 observation 丢弃。
- 框架保存 driver 实际提供的 bytes，不自行解密；在 target/mode 证据不足时不承诺 driver 提供的一定是密文或解密后的明文。Protected bit 不能单独证明 payload 表示。
- FCS 仅按 target adapter 已验证的事实报告；未知时必须返回 `"unknown"`。

Open-time 校验规则：

- 省略 `source` 等价于 `{ mode: "associated" }`。
- associated source 不接受 `channel` 字段。
- promiscuous source 必须同时满足 build gate、target capability 和 regulatory policy。
- `packet.required=true` 时 `packet.content` 不得为 `"none"`。
- `packet.requireComplete=true` 仅允许与 `packet.content="full"` 一起使用。
- `packet.content != "none"` 时，`packet.snapLength` 必须能够容纳 capability 报告的最大可支持 MAC header；否则 open 失败。content="none" 时不能因未设置 snapLength 而拒绝。
- `1 <= queueCapacity <= poolCapacity <= limits.maxPoolCapacity`。
- `maximumFrames <= queueCapacity`，且不得超过 `limits.maxBatchFrames`。
- 所有资源必须在更改 Radio 状态前完成分配和验证。

#### 11.3.3 对应包复制的实现前置验证（本轮补充）

固定 IDF 的 `wifi_csi_info_t` 有 `buf/len`、`hdr`、`payload/payload_len` 和 `rx_seq`，但结构没有独立的 header length 字段。[U-RX] 因而“同 callback 配对”成立，不等于 header/payload 的任意拼接都安全。

每个 target adapter 要产出一份 capture contract：header 的已知可读范围来自哪里、如何在该范围内解析可变头长、payload length 是否可信、两段是否有可证明的关系、FCS/Protected 表示、RX state 和 malformed 行为。不得通过对无关指针相减来猜 header length；不得假设二者连续。

在无法建立安全跨度时，`packetCapture.header/full` 不应被标为已支持。若某一 observation 缺少可信 packet，则按 required/requireComplete 丢弃或交付 CSI-only，设置 `packetUnavailable` 等统计。CSI bytes 不因 packet 缺失而被伪造，不能通过独立 Monitor 流时间戳近似匹配冒充 correlated packet。

Monitor 的 callback type 同样决定 payload 是否存在；metadata-only/misc 不能无条件按普通 MAC 帧读内存。公共 parser 只处理 `(bytes, provenReadableLength)`，不负责猜 driver buffer 的寿命或长度。

原始 bytes、driver-reported length、框架验证过的可读跨度和 captured length 是不同概念；不得把 `rx_ctrl.sig_len` 不经验证地当成可 memcpy 的长度。FCS unknown 保持 unknown。

### 11.4 Frame 与 Batch

```ts
interface WiFiCsiLayoutSegment {
  type:
    | "lltf"
    | "ht-ltf"
    | "stbc-ht-ltf2"
    | "vht-ltf"
    | "he-ltf1"
    | "he-ltf2"
    | "mixed"
    | "unknown";
  offsetBytes: number;
  lengthBytes: number;
  iqPairCount: number;
  subcarrierRanges: Array<{ start: number; end: number }>;
  nullSubcarriers: number[];
}

interface WiFiCsiInfo extends WiFiRxInfo {
  validity: {
    firstWordInvalid: boolean;
    channelEstimateValid: boolean | null;
    callbackDataValid: boolean;
    layoutKnown: boolean;
  };

  layout: {
    schema: string;
    componentOrder: "imaginary-real";
    sampleEncoding:
      | "signed-int8"
      | "signed-int12-le"
      | "signed-int12-packed"
      | "unknown";
    sampleBits: 8 | 12 | null;
    byteLength: number;
    iqPairCount: number;
    trailingPaddingBytes: number;
    segments: WiFiCsiLayoutSegment[];
  };
}

interface WiFiCsiFrame {
  readonly info: WiFiCsiInfo;

  samples(): ByteView;
  copySamples(): ByteView;
  sampleSource(): ByteSpanSource;

  packetBytes(): ByteView | null;
  copyPacketBytes(): ByteView | null;
  packetSource(): ByteSpanSource | null;

  source(options: { format: "esp32qjs-csi/1" }): ByteSpanSource;
  close(): void;
}

interface WiFiCsiBatch {
  readonly frameCount: number;
  info(index: number): WiFiCsiInfo;
  samples(index: number): ByteView;
  packetBytes(index: number): ByteView | null;
  source(options: { format: "esp32qjs-csi/1" }): ByteSpanSource;
  close(): void;
}

interface WiFiReceiveBatchOptions {
  maximumFrames?: number;
  minimumFrames?: number;
  timeoutMs?: number;
  maximumLatencyMs?: number;
}
```

单帧 `source({format})` 输出 frameCount 为 1 的合法 batch，解析器无需维护单帧特殊格式。

### 11.5 Session、状态与统计

```ts
interface WiFiCsiAPI {
  capabilities(): WiFiCsiCapabilities;
  open(options: WiFiCsiOpenOptions): WiFiCsiSession;
}

interface WiFiCsiSession {
  status(): WiFiCsiStatus;
  stats(): WiFiCsiStats;
  receive(timeoutMs?: number): WiFiCsiFrame | null;
  receiveBatch(options?: WiFiReceiveBatchOptions): WiFiCsiBatch | null;
  stop(): WiFiCsiStatus;
  configure(options: WiFiCsiOpenOptions): WiFiCsiStatus;
  start(): WiFiCsiStatus;
  close(): void;
}

interface WiFiCsiStatus {
  generation: number;
  state: "running" | "stopped" | "stopping" | "closing" | "cleanup-pending" | "closed" | "faulted";
  requested: WiFiCsiOpenOptions;
  effective: {
    source: "associated" | "promiscuous";
    channel: number;
    secondaryChannel: WiFiSecondaryChannel;
    radioGeneration: number;
    configSchema: "wifi-csi-legacy/1" | "wifi-csi-he/1";
    maxCsiBytes: number;
    packetContent: "none" | "header" | "full";
    packetSnapLength: number;
    poolCapacity: number;
    queueCapacity: number;
    powerSave: string;
    timestampAccuracy: WiFiTimestampAccuracy;
  };
  lastError: NativeErrorSnapshot | null;
}

interface WiFiCsiStats {
  callbacks: number;
  accepted: number;
  deliveredFrames: number;
  deliveredBatches: number;

  filteredMac: number;
  filteredFrameType: number;
  filteredFrameSubtype: number;
  filteredRssi: number;
  filteredDecimation: number;
  filteredRateLimit: number;
  filteredFirstWordInvalid: number;
  filteredChannelEstimateInvalid: number;

  invalidCallbackData: number;
  invalidPacketPointers: number;
  packetUnavailable: number;
  packetTruncated: number;
  droppedPacketRequired: number;
  droppedPacketRequireComplete: number;

  droppedPoolFull: number;
  droppedQueueFull: number;
  droppedCsiTooLarge: number;
  droppedClosing: number;

  receivedCsiBytes: number;
  capturedPacketBytes: number;
  leasedFrames: number;
  freePoolSlots: number;
  queue: EventQueueStats;
}
```

---

## 12. `esp32qjs-csi/1` 新 wire format

### 12.1 版本策略

- 格式名称仍为 `esp32qjs-csi/1`。
- magic 仍表达 v1，version 字段固定为 1。
- 当前开发格式直接被替换。
- 删除旧 parser 分支、旧 fixture 和旧兼容 reader。
- 不增加 `esp32qjs-csi/2`。
- 所有整数为 little-endian。
- reserved 字节必须写零；解析器遇到非零 reserved 或未知必需 flag 时拒绝。

### 12.2 Batch header：32 字节

| Offset | Type | 含义 |
| ---: | --- | --- |
| 0 | `u8[8]` | ASCII `E32QCSI1` |
| 8 | `u16` | version，固定 `1` |
| 10 | `u16` | header bytes，固定 `32` |
| 12 | `u32` | frame count |
| 16 | `u16` | directory record bytes，固定 `40` |
| 18 | `u16` | metadata record bytes，固定 `256` |
| 20 | `u32` | batch flags；v1 仅 bit 0 可置 1，表示 canonical layout |
| 24 | `u32` | total batch bytes |
| 28 | `u32` | reserved，固定 `0` |

约束：

- `frameCount` 必须为 `1..limits.maxBatchFrames`。
- `totalBatchBytes` 必须等于实际 source 总长度。
- v1 parser 必须拒绝未知 batch flag。

### 12.3 Directory record：每帧 40 字节

| Offset | Type | 含义 |
| ---: | --- | --- |
| 0 | `u32` | metadata absolute offset |
| 4 | `u32` | CSI absolute offset；无 CSI 时为 0 |
| 8 | `u32` | CSI byte length |
| 12 | `u32` | packet absolute offset；无 packet 时为 0 |
| 16 | `u32` | packet captured length |
| 20 | `u16` | packet header length |
| 22 | `u16` | record flags |
| 24 | `u32` | driver-reported packet payload length |
| 28 | `u32` | target adapter 可证明的 packet 原始长度；不得与未核验的 RX 空口长度混用 |
| 32 | `u32` | frame sequence，必须等于 metadata.sequence |
| 36 | `u32` | reserved，固定 0 |

Record flags：

| Bit | 含义 |
| ---: | --- |
| 0 | packet present |
| 1 | packet truncated |
| 2 | packet intentionally header-only |
| 3 | 802.11 parse valid |
| 4 | packet pointer layout valid |
| 5..15 | reserved，必须为 0 |

### 12.4 Metadata record：每帧固定 256 字节

| Offset | Type | 含义 |
| ---: | --- | --- |
| 0 | `u32` | frame sequence |
| 4 | `u64` | 同 boot 内的规范化时间戳，µs；来源精度由 offset 87 标志说明 |
| 12 | `u32` | ESP‑IDF receive sequence，未知时 `0xffffffff` |
| 16 | `u32` | CSI session generation |
| 20 | `u32` | shared Radio generation |
| 24 | `u8[6]` | source MAC |
| 30 | `u8[6]` | destination MAC |
| 36 | `u8[6]` | transmitter MAC |
| 42 | `u8[6]` | receiver MAC |
| 48 | `u8[6]` | BSSID |
| 54 | `u16` | address availability flags |
| 56 | `i8` | RSSI |
| 57 | `i8` | noise floor；不可用时 0，依 availability flag |
| 58 | `u8` | primary channel |
| 59 | `u8` | secondary channel enum |
| 60 | `u8` | antenna；不可用时 255 |
| 61 | `u8` | PHY enum |
| 62 | `u8` | bandwidth MHz；不可用时 0 |
| 63 | `u8` | MCS；不可用时 255 |
| 64 | `u32` | RX metadata flags |
| 68 | `u32` | CSI validity/layout flags |
| 72 | `u8` | CSI sample encoding enum |
| 73 | `u8` | sample bits；未知时 0 |
| 74 | `u8` | layout schema enum |
| 75 | `u8` | component order；v1 的 0 表示 imaginary-real |
| 76 | `u32` | CSI byte length |
| 80 | `u32` | total IQ pair count |
| 84 | `u16` | trailing padding bytes |
| 86 | `u8` | segment count，最大 3 |
| 87 | `u8` | timestamp accuracy enum |
| 88 | `u16` | 802.11 frame control；不可用时 0 |
| 90 | `u16` | duration/ID；不可用时 0 |
| 92 | `u16` | sequence control；不可用时 0 |
| 94 | `u16` | QoS control；不可用时 0 |
| 96 | `u8` | packet type enum |
| 97 | `u8` | packet subtype |
| 98 | `u8` | FCS state enum |
| 99 | `u8` | packet capture mode enum |
| 100 | `u16` | packet header length |
| 102 | `u16` | reserved，固定 0 |
| 104 | `u32` | driver payload length |
| 108 | `u32` | driver packet length |
| 112 | `u32` | captured packet length |
| 116 | `u32` | packet flags |
| 120 | `u8` | legacy rate；不可用时 255 |
| 121 | `u8` | signal mode；不可用时 255 |
| 122 | `u8` | AMPDU count；不可用时 255 |
| 123 | `u8` | RX state；不可用时 255 |
| 124 | `u8[4]` | reserved，固定 0 |
| 128 | `segment[3]` | 三个固定 40-byte segment slot |
| 248 | `u8[8]` | reserved，固定 0 |

#### 12.4.1 Address availability flags

| Bit | 含义 |
| ---: | --- |
| 0 | source available |
| 1 | destination available |
| 2 | transmitter available |
| 3 | receiver available |
| 4 | BSSID available |
| 5..15 | reserved |

不可用地址必须写全零，并由 availability flag 区分真正的 `00:00:00:00:00:00`。

#### 12.4.2 RX metadata flags

| Bit | 含义 |
| ---: | --- |
| 0 | noise floor available |
| 1 | antenna available |
| 2 | MCS available |
| 3 | bandwidth available |
| 4 | STBC available |
| 5 | STBC value |
| 6 | smoothing recommended |
| 7 | sounding value available |
| 8 | sounding frame |
| 9 | aggregation available |
| 10 | aggregation value |
| 11 | FEC coding available |
| 12 | LDPC value |
| 13 | short GI available |
| 14 | short GI value |
| 15 | channel estimate validity available |
| 16 | channel estimate valid |
| 17..31 | reserved |

#### 12.4.3 CSI validity/layout flags

| Bit | 含义 |
| ---: | --- |
| 0 | first word invalid |
| 1 | callback data valid |
| 2 | layout known |
| 3 | CSI payload truncated；v1 writer必须始终为 0 |
| 4..31 | reserved |

CSI 不允许截断。`info->len > maxCsiBytes` 时必须丢弃整条 observation。

#### 12.4.4 Packet flags

| Bit | 含义 |
| ---: | --- |
| 0 | packet present |
| 1 | payload intentionally omitted by header-only policy |
| 2 | captured packet truncated |
| 3 | pointer layout valid |
| 4 | 802.11 parse valid |
| 5 | To DS |
| 6 | From DS |
| 7 | More Fragments |
| 8 | Retry |
| 9 | Power Management |
| 10 | More Data |
| 11 | Protected |
| 12 | Order |
| 13 | sequence control available |
| 14 | QoS control available |
| 15..31 | reserved |

#### 12.4.5 v1 数值枚举表

除下表列出的值外，其余值均为非法；writer 不得写入未定义值，parser 必须拒绝。明确的 `unknown` 值属于合法值。

| 枚举 | 数值映射 |
| --- | --- |
| Secondary channel | `0=none`, `1=above`, `2=below` |
| PHY | `0=legacy`, `1=ht`, `2=vht`, `3=he-su`, `4=he-mu`, `5=he-er-su`, `6=he-tb`, `255=unknown` |
| Sample encoding | `0=unknown`, `1=signed-int8`, `2=signed-int12-le`, `3=signed-int12-packed` |
| Layout schema | `0=unknown`, `1=legacy`, `2=he` |
| Component order | `0=imaginary-real` |
| Timestamp accuracy | `0=normal`, `1=power-save-dependent`, `2=callback-time` |
| Packet type | `0=unknown`, `1=management`, `2=control`, `3=data`, `4=misc` |
| FCS state | `0=unknown`, `1=absent`, `2=present-valid`, `3=present-invalid` |
| Packet capture mode | `0=none`, `1=header`, `2=full` |
| Segment type | `0=unknown`, `1=lltf`, `2=ht-ltf`, `3=stbc-ht-ltf2`, `4=vht-ltf`, `5=he-ltf1`, `6=he-ltf2`, `7=mixed` |

`legacyRate`、`signalMode`、`ampduCount` 和 `rxState` 是 target adapter 规范化后的数值；不可用时写 `255`，不是 parser error。

### 12.5 Segment record：40 字节

沿用当前 v1 的 segment 语义：

| Offset | Type | 含义 |
| ---: | --- | --- |
| 0 | `u8` | segment type enum |
| 1 | `u8` | range count，最大 2 |
| 2 | `u8` | null subcarrier count，最大 3 |
| 3 | `u8` | reserved 0 |
| 4 | `u32` | CSI byte offset |
| 8 | `u32` | CSI byte length |
| 12 | `u32` | IQ pair count |
| 16 | `i16[4]` | 两组 start/end ranges |
| 24 | `i16[3]` | null subcarrier indices |
| 30 | `u8[10]` | reserved 0 |

### 12.6 Canonical region ordering

完整 batch 必须按以下顺序排列：

```text
32-byte header
N * 40-byte directory
N * 256-byte metadata
frame 0 CSI bytes
4-byte zero padding
frame 0 packet bytes
4-byte zero padding
frame 1 CSI bytes
...
```

要求：

- metadata offset 必须等于规范计算值。
- data region 必须按 frame index、CSI、packet 顺序排列。
- 每段开始 4-byte 对齐，padding 必须为零。
- absent section 的 offset 和 length 必须同时为零。
- packet bytes 为 `header || captured payload`，不是 JS 序列化对象。
- `packetHeaderLength <= packetCapturedLength`。
- directory、metadata 和 payload 中的重复长度/sequence 必须一致。
- parser 使用 checked arithmetic，拒绝整数溢出、重叠、越界、未知 enum、非 canonical offset 和非零 reserved。

### 12.6A 时钟、回绕与重启（本轮修订）

`normal` 只用于已验证映射到同一 boot 单调时基的 driver RX 时间；`power-save-dependent` 表示其精度受低功耗影响；新增 `callback-time` 表示原生回调入口时间近似值，不宣称是精确空口到达时间。该枚举在未冻结 v1 内直接替换，wire 长度仍为 256-byte metadata。

32-bit 微秒计数约 71.58 分钟回绕。[U-RX] 仅凭“新 timestamp 小于上次就加 2^32”不能处理长时间无包、多次回绕、乱序和 driver reset。adapter 必须结合可靠单调时间/已验证时钟锚点确定 epoch；无法证明时使用 `callback-time`，不能生成貌似精确但错误的 RX 时间。

物理 driver restart 使旧 Radio generation 失效；旧采集 session 不跨 driver restart 偷偷复用。整机 reboot 用新的 boot/session 外部标识区分，不能仅按 frame sequence 拼接。Host 端 UTC anchor 由用户/传输 metadata 提供；缺少 anchor 时只输出相对时间，不伪造 UTC。

测试包含：回绕前后、连续跨两个回绕、长时间无包后恢复、callback 处理延迟、power-save 变化、driver restart、整机 reboot 和两个设备不同 boot 时钟。建议 RF 时间验证持续至少 2 小时，并用 host 注入覆盖更长空窗；这是测试时长要求，不是开发工期估计。

### 12.7 Parser 和 fixture

必须同步重写：

- `scripts/esp32qjs_csi.py`
- `tests/python/test_wifi_csi_protocol.py`
- `tests/python/fixtures/esp32qjs-csi-v1.hex`
- `docs/api/wifi-csi-protocol.md`

不保留旧 metadata 192-byte reader。

---

## 13. `wifi.action` 与 Remain-on-Channel

```ts
interface WiFiActionAPI {
  capabilities(): WiFiActionCapabilities;
  send(options: WiFiActionSendOptions): WiFiActionResult;
  remainOnChannel(options: WiFiRocOptions): WiFiRocSession;
}

interface WiFiActionSendOptions {
  interface?: "station" | "access-point";
  channel: number;
  secondaryChannel?: WiFiSecondaryChannel;
  frame: ByteSource;
  timeoutMs?: number;
}

interface WiFiRocOptions {
  channel: number;
  secondaryChannel?: WiFiSecondaryChannel;
  durationMs: number;
  allowBroadcast?: boolean;
}
```

Action/ROC 通过专用 lane 与 fixed-channel scheduler。与 connected STA/AP 冲突时
不得隐式离开 home channel；只有 ESP-IDF 明确支持并通过 capability 的 off-channel
场景才允许。

---

## 14. `wifi.ftm`

```ts
interface WiFiFtmAPI {
  capabilities(): {
    apiVersion: "wifi-ftm/1";
    initiator: boolean;
    responder: boolean;
    maxReportEntries: number;
  };
  start(options: WiFiFtmInitiatorOptions): WiFiFtmSession;
  setResponderOffsetCm(offsetCm: number): number;
}

interface WiFiFtmSession {
  status(): object;
  receive(timeoutMs?: number): WiFiFtmReport | null;
  end(): void;
  close(): void;
}
```

- Initiator 仅 station；Responder 仅 AP 且 build 支持；
- report entries 复制到 bounded native storage；
- end/new session 必须遵守 ESP-IDF report buffer 释放规则；
- FTM event 同时可在 `wifi.watch()` 中观察。

---

## 15. `wifi.twt`

```ts
interface WiFiTwtAPI {
  capabilities(): {
    apiVersion: "wifi-twt/1";
    individual: boolean;
    broadcast: boolean;
    suspend: boolean;
    maxFlows: number;
  };
  setupIndividual(options: WiFiIndividualTwtOptions): WiFiTwtAgreement;
  setupBroadcast(options: WiFiBroadcastTwtOptions): WiFiTwtAgreement;
  agreements(): WiFiTwtAgreementStatus[];
  watch(options?: { capacity?: number }): EventQueue<WiFiTwtEvent>;
}

interface WiFiTwtAgreement {
  status(): WiFiTwtAgreementStatus;
  suspend(options?: object): object;
  resume(): object;
  teardown(): object;
  close(): void;
}
```

- 仅在 HE-capable target/build 暴露；
- iTWT/Broadcast TWT 的 ID、setup command、wake duration、interval、implicit、
  trigger、announced 等字段按 ESP-IDF 6.1 schema 完整映射；
- timeout/reject/TX fail 保留原生错误原因；
- TWT 与 power save policy 不得互相静默覆盖。

---

## 16. `wifi.vendorIe`

```ts
interface WiFiVendorIeAPI {
  set(options: {
    interface: "station" | "access-point";
    frame: "beacon" | "probe-request" | "probe-response" | "association-request" | "association-response";
    index: 0 | 1;
    enabled: boolean;
    data?: ByteSource;
  }): object;
  watch(options?: {
    oui?: string | string[];
    capacity?: number;
  }): EventQueue<WiFiVendorIeEvent>;
  clear(interface?: "station" | "access-point"): void;
}
```

Vendor IE callback 只复制公开 metadata 和 IE bytes，不保存 driver pointer；IE length、
OUI/type 和 frame/interface 组合按 IDF 校验。

---

## 17. `wifi.roaming`：802.11k/11v/11r

```ts
interface WiFiRoamingAPI {
  capabilities(): {
    apiVersion: "wifi-roaming/1";
    rrm11k: boolean;
    btm11v: boolean;
    fastTransition11r: boolean;
  };
  isRrmSupported(): boolean;
  requestNeighborReport(): WiFiNeighborReportRequest;
  isBtmSupported(): boolean;
  sendBtmQuery(options: WiFiBtmQueryOptions): object;
  watch(options?: { capacity?: number }): EventQueue<WiFiRoamingEvent>;
}
```

- 使用 IDF 6.x 的 `esp_rrm_send_neighbor_report_request()`，不保留已移除旧名；
- Neighbor Report、BTM candidate 数据必须有长度上限和 parser；
- 11r 开关属于 `wifi.connect({ftEnabled:true})` / station config；
- 自动漫游阈值和选择策略属于 JS Library，不硬编码到 native。

---

## 18. `wifi.enterprise` 与 `wifi.wapi`

```ts
interface WiFiEnterpriseAPI {
  capabilities(): WiFiEnterpriseCapabilities;
  configure(options: WiFiEnterpriseOptions): WiFiEnterpriseStatus;
  enable(): WiFiEnterpriseStatus;
  disable(): WiFiEnterpriseStatus;
  clear(): void;
  status(): WiFiEnterpriseStatus;
}
```

`WiFiEnterpriseOptions` 必须覆盖 build 支持的 EAP method、identity、anonymous
identity、username/password、CA cert、client cert/private key、private-key password、
phase2 method、FAST/PAC 等公开 ESP-IDF 字段。证书和 key 使用 ByteSource，native
复制到明确 owner；任何 status/error 均不回显秘密。

```ts
interface WiFiWapiAPI {
  capabilities(): object;
  enable(options: object): object;
  disable(): object;
  status(): object;
}
```

WAPI 仅在 target/build 支持时存在，不得用普通 WPA authMode 假装等价。

---

## 19. `wifi.wps`、`wifi.dpp` 与 `wifi.smartConfig`

### 19.1 WPS

```ts
interface WiFiWpsAPI {
  capabilities(): object;
  start(options: WiFiWpsOptions): WiFiWpsSession;
}

interface WiFiWpsSession {
  receive(timeoutMs?: number): WiFiWpsEvent | null;
  status(): object;
  cancel(): void;
  close(): void;
}
```

Session owner 执行 enable/start/disable 的精确清理；WPS timeout 参数是否有效由当前
IDF capability 报告，不伪造语义。

### 19.2 DPP

```ts
interface WiFiDppAPI {
  capabilities(): object;
  startEnrollee(options: WiFiDppOptions): WiFiDppSession;
}
```

DPP URI、configuration received、failure 等通过 Wi-Fi event broker；IDF 6.x 已移除
旧 DPP callback，因此实现不能重新制造旧 callback contract。

### 19.3 SmartConfig

```ts
interface WiFiSmartConfigAPI {
  capabilities(): {
    version: string;
    protocols: string[];
    espTouchV2: boolean;
    encryptedV2: boolean;
  };
  start(options: WiFiSmartConfigOptions): WiFiSmartConfigSession;
}
```

SmartConfig session 必须拥有 start/stop、AES key/custom data、event queue 和 credential
交付生命周期。是否自动调用 `wifi.connect()` 由显式 option 决定，默认只返回配置。

---

## 20. `wifi.nan`

```ts
interface WiFiNanAPI {
  capabilities(): WiFiNanCapabilities;
  open(options?: WiFiNanOpenOptions): WiFiNanSession;
}

interface WiFiNanSession {
  publish(options: WiFiNanPublishOptions): WiFiNanService;
  subscribe(options: WiFiNanSubscribeOptions): WiFiNanService;
  sendMessage(options: WiFiNanMessageOptions): object;
  requestDataPath(options: WiFiNanDataPathOptions): WiFiNanDataPath;
  receive(timeoutMs?: number): WiFiNanEvent | null;
  status(): object;
  close(): void;
}
```

覆盖 NAN start/stop、Publish/Subscribe、service match、follow-up message、data path
request/response/end 和 peer/channel metadata。NAN 与 STA/AP/Monitor/CSI 的 mode/channel
兼容性由 shared Radio capabilities 决定，不由应用猜测。

---

## 21. `wifi.mesh`

```ts
interface WiFiMeshAPI {
  capabilities(): WiFiMeshCapabilities;
  open(options: WiFiMeshOpenOptions): WiFiMeshSession;
}

interface WiFiMeshSession {
  start(): object;
  stop(): object;
  setConfig(config: WiFiMeshConfig): object;
  getConfig(): WiFiMeshConfig;
  send(options: WiFiMeshSendOptions): object;
  receive(timeoutMs?: number): WiFiMeshMessage | null;
  routingTable(): MacAddress[];
  status(): object;
  watch(options?: object): EventQueue<WiFiMeshEvent>;
  close(): void;
}
```

`wifi.mesh` 应覆盖当前 IDF public ESP-WIFI-MESH operational surface，包括 parent/layer、
routing table、root/fixed-root、group、vote/root switch、send/receive、ToDS queue 和
IE crypto/config；具体 method 由 `idf-wifi-api-map.json` 逐项追踪。当前本节辅助类型仍为 contract-pending，W-08 必须先补齐逐项参数/事件/取消/ownership 子规范，再注册 API。Mesh policy 很大，
建议独立 C 文件和 JS Library，不塞入核心 `esp32_mquickjs_wifi.c`。

---

## 22. `wifi.diagnostics`

```ts
interface WiFiDiagnosticsAPI {
  snapshot(): WiFiDiagnosticsSnapshot;
  dumpDriverStats(mask?: number): boolean;
  resetFrameworkCounters(): void;
  idfApiCoverage(): WiFiIdfApiCoverage;
}
```

`snapshot()` 至少包含：event queue、scan、STA reconnect attempts（仅计数，不实施策略）、
AP clients、radio leases、promiscuous subscribers、raw TX、CSI、Monitor、FTM/TWT、
allocation failures、cleanup-pending 和 last ESP-IDF errors。

`dumpDriverStats()` 映射 IDF log dump；结构化数据优先通过 snapshot，日志进入
`runtimeLogs`，不把 console 文本解析成 API。

---

## 23. BLE v1 对齐

BLE 的功能实施以[第三份文档](03_esp32qjs_ble_refactor_and_features.md)为准，本章只定义 Wi-Fi 集成边界。保留 `ble.open()` + Adapter/Connection/Scanner/GATT handle 模型，不改为 `wifi.ble`。必须继续满足：

- Central、Peripheral、Observer、Broadcaster；
- GATT Client、静态 native-cached GATT Server；
- scan/advertising raw data、MTU、RSSI、Pairing、Bond；
- 补齐 target 支持的 Extended Advertising、PHY、Data Length 和 connection parameters；
- Advertising Data parser/builder 优先作为官方 JS Library；
- NimBLE callback 不调用 JS；
- BLE/Wi-Fi software coexistence 的有效配置进入 `wifi.capabilities()` 与 BLE capabilities。

---

## 24. ESP-NOW 对齐

现有 `espNow` session、peer、PMK/LMK、v2 1470-byte payload、per-peer rate、TX queue、
显式 recovery 设计保留。只做以下统一：

- 使用新的 shared Radio/fixed-channel lease；
- `wifi.status().radio.clients.espNow` 可见；
- RX metadata 与公共 channel/RSSI/timestamp naming 对齐；
- rawTx、CSI、Monitor 与 ESP-NOW 不同信道时全部显式失败；
- 不把 ESP-NOW 移入 `wifi` namespace；
- 不增加隐式 retry、fragmentation、mesh 或 application ACK。

---

## 25. Shared Radio、Promiscuous Broker 与 TX Broker

本章是本轮重构的核心契约，替换“在 once 初始化结果上直接添加 start/stop”以及“不同 lease 各自设置 driver”的做法。基线 Radio 使用 once 状态；第一份文档仅修复其现有错误路径，完整生命周期在 W-01 实现。[R-RADIO]

### 25.1 唯一 Radio owner 与 client registry

```text
APPLICATION
WIFI_STA
WIFI_AP
ESPNOW
MONITOR
RAW_TX
CSI
ACTION_ROC
NAN
MESH
```

每个 lease 绑定 `radioGeneration + uniqueLeaseIdentity + clientKind`，在有界 registry 中登记。registry 项至少包含 requested mode/band/channel、独占配置需求、admission 状态和仍在进行的原生操作数。

client count 只是统计，不能替代身份验证。release 幂等，旧 generation、已经 release 的 token 和复制出来的过期 token 不得修改 Radio。identity 溢出时不能重用仍可能被旧对象持有的值。

共享硬件只有一个实际 owner。即使框架支持 runtime 重启，也不得同时初始化两个互不知情的 Wi-Fi service。

### 25.2 Driver 生命周期状态机

```text
uninitialized -> initializing -> stopped -> starting -> started
                      |             ^                    |
                      v             |                    v
                   faulted       stopping <---------------
                      |             |
                      +------> cleanup-pending

restart: 独占检查 -> 停止 -> 解除必要资源 -> 重新初始化/启动
         任一步失败 -> 保留准确 cleanup stage，不伪装成功
```

实现记录实际完成的阶段，而不是用一个布尔 `initialized` 推断全部资源状态。`WiFiStatus.driverState` 必须反映当前真实生命周期；`started` 不能复用上一次成功结果而忽略中间发生的 stop。

| 操作 | 前置条件 | 必须维持的行为 |
| --- | --- | --- |
| `wifi.start()` | 无残留 cleanup；mode 合法 | 幂等地满足当前请求；所有 mutation 串行 |
| `wifi.stop()` | STA/AP 和 feature 子 owner 已显式退出 | 停止物理 Wi-Fi；存在其他 owner 时无副作用失败 |
| `wifi.driver.restart()` | 独占、无 active callback/operation、无残留资源 | 每步重建准确，generation 更新；失败可诊断/重试 |
| `wifi.configure()` | 所有字段与资源冲突预验证 | 复合配置显式报告不可回滚副作用；不关闭其他 feature |
| `wifi.driver.setMode/Band/...` | 满足 IDF 状态与 lease 规则 | 与顶层 setter 经过同一 mutation lane |
| runtime teardown | 阻止新 admission | 先结束本 runtime 子资源，再释放物理 owner；不使用旧 JSContext |

`wifi.stop()` 不提供 `force` 或 `requireExclusive:false`。停止某项业务使用其自己的 `disconnect/stopAP/session.close`，不拿物理 stop 作为批量清理捷径。

### 25.3 锁与 lane

一个 Radio mutation lane 串行化 init/start/stop、mode、band、country、channel、global filter、rate、bandwidth 和 event mask 的修改。每个子操作可有自己的数据/协议 lane，但不能绕过全局副作用检查。

registry/状态锁只保护短快照与状态发布。调用 ESP-IDF、等待 Future、执行 NVS 或内存分配前退出短临界区。若 driver 调用必须释放锁，使用 reservation/revision 在提交时重新核验，避免“检查可用后 owner 已经退出”的竞态。

锁顺序写入并发文档；禁止在持有子模块锁时等待 Radio lane，而 Radio cleanup 又等待该子模块锁。跨任务只能传原生状态，不传 JSValue。

### 25.4 mode、band 与配置合并

每个 lease 保存自身要求，按**当前存活的 lease 集合**计算有效需求，不能把某个 client 曾要求 APSTA 的结果永久粘住。STA+AP 形成合法 APSTA，NAN/Mesh 等采用显式兼容表，默认拒绝未经验证的组合。

退出一个 owner 不一定立即做有破坏性的 mode 降级；如果保留当前 mode 满足剩余 owner，可以延后到安全配置点。requested/effective/status 分开报告，不隐式重启以追求配置“看起来最小”。

country/band/protocol/bandwidth 的更改要预先验证所有存活 owner。法规未能权威枚举时返回 unknown/不可枚举，并继续受 driver 约束；不能因为 channel mask 为零就宣称所有信道合法。

### 25.5 同信道多 lease 共享

本轮将基线“精确单 owner”扩展为有界多 owner 集合。这是 W-01 的行为变更，不在 F 阶段提前实现。

- primary、secondary 以及所需 band 兼容时可以共享。
- 有任何固定信道约束不一致时，新增请求无副作用失败。
- connected STA/active AP 是 home-channel 的强约束来源，不能被采集 session 随意覆盖。
- numeric CSI/Monitor/Raw TX/Action 请求必须走结构、target、法规和存活 owner 校验。
- 仅最后一个相应 fixed-channel owner 退出时解除约束。
- `channel:"current"` 表示跟随，不能误登记成固定 owner。
- 不允许通过后台 hopping 假装同时监听多个信道。

### 25.6 扫描、漫游与 driver 自发信道变化

不能只在 `open()` 时检查信道。应用 scan、connect 扫描、ROC、漫游及 driver home-channel 变化都必须进入可观测状态。

普通扫描与严格固定信道采集可能冲突。v1 默认拒绝会破坏既有严格 owner 的新扫描/ROC 请求，不偷偷暂停旧 session。需要 off-channel 例外的能力，只在公开 driver 支持且其专门契约明确时开启。

driver 自发换信道时：先更新 actual channel 和 channelGeneration，标记事件；跟随型 session 跟随实际状态；固定型 session 停止 admission 并进入 faulted/显式待重配状态。不得假装仍在旧信道采集，也不得为了恢复监测而断开 STA 或把 driver 强行切回旧信道。

控制事件即便无法进入公开 watch，native 冲突状态仍必须准确。

### 25.7 全局设置 lease 的恢复

rate/bandwidth/promiscuous/event mask 等设置采用明确的 owner/revision。记录前值、修改后值、当前 revision、剩余 owner 和清理阶段。

只有该 lease 仍拥有修改权时才能恢复前值。另一个有权限的事务已改变配置时，不能使用旧 snapshot 覆盖它；应在进入新事务前拒绝冲突，或按已审查的所有权转移协议处理。

没有可信 getter/框架写入记录时，临时覆盖能力不开放，返回明确 capability/reason。不能“恢复到默认值”冒充恢复原配置。恢复失败进入 cleanup-pending，并阻止依赖该状态的新 mutation。

### 25.8 Promiscuous Broker

Broker 是唯一调用以下 driver API 的组件：

```c
esp_wifi_set_promiscuous_rx_cb(...)
esp_wifi_set_promiscuous(...)
esp_wifi_set_promiscuous_filter(...)
esp_wifi_set_promiscuous_ctrl_filter(...)
```

Monitor 和 promiscuous CSI 注册各自需求。driver filter 取已审查的需求并集，订阅者使用有界、无分配 native filter。first/last subscriber 管理 enable/disable 和必要的配置恢复。

callback 入口 context 由 broker 稳定持有，不因为一个 session close 就释放整个 callback context。subscriber detach 先停止 admission，再从可见 registry 移除并处理已进入的 dispatch retain，最后释放 session 控制资源。

不要在 callback 中分配 JS 对象、遍历无界用户 filter、调用日志/网络，或按包 malloc。filter 表长度、订阅者数量和 native copy 成本都有 build 上限；profile 中必须记录高负载成本。

### 25.9 Raw TX completion broker：关联与拷贝

固定 IDF 的 `wifi_tx_info_t` 包含指针，不能用浅拷贝把它当成跨线程纯值事件。[U-TYPES] 需要自定义无指针的 snapshot：有效标志、按值复制的源/目的 MAC、接口、driver status、rate 等必要标量。只在 callback 内读取 driver 提供的合法字段，不保存 `data/src_addr/des_addr`。

broker 每次最多一个 native Raw TX in-flight。operation 在提交前拥有其输入数据和原生完成状态；`operationId` 由框架分配用于账本，但**不能当作 driver callback 已携带的 cookie**。

没有可证明的原始请求身份时：

```text
idle -> submitted -> completed -> idle
             |
             +-> timed-out-publicly -> draining/quarantined
                                         |
                           已证明原生操作终止/旧回调不再可能
                                         |
                                       idle
```

- Public timeout 不释放仍可能被 driver 使用的输入/state，不把 lane 交给下一包。
- callback active count 归零不是“未来无旧 callback”的充分条件。
- 只能根据固定版本公开契约的终止/停止屏障和验证证据结束 quarantine。
- 不能在旧 completion 到达时把它贴上当前 generation，再当成新 operation 的完成。
- 找不到安全且不影响其他 owner 的恢复路径时，报告 cleanup-pending/faulted，保留受限资源并要求显式恢复；不全局重启正用于 STA/ESP-NOW 的 Wi-Fi。
- callback 不可用时，能力与结果必须说明仅 driverAccepted，不能伪造 driverCompleted。

这组竞争测试通过前，不合并 periodic/batch 优化。

### 25.10 Raw TX 队列、flush 与 periodic

enqueue 在接纳时复制或按受审查方式保留 ByteSource；无论策略为何都必须明确输入 ownership。`reject-newest` 没有取得输入 ownership，不创建已接纳任务。

batch admission 默认 all-or-nothing；`drop-oldest-batch` 只删除尚未提交的完整 batch，为每个被接纳后丢弃的项记录终态，不撤销已 in-flight 的包。任何策略都不能丢失 flush 等待的计数。

`flush()` 使用调用时的 admission fence：只等待此前已接纳的项达到 completed/failed/dropped/cancelled 等终态，不被之后新 enqueue 无限延长。结果区分 driver accepted、driver completed、失败和未能确认；不是业务 ACK。

periodic timer 只进行原生有界调度/唤醒，不能在不合适的 ISR/timer context 直接阻塞调用 driver。发送由 worker 执行，busyPolicy=skip/stop，不无限积压。定时精度和实际发包间隔需实测，不能从 intervalUs 参数推导吞吐承诺。

关闭 session 先停止其 periodic 子资源与新 admission，再按 operation 终态清理。公开完成观察事件允许丢，但原生结果账本和 flush 必须保持准确且有界。

## 26. Native 内存、回调与 teardown

### 26.1 通用分配规则

RX/CSI/Monitor/TX 均使用固定容量或有明确总上限的 pool。callback-visible 控制元数据默认 internal RAM；payload 放 PSRAM 需要该 build/target 的可访问性、缓存行为和延迟证据，不能只因存在 PSRAM 就自动迁移。

所有分配、尺寸计算和 owner reservation 在 driver side effect 前完成；遇到后续 driver 错误按准确后缀回滚。callback 不可无界分配；禁止用 variable-size heap allocation 模拟无限 queue。

### 26.2 三种不同的资源寿命

| 寿命 | 结束条件 | 不能被它阻塞的资源 |
| --- | --- | --- |
| public session operation | close 后不再接纳新操作 | 保留数据不应要求 session 继续 running |
| driver/callback control | 停止产生新回调并完成可靠屏障、清理原生操作 | 已独立 retained 的数据不应继续占用 Radio |
| payload storage | event/public owner/View/Source 全部释放 | 不能提前释放以追求 close 返回 |

公开 `close()` 只等待/推进本资源的 driver control cleanup，不等待调用者未来才会释放的 ByteView。超过清理 deadline 时返回 `WIFI_CLEANUP_PENDING` 或对应 feature 的既有/正式错误，handle 保持 closing；不可伪装 closed。

如果 driver 已清理完而仅剩数据 lease，可以标记 session closed，释放 Radio，旧 pool 进入 retired-storage registry；新 open 仍受全局总预算约束。

### 26.3 精确关闭顺序

1. 标记 closing，拒绝新 admission、enqueue、start/configure。
2. 停止本 session 的原生 source/periodic producer。
3. 从 broker subscriber registry 退出；若为最后 owner，按规范注销/停止 driver callback source。
4. 完成每个已开始 operation 的终止或 quarantine，不用 active=0 替代终止屏障。
5. 等待已进入的 callback/dispatch 引用归零；所有失败保留相应资源。
6. 丢弃尚未交付的 queue 条目；Future 已 dequeue 但未转换的数据走同一 drop owner。
7. 释放已不再需要的 Radio/config lease；恢复失败则保留 cleanup-only 状态。
8. public session 关闭；最后一个 payload retain 退出后才释放旧 pool。

runtime teardown 时按相同 native 路径处理，worker 不再触碰被销毁的 JSContext。finalizer 只是兜底；不能在 GC 中无限等待外部 view 或不可达的 JS 消费者。

### 26.4 slot 的 exactly-once 账本

```text
free -> producer-owned -> queued -> public Frame/Batch-owned
                                     + View/Source retains
                            -> owner closed -> last retain -> free
```

过滤失败、队列拒绝、oversize、closing、cancel、conversion failure 都必须回到唯一 drop/release 路径。复制返回的 owned storage 不再 retain capture slot；`ByteSpanSource` 的异步发送可能延长 retain，必须计入资源账本。

### 26.5 总预算与构建 profile

新增/复用统一的无线资源预算接口，记录 internal、PSRAM、control reserve、active pool、retired pool、queue 和在途 TX。Wi-Fi/BLE 共用总量检查；每个模块再分子预算。

最坏预算至少包括：

```text
sum(pool_capacity * aligned_slot_stride)
+ all_queue_storage
+ per-session/control/Future/lease state
+ in-flight native transmit data
+ retired-storage upper bound
+ worker/task stacks
```

driver、NimBLE、JS heap、TLS 和其他组件的内存单独列出；不能把 payload 小计称作整机内存需求。

供预算检查用的算术示例（不是默认值、实测值或芯片能力）：CSI 16×(1024+1500) 为 39.4 KiB；Monitor 16×1500 为 23.4 KiB；若 BLE 扩展扫描配置 32×1650，为 51.6 KiB。三项仅 payload 已为 114.4 KiB，尚不含 metadata/queue/stack。

| Profile | 定位 | 配置要求 |
| --- | --- | --- |
| low-memory | 无 PSRAM、少量连接和小数据流 | 较小 pool、可选模块关闭、保留控制 reserve |
| standard | 普通 STA/BLE/ESP-NOW 应用 | 按真实 Build Context 明确每域预算 |
| capture | Monitor/CSI/Raw TX 分析 | 额外采集/retired 预算及端到端吞吐证据 |

本文件不代替板卡实测给出通用默认容量。每个完整 Build Context 必须明确这些值并通过乘法/对齐溢出与相互约束检查；用户传入的 session option 不能突破 build cap。

### 26.6 秘密与诊断

secret-owned native buffer 释放前清零；避免不必要的 JS/RPC 副本，不宣称可清零所有 JS 字符串历史副本。status/diagnostics 只输出尺寸、状态、drop、owner identity 和脱敏错误。

diagnostics 至少显示 active/retired pool bytes、control-reserve 使用、queue high-water、cleanup stage、quarantined operation 数、allocation failure 与 clock fallback 统计。关闭后 snapshot 可以保留值副本，但不保留失效 native pointer。

---

## 27. ESP-IDF 6.1 覆盖矩阵

下表是规范性方向；最终完整逐 symbol 清单由 `docs/idf-wifi-api-map.json` 生成和检查。

| 能力族 | ESP-IDF API/头文件 | JS API | 覆盖方式 |
| --- | --- | --- | --- |
| Driver lifecycle | `esp_wifi_init`, `esp_wifi_deinit` | Framework-owned; build profile + `wifi.driver.restart()` | mapped as safe lifecycle, not raw callback ownership |
| Driver lifecycle | `esp_wifi_start`, `esp_wifi_stop` | `wifi.start()`, `wifi.stop()` | direct public workflow |
| Mode/storage | `esp_wifi_set_mode`, `esp_wifi_get_mode`, `esp_wifi_set_storage` | `wifi.driver.setMode()`, `getMode()`, `setStorage()` | typed low-level control |
| Configuration | `esp_wifi_set_config`, `esp_wifi_get_config` | `wifi.driver.setInterfaceConfig()`, `getInterfaceConfig()` | complete target-gated STA/AP fields |
| Station | `esp_wifi_connect`, `esp_wifi_disconnect`, `esp_wifi_sta_get_ap_info` | `wifi.connect()`, `wifi.disconnect()`, `wifi.status()` | common path stays shallow |
| Scan | `esp_wifi_scan_*` family | `wifi.scan()` / cancellation through Future | driver scan storage never leaks |
| SoftAP | `esp_wifi_ap_get_sta_list`, `esp_wifi_deauth_sta` | `wifi.apClients()`, `wifi.deauthClient()` | optional IP enrichment through `net` |
| Power | `esp_wifi_set_ps`, `esp_wifi_get_ps` | `wifi.setPowerSave()`, `wifi.status()` | shallow common API |
| TX power | `esp_wifi_set_max_tx_power`, `esp_wifi_get_max_tx_power` | `wifi.setTxPower()`, `wifi.status()` | quarter-dBm mapping preserved |
| Country | `esp_wifi_set_country_code`, `esp_wifi_set_country`, getters | `wifi.setCountry()`, `wifi.driver.setCountryDetails()` | regulatory validation required |
| Channel | `esp_wifi_set_channel`, `get_channel`, `get_home_channel` | `wifi.setChannel()`, `wifi.driver.getChannel()`, `getHomeChannel()` | shared fixed-channel lease |
| MAC | `esp_wifi_set_mac`, `esp_wifi_get_mac` | `wifi.setMac()`, `wifi.getMac()` | interface must be disabled where IDF requires it |
| Protocol | `esp_wifi_set/get_protocol`, `set/get_protocols` | `wifi.driver.set/getProtocol(s)()` | 2.4/5 GHz target gating |
| Bandwidth | `esp_wifi_set/get_bandwidth`, `set/get_bandwidths` | `wifi.driver.set/getBandwidth(s)()` | target and PHY combinations validated |
| Band | `esp_wifi_set/get_band`, `set/get_band_mode` | `wifi.driver.set/getBand()`, `set/getBandMode()` | C5 and future dual-band targets |
| Rates | `esp_wifi_config_11b_rate`, `esp_wifi_config_80211_tx_rate`, `esp_wifi_config_80211_tx` | `wifi.driver.configure11bRate()`, `configureTxRate()` | pre-start restrictions preserved |
| Connection metadata | `esp_wifi_sta_get_aid`, `esp_wifi_sta_get_negotiated_phymode`, `esp_wifi_sta_get_rssi` | `wifi.driver.getAid()`, `getNegotiatedPhy()`, `getRssi()` | station-only target checks |
| Timing | `esp_wifi_get_tsf_time` | `wifi.driver.getTsfTime()` | returns driver TSF semantics |
| Inactive/RSSI | `esp_wifi_set/get_inactive_time`, `esp_wifi_set_rssi_threshold` | `wifi.driver.set/getInactiveTime()`, `setRssiThreshold()` | events delivered through `wifi.watch()` |
| Radio policy | `esp_wifi_set_dynamic_cs`, connectionless wake interval, force wakeup | `wifi.driver.setDynamicCarrierSense()`, `setConnectionlessWakeInterval()`, `wifi.acquireWakeLock()` | wake-lock handle is ref-counted |
| PMF/coexistence | `esp_wifi_disable_pmf_config`, coexistence power config | `wifi.driver.disablePmf()`, `setCoexistencePowerManagement()` | no silent security downgrade |
| Event masks | `esp_wifi_set/get_event_mask` | `wifi.driver.set/getEventMask()` | global event broker remains owner |
| Promiscuous RX | `esp_wifi_set_promiscuous*` | `wifi.monitor` + shared promiscuous broker | callbacks are never exposed directly |
| Raw 802.11 TX | `esp_wifi_80211_tx`, `esp_wifi_register_80211_tx_cb` | `wifi.rawTx` | single/batch/periodic TX with completion queue |
| Action/ROC | `esp_wifi_action_tx_req`, `esp_wifi_remain_on_channel` | `wifi.action` | off-channel lease and completion events |
| Vendor IE | `esp_wifi_set_vendor_ie`, vendor IE callback | `wifi.vendorIe` | bounded copied events |
| CSI | `esp_wifi_set_csi_*` | `wifi.csi` | CSI + correlated packet capture |
| FTM | `esp_wifi_ftm_*` | `wifi.ftm` | initiator/responder target gating |
| HE/TWT | `esp_wifi_sta_itwt_*` and supported broadcast-TWT APIs | `wifi.twt` | compiled only on HE-capable targets |
| RRM/WNM | `esp_rrm_*`, `esp_wnm_*` | `wifi.roaming` | 802.11k/11v; 11r remains station config |
| Enterprise | `esp_eap_client_*` | `wifi.enterprise` | credentials/certificates use owned byte sources |
| WAPI | public WAPI station APIs when compiled | `wifi.wapi` | optional build feature |
| WPS | `esp_wifi_wps_*` | `wifi.wps` | session + Wi-Fi event broker |
| DPP | `esp_supp_dpp_*` | `wifi.dpp` | IDF 6.1 Wi-Fi events, no legacy DPP callback |
| SmartConfig | `esp_smartconfig_*` | `wifi.smartConfig` | bounded session events |
| NAN | `esp_wifi_nan_*` | `wifi.nan` | publish/subscribe/message/datapath handles |
| ESP-WIFI-MESH | `esp_mesh_*` public API | `wifi.mesh` | optional session-oriented adapter |
| Antenna | public `esp_phy_*ant*` replacement APIs | `wifi.driver.get/setAntenna*()` | IDF 6.x moved antenna API out of `esp_wifi` |
| Diagnostics | `esp_wifi_statis_dump` and public counters/events | `wifi.diagnostics` | structured counters plus optional log dump |
| ESP-NOW | `esp_now_*` | existing top-level `espNow` module | kept separate; shares radio broker |

### 27.1 Allowlist headers

至少扫描：

```text
components/esp_wifi/include/esp_wifi.h
components/esp_wifi/include/esp_wifi_he.h
components/esp_wifi/include/esp_wifi_types*.h
components/esp_wifi/include/esp_now.h
components/esp_wifi/wifi_apps/nan_app/include/esp_nan.h
components/esp_wifi/include/esp_mesh.h
components/wpa_supplicant/esp_supplicant/include/esp_eap_client.h
components/wpa_supplicant/esp_supplicant/include/esp_wps.h
components/wpa_supplicant/esp_supplicant/include/esp_dpp.h
components/wpa_supplicant/esp_supplicant/include/esp_rrm.h
components/wpa_supplicant/esp_supplicant/include/esp_wnm.h
components/esp_wifi/include/esp_smartconfig.h
components/esp_phy/include/esp_phy.h
```

实际路径以 IDF 6.1 source tree 为准，generator 读取 compile_commands/IDF component
include path，不把路径字符串硬编码成永久 ABI。

---

## 28. Kconfig 设计

```text
CONFIG_ESP32_MQUICKJS_FEATURE_WIFI
CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_DRIVER_ADVANCED
CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_MONITOR
CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_RAW_TX
CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_CSI
CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_ACTION
CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_FTM
CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_TWT
CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_VENDOR_IE
CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_ROAMING
CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_ENTERPRISE
CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_WAPI
CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_WPS
CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_DPP
CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_SMARTCONFIG
CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_NAN
CONFIG_ESP32_MQUICKJS_FEATURE_WIFI_MESH
CONFIG_ESP32_MQUICKJS_WIFI_ALLOW_SECRET_READBACK
```

资源上限：

```text
CONFIG_ESP32_MQUICKJS_WIFI_EVENT_QUEUE_LEN
CONFIG_ESP32_MQUICKJS_WIFI_MONITOR_POOL_CAPACITY
CONFIG_ESP32_MQUICKJS_WIFI_MONITOR_MAX_FRAME_BYTES
CONFIG_ESP32_MQUICKJS_WIFI_RAW_TX_QUEUE_LEN
CONFIG_ESP32_MQUICKJS_WIFI_RAW_TX_MAX_BATCH_FRAMES
CONFIG_ESP32_MQUICKJS_WIFI_CSI_POOL_CAPACITY
CONFIG_ESP32_MQUICKJS_WIFI_CSI_MAX_FRAME_BYTES
CONFIG_ESP32_MQUICKJS_WIFI_CSI_MAX_PACKET_BYTES
CONFIG_ESP32_MQUICKJS_WIFI_FEATURE_EVENT_QUEUE_LEN
```

依赖按 IDF/SOC capability 设置。Unsupported target 必须强制 off，而不是编译一个运行时
永远失败的空对象。`wifi.capabilities().namespaces` 只列出实际注册对象。

---

## 29. 文件级修改清单

### 29.1 重构现有文件

```text
components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi.c
components/esp32_mquickjs/src/modules/wifi/esp32_mquickjs_wifi_future.c
components/esp32_mquickjs/internal/esp32_mquickjs_wifi.h
components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c
components/esp32_mquickjs/internal/esp32_mquickjs_wifi_radio.h
components/esp32_mquickjs/Kconfig.projbuild
components/esp32_mquickjs/CMakeLists.txt
components/esp32_mquickjs/runtime-features.json
types/esp32qjs-c-api.d.ts
types/esp32qjs-js-api.d.ts
api-manifest.json
```

### 29.2 新增 Wi-Fi 子模块

```text
src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c
src/modules/wifi_monitor/esp32_mquickjs_wifi_monitor.c
src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx.c
src/modules/wifi_action/esp32_mquickjs_wifi_action.c
src/modules/wifi_ftm/esp32_mquickjs_wifi_ftm.c
src/modules/wifi_twt/esp32_mquickjs_wifi_twt.c
src/modules/wifi_vendor_ie/esp32_mquickjs_wifi_vendor_ie.c
src/modules/wifi_roaming/esp32_mquickjs_wifi_roaming.c
src/modules/wifi_enterprise/esp32_mquickjs_wifi_enterprise.c
src/modules/wifi_provisioning/esp32_mquickjs_wifi_wps.c
src/modules/wifi_provisioning/esp32_mquickjs_wifi_dpp.c
src/modules/wifi_provisioning/esp32_mquickjs_wifi_smartconfig.c
src/modules/wifi_nan/esp32_mquickjs_wifi_nan.c
src/modules/wifi_mesh/esp32_mquickjs_wifi_mesh.c
src/modules/wifi_common/esp32_mquickjs_wifi_rx_metadata.c
src/modules/wifi_common/esp32_mquickjs_wifi_80211_parser.c
src/modules/wifi_common/esp32_mquickjs_wifi_promiscuous_broker.c
src/modules/wifi_common/esp32_mquickjs_wifi_tx_broker.c
```

对应 internal headers/resources 分文件；按 owner 和职责拆分，不以不准确的代码行数描述现有文件规模。

### 29.3 文档和工具

```text
docs/api/wifi.md
docs/api/wifi-driver.md
docs/api/wifi-monitor.md
docs/api/wifi-monitor-protocol.md
docs/api/wifi-raw-tx.md
docs/api/wifi-csi.md
docs/api/wifi-csi-protocol.md
docs/api/wifi-action.md
docs/api/wifi-ftm.md
docs/api/wifi-twt.md
docs/api/wifi-security.md
docs/api/wifi-provisioning.md
docs/api/wifi-nan.md
docs/api/wifi-mesh.md
docs/idf-wifi-api-map.json
scripts/generate_idf_wifi_api_map.py
scripts/esp32qjs_monitor.py
scripts/esp32qjs_csi.py
```

---

## 30. 实施阶段与工单

下面的任务顺序替换原方案“先写全量正式声明，再逐个实现”的顺序。W-00 已实现代表配置的符号/字段清单和检查工具，其余 task 保持 planned。

### W-00：覆盖清单和契约冻结点

**实际状态**：in-progress / coverage review。已用 C3/S3/C5 五种实际编译配置生成
`docs/idf-wifi-api-inventory.json` 和逐符号 `docs/idf-wifi-api-map.json`，并集 1,267 条，
包括结构字段、数组、bitfield、enum 与宏。`scripts/generate_idf_wifi_api_map.py` 检查
未知条目、字段变化和错误提前宣传 planned API；CI 已接入映射、固定 SDK 头文件保护
及已记录 variant 的构建后语义比较。C5 disabled 与 NAN-Sync enabled 分别保留基线。
完整 Python 359/359 通过。字段业务契约、NAN-USD 和其他未记录配置仍待审查，
不宣称 W-00 全部验收通过。见[本轮实施记录](investigations/2026-09-07-wifi-refactor-start.md)。

**输入**：第一阶段 F-00/F-10、固定 IDF headers 和现有 runtime registration。

**工作**：生成 public symbol/结构字段 inventory；填写 disposition、implementation、contract、validation；列出未知的 callback length、取消和配置恢复问题。建立目标文档与实际 manifest 的不同生成入口。

**验收**：未知符号 CI 失败；planned 条目不进入 actual feature 列表；所有新增模块有字段/状态/owner/completion 说明。高级占位类型明确 contract-pending。

### W-01：Radio 状态机和 lease 重构

**代码**：现有 `wifi_radio` 与相关 internal headers；第一阶段建立的 identity/helper 复用。

**工作**：实现第 25 章；去除对一次成功启动结果的永久依赖；建立有界 lease registry、mode/channel 兼容表、mutation lane、可重试 cleanup stage；新增同信道多 owner；记录 driver 自发信道变化。

**验收用例**：init/start/stop/restart 全部阶段错误注入；A/B 同信道共享，A 关闭不影响 B；不同信道冲突无副作用；STA/AP 信道变化更新实际状态；有子 owner 的 stop/restart 被拒绝；旧 token 无效；恢复前值失败时 lane 不被新操作复用。

W-01 未通过，不实现跨 feature 的 mode/channel setter。

### W-02：基础 Wi-Fi、扫描与事件 broker

**代码**：现有 `wifi.c`/`wifi_future.c`、Wi-Fi event converter、原生 operation 状态。

**工作**：保持顶层 connect/disconnect/scan/status；新增 start/stop/configure、SoftAP/APSTA、MAC/country/channel、watch；补 scan bitmap/coexistence 参数；实现 event descriptor 和秘密交付边界。

**验收用例**：连接/扫描结束时 watch 已满；结果 buffer 在成功/取消/错误/转换失败后释放；APSTA 冲突无断网副作用；不能通过 event mask 关闭内部完成事件；未知/敏感/变长 raw event 不越界和不泄密。

### W-03：公共 RX metadata、parser 与 Monitor

**代码**：`wifi_common`、`wifi_monitor`、promiscuous broker；拟新增文件见第 29 节。

**工作**：无分配 bounded 802.11 parser；按 callback type 验证可读数据；normalized WiFiRxInfo；bounded filters；Frame/Batch/View；filter union/last-owner cleanup；native wire Source。

**验收用例**：0～64 字节短输入、可变 MAC header、Addr4/QoS/HT Control、metadata-only callback、unknown subtype、snapLength/requireComplete、pool 与 queue 饱和、两 subscriber 关闭顺序、retained view 后 close。

parser 安全边界用 provenReadableLength，不用猜测的 driverLength。

### W-04：Raw TX 工作包

本文其他位置的 W-04 是下列 W-04A/W-04B 的统称；先通过 one-shot 回调隔离，再扩展队列与周期发送。

### W-04A：Raw TX one-shot 与可靠 completion

**工作**：strict validator、单 in-flight broker、无指针 tx snapshot、输入 ownership、timeout quarantine、driverAccepted/driverCompleted 结果区分。

**验收用例**：无效 frame 不触发 driver；connected sequence 限制；callback 中地址指针返回后失效；timeout 后旧 completion 与新请求交错；callback 缺失/注销失败；不能为了恢复而重启别人的 STA/ESP-NOW。

必须取得双板真实接收证据；本机 driver success 不等于对端应用收到。

### W-04B：Raw TX queue/batch/periodic

**前置**：W-04A 的竞争测试通过。

**工作**：native FIFO、admission fence、batch atomic admission、flush 终态账本、受限 drop-oldest-batch、native periodic worker、busy skip/stop、rate lease。

**验收用例**：部分 batch 空间不足；尚未发出的 batch 被丢弃；flush 期间并发 enqueue；定时器触发时前包未结束；session close 自动停止其 periodic；恢复 rate 失败；callback/结果观察队列饱和。

### W-05：CSI correlated packet

**前置**：Radio/broker/parser 已稳定。

**工作**：保留 legacy/HE adapter；建立各 target capture contract；同 callback 同 observation 复制 CSI 与可证明 packet；支持 none/header/full、required/requireComplete；更新 Frame/Batch/Source 方法和统计。

**验收用例**：hdr/payload 缺失、不可信布局、短 header、packet 截断而 CSI 不截断、CSI oversize、未知 FCS/加密表示、同 channel Monitor+CSI、旧 pool 被 view 持有时关闭和重开。

C3/S3 legacy、C5 HE/相关 band 只按实际支持的组合测试。不支持/尚未验证的字段不能因为结构成员存在就标 true。

### W-06：统一 wire、时间戳与 Host 工具

**工作**：实施第 9.4/12 节；writer/parser/fixture 同步替换唯一 v1；严格 canonical layout；新增 callback-time 时间标志；相对时间/UTC anchor/boot 身份处理；Monitor JSONL/PCAPNG 转换与 CSI packet 对照。

**验收用例**：checked arithmetic、重叠区、非零 reserved、未知必需 flag、错误 enum、目录/metadata 重复值不一致、零 section、padding、截断输入；时间回绕/长空窗/restart；旧 192-byte metadata 明确拒绝，无 fallback reader。

### W-07：完整 Driver typed control

**工作**：补齐第 8/27 节各 public setter/getter、target-specific schema、band/protocol/bandwidth、天线、inactive time/RSSI/TSF、PMF/coexistence/wakelock、restore/restart。

**验收用例**：前置状态、单位转换、权限/秘密读回、unsupported option、有效参数 readback、无 getter 的配置恢复拒绝、shadow state revision、feature-disabled 构建无残留对象。

### W-08：高级功能逐模块扩展

按依赖与明确契约分批：Vendor IE/Action/ROC → FTM/TWT → Roaming → Enterprise/WAPI → WPS/DPP/SmartConfig → NAN → Mesh。

每个模块必须先提交完整 option/event/result/timeout/ownership 子规范和 API map 条目；原文 `object` 与未定义类型不得直接进入正式发布声明。每个模块单独编译 gate、资源预算、错误注入和硬件证据。

没有目标设备时保留 `not-run`；公共底层不支持时记录有证据的 target-unsupported；仅因暂未实现则保留 planned，不删掉总目标。

### W-09：内存预算、旧 generation 与关闭

**工作**：与第一/三份文档共用 budget 实现；落实第 26 章；active/retired/control reserve 计数；明确 close deadline 和 cleanup-only handle。

**验收用例**：保留所有 view 后反复重开直到预算拒绝；关闭 view 后预算恢复；pool 满不挤占控制状态；JS conversion/Source send 失败；worker 调度失败；close 不等待后续 JS 语句才能释放的数据。

该项从 W-01 起持续执行，不等到功能堆完再补。

### W-10：ESP-NOW/BLE 接入与集成

**工作**：ESP-NOW 切换至新 Radio lease，不改变独立 namespace 和已有 v2 payload/TX queue 语义；BLE 只对齐共存信息和统一总预算。

**验收用例**：同信道 CSI/Monitor/ESP-NOW；异信道拒绝；某个 session 关闭不停止其他 owner；ESP-NOW timeout 不抢占 Raw TX 的 state；BLE 压力不破坏 Wi-Fi 控制终态。

### W-11：Host、三 target 与 RF 验收

执行第 31 节；每个 enabled feature 记录真实结果。先逐模块成功/失败路径，再组合负载；记录有效 PHY/channel、peer、丢弃原因和资源账本，不用平均吞吐掩盖 tail latency/队列饱和。

### W-12：迁移、生成物和冻结评审

删除被替换的原 v1 字段/reader/fixture；更新 `docs/api/docs.json`、相关 API 文档、类型、manifest、feature catalog 与 Host consumer。产品/Hub consumer 属于独立交付对象，未经用户授权不在另一个仓库自动写入，但必须列出依赖其更新的验收阻塞。

仅在 implementation 与 validation 各项完成后更新稳定性。源代码存在不等于 complete；API map 全分类不等于全实现。

---

## 31. 测试要求

### 31.1 Host/C 单元测试

必须覆盖：

- 所有 option plain-object 和 unknown-field rejection；
- STA/AP complete config field mapping；
- driver state requirement；
- scan buffer cleanup；
- Wi-Fi event descriptor、变长 payload 边界、秘密过滤和未经审查 raw export 的拒绝；
- 802.11 header 0..64 任意短输入、Addr4、QoS、HT Control；
- monitor/CSI pool exactly-once return；
- TX strict validator、connected sequence rule、TX callback timeout race、无 cookie completion 的隔离和 callback 内无指针快照；
- fixed-channel/rate/promiscuous lease rollback；
- WPS/DPP/SmartConfig/FTM/TWT cancel/close；
- secret zeroization；
- IDF API map 无遗漏。

建议新增：

```text
tests/c/test_wifi_80211_parser.c
tests/c/test_wifi_promiscuous_broker.c
tests/c/test_wifi_tx_broker.c
tests/c/test_wifi_raw_tx_validator.c
tests/c/test_wifi_monitor_resources.c
tests/c/test_wifi_csi_packet_capture.c
tests/python/test_idf_wifi_api_coverage.py
tests/python/test_wifi_public_surface.py
tests/python/test_wifi_monitor_protocol.py
tests/python/test_wifi_csi_protocol.py
```

### 31.2 JavaScript 硬件测试

```text
wifi/basic-station-hardware.js
wifi/apsta-hardware.js
wifi/full-driver-config-hardware.js
wifi/all-events-hardware.js
wifi/raw-tx-hardware.js
wifi/raw-tx-periodic-hardware.js
wifi/monitor-hardware.js
wifi/action-roc-hardware.js
wifi/ftm-hardware.js
wifi/twt-hardware.js
wifi/roaming-hardware.js
wifi/enterprise-hardware.js
wifi/provisioning-hardware.js
wifi/nan-hardware.js
wifi/mesh-hardware.js
wifi_csi/correlated-packet-hardware.js
wifi_csi/monitor-coexistence-hardware.js
```

只有 target/build 支持的测试才运行。区分 pass、fail、not-run 与 not-applicable：有能力但无设备是 not-run；目标确实不支持才是 not-applicable。每项给出 capability reason，不能把编译缺失或没有运行当成功。

### 31.3 硬件矩阵

| 能力 | ESP32-C3 | ESP32-S3 | ESP32-C5 |
| --- | --- | --- | --- |
| STA/scan/AP/APSTA | 必测 | 必测 | 必测 |
| 2.4 GHz protocol/bandwidth/LR | 必测 | 必测 | 必测 |
| 5 GHz / 11a/ac/ax | capability | capability | 必测 |
| Monitor | 必测 | 必测 | 2.4/5 GHz 必测 |
| Raw TX | 必测 | 必测 | 2.4/5 GHz 必测 |
| Legacy CSI | 必测 | 必测 | capability |
| HE CSI | 不适用 | 不适用 | 必测 |
| Action/ROC/FTM | capability | capability | capability |
| TWT | capability | capability | 必测（若 build 开启） |
| Enterprise/WPS/DPP | capability | capability | capability |
| NAN/Mesh | capability | capability | capability |
| BLE + Wi-Fi 共存 | 必测 | 必测 | 必测 |
| ESP-NOW + Wi-Fi 共存 | 必测 | 必测 | 必测 |

矩阵中的组合行不表示某芯片支持行内列出的全部 PHY/协议。每个子用例仍按该 Build Context 的真实 capability 分拆；不支持记 not-applicable 并附原因，需要硬件但未运行记 not-run。尤其不能从“C5 双频”推导支持所有 11a/ac/ax 模式或所有带宽。

### 31.4 生命周期与内存

每个可用 session 至少：

- 500 次 open/start/stop/close/reopen；
- queue/pool saturation；
- retained ByteView/source 后关闭 parent；
- timeout/cancel 时 callback 尚未返回；
- runtime restart 存在 pending Future；
- 预热后同等静止状态比较 current free/largest block/owner 账本；minimum free 只记录历史内存压力，不要求其保持不变；
- 原设计的一小时混合 soak 保留为基础场景；涉及 RX 时间戳的验证至少 2 小时，并单独覆盖多次回绕和长时间无包。

---

## 32. 迁移与破坏性变化

### 32.1 保留顶层基础 API

当前已有以下 API 不再迁移到 nested namespace：

```text
wifi.connect
wifi.disconnect
wifi.scan
wifi.status
wifi.setPowerSave
wifi.setTxPower
```

可在 v1 内增强参数和返回结构，但不改成：

```text
wifi.station.connect
wifi.station.disconnect
wifi.radio.setTxPower
```

### 32.2 新增顶层 API

```text
wifi.start
wifi.stop
wifi.configure
wifi.startAP
wifi.stopAP
wifi.apClients
wifi.deauthClient
wifi.setCountry
wifi.setChannel
wifi.getMac
wifi.setMac
wifi.acquireWakeLock
```

### 32.3 CSI 破坏性变化

- `queue` → `buffering`；
- 公开 pool/queue capacity；
- `maxFrameBytes` → `maxCsiBytes`；
- `frame.source()` 原始 CSI 改为 `sampleSource()`；
- 增加 packet capture；
- 直接替换 `esp32qjs-csi/1` 旧 192-byte metadata，不保留 reader fallback。

### 32.4 不保留开发期 alias

此前文档、示例或 Hub consumer 使用的 nested prototype 必须一次性更新；不注册双名字，
避免 v1 冻结前就背上长期兼容负担。

---

## 33. 使用示例

以下为目标 API 示例，不代表基线固件已支持。SSID/凭据、frameBytes/probeFrame/csiCapture 和网络环境由应用提供；原始帧仅用于自有或获授权的实验设备。device JS 采用项目 ES5-like 方言。

### 33.1 基础 Station

```js
wifi.start({ mode: "station" });
wifi.setCountry("TW", { policy: "manual" });
wifi.setPowerSave("minimum");
wifi.setTxPower(18);

var result = wifi.connect("Office WiFi", {
  password: "secret",
  minimumAuthMode: "wpa2-psk",
  pmf: "optional",
  timeoutMs: 15000
});

print(result.ssid, result.channel, result.rssi);
print(JSON.stringify(net.status()));
```

### 33.2 SoftAP + Station

```js
wifi.connect("Uplink", { password: "secret" });

wifi.startAP({
  ssid: "ESP32QJS",
  password: "12345678",
  channel: wifi.status().channel
});

print(JSON.stringify(wifi.apClients({ includeIp: true })));
```

### 33.3 完整 Driver 配置

以下是具备所列双频、协议及带宽能力的专用配置示例；运行前以当前 capability/schema 确认全部组合，不是 C3/S3/C5 通用默认值。不支持时应拒绝，而不是偷偷删掉 ghz5 或降低协议。

```js
wifi.configure({
  storage: "ram",
  mode: "apsta",
  country: { code: "TW", policy: "manual" },
  protocols: {
    station: { ghz2: ["11b", "11g", "11n"], ghz5: ["11a", "11n", "11ac"] }
  },
  bandwidths: {
    station: { ghz2MHz: 40, ghz5MHz: 40 }
  },
  powerSave: "none",
  txPowerDbm: 18,
  start: true
});
```

### 33.4 Raw TX

```js
var tx = wifi.rawTx.open({
  interface: "station",
  channel: "current",
  sequenceControl: "driver",
  queue: { capacityPackets: 16 }
});

try {
  tx.enqueue(frame);
  tx.flush(2000);
} finally {
  tx.close();
}
```

### 33.5 主动 CSI

```js
// TX board：应用侧 helper，返回 owner，不在启动后立即关闭。
function startProbeTrain(frame) {
  var tx = wifi.rawTx.open({ channel: 6 });
  var periodic;
  try {
    periodic = tx.startPeriodic({
      frame: frame,
      intervalUs: 10000,
      count: 100,
      busyPolicy: "skip"
    });
  } catch (error) {
    tx.close();
    throw error;
  }
  return {
    status: function () { return periodic.status(); },
    close: function () {
      try { periodic.close(); } finally { tx.close(); }
    }
  };
}
// TX 应用保存 startProbeTrain(probeFrame) 的返回值，结束时显式 close。
// intervalUs 的发送由 native 实现，不由 JS loop 控制。

// RX board：以下代码运行在另一台设备；capture 按其能力预先定义。
var csi = wifi.csi.open({
  source: { mode: "promiscuous", channel: 6 },
  capture: csiCapture,
  packet: { content: "full", snapLength: 1500 },
  buffering: { poolCapacity: 16, queueCapacity: 12 }
});
try {
  var observation = csi.receive(2000);
  if (observation !== null) {
    try {
      print(observation.info.sequence);
      // 若调用 samples()/packetBytes()/source()，返回的数据 owner 也要 close。
    } finally {
      observation.close();
    }
  }
} finally {
  csi.close();
}
```

---

## 34. Definition of Done

- [ ] 基础 API 保持在 `wifi` 顶层，未注册 `wifi.station/accessPoint/radio`。
- [ ] 高级静态路径最多一层 namespace。
- [ ] `docs/idf-wifi-api-map.json` 覆盖 allowlist 中每个 public symbol。
- [ ] CI 对新 IDF public symbol 无映射时失败。
- [ ] C 注册、`.d.ts`、docs、manifest、runtime feature catalog 一致。
- [ ] 所有 callback 经 broker/原生 operation 状态/有界队列，不直接调用 JS；队列丢失不影响内部控制完成。
- [ ] Monitor 和 CSI 不保存 driver pointer，slot exactly-once return。
- [ ] Raw TX one-shot 的迟到 callback 隔离先通过，再开放 queue/batch/periodic；完成事件不依赖观察队列。
- [ ] Raw TX 不错误承诺 encrypted/QoS/control/PHY injection。
- [ ] CSI correlated packet 与 sample 同 callback、同 slot、同 generation。
- [ ] `esp32qjs-csi/1` 和 `esp32qjs-monitor/1` writer/parser/fixture 全部为新 v1。
- [ ] STA/AP/ESP-NOW/Monitor/RawTx/CSI/NAN/Mesh 信道冲突无副作用失败。
- [ ] Enterprise/provisioning secret 不进入 status/error/log，close 时清零。
- [ ] C3/S3/C5 feature-gated build 和所有已启用能力的硬件矩阵通过；未启用/无硬件项有准确状态，未伪装通过。
- [ ] 500-cycle lifecycle、基础混合 soak 及跨回绕测试通过；清理后内存/资源账本回到定义状态，minimum-free 不作为错误的单独通过条件。
- [ ] Hardware evidence 记录 commit、board、target、antenna、band/channel、peer/router 和命令。

---

## 35. 规范性参考

本节固定源码用于核验底层能力；用户原设计中的接口仍是目标契约，不能由参考链接自动推导成当前已支持。

- [ESP32QJS baseline commit](https://github.com/99percentpeople/esp32qjs/tree/9a74f1197d53863e079c30f8559ccd5b6cd60b42)
- [ESP-IDF v6.1 `esp_wifi.h`](https://github.com/espressif/esp-idf/blob/v6.1/components/esp_wifi/include/esp_wifi.h)
- [ESP-IDF v6.1 generic Wi-Fi types](https://github.com/espressif/esp-idf/blob/v6.1/components/esp_wifi/include/esp_wifi_types_generic.h)
- [ESP-IDF v6.1 HE/TWT API](https://github.com/espressif/esp-idf/blob/v6.1/components/esp_wifi/include/esp_wifi_he.h)
- [ESP-IDF v6.1 NAN API](https://github.com/espressif/esp-idf/blob/v6.1/components/esp_wifi/wifi_apps/nan_app/include/esp_nan.h)
- [ESP-IDF v6.1 ESP-WIFI-MESH API](https://github.com/espressif/esp-idf/blob/v6.1/components/esp_wifi/include/esp_mesh.h)
- [ESP-IDF v6.1 Enterprise EAP API](https://github.com/espressif/esp-idf/blob/v6.1/components/wpa_supplicant/esp_supplicant/include/esp_eap_client.h)
- [ESP-IDF v6.1 WPS API](https://github.com/espressif/esp-idf/blob/v6.1/components/wpa_supplicant/esp_supplicant/include/esp_wps.h)
- [ESP-IDF v6.1 DPP API](https://github.com/espressif/esp-idf/blob/v6.1/components/wpa_supplicant/esp_supplicant/include/esp_dpp.h)
- [ESP-IDF v6.1 RRM API](https://github.com/espressif/esp-idf/blob/v6.1/components/wpa_supplicant/esp_supplicant/include/esp_rrm.h)
- [ESP-IDF v6.1 WNM API](https://github.com/espressif/esp-idf/blob/v6.1/components/wpa_supplicant/esp_supplicant/include/esp_wnm.h)
- [ESP-IDF v6.1 SmartConfig API](https://github.com/espressif/esp-idf/blob/v6.1/components/esp_wifi/include/esp_smartconfig.h)
- [ESP32QJS API stability policy](https://github.com/99percentpeople/esp32qjs/blob/9a74f1197d53863e079c30f8559ccd5b6cd60b42/docs/api-stability-plan.md)
- [ESP32QJS wireless concurrency contract](https://github.com/99percentpeople/esp32qjs/blob/9a74f1197d53863e079c30f8559ccd5b6cd60b42/docs/wireless-concurrency.md)

最终实现必须以 pinned ESP-IDF header、target compile capability 和硬件结果为准；本文中的
类型是目标公共 contract，不能覆盖或伪造底层不存在的能力。


## 36. 推荐提交依赖与交付清单

```text
F-CORE
  -> W-00 inventory
  -> W-01 Radio + W-09 预算/生命周期基础
  -> W-02 基础 Wi-Fi/events
  -> W-03 RX parser/Monitor
  -> W-04A Raw TX one-shot
  -> W-05 CSI correlated packet + W-06 wire/host
  -> W-04B queue/periodic
  -> W-07 全量 Driver + W-08 各高级模块
  -> W-10 集成 -> W-11 全矩阵 -> W-12 冻结评审
```

W-06 的 schema/fixture 可提前与 W-03/W-05 协同开发，但 writer/parser 必须原子化交付；不能先发布一端。W-09 为持续要求；第二、三份文档共用预算和 owner invariant。

每个实现 PR 必须提供：

| 交付 | 要求 |
| --- | --- |
| 设计差异 | task ID、已替换字段/行为、尚未解决的子契约 |
| 原生实现 | owner、lane、callback、取消/关闭路径和精确回滚 |
| 公共契约 | 已实现注册、类型、文档、manifest、feature gate 一致 |
| 测试 | 实際命令、Context/目标、结果；失败和未运行项明确 |
| 硬件证据 | firmware/IDF SHA、peer、band/channel、天线、参数和原始日志 |
| 内存 | current/largest/retired/control reserve 快照，不只报告 minimum-free |

## 37. 当前不能直接照草案实现的子契约

以下属于原方案尚未展开的细节，不用常识填成“IDF 已保证”。它们是对应任务的**开始实现前检查项**。

| 子契约 | 必须补齐 | 完成前的处理 |
| --- | --- | --- |
| `WiFiStationDriverConfig` / `WiFiAccessPointDriverConfig` 等 | 每目标 public 字段、secret、单位、只读/可写/状态 | 先生成 inventory，再审查 mapping |
| FTM/TWT/WAPI/WPS/DPP/SmartConfig 的辅助类型 | option/result/event schema、取消时机、credential owner | contract-pending，不注册空对象 |
| NAN/Mesh | 逐 public 能力、mode/channel 兼容、接纳/终止和消息 buffer | 模块单独评审，不假装基本几个方法就是完整覆盖 |
| Raw TX timeout barrier | 固定 driver 是否确保旧回调终止；共享 Wi-Fi 不可重启时的行为 | 保持 quarantine，不复用 operation |
| CSI `hdr/payload` 跨度 | 每 target/mode 的可读边界、FCS、Protected 表示 | 不宣称 header/full capture 已验证 |
| 配置临时覆盖的前值 | authoritative getter 或唯一写入 shadow + revision | 未能证明则拒绝临时覆盖 |
| 新 wire 与旧开发文件区分 | 固定长度、canonical flags、strict parser 与 transport build 信息 | 旧文件拒绝，无兼容 reader |

辅助基础类型（如 `ByteSource`、`ByteView`、`ByteSpanSource`、`EventQueue`、`NativeErrorSnapshot`）优先复用框架既有定义，不在本模块定义同名但不同语义的替身。

## 38. 来源、修订范围与实施状态

**S-W**：用户附件 `esp32qjs_wireless_api_v1_complete_wifi_design(1).md`，为本文件第 1～35 节的主要来源。保留完整 Wi-Fi 能力目标、浅层 API、legacy/HE CSI、原始帧限制、唯一 v1 布局和 target-gated 方向。

**本轮修订**：第 0、25、26、30、36、37 节及其他标注修订条款，来自上一轮评审后提出的实施决策；不能作为“基线代码已经实现”的证据。尤其是共享 Radio、控制事件可靠性、TX quarantine、`callback-time` 时间标志和 capability/manifest 分层。

**固定版本源码依据**：

- [R-RADIO]：现有 Radio once 状态和 lease 行为。
- [R-CONCURRENCY]：现有无线 callback/queue/retained storage 约束。
- [R-AGENTS]：Build Context、命令、ES5-like、生成物与唯一 v1 规则。
- [U-RX]：CSI 指针字段和 32-bit RX timestamp；字段存在不替代 packet-layout 实测。
- [U-TYPES]：scan bitmap/coexistence、指针型 TX 信息与变长/敏感事件；具体字段支持仍按 target/build 检查。

本文件生成时未执行实现、三 target build、RF 或硬件测试。所有工单与验收框仍待开发和实际记录，不用本文的详细程度代替验证证据。

[R-RADIO]: https://github.com/99percentpeople/esp32qjs/blob/9a74f1197d53863e079c30f8559ccd5b6cd60b42/components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c
[R-CONCURRENCY]: https://github.com/99percentpeople/esp32qjs/blob/9a74f1197d53863e079c30f8559ccd5b6cd60b42/docs/wireless-concurrency.md
[R-AGENTS]: https://github.com/99percentpeople/esp32qjs/blob/9a74f1197d53863e079c30f8559ccd5b6cd60b42/AGENTS.md
[U-RX]: https://github.com/espressif/esp-idf/blob/v6.1/components/esp_wifi/include/local/esp_wifi_types_native.h
[U-TYPES]: https://github.com/espressif/esp-idf/blob/v6.1/components/esp_wifi/include/esp_wifi_types_generic.h


## 第一阶段实施交接（2026-09-07）

本轮只实施 01 的现有行为修复。本文件的新能力仍为 planned，未展开接口继续保持
`contract-pending`，不加入 native registration、正式类型或 manifest。当前 Radio
仍是 boot-scoped once 启动、单固定信道 owner；新 Driver 生命周期和多 owner 属于
W-01。CSI 仍是单个未释放 pool，旧 View/Source 存活期间禁止 reopen，多代预算留给
W-09/B-10。BLE 的 operation cookie、native quarantine 与 Host 清理后缀应在 B-01
机械拆分时保留；新的 Monitor、Raw TX、CSI wire、BLE 高级功能和安全政策在各自
任务中评审，不能用本次 Host 测试替代 RF/对端验收或提升 feature 稳定等级。
