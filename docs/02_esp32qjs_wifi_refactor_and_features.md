# ESP32QJS 第二阶段：Wi-Fi 重构与新功能实施文档

- **当前状态（2026-09-13）**：Wi-Fi 主体实现与本轮可执行的短时验收已收尾，firmware 提交 `70f4e88`、`388069d`，Host 适配提交 `27fa8dd`。C5 合并镜像完成 15 项短时功能回归、6 次带 pending/retained 资源的 runtime restart；后创建 Station 的崩溃已修复。相同 healthy 状态的内存账本相等、largest block 不变，原 workspace 核对一致。最终提交镜像 `20f988e2…` 已刷入，四项定向复测和覆盖元数据核对通过。
- **软件结果**：Host C 138 项通过；完整 Python 1252 项的原终态为 2 failures / 1 skipped，两个旧测试断言修正后相关 26 项通过，原记录保留。MQuickJS、生成物、三目标矩阵及本次四项受影响配置通过。覆盖表 165 个条目同步 Host 证据，仅 15 个已满足字段/契约审查的条目提升为 implemented；其余状态与硬件资格不冒进。
- **剩余资格与后续阶段**：公开功能继续 Candidate / v1；缺少对端的 RF / 三目标实机资格仍 not-run，不把本轮短测当成全部硬件资格冻结。BLE 和长时间 soak 按用户安排后置。详见[当前短时验收记录](investigations/2026-09-13-wifi-c5-short-acceptance.md)。

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

第一份文档的 F-CORE 已完成本轮收尾，W-01 已开始内部生命周期与共享 lease 实施；见[最终证据及测试边界](investigations/2026-09-08-fcore-closeout.md)。连接交接/入站队列、原生 handle 复用交错、payload 转换及构造失败已有生产实现回归。W-00 符号/字段覆盖工具已实现，见[本轮收尾与第二阶段进度](investigations/2026-09-07-wifi-refactor-start.md)。按用户最新安排，先完成全部 Wi-Fi API，再统一执行 Wi-Fi 阶段测试；相关测试完成后才开始 BLE。Wi-Fi 完成后执行实机功能测试；长时间 soak 明确延至 BLE API 也完成后。实现期间仅保留必要编译与生成物一致性检查，新增竞争/错误注入及硬件项目登记为待测，不沿用历史通过结果。若现有无线硬件验证仍未完成，可以继续受控开发，但所有相关 feature 保留其真实的 Hardware pending/Candidate 状态。

共享 Future、ByteView、EventQueue、generation 和内存预算器只扩展一套。Wi-Fi 与 BLE 分支不得各自复制底层实现。框架继续消费不可变 Build Context，不解析 Board、Library、Agent 或产品 workspace manifest。

开发按完整功能批次推进和汇报，每次修改仅作必要的相关语法/契约检查，不逐次全量
构建。受影响配置的增量构建按批次需要执行；C3/S3/C5 与 feature-disabled 完整矩阵
保留到 Wi-Fi 阶段收尾。提前扩展构建范围仅用于定位实际失败或目标差异。进度统一
回填剩余工作表，减少内部步骤的重复调查记录。

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

  connect(ssid: string | ByteSource, options?: WiFiConnectOptions): WiFiConnectResult;
  disconnect(timeoutMs?: number): WiFiStatus;
  scan(options?: WiFiScanOptions): WiFiScanRecord[];

  startAP(options: WiFiAccessPointOptions): WiFiAccessPointStartResult;
  stopAP(timeoutMs?: number): WiFiStatus;
  apClients(options?: WiFiAPClientsOptions): WiFiAPClient[];
  deauthClient(address: MacAddress): boolean;

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
  readonly wps?: WiFiWpsModule;
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
    secondary: WiFiSecondaryChannel | null;
  };

  phy: {
    format: WiFiPhyFormat;
    bandwidthMHz: 20 | 40 | 80 | 160 | null;
    mcs: number | null;
    legacyRate: number | null;
    stbc: boolean | null;
    shortGuardInterval: boolean | null;
    guardIntervalNs: 400 | 800 | 1600 | 3200 | null;
    heLtfSize: 1 | 2 | 4 | null; // multiplier, not symbol count
    dcm: boolean | null;
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
  subtype: number | null;
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

**当前实现**：`wifi.capabilities()` 已注册，正式字段以
[`WiFiCapabilities`](../types/esp32qjs-c-api.d.ts) 和 [API 文档](api/wifi.md) 为准。
本批提供实际 target/IDF、已开放 mode/interface、物理 band/法规信道快照、
公开 feature 开关、真实 namespace 与共享 admission limits。读取不启动 Radio；
无可用法规快照返回 null 信道及 countryError。SoftAP/CSI 遵循实际编译 gate，
尚未开放模块的 feature 为 false，不由 SDK 符号存在推断实现或 RF 验证通过。
下面是后续完整能力对象的目标草案：security、protocol/bandwidth、buildProfile
以及 Raw TX/power limits 尚未进入正式类型，需随对应实现审查。

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

**当前实现**：`wifi.watch(options?)` 已注册，固定 SDK 的 53 个公开事件都有
descriptor/name/category，常用 Station/AP/scan/channel 事件提供 typed value
snapshot。[五类扩展 snapshot](investigations/2026-09-09-w02-watch-values.md)已补 FTM 摘要、Action/ROC 状态、AP 密码错误的 MAC 和 beacon rate。原生入口与订阅队列均有界，控制终态和观察分发分离。[TWT 七类 snapshot](investigations/2026-09-09-w02-watch-twt.md)已补 setup/teardown/probe/suspend/wakeup、完整 64-bit wake word 和有界 flow 数组；[neighbor report snapshot](investigations/2026-09-09-w02-watch-neighbor.md)已接入变长 TLV 校验、64 项上限、两槽共享池和关闭/转换释放，专属请求仍待 W-08；其余未审查事件只交付元数据；WPS/DPP/NAN 不读取 payload，全部标记 sensitive。raw 布局尚未
批准，includeRawEventData 只返回 unavailable reason。正式 discriminated
`WiFiEvent` 与过滤字段以 `.d.ts` 为准；下方保留完整目标草案，不能据此推断
已导出 raw bytes 或已实现高级 session。见[事件订阅实施记录](investigations/2026-09-08-w02-watch.md)。

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

**当前实现与目标区分**：`start({ mode?, storage? })` 已注册并复用接口执行器，
缺省保留已配置 mode/storage，冷启动默认为 Station/RAM；运行中只接纳相同设置。
AP/APSTA 使用已存配置且广播前校验，详见[启动 options 记录](investigations/2026-09-08-w02-start-options.md)。
`stop({ timeoutMs? })` 已接入同次调用共享的原生等待预算（默认 1000，整数 1–60000 ms），
超时保留清理后缀；SDK 同步调用不可抢占，因此不是硬性返回 deadline。
详见[stop timeout 记录](investigations/2026-09-08-w02-stop-timeout.md)。`configure(options)` 已注册，
支持 Station/AP/APSTA；正式字段以 `types/esp32qjs-c-api.d.ts` 和 manifest 为准。
详见[公开入口、错误与待验收范围](investigations/2026-09-08-w02-public-configure.md)。

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

**原生进度**：[停机事务](investigations/2026-09-08-w02-config-transaction.md)、
[接口执行器](investigations/2026-09-08-w02-configuration-executor.md)、
[事件边界](investigations/2026-09-08-w02-radio-event-boundary.md)、
[原子缺省/授权](investigations/2026-09-08-w02-config-selection.md)和
[外层捕获](investigations/2026-09-08-w02-outer-capture.md)已接入公开 configure。
两个接口配置和 controls 均经快照/读回；失败保留中央清理义务。返回 WiFiStatus，
radio.storage 报告实际存储选择；错误仅携带本次到达的配置/TX power 元数据。
运行验收未执行，既有内部增量记录是历史编码证据。保持 Station 资源的 stopAP
已接入，匹配已存配置的共享 AP 重开也已编码；新配置的 live activation 已接入固定 SDK 的分配前窗口，运行与共享策略验收仍待完成。以下 control object 已进入正式源类型。

```ts
interface WiFiCountryDetails {
  code: string;
  policy?: "auto" | "manual";
  startChannel?: number;
  channelCount?: number;
  environment?: "indoor" | "outdoor" | "X" | null;
  ghz5ChannelMask?: number;
}
interface WiFiProtocolConfig {
  ghz2?: WiFiProtocol[];
  ghz5?: WiFiProtocol[];
}
interface WiFiBandwidthConfig {
  ghz2MHz?: 20 | 40;
  ghz5MHz?: 20 | 40;
}
```

Country 仅 code/policy 时保留 SDK 选法规参数的路径；提供其他字段时要求显式 startChannel+channelCount。PHY 对象至少提供一个频段，protocol 为完整位集合，11AC/AX 不与 40 MHz 同用。未知字段、NUL 后缀与重复协议拒绝；具体范围/gate/未验收项见捕获记录。缺省与 AP 停机授权后续见下方实现；公开结果/错误已接入；高级认证仍待完成。

[缺省/授权与捕获执行交接](investigations/2026-09-08-w02-config-selection.md)已编码：健康 driver 默认保持当前 mode/storage/started；冷启动 mode 根据 station/accessPoint 组合推导（均无则 Station），storage=RAM、start=true。mode=NULL 时仅推导 mode，不覆盖已知 storage/started。最终 start=true 且含 AP 时，目前要求完整 accessPoint 输入。解析、最终纯校验和精确 token admission 在同一次 Radio mutex 内完成，失败不释放 owner。任何正在运行的 AP 都要求 allowDisconnect=true，不以瞬时零客户端结果跳过权限检查；该权限仍不能关闭其他 feature。现有 startAP 已走新准入，公开 configure 绑定与结果/错误同步已完成编码，测试未运行。

`wifi.configure()` 是复合事务，不是隐藏兼容层：先验证和分配，随后按 IDF 要求
stop/configure/start；默认不允许断开现有连接。无法回滚的 driver side effect 必须在
返回值和错误 `details.stage` 中说明。

[AP-only 原生关闭核心](investigations/2026-09-08-w02-ap-stop-native.md)已增加 APSTA→STA 模式切换、独立 AP_STOP 屏障、lease 保留与 Application requirement 缩减。已接受的模式切换只重试事件/读回后缀；不确定 setter 失败不重放。[公开 stopAP 接入](investigations/2026-09-08-w02-ap-stop-public.md)已完成 AP helper/netif 退休与同 token 的整体 stop/runtime 失败接管；Station 原生断连排空后才允许接管。代码与 C5 构建不等于实际 Station 连续性或竞争验收通过。

### 7.2 Station 连接

**当前实现**：本节列出的顶层 listenInterval/failureRetryCount、RM/BTM/MBO/FT/
OWE、SAE PWE/H2E identifier 已进入 capture，并根据固定 SDK 的 build gate、
依赖与冲突表验证。共享 Station config parser 供后续 configure 复用。
正式 scanMethod 为 fast/all-channel；[PMF disabled](investigations/2026-09-09-w02-connect-pmf.md)已接入停止后的 dedicated 操作，保留 optional/required 和安全依赖校验。
[嵌套 driver 输入](investigations/2026-09-09-w02-connect-driver.md)已接入当前已实现 Station 字段，同名字段双层定义拒绝，SSID 使用位置参数、timeoutMs 留顶层；WiFiConnectResult 已接入，见[连接结果记录](investigations/2026-09-08-w02-connect-result.md)。见[Station 参数实施记录](investigations/2026-09-08-w02-station-options.md)。

后续已补 `rssi5gAdjustment` 及 HE/VHT/SAE-PK、transition-disable、WPA3 compatible
控制。所有新增选项均已进入生产 capture 和实际 capabilities；PK-only 的强制
threshold/PMF/H2E 约束与 PHY target gate 见[本批记录](investigations/2026-09-08-w02-station-phy-security.md)。
当前 raw 字段与嵌套输入已接入；额外认证 owner 和阶段运行验收仍待完成。[二进制 SSID](investigations/2026-09-09-w02-binary-ssid.md)已接入 ByteSource 捕获与 ssidBytes 交付；Station 字节输入因 SDK 无独立长度字段仍拒绝 NUL，AP 允许。

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
  driver?: Partial<Omit<WiFiStationDriverConfig, "ssid">>;
}

interface WiFiConnectResult {
  connected: true;
  ssid: string | null;
  ssidBytes: number[];
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

**当前实施子集**：现有 channel/showHidden/timeoutMs 已追加
ssid、bssid、homeChannelDwellMs、coexistenceBackgroundScan、maxRecords。
过滤条件先由 Future capture 复制，再转入独立原生扫描槽；取消 Future 不回收
仍被 driver 使用的过滤存储。maxRecords 为 1–32，仅限制返回值和框架结果分配，
不限制 SDK 扫描列表。详见[扫描参数增量](investigations/2026-09-08-w02-scan-options.md)。
mode/activeMinMs/activeMaxMs/passiveMs 与 channel:"all" 已接入正式 v1，
替换 passive/dwellMs，详见[剩余工作与扫描 timing](investigations/2026-09-08-wifi-api-remaining.md)。
channels 与第 7.3 节列出的扩展结果字段已接入正式 v1，见[扫描字段与法规边界](investigations/2026-09-08-w02-scan-records.md)。
显式 5 GHz 选择仍需 manual 非零法规 mask；隐式 SDK 法规表无法公开查询，不能假装全部允许。
本次新增路径的阶段测试等待 Wi-Fi API 全部完成后统一执行。

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

**当前实施子集**：`startAP({ssid,password?,channel?,hidden?,authMode?,maxConnections?})`
和无参数 `stopAP()` 已实现。第一批为冷启动、独占 2.4 GHz SoftAP，open/WPA2-CCMP；
须先停止并退出原有 Radio owner，AP 存活时拒绝新 owner。配置在 driver start 前
写入 RAM。已追加 `apClients({ includeIp?: boolean })`，返回 address/aid/rssi/phy 和按需 IP；aid 为列表之后的观察值，
客户端已离开时为 null，不构成稳定 identity；includeIp 已提供本地 DHCP 的稍后 IPv4 观察值。`getMac`
已支持 station/access-point 的已初始化 driver 查询，不隐式启动。定向 `deauthClient` 已接通；不同 AP 配置在保留 Station 连接时的激活已由下述固定 SDK 窗口接通。后续已接入 build-gated WPA/WPA3/
OWE、cipher、PMF disabled/optional/required、SAE、beacon/DTIM/CSA、GTK rekey、
transition-disable 和 FTM responder 参数；共享 AP parser/native validator 与 readback 策略
见[AP 参数记录](investigations/2026-09-08-w02-ap-options.md)。

后续已接入 SAE-EXT、WPA3 compatible 和 BSS max idle 的输入、纯原生验证、
启动前读回与返回字段；默认/冲突、显式 compatible 的 WPA2/CCMP override 和
实际 gate 见[AP 扩展字段记录](investigations/2026-09-08-w02-ap-extensions.md)。
[startAP 嵌套 driver](investigations/2026-09-09-w02-ap-driver.md)已接入当前 raw 字段（SSID 留顶层），
保留整数 TU 和 raw channel 范围，双层同义字段重复拒绝；最终统一安全与 target 校验。
configure/APSTA 和已支持 raw driver config 已开放；[共享重开](investigations/2026-09-08-w02-ap-reopen.md)现允许在 Station 存活时启用与已存配置匹配的 AP。`startAP({allowDisconnect:true,...})` 已接入现有 stopped 配置事务，可替换运行 AP 或从 STA 启动不同配置的 AP；保留当前 Station mode/已存配置与 storage，明确断开现有连接且不自动重连。准入仍排除其他 feature owner，失败保留中央清理后缀。保持 Station 连接的不同配置激活已接入固定 C3/S3/C5 SDK 的原生 AP 分配前窗口；安全配置/PMF 先安装读回，再创建及启动 AP。失败保留原错误、回滚结果和关闭责任；确认从未启动的 AP 使用事件屏障退休，不伪造 AP_STOP。完整高级认证和 5 GHz/RF 验收仍待完成。
`stopAP(timeoutMs?)` 已接入默认 1000 ms、整数 1–60000 ms 的共享协作等待预算，
重试只继续已有清理后缀；返回 WiFiStatus 不变。同步 SDK 调用不可抢占。
startAP 返回配置读回快照 WiFiAccessPointStartResult；当前查询使用 WiFiAccessPointStatus，
二者不是同名接口合并，也没有兼容别名。见[stopAP timeout 记录](investigations/2026-09-08-w02-stop-ap-timeout.md)。
`wifi.status().accessPoint` 已接入当前 AP 采样及关闭/查询失败诊断；没有 AP helper 资源或未编译 SoftAP 时为 null。
只输出白名单非秘密字段，SDK 临时配置在 JS 分配前清零；信道/客户端/配置不是原子 RF 快照。
见[AP 状态记录](investigations/2026-09-08-w02-ap-status.md)。
固定 SDK 只允许对已启用接口 set_config，
因此不采用“运行 STA 时先切 APSTA，再写 AP 配置”的顺序。

```ts
interface WiFiAccessPointOptions {
  allowDisconnect?: boolean; // 默认 false；true 授权断连及 STOP/配置/START，不自动重连 Station
  ssid: string | ByteSource;
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
  driver?: Partial<Omit<WiFiAccessPointDriverConfig, "ssid">>;
}

interface WiFiAccessPointStatus {
  started: boolean; // 最近 AP_START/AP_STOP 的观察，不是 DHCP/client 就绪
  cleanupPending: boolean;
  cleanupStage: string | null;
  queryError: number | null;
  queryStage: "admission" | "mode" | "allocate" | "config" | "mac" | "clients" | "channel" | "state" | null;
  ssid: string | null; // 非法 UTF-8 或采样失败为 null
  ssidBytes: number[] | null;
  hidden: boolean | null;
  channel: number | null; // 实际 SDK 当前信道
  authMode: WiFiAPAuthMode | "unknown" | null;
  maxConnections: number | null;
  clientCount: number | null;
  mac: MacAddress | null;
}

interface WiFiAPClientsOptions {
  includeIp?: boolean; // 默认 false；查询本机 DHCP 的稍后观察值，不代表关联身份
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

**当前实现**：setCountry 与 setChannel 已注册，具体准入/返回/错误见
[Radio 控制增量](investigations/2026-09-08-w02-radio-controls.md)。须先显式启动；
国家设置限未连接的 Station，信道设置支持空闲 Station 与独占 AP。AP/APSTA
国家配置事务仍待实现；不把即时 readback 当成 AP CSA 完成。
`setMac` 已实现 stopped/零 owner 准入、跨接口冲突校验和精确读回；
`acquireWakeLock` 已实现 16 个独立原生 token、幂等 close、GC/retirement 释放
与失败保留。唤醒锁未释放时阻止 Radio stop/shutdown，失败只重试存活记录。
本批编译不代替 native failure/竞争/GC 与 RF 阶段测试，见
[MAC、唤醒锁与能力发现记录](investigations/2026-09-08-w02-mac-wake-capabilities.md)。

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
wifi.start();
wifi.setCountry("TW", { policy: "manual" });
wifi.setTxPower(18);
wifi.setPowerSave("none");
wifi.setChannel(6);
```

`WiFiWakeLock` 映射 force-wakeup acquire/release，必须 generation-checked、幂等关闭，
不能让调用者漏掉 release。

---

## 8. `wifi.driver`：完整高级 Driver 控制

2026-09-12 增量：`capabilities()`、`status()`、`restore()` 已接通公开注册与正式
类型。status 复用 `wifi.status().radio` 转换；capabilities 枚举当前 Driver 方法、
主要 SDK 控制、准入要求、构建选项和字段表。restore 要求 initialized/stopped、零
owner 及无原生清理，保留 generation 和 storage 选择。固定 SDK 两层包装吞掉
默认加载器 OOM 的路径已在现有 build-local 补丁中修正；NVS setter/commit 的
内部错误仍不可观察，成功仅表示加载器接受和 mode/storage 核对，不证明持久化
或凭据擦除。详细范围见 [Driver 文档](api/wifi-driver.md#driver-discovery-and-restore)。
当前批次增量编译/链接见 `build/w07-driver-final-evidence.json`；动态、完整矩阵、
实机/RF 仍后置。共享策略和其余故障恢复继续收尾，不能据此勾选整个 W-07。


`wifi.driver` 不是任意 ABI escape hatch，而是对 ESP-IDF 公开 Wi-Fi Driver 控制面的 typed、状态安全映射。**所有 setter 与顶层 Wi-Fi 共用 Radio mutation lane 和 lease 检查**；不允许 Driver namespace 成为绕过资源管理的第二条路径。

```ts
interface WiFiDriverAPI {
  capabilities(): WiFiDriverCapabilities;
  status(): WiFiDriverStatus;

  getMode(): WiFiRadioMode;
  setMode(mode: WiFiRadioMode): WiFiRadioMode;
  setStorage(storage: "ram" | "flash"): "ram" | "flash";

  getInterfaceConfig(
    iface: WiFiInterface,
    options?: { includeSecrets?: boolean }
  ): WiFiStationDriverConfigSnapshot | WiFiAccessPointDriverConfigSnapshot;
  setInterfaceConfig(
    iface: WiFiInterface,
    config: WiFiStationDriverConfig | WiFiAccessPointDriverConfig
  ): WiFiStationDriverConfigSnapshot | WiFiAccessPointDriverConfigSnapshot;

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
  setCountryDetails(details: WiFiCountryDetails & { startChannel: number; channelCount: number }): WiFiCountryStatus;

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
  configureTxRate(iface: "station" | "access-point", config: WiFiTxRateConfig): WiFiTxRateStatus;
  txRateStatus(iface: "station" | "access-point"): WiFiTxRateStatus;
  disablePmf(iface: "station" | "access-point"): void;

  setConnectionlessWakeInterval(milliseconds: number): number;
  setCoexistencePowerManagement(enabled: boolean): boolean;

  getEventMask(): number;
  setEventMask(mask: number): number;

  getAntenna(): WiFiAntennaConfig;
  setAntenna(config: WiFiAntennaConfig): WiFiAntennaConfig;
  getAntennaGpio(): WiFiAntennaGpioConfig;
  setAntennaGpio(config: WiFiAntennaGpioConfig): WiFiAntennaGpioConfig;

  // clearFastConnect omitted: fixed SDK stub, no cache-clearing effect.
  restore(): boolean;
  restart(options?: WiFiDriverRestartOptions): WiFiStatus;
}
```

`configureTxRate` / `txRateStatus` 已接入唯一 v1 注册、正式类型与
[driver 文档](api/wifi-driver.md)，实际范围见[速率记录增量](investigations/2026-09-09-w07-tx-rate.md)。
这是 stopped/零 owner 的显式配置与框架写入记录，没有 SDK getter；后续已接入
[Station 临时 rate lease](investigations/2026-09-09-w04-raw-tx-rate-lease.md)，AP 临时 rate、
restart 重放仍待实现。后续已接入 getProtocol/getProtocols/getBandwidth/getBandwidths，
见[PHY 读回记录](investigations/2026-09-09-w07-phy-readback.md)。对应四个 setter 已接入
[停止后的 PHY 事务](investigations/2026-09-09-w07-phy-write.md)。随后接入九个
[Driver 观察接口](investigations/2026-09-09-w07-driver-observations.md)，覆盖 band/band mode、
power save/TX power、RSSI/AID/negotiated PHY、TSF 与 inactive time 读取。已接入
[精确 owner 下的 inactive time 与显式 RSSI rearm](investigations/2026-09-09-w07-connection-controls.md)，以及
[Station-only 频段控制](investigations/2026-09-09-w07-band-control.md)。频段变更要求空闲且未关联，AP 必须显式停止；共享 AP/global policy 和完整恢复仍待实现。
[mode/country/current/home channel 读回](investigations/2026-09-09-w07-state-readback.md)已接入，home channel 无 revision，不能复用 current-channel generation。
[事件 mask getter/setter](investigations/2026-09-09-w07-event-mask.md)已接入，保留内部控制事件，仅可写 SDK probe-request 位。
[动态 CS/11b/coexistence 控制](investigations/2026-09-09-w07-policy-controls.md)已接入；
[无 getter 策略写入记录](investigations/2026-09-09-w07-policy-record.md)已接入四个原生槽、
非回绕 revision 与独立状态转换；[内部策略重放](investigations/2026-09-09-w07-policy-replay.md)已补
冻结快照、pre-/post-start 与 owner 发布边界；[Station/AP 凭据 checkpoint](investigations/2026-09-09-w07-restart-configs.md)
已补 inactive interface 捕获、RAM 重放、启动后语义比较及安全释放，完整配置恢复与公开 restart 仍待完成。当前 C5 coexistence power gate 关闭。
[Interval 与 ESP-NOW 共享接入](investigations/2026-09-09-w07-interval-integration.md)已完成公开 setter、
初始化接受基准、精确临时 token 和关闭前值恢复；运行/竞争验收仍未执行。其余草案不构成已注册 API。

### 8.0 生命周期与破坏性操作补充

[显式 PMF 控制](investigations/2026-09-09-w07-pmf-control.md)已注册 `disablePmf(interface)`，停止/零 owner、安全策略预验证、完整读回；重启重放与配置回滚保留 SDK 已观察到的 disabled 前值。[AP 显式 disabled](investigations/2026-09-09-w02-ap-pmf-disabled.md)已接入 startAP 的停止后事务；[raw configure PMF 三态配置](investigations/2026-09-09-w02-config-pmf.md)已接入 Station/AP 停止后事务，并替换旧 pmfRequired；Station connect 生命周期仍待集成。
[天线配置观察](investigations/2026-09-09-w07-antenna-observations.md)已注册 `getAntenna()`/`getAntennaGpio()`，保留 SDK 全字段与原始 bit width，仅观察 shared PHY 保存值；setters 仍为 contract-pending，需跨 modem 和 GPIO 路由所有权协调。
[显式 mode 选择](investigations/2026-09-09-w07-mode-selection.md)已注册 `wifi.driver.setMode()`，使用现有 Radio mode 字符串 off/station/softAP/station+softAP，严格停机/零 owner、读回和已知前值回滚；不启动接口，NAN mode 待 W-08 生命周期完成。

[显式 storage 选择](investigations/2026-09-09-w07-storage-selection.md)已注册 `wifi.driver.setStorage()`：停止且零 owner、SDK 接受值、失败后 unknown 和显式重选修复。固定 SDK 的 `clearFastConnect` 为 stub，C5 实现只检查 init 后返回，当前不注册会虚报清理成功的 API。

[原生 restart 阶段边界](investigations/2026-09-09-w07-restart-phases.md)已拆出 checkpoint/STOP、物理 rebuild 和 replay，允许 runtime 在阶段之间退休旧 helper、接入新 helper；这些仅为内部入口；后续已接入 [runtime helper/netif executor](investigations/2026-09-09-w07-restart-runtime.md) 和中央失败清理，[STOP 前功率/信道历史观察](investigations/2026-09-09-w07-stop-snapshot.md)已接入真实关闭路径，[Radio 写入失效标记](investigations/2026-09-09-w07-stop-validity.md)已接入，[合格 STOP 观察的停机捕获与临时 Station 准备](investigations/2026-09-09-w07-restart-stopped.md)已接入；停机写入后/无观察来源的完整恢复及公开 restart 准入/注册仍未完成。后续审计确认 [inactive time 的 NVS 写入诊断](investigations/2026-09-09-w07-inactive-persistence.md)已修正；[原启用接口的阈值 checkpoint/STOP 历史/START 后重放](investigations/2026-09-09-w07-restart-inactive.md)已接入，[同代隐藏阈值历史与目标重新启用时恢复](investigations/2026-09-09-w07-inactive-history.md)已接入；[停用接口的原生 pending 转交与激活恢复](investigations/2026-09-09-w07-inactive-deferred.md)已接入；未知来源、停机后写入和完整故障清理协调继续列为完整 restart 的实现与验收缺口。[RSSI 一次性请求语义](investigations/2026-09-09-w07-rssi-request.md)已定义为显式 rearm、不参与自动配置重放，带 generation 的原生请求记录已接入 status；运行验收待执行。

[合格 STOP 的零 owner 原子准入](investigations/2026-09-09-w07-restart-admission.md)已接入内部 Radio/runtime/AP helper 协调；同锁解析保存的 mode 并占用 lifecycle，AP pending/detach error 在重建前拒绝。该原子准入增量之后已接入下述公开 Candidate；上述其余恢复缺口仍保留。

[公开 Candidate restart](investigations/2026-09-09-w07-public-restart.md)已接入真实重建执行器、严格 timeout 捕获、状态返回和本次错误详情。后续已接通健康、已初始化的停机写入/无 STOP 历史来源：独占生命周期内准备源 helper，以 RAM START 后读取实际配置与运行观察，再沿原 checkpoint/STOP/rebuild/replay 路径恢复。干净未初始化来源已接通原子准入、空 checkpoint 和 Station/RAM 冷启动；initialized off 已接通保留配置并最终回到 off。完整 checkpoint 仍保留时，公开显式重试沿同一 lifecycle 完成 STOP/helper 退休后重建；原生退休不明或缺少完整快照的 faulted 来源仍需按原生依据核对。当前语义见 [Driver restart](api/wifi-driver.md#driver-restart)。

后续 [停机后的 storage/RSSI 写入边界](investigations/2026-09-09-w07-stopped-controls.md)已按固定 SDK 三目标实现证据放宽，不作废原 RF 历史；storage 未知/故障、其他 writer、owner 和代际检查仍生效。其余停止状态恢复继续保留。

`WiFiDriverRestartOptions` 在本轮定义为 `{ timeoutMs?: number }`；restart 只在 STA/AP/ESP-NOW/CSI/Monitor/Raw TX/其他子 owner 均退出、没有 cleanup-pending 时执行。没有 `force` 或 `requireExclusive:false` 绕过路径。

`restore()`、setMode/setBand/setMac 等各有前置状态；不符合时失败，不能替调用者停止旧资源。`wifi.configure({allowDisconnect:true})` 只授权其文档列明的 Wi-Fi 配置事务，不能越权关闭 ESP-NOW/采集 session。

`setEventMask()` 不得禁用框架完成 Future/维护生命周期必需的事件；请求掩盖保留控制事件时拒绝。观察过滤应优先使用 `wifi.watch({events})`。

rate/bandwidth 等配置的恢复需要已知前值和独占 mutation 权。没有 public getter 且未能从框架唯一写入记录获得可信前值时，不提供会伪造恢复的临时 lease。

### 8.1 完整 config schema

`WiFiStationDriverConfig` 和 `WiFiAccessPointDriverConfig` 必须覆盖当前 target 的
`wifi_sta_config_t` / `wifi_ap_config_t` 所有公开字段。不要手写一个永久不更新的
最小子集；由脚本从 IDF 6.1 header + checked mapping 生成 declaration fragment。字段适用条件、单位、bitfield、固定数组、秘密读回和读写方向必须人工审查；生成工具不直接把 C 内存布局当作 JS schema。

**实施进度**：[`wifi-driver-config-fields.json`](wifi-driver-config-fields.json) 已逐项
记录 Station 35 / AP 21 个公开 leaf，另明确排除 3 个 reserved leaf。新
`scripts/generate_wifi_config_schema.py` 复用预处理 inventory，递归展开 threshold、
PMF、BSS idle，检查全部五份已记录 variant 的字段及声明，并可验证 live SDK
header hash。生成的[完整输入声明提案](generated/wifi-driver-config-schema.md)留在
契约文档中；个人安全/PHY parser、交叉字段依赖和读回已接入 configure，并在正式
源类型声明实际支持的字段/取值。额外高级认证值与 credential owner 仍为
contract-pending，不导入正式类型。字段审查新补出的 `rssi5gAdjustment` 已进入
现有 connect capture 与 capabilities，详见[本批记录](investigations/2026-09-08-w02-config-schema.md)。

后续[内部 driver capture](investigations/2026-09-08-w02-driver-capture.md)已复用
现有安全/PHY parser，接入 ByteSource SSID、原始 TU 和显式 PMF 布尔；生成器
同时生成实际消费的 C 字段清单。前一版提案中的 Uint8Array 已按 MQuickJS 的
实际 ByteSource 契约修正。该入口的已实现子集已通过公开 configure 注册；高级认证
及其 credential owner 的复合选项/错误/结果仍待完成，不能据此称完整公开 config
schema 已实现。

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

Driver 接口配置读回已按上述开关接入；默认值和 setter 返回值都隐藏密码与 SAE
identifier，临时原生副本在所有退出清零。实际值使用独立的
WiFiStationDriverConfigSnapshot/WiFiAccessPointDriverConfigSnapshot，字段生成表
覆盖所有非保留公开叶子；枚举保留原始 ID，SSID/秘密值保留有界字节表示。
详见[实际 Driver 契约](api/wifi-driver.md#interface-configuration)。编译及字段核对
不代表秘密读取、GC/OOM、原生失败或硬件行为已完成动态验收。

---

## 9. `wifi.monitor` 原始 802.11 帧捕获 API

### 9.1 能力

```ts
interface WiFiMonitorCapabilities {
  apiVersion: "wifi-monitor/1";
  available: boolean;
  stability: "candidate";
  target: string; idfVersion: string;
  frameTypes: Array<Exclude<WiFiPacketType, "unknown">>;
  supports: {
    receive: boolean; frameSource: boolean; receiveBatch: boolean; configure: boolean;
    fixedChannel: boolean; typeFilter: boolean; subtypeFilter: boolean;
    sourceMacFilter: boolean; destinationMacFilter: boolean; bssidFilter: boolean;
    rssiFilter: boolean; nativeDecimation: boolean; nativeRateLimit: boolean;
    wireSource: boolean; hostPcapngConverter: boolean;
  };
  timestampAccuracy: "callback-time";
  limits: {
    maxSessions: number; maxPoolCapacity: number; maxQueueCapacity: number;
    maxSnapLength: number; maxBatchFrames: number; maxMacsPerRole: number;
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

当前已注册 open/capabilities、Session status/stats/receive/start/stop/close 和 Frame
bytes/copyBytes/source/close。实际单帧接口见 [Monitor API](api/wifi-monitor.md)；
已追加停止后的 configure 完整替换，见[交接记录](investigations/2026-09-09-w03-monitor-configure.md)。
已追加 receiveBatch 与 Batch info/bytes/close，见[Batch 接入记录](investigations/2026-09-09-w03-monitor-batch.md)。
下方完整目标中的统一 wire、完整 PHY/time 仍待实现，
不作为实际注册能力。接入记录见[公开单帧 Monitor](investigations/2026-09-09-w03-monitor-public.md)。
默认 current channel、全部四种
可交付 callback type、validOnly=true、sampleEvery=1、maximumRateHz=0（不限速），
pool/queue 各 16、snapLength=2048、requireComplete=false、powerSavePolicy=preserve。
pool/queue 各 1–128，snapLength 1–16384，sampleEvery 1–UINT32_MAX，maximumRateHz
0–1000000，minimumRssi -128–127。types/subtypes 允许空数组表示不匹配任何对应
帧；每个 MAC role 接收一个字符串或 1–8 个地址，拒绝空列表与重复地址。相同 role
按 OR、不同 role 按 AND，重复 type/subtype 拒绝。已定义但 SDK 不交付的 unknown
不作为可选 capture type。输入 undefined 使用默认值，null 不作省略别名。

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
| 12 | `u32` | driver-reported payload length；不是读取边界 |
| 16 | `u32` | target adapter 已证明的原始 packet 可读 span 长度 |
| 20 | `u16` | 实际捕获的 MAC header 前缀长度；可短于完整 header |
| 22 | `u16` | 与 CSI directory 相同的 packet record flags |

Canonical ordering：header、全部 directory、全部 metadata、按 frame index 排列的 packet bytes；每个 packet section 4-byte 对齐，包括最后一段的 padding，padding 为零。长度、header 前缀和重复字段关系同第 12.6 节。

新增 host 工具：

```text
scripts/esp32qjs_monitor.py
```

职责：严格解析 `esp32qjs-monitor/1`、输出 JSONL 摘要，并在用户提供 UTC boot anchor 或可接受相对时间时转换 PCAPNG。设备端不承担 PCAPNG wall-clock 策略。

当前已接入 Monitor Batch wire Source 与共用严格 parser、JSONL CLI；见 [实施记录](investigations/2026-09-09-w06-monitor-source.md)。已接入 [PCAPNG/显式时钟锚点导出](investigations/2026-09-09-w06-monitor-pcapng.md)；生命周期、跨语言和外部 reader 测试源码暂未运行，仍待 Wi-Fi 集中验收。

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
  stability: "candidate";
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
  rateLeaseInterfaces: ("station")[];
  supports: {
    driverSequence: true;
    applicationSequence: true;
    txDoneCallback: boolean;
    fixedChannel: boolean;
    nativeQueue: boolean;
    batchAdmission: boolean;
    periodicTx: boolean;
    rateLease: boolean;
    callerFcs: false;
  };
  limits: {
    minimumFrameBytes: 24;
    maximumFrameBytes: 1500;
    maximumQueueCapacity: number;
    maximumBatchFrames: number;
    minimumPeriodicIntervalUs: number | null;
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
  radioGeneration: number;
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
  rawRate: number;
  rawStatus: number;
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

当前已注册 open、send、enqueue、enqueueBatch、flush、startPeriodic、status、stats、close
及周期对象 status/stop/close；实际字段、调度机会计数及 timeout 副作用见
[Raw TX API](api/wifi-raw-tx.md)。Station rate 已接入正式类型与 capture/native lifecycle，
详见 [临时速率租约](investigations/2026-09-09-w04-raw-tx-rate-lease.md)。capabilities.periodicTx/rateLease 为 true，
rateLeaseInterfaces 为 station；AP 临时 rate 与兼容 lifecycle 契约仍待完成，稳定等级仍为 Candidate。

```ts
interface WiFiRawTxOpenOptions {
  interface?: "station" | "access-point";
  channel?: "current" | number;
  sequenceControl?: "driver" | "application";
  validation?: "strict" | "basic";
  rate?: WiFiTxRateConfig;    // exclusive pre-start Station; known previous write required
  timeoutMs?: number;
  queue?: {
    capacityPackets?: number;
    overflow?: "reject-newest" | "drop-oldest-batch";
  };
}

interface WiFiRawTxSession {
  send(frame: ByteSource, options?: { timeoutMs?: number }): WiFiRawTxSessionResult;
  enqueue(frame: ByteSource): WiFiRawTxAdmission;
  enqueueBatch(frames: ArrayLike<ByteSource>): WiFiRawTxAdmission;
  flush(timeoutMs?: number): WiFiRawTxFlushResult;
  startPeriodic(options: WiFiRawPeriodicTxOptions): WiFiRawPeriodicTx;
  status(): WiFiRawTxStatus;
  stats(): WiFiRawTxStats;
  close(): void;
}

interface WiFiRawPeriodicTxOptions {
  frame: ByteSource;
  intervalUs: number;
  count?: number;             // schedule opportunities, including skips; 0 = continuous
  startDelayUs?: number;
  timeoutMs?: number;
  busyPolicy?: "skip" | "stop";
  stopOnError?: boolean;
}

interface WiFiRawPeriodicTx {
  status(): WiFiRawPeriodicTxStatus; // full fields in the current API/types
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

原生恢复前提已补[物理终止证据传递](investigations/2026-09-09-w04-raw-tx-termination.md)：
broker 保留 exact token，Session/周期任务消费后才退出；physical deinit 不会伪造
MAC completion。[原生物理恢复](investigations/2026-09-10-w04-raw-recovery-native.md)已接入共享 owner 准入、STOP、SDK task 屏障、deinit 和原 owner 退休；[公开 Raw TX recover 与前值 checkpoint](investigations/2026-09-10-w04-raw-recovery-public.md)已接入共享 Future/runtime 路由，恢复临时速率的永久前值，取消后保留中央清理；当前仅接纳可信健康 STARTED 来源，已故障/不可读来源与完整运行验收仍待完成。

[STA/AP 共用启动前速率事务](investigations/2026-09-10-w04-rate-transaction.md)已接入现有 Radio 借用/恢复路径；AP 的配置验证、helper 生命周期和公开临时 rate 已由后续 [AP/APSTA 接入](investigations/2026-09-10-w04-apsta-rate.md)完成编码，运行验收未执行。

[AP 临时速率原生生命周期](investigations/2026-09-10-w04-ap-rate-native.md)已编码启动前快照核对、rate owner 交接和关闭时原子换回 lifecycle；runtime/AP helper 与公开 Session 已在后续接入并链接。

[AP-only 临时速率 Session/helper 接入](investigations/2026-09-10-w04-ap-rate-session.md)已编码并开放对应 rate 参数；关闭保留 helper token，物理恢复消费原 owner 而不重复关闭重建 helper。[停止态 APSTA 临时速率](investigations/2026-09-10-w04-apsta-rate.md)随后已接入；集中验收仍未完成。

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

CSI BSSID/frameTypes/frameSubtypes、公共 `WiFiRxInfo` 字段及 slot/sequence 耗尽路径
已编码接通，实际参数及 null/过滤语义见 [CSI API](api/wifi-csi.md)。默认 packet none
仍检查同次已证明可读的 header，不分配 packet bytes。完整性依据 driver packet
report，Host 与 Monitor 共享验证规则。实现和验证状态以
[剩余工作清单](investigations/2026-09-08-wifi-api-remaining.md)为准；共享总预算和集中
运行/RF 验收仍待完成。

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
- `content: "full"`：在同次 native RX 完成复制所证明的 packet 范围内解析完整可变 MAC header，然后复制该 packet 前缀，最多 `snapLength` 字节；不拼接 SDK 的固定 `hdr+24` payload 指针。
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
| 20 | `u16` | 实际捕获的 packet header 前缀长度；可短于完整 header |
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
| 16 | `u32` | capture Session generation；CSI 和 Monitor 各自的采集代次 |
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
| 97 | `u8` | packet subtype，0–15；frame control 不可用时 255 |
| 98 | `u8` | FCS state enum |
| 99 | `u8` | packet capture mode enum |
| 100 | `u16` | parser 判定的完整 MAC header 所需长度；未知时 0，可大于捕获长度 |
| 102 | `u16` | reserved，固定 0 |
| 104 | `u32` | driver-reported payload length；不可用时 0，依 packet bit 17；不是读取边界 |
| 108 | `u32` | driver-reported 原始 packet length；不可用时 0，依 packet bit 18；不等于已证明的可读 span |
| 112 | `u32` | captured packet length |
| 116 | `u32` | packet flags |
| 120 | `u8` | legacy rate；不可用时 255 |
| 121 | `u8` | signal mode；不可用时 255 |
| 122 | `u8` | AMPDU count；不可用时 255 |
| 123 | `u8` | RX state；不可用时 255 |
| 124 | `u16` | guard interval ns：0 不可用；400/800/1600/3200 |
| 126 | `u8` | HE-LTF size 倍数：0 不可用；1/2/4，非 symbol 数量 |
| 127 | `u8` | reserved，固定 0 |
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
| 17 | smoothing value available |
| 18 | DCM available |
| 19 | DCM value |
| 20..31 | reserved |

所有 value 位仅在对应 availability 位置 1 时可置 1；特别是 smoothing 的 bit 6 依赖 bit 17。不可用不等于已知 false。

GI 是 PHY symbol 的保护间隔，与 timestampAccuracy 独立。HT/VHT 可用值为
400/800 ns，HE 为 800/1600/3200 ns；同时存在 short GI 与数值时必须一致。
HE-LTF/DCM 仅用于 HE PHY，未知值为 0/null 或 availability=false。
SU/ER-SU 的 DCM/STBC 原始位同时为 1 且 GI code 为 3 时，归一化为
4x LTF、800 ns、data DCM/STBC 均 false。MU 的 DCM 与 TB 的 GI/LTF/DCM
需要额外每用户/trigger 信息，当前保持未知。详见
[本轮实现及验证边界](investigations/2026-09-10-w06-guard-interval.md)。

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
| 15 | frame control available |
| 16 | duration/ID available |
| 17 | driver-reported payload length available |
| 18 | driver-reported packet length available |
| 19..31 | reserved |

frame control 不可用时 offset 88 为 0、subtype 为 255、bits 5–12 为 0；可用时 subtype 和 bits 5–12 从 frame control 位域派生。duration、sequence、QoS 不可用时对应 word 为 0；可用时要求 frame control 可用，且已证明 span 至少分别为 4/24/26 字节。parse-valid 说明原始 callback header 解析成功，不代表 snapLength 捕获的前缀包含完整 header。

driver payload/packet report 的 availability 分别独立于 section present 和 pointer-valid；不可用时对应长度为 0，已知为零则长度为 0 且 availability 为 true。Monitor SDK 只有原始 packet report，没有独立 payload_len，因此不以解析后的剩余长度冒充 driver payload report。目录的 payload length 与 metadata offset 104 共用 bit 17。

Full capture 的 truncated 与 captured < driver packet report 一致；report 不可用时
以 proven readable span 为基准。readable 不能超过已知 driver packet report；原生只
复制部分帧时，即使 captured 等于 readable 仍为 truncated。header-only 不标截断。
目录只能核对 readable 下界关系，完整 metadata 和 Host parser 核对 report 关系。

#### 12.4.5 v1 数值枚举表

除下表列出的值外，其余值均为非法；writer 不得写入未定义值，parser 必须拒绝。明确的 `unknown` 值属于合法值。

| 枚举 | 数值映射 |
| --- | --- |
| Secondary channel | `0=none`, `1=above`, `2=below`, `255=unknown` |
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

CSI section 非空时 segment count 为 1–3；各 segment 非空且 offset 连续，segment
length 之和加 trailing padding（0–3 字节）必须等于 CSI length，IQ count 之和必须
等于 metadata 总数。signed-int8 / signed-int12-le / signed-int12-packed 分别是
每 IQ pair 2 / 4 / 3 字节，sample bits 分别为 8 / 12 / 12；已知 encoding 的每个
segment 必须满足这一长度关系。unknown encoding 的 sample bits 为 0。

range 的 start 不大于 end，两组 range 不重叠，但保留原始排列顺序（如正频后负频）；
known layout 的 range 元素总数等于该 segment 的 IQ count，schema、encoding 和
segment type 均不能为 unknown。同一 segment 的 null index 不重复；null 列表沿用
既有 layout 的索引语义，不据此从 payload 中删除样本。未用 range/null/segment
slot 全部写零。

无 CSI section 时 layout、CSI flags、encoding/schema/count/padding 均为零。Monitor
额外不携带 RX flags 中的 CSI channel-estimate validity。结构校验不证明 target
推导出的 RF layout 正确；仍须 target-specific 验收。

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
- 每段开始 4-byte 对齐，padding 必须为零；最后一段结束也补齐到 4-byte，计入 total bytes。
- absent section 的 offset 和 length 必须同时为零。
- packet bytes 为 `header || captured payload`，不是 JS 序列化对象。
- directory 的 captured header prefix length 为 `min(metadata.headerLength, packetCapturedLength)`；metadata.headerLength 未知时为 0。该前缀可能被 snapLength 截断，不能作为完整 MAC header 的证明。
- directory 的 captured packet length 必须等于 metadata offset 112，并且不大于 directory 中 adapter 已证明的可读 span。CSI directory 的 CSI length/sequence 分别等于 metadata offset 76/0。
- directory 的 driver payload length 必须等于 metadata offset 104；原生报告长度不作为复制、分配或 section 边界。metadata offset 108 的原始 driver packet length 可以不同于 directory 的已证明可读 span，不要求二者相等。
- directory 与 metadata 的 packet present/truncated/header-only/parse-valid/pointer-valid 标志必须逐项对应，位位置不同，不能直接比较整个 flags word。
- parser 使用 checked arithmetic，拒绝整数溢出、重叠、越界、未知 enum、非 canonical offset 和非零 reserved。

只有实际 section length 决定输出 offset 和复制长度。无 packet section 时 offset/length、captured header prefix、present/header-only/truncated 均为 0；仍允许保留已知的 driver 报告、已证明的 span 和原始 header 解析事实。报告的 payload length 可大于已证明 span，例如 header 可读而 payload pointer 尚不能证明时。

存在 packet section 时必须 pointer-valid；parse-valid 必须 pointer-valid。full capture 的 truncated 等价于 captured length 小于已证明 span。header-only 必须是已成功解析的完整 header，captured length 等于 metadata.headerLength，且 truncated 为 false；有意省略 body 不算 snap 截断。

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

[原生终态记录与取消保护](investigations/2026-09-09-w08-action-native.md)已编码，
[Radio/控制终态/两级排空](investigations/2026-09-09-w08-action-radio.md)已接入；
[公开 send Future 与 runtime 清理](investigations/2026-09-09-w08-action-public.md)已接入 Candidate；
[ROC Session/open/wait/close 与 runtime 清理](investigations/2026-09-09-w08-roc-public.md)已接入 Candidate；
[SDK 独立退休证明](investigations/2026-09-10-w08-action-quiescence.md)已接入缺失/歧义终态回收；
[Action/ROC 内部物理恢复阶段](investigations/2026-09-10-w08-action-physical-recovery.md)已编码精确 lifecycle 准入、STOP/deinit 后缀和原 owner 退休；[恢复 checkpoint](investigations/2026-09-10-w08-recovery-checkpoint.md)已补保留 operation owner 的完整 PHY/home/凭据保存；[恢复 helper 退休与中央失败清理](investigations/2026-09-10-w08-recovery-helper.md)已接入；[分阶段 runtime 协调与公开 wifi.action.recover](investigations/2026-09-10-w08-recovery-public.md)已接入可读来源、精确 sequence/generation、原 owner 排空和成功配置重放；不可读/已故障来源及完整运行验收仍待完成。
SDK 的 TX_DONE 不是 duration 终止；8-bit op_id 复用前必须完成原生及事件排空。
三目标 SDK cancel 不校验 context/op_id，本批 adapter 在原生 ioctl 执行点校验
框架请求的 owner；仅本地 token 验证或 cancel 返回成功均不能释放原生责任。

```ts
interface WiFiActionAPI {
  capabilities(): WiFiActionCapabilities;
  send(options: WiFiActionSendOptions): WiFiActionResult;
  status(): WiFiActionStatus;
  remainOnChannel(options: WiFiRocOptions): WiFiRocSession;
}

interface WiFiActionSendOptions {
  interface?: "station" | "access-point";
  channel: number;
  secondaryChannel?: WiFiSecondaryChannel;
  destination: string;
  bssid?: string;
  payload: ByteSource; // 1-1476-byte Action body including category, no MAC header/FCS
  waitMs?: number; // native residency 1-60000, default 100
  timeoutMs?: number; // public deadline 1-60000, default 1000
  noAck?: boolean;
}

interface WiFiRocOptions {
  interface?: "station" | "access-point";
  channel: number;
  secondaryChannel?: WiFiSecondaryChannel;
  durationMs: number;
  allowBroadcast?: boolean;
  timeoutMs?: number;
}
interface WiFiRocSession {
  status(): WiFiRocStatus;
  wait(options?: { timeoutMs?: number }): WiFiRocStatus;
  close(options?: { timeoutMs?: number }): void;
}

```

Action/ROC 通过专用 lane 与 fixed-channel scheduler。与 connected STA/AP 冲突时
不得隐式离开 home channel；只有 ESP-IDF 明确支持并通过 capability 的 off-channel
场景才允许。

---

## 14. `wifi.ftm`

[SDK 报告释放修复](investigations/2026-09-10-w08-ftm-sdk-report.md)、
[Radio 单会话/屏障](investigations/2026-09-10-w08-ftm-radio.md)、
[原生 Session/报告存储/runtime 清理](investigations/2026-09-10-w08-ftm-session.md)
之后，现已接入[公开 FTM Session/Future](investigations/2026-09-10-w08-ftm-public.md)。
正式字段以 [FTM API](api/wifi-ftm.md) 与 `.d.ts` 为准；实现仍为 Candidate。

```ts
interface WiFiFtmModule {
  capabilities(): WiFiFtmCapabilities;
  start?(options: WiFiFtmOptions): WiFiFtmSession;
  status?(): WiFiFtmGlobalStatus;
  recover?(options: WiFiFtmRecoveryOptions): WiFiFtmRecoveryResult;
  setResponderOffsetCm?(centimeters: number): WiFiFtmResponderOffsetStatus;
  responderOffsetStatus?(): WiFiFtmResponderOffsetStatus;
}
interface WiFiFtmSession {
  status(): WiFiFtmStatus;
  receive(options?: WiFiFtmWaitOptions): WiFiFtmReport | null;
  end(options?: WiFiFtmWaitOptions): void;
  close(options?: WiFiFtmWaitOptions): void;
}
```

- Wi-Fi + SDK FTM 并开启 initiator 或 responder + SoftAP 时注册 namespace；
  initiator 才注册 Session/start/status/recover，responder + SoftAP 才注册 offset 方法。
  capabilities 与实际 gate 同步，关闭 gate 不注册不可调用的占位 API。
- 已启动 Station；同信道可与 APSTA 共享，离信道仍受 Radio owner/连接约束。
- frameCount、burstPeriodMs、maxReportEntries 和 timeoutMs 均有严格范围；固定
  SDK 拒绝的 100 ms 在 capture/Radio 预验证前置拒绝。
- receive timeout 返回 null 且只结束等待；end 保留报告；close 放弃报告。
  end/close/open timeout 抛错误后仍保留原生清理责任。
- 详细 entry 有界复制，64-bit 时间戳使用 low/high words；重复 receive 读取已保存
  的报告，不重读 SDK。global watch 继续只观察摘要。
- responder offset 的 `setResponderOffsetCm` / `responderOffsetStatus` 已接入停止 AP/APSTA 的零 owner 准入、独立 responder gate、写入记录和启动前重放，见 [offset 实施记录](investigations/2026-09-10-w08-ftm-offset.md)。缺失/歧义终态的[原生物理恢复阶段](investigations/2026-09-10-w08-ftm-physical-recovery.md)已编码；[共享 runtime/AP helper 协调和公开 recover](investigations/2026-09-10-w08-ftm-recovery-public.md)已接入，限定可信可读的健康 STARTED 来源；已故障/不可读来源及完整运行验收仍待完成。
- [恢复销毁顺序](investigations/2026-09-10-w08-recovery-teardown.md)已修复：Future prepare 请求取消后，即使尚未排空也推进已准入的中央恢复清理及全部原 owner 服务；所有 gate 通过前保留 runtime。
- GC/OOM/Future/队列竞争、完整 target/gate 矩阵与 RF 验收统一留到 Wi-Fi 阶段测试。

---

## 15. `wifi.twt`

`capabilities/status/probe` 已接入 C5 HE Candidate，正式字段与超时语义见
[API](api/wifi-twt.md)和[公开 probe 交接记录](investigations/2026-09-10-w08-twt-probe-public.md)。
probe 使用已关联 Station，独立 Radio lease/原生 identity、Future worker 发布和
boot cleanup；完成不等于原生排空，失败后保留占用并只重试未完成后缀。
probe 返回关联 AP 的存活观察，不是 TWT Agreement 或精确 RF cookie。
iTWT/bTWT Agreement setup/teardown、suspend/恢复及完整故障恢复仍待完成。
[iTWT setup 响应/dwell 定时器](investigations/2026-09-10-w08-twt-setup-timer.md)
已使用不复用数字身份，替换跨异步队列借用的 pending dialog 地址；原生消费前
核对当前请求和阶段，失败保留原始 timer 错误与 handle。实际 C5 SDK timer
table 和两个 timeout process 已接入保护；setup 提交 wrapper 目前仅在 archive，
公开 Agreement caller、TX/RF 身份、完整退休及阶段运行仍待完成。
[setup 原生结果与观察队列](investigations/2026-09-10-w08-twt-setup-result.md)
已增加八槽结果记录、不可复用请求 ID、SDK/观察错误分离和首个结果保留；实际
setup 发布链已改为零等待。预留/读取/释放尚待 Agreement caller；释放接口
只消费独立退休证明，不能由 setup event 或 timeout 推导排空。
[setup 调用交接](investigations/2026-09-10-w08-twt-setup-submit.md)已编码稳定
native 配置、公开 SDK preflight 保留和精确 identity 交还；API/driver/timer
错误与未完成返回分别保留。当前桥接仅在 archive，仍待 Radio/Agreement
caller 与完整退休；没有增加公开占位 API。
[setup TX 回调隔离](investigations/2026-09-10-w08-twt-setup-tx.md)已绑定发送前
不可复用身份并在 callback 19 校验当前 pending/temporary ID，拒绝迟到和重复
individual 完成；原 bTWT 分支保留。TX ledger 延迟预算增加 256 B，静态对象
尺寸不变；生产构建/链接不替代后置竞争及 RF 验收，Agreement/完整退休仍待完成。
[setup 精确取消](investigations/2026-09-10-w08-twt-setup-cancel.md)已接入内部
native dispatch，按 registry identity 检查 pending/flow/timer，保留 stop/delete
失败后缀；已建立 Agreement 必须 teardown，异常 AP bitmap 不能直接清除。
既有 timer/TX 预算保持，取消确认不等同联合退休；worker caller 和公开 Agreement
仍待接入，竞争/RF 用例后置。
[setup 观察事件排空](investigations/2026-09-10-w08-twt-setup-event.md)已接入
Radio 常驻 handler，按复制的 identity/sequence 确认队列顺序；发布期间
固定记录，失败或新观察撤销旧证明，释放还需精确 revision 和独立原生退休。
八槽结果账本因对齐增加 64 B 延迟 INTERNAL 预算。完整联合退休和 Agreement
caller 仍待完成，fixture 仅 AST，未执行竞争或实机验收。
[pending setup 联合释放](investigations/2026-09-10-w08-twt-setup-retire.md)已串联
精确取消、flow 证据保留、原生 TX/timer 静止查询、TASK/native/event 屏障和
原生复核释放。新观察撤销 cancelled，变化的 revision 重新建立屏障；实际
关闭执行器仍待 Radio/Agreement caller，已建立 Agreement 的 teardown、
bTWT/信息定时器及物理恢复未完成。结果/timer/TX 账本上限不再增加，运行后置。
[iTWT teardown 提交/观察](investigations/2026-09-10-w08-twt-teardown-submit.md)
已接入内部 native dispatch：精确 request/flow 准入、一次尝试及原始提交错误，
事件 29 原生先存结果后零等待观察。flow-only 事件不冒充 RF cookie。结果账本
本批另增 64 B（八槽各 72 B）；teardown 记录不能用 pending 退休证明释放。
TX/PM 身份、完整 teardown 关闭及 Radio/Future/Agreement 仍待完成，运行后置。
下面保留先前内部实施证据；后续 Agreement 草案继续 contract-pending。

[SDK/输入捕获](investigations/2026-09-10-w08-twt-capture.md) 已编码：iTWT/bTWT
严格字段、GC roots、原异常保留、uint64 时间换算、C5 响应超时/最小睡眠和 SDK
最大睡眠约束。SDK 会改写 flow ID，部分完成事件没有 cookie，0 ms suspend
不是 resume。[原生请求记录](investigations/2026-09-10-w08-twt-lane.md)已补共享
boot identity、独立 cookie、SDK 写回/AP 结果、迟到事件、关闭及精确退休证明消费；
实际 SDK 退休探针、owner registry、Radio/Future/Agreement 与公开契约仍待完成。
[SDK 原生快照与 setup 准入](investigations/2026-09-10-w08-twt-sdk.md)已补
C5 pending 表/flow/ID 检查和 bTWT ID 校验；timer handle 为空不构成 callback
退出证明，原生 TX/timer 退休和最终 SDK→wrapper 调用链仍待公共接入后验证。
[timer/native queue 顺序屏障](investigations/2026-09-10-w08-twt-fence.md)已实现
实际 marker 调度、callback 存储退休和精确 token/revision；尚未独立证明 TWT
driver timer/TX 已退休，不替代后续原生/事件屏障和 owner 接入。
下列 Agreement 接口继续是目标草案，不进入正式类型或注册表；
正式 `WiFiTwtModule` 已包含 capabilities/status/probe、setupIndividual/agreements，
individual 对象已包含 status/close/suspend/resume；`broadcasts(options?)` 已实现
AP 公告查询（固定 32 项原生快照、Future、无隐式 setup/PS 写入），见
[广播发现](investigations/2026-09-10-w08-twt-broadcast-discovery.md)。
广播 setup/teardown 的 owner/TX/timer 生命周期仍待完成；其余草案方法保持 contract-pending。
其中[响应/dwell timer 适配](investigations/2026-09-10-w08-btwt-timer.md)已接入
原生 callback/queue 与连接关闭路径；TX/RX 完成关联、结果捕获和完整联合
回收仍待实现，未因内部 timer 适配而注册 setupBroadcast。

```ts
interface WiFiTwtAPI {
  // Agreement capabilities will extend the sole v1 after implementation.
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

[配置接口 Candidate](investigations/2026-09-09-w08-vendor-ie.md)、
[预启动交接](investigations/2026-09-09-w08-vendor-prestart.md)和
[有界接收 watch](investigations/2026-09-09-w08-vendor-watch.md) 已接入。
正式 `set/clear/status/watch` 字段、类型和错误见 [API](api/wifi-vendor-ie.md) 与
源类型 `WiFiVendorIeModule`；不再保留返回 object 的配置草案。

`watch` 支持 OUI 过滤、单队列 1..32 容量、最多四订阅和合计 64 个事件槽；
关闭后 native queue 的容量计费保留到真正销毁。callback 只复制当前 IE 与
SDK metadata，不保存 driver pointer。SDK 没有提供接收 interface 或 slot index，
不得从 frame 推断。物理 Radio generation 隔离迟到 callback，注销成功但尚未
排空时只重试 drain 后缀。模块仍为 Candidate，运行竞争/RF/实机验收未执行。

---

## 17. `wifi.roaming`：802.11k/11v/11r

已接入 Candidate：`capabilities()`、RRM gate 下的 `isRrmSupported()`、WNM gate
下的 `isBtmSupported()` / `sendBtmQuery(options)`。三个 SDK gate 均关闭时模块
不注册。11r 使用既有 Station `ftEnabled`，没有新增独立开关。

BTM typed options/results 已展开到[API 契约](api/wifi-roaming.md)与源类型：
reason 为十种 SDK reason 的字符串；candidates 最多 16 个独立 unicast BSSID，
含 uint32 BSSID information、operatingClass/channel/phyType 及可选 preference。
仅框架生成有界 SDK candidate 字符串；不接受任意原始串或隐式 scan-cache list。
APSTA 必须显式 `allowApChannelChange:true`，其他共享/固定信道 owner 阻止提交。
返回 `WiFiBtmQueryResult` 只确认 SDK submit，不是 RF 完成或 roam 完成；不得重试
一次已提交但上层未确认的请求来假装恢复。原始 SDK 错误与准入阶段分别保留。

RRM gate 下的 `requestNeighborReport(options?)`、`status()` 和
`WiFiNeighborReportRequest.status/receive/cancel/close` 已接入 Candidate，
详见[公开请求记录](investigations/2026-09-10-w08-neighbor-public.md)及
[正式 v1 契约](api/wifi-roaming.md)。open/receive 为 native-driver Future；
共享字段 decoder、原生 identity/报告预算、精确退休及 runtime 排空已接入。
观察发布在 Future polling 返回后的 runtime poller 执行，并受未退出 waiter 保护。

固定 SDK 的 8-bit dialog token 回绕/reset 后仍有 RF 迟到包歧义，结果明确标注
`correlation:"sdk-dialog-token"`，不能把 callback identity 等同 RF freshness。
专属 roaming `watch()` 已复用 Wi-Fi broker 接入有界 typed observations，
详见[实现与边界](investigations/2026-09-10-w08-roaming-watch.md)。完整 RF/生命周期验收及这些歧义的进一步处理仍未完成；
不保留 request 的旧占位签名，也不将新增未执行 fixture 记为通过。
自动漫游阈值和 AP 选择策略仍由 JS Library 决定。

---

## 18. `wifi.enterprise` 与 `wifi.wapi`

[原生凭据 profile](investigations/2026-09-10-w08-enterprise-profile.md)已编码：
不可变复制、双 profile/字节预算、精确引用和最后释放清零。SDK 借用证书/密钥、
identity 映射、disable reset、SDK 密码副本清零及部分失败退休已列入前置审查。
公开 configure/enable/disable/clear/status 和以下类型仍 contract-pending，
尚未接入 Radio、JS capture 或认证运行验证。
[SDK 凭据副本清理/替换](investigations/2026-09-10-w08-eap-secrets.md)已加入
build-local 修补：global/SM 清零、OOM 保留、PAC 旧值释放；SDK 安装事务、
worker 退休和完整认证运行仍未完成，不能据此开放并发改凭据。
后续[worker 退休保护/原生观察](investigations/2026-09-10-w08-eap-lifecycle.md)
已编码独立 exit ack、分配失败清理、失败保留与 SDK task 快照；完整 driver/callback
所有权及安装事务仍待接入，不能将资源快照为零当作 driver 已完全退休。
后续 [EAP control/失败清理](investigations/2026-09-10-w08-eap-control.md) 已编码
任务串行化、API lock 失败处理、driver/callback/method 步骤记录、后缀重试和
deattach 错误传播；不确定 callback 接管保留有界隔离并要求重启。完整 profile
安装、Radio identity、runtime cleanup 和公开 API 仍未接入，阶段运行测试后置。
后续 [profile 安装/借用退休](investigations/2026-09-10-w08-eap-install.md) 已编码
独立 profile 引用、精确 identity、全字段安装/enable、失败清理保留和 clear。
PEM/DER 长度分别处理，短 PAC 拒绝；TLS/PEAP/TTLS 安装要求 CA 或支持的 bundle。
Radio admission、runtime cleanup 与公开 API 仍未接入，认证与运行测试 not-run。
后续 [Radio 借用/runtime 清理](investigations/2026-09-10-w08-eap-radio.md) 已接入
精确 helper lease 固定、生命周期门槛、失败 identity 保留和退出时断连/凭据退休。
成功安装不占用 scan/connect 槽。当前排他准入；公开配置/启停策略、JS/Future/GC
与完整共存仍待实现，动态运行和实机认证未验证。
后续 [JS 凭据捕获](investigations/2026-09-10-w08-eap-capture.md) 已编码：严格
字段/方法、GC roots、ByteView read lease、现有预算内未完成 profile 构造及失败
清零。configure 重入保护、公开入口/Future 和 stop/restart 策略仍待接入；类型与
注册表不提前加入占位接口，新增真实 VM 回归仅 AST。
后续 [配置 owner/修订号](investigations/2026-09-10-w08-eap-config.md) 已编码并
接入 runtime 开关：过期提交拒绝、独立控制 token、失败保留和退出释放。公开
configure/status/capabilities、启停 Future 与 stop/restart 策略仍待接入；运行未验证。

后续 [Enterprise 公共 API/Future](investigations/2026-09-10-w08-eap-public.md)
已接入 Candidate 的六个入口，类型/注册/文档同步：configure 返回预分配的配置
revision 收据，enable/disable/clear 使用原生 Future；超时后原生工作仍可完成，
未退休引用保持存活。正式契约见 [API reference](api/wifi-enterprise.md)。这替代
上文各批次“公开入口未接入”的状态；stop/restart、完整共存、认证/运行验证仍待完成。

后续 [Enterprise stop 事务](investigations/2026-09-10-w08-eap-stop.md) 已接入：
未连接且空闲的 Station 可原子交接精确 helper pins，先退休 SDK，再执行原有
stop/helper 后缀；失败保留同一 lifecycle/config control，退出可接管重试。
已连接 Station 仍须显式 disconnect。配置保留但 EAP 关闭；完整 restart 恢复
策略、共存与运行验收继续保留，不将显式重启后的手工 enable 当作完整重放。

```ts
interface WiFiEnterpriseAPI {
  capabilities(): WiFiEnterpriseCapabilities;
  configure(options: WiFiEnterpriseOptions): WiFiEnterpriseConfiguration;
  enable(options?: WiFiEnterpriseControlOptions): WiFiEnterpriseStatus;
  disable(options?: WiFiEnterpriseControlOptions): WiFiEnterpriseStatus;
  clear(options?: WiFiEnterpriseControlOptions): WiFiEnterpriseStatus;
  status(): WiFiEnterpriseStatus;
}
```

`WiFiEnterpriseOptions` 必须覆盖 build 支持的 EAP method、identity、anonymous
identity、username/password、CA cert、client cert/private key、private-key password、
phase2 method、FAST/PAC 等公开 ESP-IDF 字段。证书和 key 使用 ByteSource，native
复制到明确 owner；任何 status/error 均不回显秘密。

WAPI 已按实际 SDK 生命周期展开，唯一 v1 契约见 [wifi.wapi](api/wifi-wapi.md)：
`capabilities/status/enable/disable`。enable/disable 只接受 timeoutMs，使用既有
Station 凭据 owner，不另存密码或支持虚构的证书字段。未初始化时选择下一次策略；
已初始化时要求健康停止、零 owner，并沿用 Radio checkpoint/物理重建/重放来立即
应用，成功后恢复并启动保存的接口。普通 runtime 结束不另建 WAPI 清理 owner。

C3/S3/C5 的 SOC/build 支持 WAPI-PSK。`esp_wifi_internal_wapi_init/deinit` 仍只由
supplicant 生命周期调用；init/deinit 包装按编译开启的默认策略工作，关闭策略会
跳过原生 WAPI 创建。重复初始化、清理不确定与 identity 耗尽明确失败；不把已提交
策略等同于实际启用，原生清理错误通过 Radio/status 保留，不重复执行原生释放。

`minimumAuthMode:"wapi"` 仍是认证下限，不是精确 WAPI-only 选择器；能力字段明确
`exactAuthSelection:false`。固定 SDK 无独立的 WAPI station selector，不能用普通 WPA
认证结果证明 WAPI。公开控制本批已编码，生产编译及原生链接核对进行中；集中故障、
GC/OOM、callback/TX、三目标完整矩阵和对端 WAPI RF 验收仍未执行。

---

## 19. `wifi.wps`、`wifi.dpp` 与 `wifi.smartConfig`

### 19.1 WPS

当前已修复[原生凭据与设备信息边界](investigations/2026-09-11-w08-wps-credentials.md)：
factory format、短凭据旧尾部、配置错误处理及部分秘密副本释放。以下仍是目标
契约，公开 Session、精确原生完成/退休和 Radio owner 尚未接入，不代表可调用。
随后接入[原生结果 owner 与 timer 身份](investigations/2026-09-11-w08-wps-native.md)：
托管 PIN/凭据保留、终态和关闭撤销已有 native helper；record 最终退休/复用、
IPC/Radio/Future 与公开入口仍待完成，不把 SDK disable 返回当作完整退休证据。
随后接入[分步停止与 SDK 状态释放](investigations/2026-09-11-w08-wps-cleanup.md)：
逐项检查清理返回值、保留失败后缀，并在实际 callback 退出后释放 SDK heap；
结果 record 继续 held。scan/native 排空、record release/reuse 和公开 Session
尚未完成，运行测试后置，不把内部状态释放当成公开 close/reopen 已交付。
随后接入[retained worker 与结果释放](investigations/2026-09-11-w08-wps-worker.md)：
IPC 输入/输出随 worker 存活，timer/native 顺序后核对扫描状态和 callback
revision，原生结果可精确清零释放；未知 IPC 交接保持故障。Radio/Station
配置与事件交接、完整错误/安全边界、公开 Session/Future/watch 仍待接入。
随后接入[原始错误与接收端边界](investigations/2026-09-11-w08-wps-errors.md)：
初始化/扫描/TX 原始错误与阶段、失败撤销、配置/连接顺序和 RX peer/phase
检查已编码；五配置生产构建和静态产物核对通过。Radio/Station 恢复、公开
Session/Future/watch、完整安全和集中运行/RF 验收继续待完成。
随后接入[Radio 与 Station 配置恢复](investigations/2026-09-11-w08-wps-radio.md)：
精确 helper 持有、临时 RAM storage、配置/信道/存储恢复及 boot event fence
已编码并通过五配置生产构建/静态核对。尚无公开 caller；Station helper
连接/IP 状态交接、runtime/Session/Future/GC/watch 和集中运行/实机仍待完成。
随后接入[Station helper 与原生 Session](investigations/2026-09-11-w08-wps-session.md)：
helper 预留、DHCP/IP 排空、两阶段关闭、后台调度、PIN/凭据转移和 runtime
退出门槛已编码。五配置构建/静态产物核对通过；C5 roaming 使用保留原配置的
独立 4 MiB app 测试上下文。JS options/Session/Future/watch、AP registrar 契约
及集中运行/实机验收仍未完成，公开入口尚未注册。

当前公开 Station enrollee 已接入 `wifi.wps.capabilities/status/start` 与
`WiFiWpsSession.status/watch/receive/cancel/close`，唯一 v1 契约见
[WPS API](api/wifi-wps.md) 与源类型。`receive/close` 接收 `{timeoutMs?}` 并支持
Future；`cancel()` 仅请求关闭，不声称原生回调已退休。PIN 与凭据均在 JS 转换
成功后提交，观察不携带秘密。以上历史“未公开”由此记录替代。

固定 SDK 的 120 秒协商期限不受公开 Session/wait 预算扩展。AP registrar 是
SDK 已有、框架尚未完成的功能，继续保留任务；不得以 `apRegistrar: false`
解释为目标不支持而删去。完整安全/错误注入、运行与实机验收继续待完成。

随后核对并修正 [registrar 初始化与凭据边界](investigations/2026-09-11-w08-wps-registrar-init.md)。
SDK registrar 有独立 `ESP_WIFI_WPS_SOFTAP_REGISTRAR` 开关，SoftAP 本身不代表启用；
公开 capabilities 已按真实开关报告。新增三目标启用构建上下文，native owner、
多客户端/回调退休、Radio/Session/Future 和公开 registrar 继续待实现。

随后接入 [EAP 客户端所有权修正](investigations/2026-09-11-w08-wps-registrar-peers.md)：
每客户端独立协议/凭据，不从 method reset 关闭全局 registrar，按依赖顺序释放
客户端、authenticator 和 registrar。原生回调/定时器身份及排空、共享 Radio、
公开 AP Session 仍未实现；新增运行用例按阶段安排后置。

随后补齐 [registrar 协商定时器身份与启动失败](investigations/2026-09-11-w08-wps-registrar-timers.md)：
PBC/PIN walk-time 使用不复用数字 ticket，注册失败保留原状态并传递错误。
这不替代其余回调排空或原生结果 owner；公开 AP Session 仍待实现。

随后接入 [AP 原生结果 owner 与事件交付](investigations/2026-09-11-w08-wps-ap-result.md)：
精确数字事件身份、PIN copy/commit、首终态存储和零等待观察；保留结果独立于
SDK heap。绑定后的排空/release、Radio/worker 和公开 AP Session 仍待完成。

随后接入 [AP 关闭前缀与父对象保留](investigations/2026-09-11-w08-wps-ap-close.md)：
回调执行期间拒绝释放，逐步清理保留原始错误，AP 退出不再忽略关闭失败或在
释放配置后调用阻塞 disable。托管结果仍需 EAP/native 排空才能释放；该前缀
不代表原生退休、共享 Radio/worker 或公开 AP Session 已完成。

随后接入 [EAPOL timer 与 peer 存活保护](investigations/2026-09-11-w08-wps-eapol.md)：
数字身份查找真实客户端并取得 SAE 锁，receive/step/dispatch 持有 AP activity，
EAP abort 不再排队全局 disable。延迟 peer 删除、跨任务析构和完整排空/release
仍未完成，托管状态继续保留；公开 AP Session 仍待后续接入。

随后接入 [延迟客户端身份与 EAP 释放交接](investigations/2026-09-11-w08-wps-peer-retire.md)：
数字 peer ticket、删除义务、表内转移、Wi-Fi task 析构以及 child/producer
排空检查已编码，九配置生产构建通过。完整 native 输入排空、AP PIN 辅助
定时器、部分初始化失败保留、托管 release、Radio/worker/公开 Session 仍待完成；
新增运行用例仅 AST，RF 和集中验收继续后置。

随后接入 [初始化失败与关闭义务](investigations/2026-09-11-w08-wps-ap-init-cleanup.md)：
修改前 owner、部分 allocation 保留、原始/清理错误分离及 EAP 方法登记身份
已编码，九配置构建和静态产物核对通过。辅助 AP PIN set/random 当前没有
caller/ELF 入口，reenable 没有 timer
注册路径；新入口若使用这些函数须先补安全边界。完整 native 输入排空、托管
release、Radio/worker/公开 Session 与阶段运行/实机仍待完成。

随后接入 [AP 输入存活与 WPS IE 所有权](investigations/2026-09-11-w08-wps-ap-input.md)：
真实 STA 表查找/锁定先于 driver cookie 解引用，WPA key/Station owner 分流、
WPS IE 替换/失败释放已编码。WPA main 合并既有 Enterprise 修复，增加独立
no-Enterprise 构建上下文。native 无 cookie 输入排空、托管 release、Radio/
worker/公开 Session 与阶段运行/实机仍待完成。

随后接入 [AP 托管命令、原生退休与 worker](investigations/2026-09-11-w08-wps-ap-native.md)：
精确命令身份、旧 AP/Station SDK 命令拒绝、分步 SDK/result 释放和 retained
IPC/timer worker 已编码，C3 registrar 增量构建通过。Radio/AP lease、runtime
及公开 AP Session/Future/watch 尚未完成，运行/实机仍待集中验收；完整构建
矩阵按用户要求改为模块节点执行。

当前 [AP Radio/helper、Session 与公开接入](investigations/2026-09-11-w08-wps-ap-session.md)
已编码：保留现有 AP/STA helper，复用 WPS operation/fence；registrar 启用构建
提供 `wifi.wps.startAP/apStatus` 和 `WiFiWpsAPSession`，receive/close 支持 Future，
watch 为 metadata 有界观察。唯一 v1 正式字段见 [API](api/wifi-wps.md)。
此项替代上述历史“AP 公开尚未接入”；当前 RF 声明仅支持 2.4 GHz，5 GHz、
完整安全/失败恢复及集中运行/实机验收仍保留。host ROM generator 同步实际
registrar 开关，增量检查实际注册表与 ELF；不重复全量矩阵，也不执行动态 fixture。

### 19.2 DPP

当前增量已接通 Radio、共享 Station 预约、Session/runtime 及公开
`startEnrollee/status/watch/receive/close/cancel`：bootstrap/listen、URI/配置
copy/commit、TX buffer 回收与一份后续帧调度、关闭后缀及信道恢复。接收结果不
自动安装首项配置，receipt commit 后保留行供后续显式选择。认证选择预验证、
新 native identity/配置安装与独立 Network Introduction 原生路径已编码；
Station 连接交接、公开连接方法及阶段验收仍未完成。见[当前接口](api/wifi-dpp.md)
与[实施记录](investigations/2026-09-08-wifi-api-remaining.md)。本批仅生成生产 ROM
表、定向生产语法编译与 fixture AST，未构建新镜像或运行测试。下方逐批记录为
历史状态；只有尚未展开的连接契约继续 `contract-pending`，不注册空对象。

当前 [bootstrap 所有权与凭据事务](investigations/2026-09-11-w08-dpp-input.md)
已加入 build-local SDK 修补：结构化参数、数字排队身份、deinit 回收、异常清零、
严格密钥长度、完整安装比较和失败保留旧配置。保留真实 hex-DER 输入 ABI，
不依照错误的 raw-key 头文件说明直接传 scalar。后续已接入
[原生结果/异步任务/关闭](investigations/2026-09-11-w08-dpp-native.md)：有界结果
copy/commit、首终态、数字 ticket、精确取消/driver 静止检查、默认事件屏障与
内部原生命令。TX/ROC 生产完成边界、Radio/worker/Session/Future、凭据选择连接
和公开入口仍待完成，下方仍是 contract-pending 目标草案。仅做受影响配置增量
编译及静态检查，新增动态用例继续后置。

后续 [ROC 原生完成与回收](investigations/2026-09-11-w08-dpp-roc.md)已接入：
提交前数字身份、SDK 原生 done_cb、回调返回后的物理回收检查及取消后缀；
删除默认 ROC 事件消费者，RX 切换检查停止错误。一次 C3 DPP 增量构建通过，
新 fixture 仅 AST。TX 实际帧关联、原生执行队列压力恢复、Radio/worker/公开
接入仍待完成，不将此批次记作 DPP 已可用。

后续 [TX 原生捕获与数字调度](investigations/2026-09-11-w08-dpp-tx.md)已接入：
实际 EB 过滤、发布前有界记录、TX 与 dwell 分开完成、64-bit ticket、精确
取消/重用以及请求清零；默认 TX 消费者已移除，DPP TX/ROC 观察零等待。
原生队列/timer 隔离、全部 TX buffer 退休、Radio/worker/公开 Session/Future
和凭据选择连接仍未完成；动态验收按 Wi-Fi 阶段安排后置。

后续 [DPP worker 与共享 eloop 失败重唤醒](investigations/2026-09-11-w08-dpp-worker.md)
已编码：原生 begin/bootstrap/listen、结果 copy/commit、部分失败关闭、IPC
storage 保留和纯参数预验证。共享 eloop 的失败 wake 复用原列表/定时器重试，
不增加另一套任务队列。Radio/公开 Session/Future、原生 chm timer 隔离与完整
TX buffer 退休及凭据选择连接仍待完成，fixture 继续只作 AST 检查。

随后 [CHM 信道超时隔离](investigations/2026-09-11-w08-dpp-chm.md)已编码：两个
timer 复用、每 arm 数字身份/deadline、原生 reset 先撤销、到期队列失败由 DPP
poll 处理，以及原始错误/失败 handle 保留。C3/S3 与 C5 的原生布局分别核对，
私有结果可读共享 CHM 诊断。本批仅 C3 生产源定向语法编译、三目标 archive
静态核对和 fixture AST，无新 firmware build/ELF 或运行结果。全部 TX buffer
退休、Radio/helper/公开 Session/Future 与凭据选择连接继续待完成。

```ts
interface WiFiDppAPI {
  capabilities(): object;
  startEnrollee(options: WiFiDppOptions): WiFiDppSession;
}
```

DPP URI、configuration received、failure 等通过 Wi-Fi event broker；IDF 6.x 已移除
旧 DPP callback，因此实现不能重新制造旧 callback contract。

### 19.3 SmartConfig

当前已接入 [Candidate 凭据获取 Session](api/wifi-smartconfig.md)：
`wifi.smartConfig.start/status/capabilities`、`WiFiSmartConfigSession.status/receive/close`。
start 为同步保留对象并调度；receive/close 绑定原生 Future。只有转换成功才消费
保留凭据，失败可重读。显式 `autoConnect` 已接入捕获退出/事件屏障、原生 Station
连接、IPv4、ACK 及连接交接；默认仍只获取凭据。ACK 完成仅表示本地发送结束，
不证明手机收到。见[自动连接与清理边界](investigations/2026-09-11-w08-smartconfig-connect.md)。
ESPTouch v2 二进制 `customDataBytes` 也已通过同步事件捕获及同一凭据 owner 接入，
见[长度与存活期证据](investigations/2026-09-11-w08-smartconfig-custom.md)。专属
`Session.watch()` 已接入有界 metadata EventQueue，终态等待对应 Future 结束，
队列满不影响控制完成；见[观察队列记录](investigations/2026-09-11-w08-smartconfig-watch.md)。
SDK [解码堆副本清理](investigations/2026-09-11-w08-smartconfig-heap.md)已接入专属
secure free；[两处 NULL 写入与 v2 失败释放指针](investigations/2026-09-11-w08-smartconfig-oom.md)
已修复。[原生 OOM 交付](investigations/2026-09-11-w08-smartconfig-oom-status.md)
已串联 allocator、owner、decoder 与凭据交付门槛。栈副本、完整失败恢复及
运行验收仍须处理，未展开部分不注册占位 API。下方记录保留历史
阶段，当前实现仍为 Candidate，运行/RF 验收未执行。

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

2026-09-10 已加入[SDK ACK 所有权与凭据日志修复](investigations/2026-09-10-w08-smartconfig-native.md)：
ACK 不再共用可复活旧任务的布尔开关，停止保留原 worker 直到 socket/参数退休；
完成原生状态先于零等待观察，默认 SDK handler 不复制或打印凭据。
这是 Session 的原生前置工作，尚未注册 SmartConfig API。解码器停止、SC_EVENT
排空、Radio lease、AES/custom data 和公开 Session 仍需接入；阶段运行尚未执行。

2026-09-11 已加入[原生事件与凭据 owner](investigations/2026-09-11-w08-smartconfig-events.md)：
精确 identity/generation、第一份凭据保留、copy/commit、关闭清零和 SC_EVENT
原生捕获已编码；托管凭据不投递默认观察队列。此记录的 release 仍需外层
证明 decoder/timer/native/ACK 退休，不能仅用记录 token 或 SDK stop 返回代替。
Radio/coordinator、AES/custom data、专属观察与公开 Session 仍待接入。

后续 [定时器与原生 coordinator](investigations/2026-09-11-w08-smartconfig-decoder.md)
已编码并通过五种构建：九个 timer 的精确身份/失败 handle 保留、原生启停与
队列检查、保留凭据的 finish_capture、精确 ACK reservation/receipt、完整关闭
后缀及 key 清零。当前只供内部 worker 调用，调用者仍须提供 STARTED Radio
排他授权和串行 lifetime；尚无正式 API。Radio/helper/runtime、JS Session/
Future/专属观察、custom data 和自动连接继续待接入，动态竞争/RF 验证 not-run。

后续 [Radio/runtime Session](investigations/2026-09-11-w08-smartconfig-session.md)
已连接 operation/三 helper leases、home-channel 恢复、有界 handle/单活动 Session、
后台调度、凭据转移及 core 销毁门槛。ACK 私有元数据可跨凭据 commit 保留。
这是内部 Session/runtime 接入，尚未注册 JS API；custom data、SDK 内部秘密
副本清零、显式自动连接及 JS/Future/观察仍待完成，运行验收继续后置。

---

## 20. `wifi.nan`

固定 SDK 的 C3/S3/C5 三目标中，仅 C5 有 `SOC_WIFI_NAN_SUPPORT` 和非空 SD/common
driver 对象；NAN-Sync 遵循这个 target/build gate，不能仅凭公共头文件推断 C3/S3 支持。

2026-09-12 原生接入进度：已通过 build-local `nan_app.c` 接入有界 native observer，
注册身份不复用，注销先停止接纳，再等待已进入回调结束。NDP 请求/拒绝/终止控制
通知不依赖观察事件分配；普通事件先进入 native observer，再尝试非阻塞 SDK 事件
发布。SDK 同步启动/停止改为有界等待，超时保留 context，迟到 STOP 后只补清理
后缀；另处理初始化 OOM、重复初始化、NDP response 二次解锁、短 SSI 读取及本文件
四处密钥 debug dump。源码检查和待执行 fixture 见剩余清单本批记录。
同日生命周期增量已接入 Radio 的独占 NAN lease/operation、NAN START/STOP 事件
及屏障、逐步检查的 netif 创建/退休和后台原生 Session。Session/control 使用共享
`wifi.nan` 预算，超时或关闭保留 registry/worker/observer 所需引用；runtime 销毁
等待原生清理。停止已受理后只重试屏障后缀，SDK handler 注销失败保留标记和原错误；
最后恢复进入前的停机 mode/storage，再释放原 lease。生产定向语法检查与待执行
SDK/Session/Radio fixture 见剩余清单，不作为动态验收结果。
同日公开生命周期已接通 `wifi.nan.capabilities/status/open` 与
`WiFiNanSession.status/ready/close/cancel`，字段、单位、owner、错误和清理契约见
[Wi-Fi NAN API](api/wifi-nan.md)。ready/close 注册原生 Future；单次等待超时/取消
与 Session 启动截止时间分开，已开始的 close 保留原生清理。JS 构造完成后才
激活 worker，最后外部引用释放触发关闭，全局状态可观察失去 JS 句柄后的清理。
NAN module、class 和 Future 同用 Wi-Fi + NAN_SYNC gate，不依赖 IPv4；
capabilities 和 diagnostics 已同步。channel 接受 SDK uint8 字段的 1..255，实际
NAN 信道与监管限制仍由目标 driver 校验，不以 14 硬编码为目标能力。
同日服务前置已接入 C5 SD 实际 buffer 分配/提交/recycler 返回的精确账本，
完整 native SD TX 与应用回调冻结，关闭保留 Radio owner 到真实 buffer 退休。
发布/订阅/取消的阻塞 driver 调用已移出 SDK 数据锁，checked 内部入口保留原错误，
取消成功保留 service ID 冻结。C5 目标对象和生产对象局部链接已核对包装调用，
后续公开发布/订阅、Service 句柄、早到事件与 host ID 发布交接、follow-up send
及其独立 identity/Future/回调和 buffer 退休已接通。NDP 的五类管理帧也已进入同一
32 槽账本，完整 NAF TX 与 NDP 应用回调纳入全局关闭屏障。SDK 请求超时/终止提交
保留 host NDL，确认事件分配改为可丢观察路径。NDP 独立操作身份、早到 host/native
绑定及 Radio 串行 request/response/end 内部入口已编码，14 个实际分配/删除调用
与 6 个精确服务查询已通过 C5 对象/局部链接核对。
管理帧 RX、空数据 TX 完整回调、建立/空闲定时器的数值
身份隔离及失败清理已接入共享关闭流程，并通过四单元编译和九对象局部链接；
后续已接入普通/空数据入队前跟踪、精确回调授权和单连接 buffer/timer/host 记录
退休，内部 release 复用原生 ioctl 串行入口。数据占用上限 24/32，管理帧保留
8 槽；组播数据属于 Session。8 位协议 ID 回绕避开仍有引用的 ID；未证明原生
退休的记录仍保留至父 Session STOP。四生产单元编译、十三对象局部链接通过，
新增用例仅 AST。后续已接通 Service 的 requestDataPath/receiveDataPath 和独立
DataPath 的 ready/respond/status/close/cancel，复用 Session worker/Future；
未显式接受的入站请求默认拒绝，服务或父 Session 关闭接管连接清理。
公开批次通过 C5 十生产单元编译、十九对象局部链接及 manifest 一致性检查；
完整运行验收仍后置。后续已接通 NCS-SK-128 的 1..4 份服务凭据、Session 组管理
保护及 publish/subscribe/send 的 Vendor 属性；框架与 SDK host 复制材料关闭后清零，
未匹配 PMKID、缺失 M1、MAC/加密派生及协商组密钥错误不交付成功结果。
安全模块构建副本去除密钥二进制日志、封闭单帧暂存解析状态，使用原 worker/Radio
及资源预算。两个 C5 分支共十二单元编译，后续 SDK 修订仅刷新三个受影响单元；
十七实际对象局部链接通过，fixture 仅 AST。后续 USD 已接入 sync+USD C5 的共享
传输、Radio/Session/Service 和公开发现参数，十六生产单元及局部链接通过；
后续 USD-only 注册已完成并通过 C3 定向编译。固定 SDK 已自带 pairing/PASN；
先前“依赖额外组件”的判断已纠正。配对原生初始化、数字定时身份与父 Radio
关闭已接入构建副本；公开确认流程、精确服务/凭据绑定及完整发送退休仍待接通，
后续显式 PIN 配对已通过 `preparePairing/confirm` 和 Pairing Future 接通；
后续缓存元数据读取和 credentialId 重验证已接通；PIN bootstrap 请求/接收及显式确认
已接入同一 Pairing 句柄。SDK 回调无 cookie，保留同服务八个对端组合的有界隔离，
COMEBACK 不伪造支持；未经确认不认证，发送未回收不复用。
生产编译/局部链接通过，新增动态测试未执行。仅已实现契约进入正式类型；未展开
草案继续 contract-pending，不把局部接通算作完整 NAN 验收。native notice 不等于 TX
buffer 已回收，也不证明旧 service/datapath ID 可以立即复用。

```ts
interface WiFiNanAPI {
  capabilities(): WiFiNanCapabilities;
  status(): WiFiNanGlobalStatus;
  open(options?: WiFiNanOpenOptions): WiFiNanSession;
}

interface WiFiNanSession {
  publish(options: WiFiNanPublishOptions): WiFiNanService;
  subscribe(options: WiFiNanSubscribeOptions): WiFiNanService;
  status(): WiFiNanStatus;
  ready(options?: WiFiNanWaitOptions): WiFiNanStatus;
  close(options?: WiFiNanWaitOptions): void;
  cancel(): void;
}

interface WiFiNanService {
  send(options: WiFiNanSendOptions): WiFiNanSendResult;
  requestDataPath(options: WiFiNanDataPathOptions): WiFiNanDataPath;
  receiveDataPath(options?: WiFiNanWaitOptions): WiFiNanDataPath | null;
  // 服务状态、关闭和 events 的完整契约见正式 .d.ts 与 API 文档。
}

interface WiFiNanDataPath {
  status(): WiFiNanDataPathStatus;
  ready(options?: WiFiNanWaitOptions): WiFiNanDataPathStatus;
  respond(options: WiFiNanDataPathResponseOptions): WiFiNanDataPathStatus;
  close(options?: WiFiNanWaitOptions): void;
  cancel(): void;
}
```

覆盖 NAN start/stop、Publish/Subscribe、service match、follow-up message、data path
request/response/end 和 peer/channel metadata。NAN 与 STA/AP/Monitor/CSI 的 mode/channel
兼容性由 shared Radio capabilities 决定，不由应用猜测。

---

## 21. `wifi.mesh`

核心公开接口已注册到唯一 v1，正式参数、结果、事件、超时和 ownership 见
[Mesh API 文档](api/wifi-mesh.md)及 `types/esp32qjs-c-api.d.ts`。Session 不可直接
构造，使用 `wifi.mesh.open(options)`；SDK/Radio、worker/Future 与 runtime 清理
已接通，尚未通过集中运行或 RF 验收。

当前公开行为包括：

- capabilities/open、Session status/ready/close/cancel/recover；
- send/receive/watch，接收完整转换后才消费，原生发送存储保留到 SDK 返回；
- routingTable/groups/addGroups/removeGroups、setToDSState；
- connect/disconnect/flushUpstream，命令成功不等于关联、IP 或对端收到；
- configuration 与 router/ID/type、自组织/固定根/冲突、关联期限/根修复、IE 加密；
- waiveRoot/switchChannel、device/network duty/signaling、subnet/group membership/
  upstream capacity、powerStatus/tsfTime。配置秘密默认隐藏，SDK 两步修改保留前缀诊断。
- setParent、scan/receiveScan/flushScan 已接入同一 Session worker/Radio owner，
  正式类型、manifest、API 文档和 SDK 清单已同步。原生扫描未确认结束时不复用
  输入或 scan identity；读取转换失败保留原生结果，成功 flush/关闭后释放。

`wifi.mesh` 的最终范围仍包括当前 IDF public ESP-WIFI-MESH operational surface：
parent/layer、routing table、root/fixed-root、group、vote/root switch、收发、ToDS
queue、IE crypto/config 及电源策略。当前 open 配置、动态控制、手动 parent 和扫描
已经分别接通；83 个 public SDK 函数均在 `idf-wifi-api-map.json` 有具体入口或框架
生命周期映射。SDK 明确未实现的指定 root/UPLINK duty 选项不注册占位参数，原生
stop 由固定 SDK deinit 内部调用并纳入关闭屏障。API 实现已收齐，所有映射仍保持
in-progress，实际运行/GC/竞争/故障/RF 验收后才能确认完整 Mesh 正确性。
当前测试入口见[剩余门槛](investigations/2026-09-08-wifi-api-remaining.md#进入统一测试前的剩余门槛)。

### 21.1 原生层已落实的约束

- 固定 SDK 的 C3/C5/S3 均有 Mesh 库；C5 与 C3 的 init/deinit 指令一致，stop 的
  已观察差异为 parent 结构清零长度，S3 的 deinit 同样按 initialized 状态委托 stop。
  这些是 SDK 静态核对，不能代替三目标编译或生命周期实测。
- Mesh 必须独占 Radio，调用者负责已启动 APSTA、RAM storage、禁用默认 DHCP
  的 Mesh netif，以及关闭后的物理 STOP、netif 退休和原配置恢复；原生层不绕过
  Radio 自建 Wi-Fi driver。C5 仍按 Mesh 当前 SDK 的 0..14 信道契约：0 表示查找，
  不从芯片双频能力推导 Mesh 5 GHz 支持。
- 原生配置限制：tree 最大 25 层，chain 最大 1000 层；容量 1..1000；SDK RX
  queue 16..128；send block budget 1..60000 ms。SSID 为 1..32 bytes，AP 总连接
  数最多 10；IE 加密使用 SDK 默认 crypto 函数及显式 8..64 byte ASCII key，关闭
  加密必须明确选择。未开放自定义原生函数指针。临时凭据在 setter 返回后清零，
  status/event 不复制密码、SSID 或 key；SDK 自身凭据由 SDK 生命周期持有。
- 可靠发送不能靠 NONBLOCK 消除等待。原生 worker 必须保留稳定输入直到 send
  返回，Future timeout/cancel 只结束公开等待。close 封闭新操作，busy 时保留
  owner；init 的未发布失败及 deinit 的未知清理失败不重试创建/释放，要求设备重启。
- 原生 self/ToDS 各一个 `MESH_MTU` 接收槽，worker 只作零等待 SDK receive；
  copy 不消费，转换成功后按 owner/sequence commit。序号在破坏性读取前预约，
  迟到事件或耗尽不能把已读取消息变成可复用旧记录。ToDS 读取核对实际 root。
- Mesh 原生事件先更新控制状态，再进入有界观察 ring 和 SDK default-loop post；
  ring/默认队列满不丢失断连、STOP、layer、voting、ToDS 控制状态。复制固定非秘密
  字段，校验 payload 长度；观察事件转换失败可重试。Native query 期间发生事件会
  使诊断快照失效，不用旧 parent/type 覆盖较新的断连状态。
- 共享预算仅覆盖本层控制存储与 3000 bytes 接收池；原生 Mesh workers、队列、
  路由表和 boot mutex 仍是 SDK 账本，不宣称已经计入框架额度或完成整机峰值验收。

Radio 内部交接已编码：健康零 owner STOP 或冷初始化准入，整个生命周期保留
中央排他 token。原配置复用共享 checkpoint/replay；Mesh 临时配置保持 RAM，C5
先用空 Station 切换至 2.4 GHz，再启动本 Mesh 的 AP，避免提前广播旧 AP 凭据。
close 在原生退休、物理 STOP 和 netif detach/fence 后恢复前驱，最终回到 STOP；
前驱包含 AP 时必须显式 `allowApRestart`，因为恢复验收会短暂启动该 AP。已完成
原生 deinit 不因后续关闭超时而重复；未知原生 init/deinit 失败要求设备重启。
原配置重放/START 失败保留快照及生命周期，内部显式 recover 才允许新一轮物理
恢复；不会再次创建已经退休的 Mesh。未满足完整 checkpoint 前置的旧状态仍拒绝。

两个 netif 禁用默认 DHCP，不使用 SDK 的 assert factory；部分 attach/handler 失败
按实际 netif 清理，已完成的另一端不重复创建。root DHCP 从丢弃观察事件前的原生
控制快照决定；角色改变使采样失效，非 root/断连停止 DHCP。IP readiness 不表示
ToDS/Internet 可达。原生收发释放 Radio mutex，busy pin 保留 binding；seal 只封闭
准入，不在发送返回前执行 deinit。状态和 copy/commit 不阻塞获取 Radio 锁。

公开 Session/Future、routing/group/vote/PS/ToDS 与配置控制已接通。剩余手动 parent、
扫描处理及最后的 SDK 操作覆盖继续实施。生产故障/竞争用例仍只解析 Python AST，
集中运行验证后置；当前局部证据见 build/w08-mesh-controls-evidence.json。

---

## 22. `wifi.diagnostics`

**当前实现**：`snapshot()` 已接入唯一 v1 的正式类型、注册和
[API 文档](api/wifi.md)，复用全局 memory/EventQueue、Wi-Fi/Radio、Vendor IE、
FTM/TWT、Enterprise/配网状态，并增加 Monitor/CSI 全 generation 的
只读账本转换。观察是跨模块顺序采样；字段单位、feature-null、统计口径和退休时
暂不可读状态已展开。`dumpDriverStats()` 已接通稳定 Radio 下的 SDK 日志转储，
参数预验证、原始错误、mutex 与副作用在正式 API 文档展开。`idfApiCoverage()` 已
接通从清单/映射/manifest 生成的 flash 常量统计，明确分开参考配置和当前 capabilities，
保留未展开条件头文件缺口；构建检查拒绝过期生成物及 SDK 输入。
`resetFrameworkCounters()` 已接入正式契约：清理注册队列、ingress、连接、可读
CSI/Monitor 和全局 memory manager 的观察历史；峰值从当前占用开始，保留
owner/原生操作/身份/过滤节奏/故障。逐 provider 重置不承诺跨模块原子性，跳过的
generation 与时间范围由 `snapshot.counterReset` 明示。EventQueue/ingress
high-water、饱和聚合及框架实际连接提交/reconnect 计数已接通。
动态验证仍 not-run；完整整机归属/峰值与目标运行证据继续缺失，不能据已有入口
将本节或 W-09 标为通过。
当前 `memory.wireless` 已接入共享配额、角色小计、退休池与 boot 峰值；覆盖边界见
[system memory reference](api/sys.md)。

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

当前已接通 framework Build Context loader 与 CMake 的共享校验入口：无线启用时
必须显式给出 internal/PSRAM/control reserve 三个十进制配额，拒绝重复、越界、
零控制预留及控制额度占满内部配额；PSRAM 额度不超过 manifest 物理容量，mode/bytes、
`SPIRAM` 与实际 target 必须相符。实际 sdkconfig 与固定输入配额不符直接失败，
不接受 Kconfig 静默裁剪或旧 cache 值。Future worker 栈先做配置下界检查，目标 C
再按真实 StaticTask_t/StackType_t 大小检查栈与 TCB 能放入 data 配额。动态 pool、
queue、copy 的乘法/对齐及每次联合准入仍由生产 allocator 和各模块 cap 检查。
这不要求所有模块最大池同时放入额度，也不保证 allocator/SDK/JS 的整机峰值；
其他组件内存与预热后的实机测量继续单列。low-memory/standard/capture 是容量
选择方向，不添加 Board policy 到 firmware，也不把 CI 配额当作硬件合格值。

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
| SoftAP | `esp_wifi_ap_get_sta_list`, `esp_wifi_deauth_sta` | `wifi.apClients()`, `wifi.deauthClient()` | 可选 includeIp 通过本 AP netif 的 DHCP 查询，实机验收待执行 |
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

下面的任务顺序替换原方案“先写全量正式声明，再逐个实现”的顺序。W-00 已实现代表配置的符号/字段清单和检查工具，W-01 的现有 feature 范围内部生命周期收尾已实现，W-02 已实现基础生命周期、扫描扩展、独占 AP、常用控制、能力发现和 watch 的首批代码；参数/高级事件与其他新模块的剩余范围以当前清单为准。

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

**进度**：现有 feature 范围的原生收尾已实现：跨步骤生命周期 token 封闭 acquire/mutation 准入，APPLICATION 与 STA helper/feature lease 分离，shutdown 注销 Radio listener 并等待 callback 退出；restart 在同一独占范围执行 shutdown/init/start，失败保留 token/cleanup suffix。W-02 已接入 start(mode/storage)、configure 和无参数 stop；start 的冷启动默认值为 Station/RAM。静态 Radio mutex 是 boot-owned 有界存储，stop/runtime retirement 按契约保留已初始化 driver；完整 deinit 由内部 shutdown/restart 负责，不再把这些有意保留项当成内存泄漏。公开 restart、AP owner 的产品操作、watch 和后续配置恢复仍属于对应 API 集成；长期硬件验收继续后置。见[实施记录](investigations/2026-09-08-w01-radio-foundation.md)。

**代码**：现有 `wifi_radio` 与相关 internal headers；第一阶段建立的 identity/helper 复用。

**工作**：实现第 25 章；去除对一次成功启动结果的永久依赖；建立有界 lease registry、mode/channel 兼容表、mutation lane、可重试 cleanup stage；新增同信道多 owner；记录 driver 自发信道变化。

**验收用例**：init/start/stop/restart 全部阶段错误注入；A/B 同信道共享，A 关闭不影响 B；不同信道冲突无副作用；STA/AP 信道变化更新实际状态；有子 owner 的 stop/restart 被拒绝；旧 token 无效；恢复前值失败时 lane 不被新操作复用。

本批不开放跨 feature 的 mode/channel setter；它们仍需各自配置/字段契约与 W-07 验收，不能从内部 Gate 推导已支持。

### W-02：基础 Wi-Fi、扫描与事件 broker

[本批 SoftAP 实施与验证记录](investigations/2026-09-08-w02-softap.md)。

**实际状态**：in-progress。当前逐项缺口见[剩余工作清单](investigations/2026-09-08-wifi-api-remaining.md)。已增加独占冷启动 `startAP(options)` / `stopAP()`，实际字段子集与安全边界见第 7.4 节；已追加 `apClients()` 和 `getMac("station" | "access-point")`，实现与待测边界见[查询 API 记录](investigations/2026-09-08-w02-client-queries.md)。现已追加 [includeIp 的严格输入、DHCP 查询与 GC 安全转换](investigations/2026-09-08-w02-client-ip.md)，测试源码已补充、运行待执行。configure 的 APSTA 启动已编码；保留 Station 资源的 stopAP 已编码并待集中验收；匹配已存配置的共享 AP 重开已编码并保留 Station 资源；显式 allowDisconnect 的 startAP 已接入 STOP/配置/START，保持 Station 连接的新配置激活已接入固定 C3/S3/C5 SDK，配置与 PMF 在原生 AP 分配前安装并读回，动态验收后置；deauthClient 已将 MAC→AID 查询和定向断开放进单个 Wi-Fi task 命令，使用固定 SDK 的任务内直接执行边界，生产实现及待验收范围见客户端查询记录的最新增量。已追加 `wifi.start({mode?,storage?}): WiFiStatus`，缺省恢复已配置接口，冷启动默认 Station/RAM；不建立 Station 连接。运行中的不同设置在 mutation 前拒绝，同设置保留 owner；其他 feature 启动的 Station 在同锁内取得 Application anchor。`wifi.stop({timeoutMs?}): WiFiStatus` 已接入默认 1000 ms 的共享协作等待预算，不接受 force，只接纳已退出连接/操作/其他 feature 的 Radio。scan 已追加 ssid/bssid 过滤、homeChannelDwellMs、coexistenceBackgroundScan 与 maxRecords，原生过滤存储隔离和待测项见[扫描参数增量](investigations/2026-09-08-w02-scan-options.md)。现有 connect/disconnect 和 setter 保留；configure/APSTA 及当前字段已进入正式类型/注册表，stop timeout 与 start options 已编码、待集中验收；完整高级认证仍待实现。watch 已接入有界 broker、常用 typed snapshot 与元数据/秘密边界，见[事件订阅记录](investigations/2026-09-08-w02-watch.md)。

新增 `setMac`、`acquireWakeLock`、`capabilities` 的实际实现与待验收边界见[本批记录](investigations/2026-09-08-w02-mac-wake-capabilities.md)。完整能力字段、事件与配置仍在后续范围。

连接返回已改为独立 `WiFiConnectResult`，身份来自本次关联事件并在 IP 就绪时复制到完成队列；RSSI/协商 PHY 是有身份检查的交付时读数。状态、清理 suffix 与待测竞争边界见[连接结果记录](investigations/2026-09-08-w02-connect-result.md)。此项已编码不等于集中竞争验收通过。

**代码**：现有 `wifi.c`/`wifi_future.c`、Wi-Fi event converter、原生 operation 状态。

**工作**：保持顶层 connect/disconnect/scan/status；新增 start/stop/configure、SoftAP/APSTA、MAC/country/channel、watch；补 scan bitmap/coexistence 参数；实现 event descriptor 和秘密交付边界。

**验收用例**：连接/扫描结束时 watch 已满；结果 buffer 在成功/取消/错误/转换失败后释放；APSTA 冲突无断网副作用；不能通过 event mask 关闭内部完成事件；未知/敏感/变长 raw event 不越界和不泄密。

### W-03：公共 RX metadata、parser 与 Monitor

**当前进度**：内部无分配 MAC header parser 已编码，覆盖 Addr4/QoS/HT Control、
短输入、未知布局和地址角色；target adapter 已调用 parser，并补有界原生 filter，
共享 registry 的准入/dispatch retain/关闭及 driver 配置事务已编码。CSI 已接入共享
enable lease，每次申请有独立 identity；稳定 RX callback 已实现，但当前 enable-only
路径不会安装它。registry 精确 token 集合/需求并集与 RX 的 Radio 预留、driver 提交、
激活、关闭排空事务已编码；Monitor 原生帧池、无指针事件 token、队列拒收回收和
Frame/retained-ref 所有权已编码。EventQueue bridge、转换失败回收和 native context
最终释放已编码；Monitor 原生 capture 的 Radio 启停/关闭后缀与信道冲突停止已编码。
Session native owner、登记表、poller/reaper 与 runtime teardown 接入已编码；
原子参数捕获与 target channel 静态校验已编码；单帧 Frame/Session JS 绑定、原始
字节 Source 与公开生命周期已接入；停止后的 configure 完整替换和 Radio lease 交接
已接入，start/stop/configure 返回状态快照；Batch receive/info/bytes/close 已接入，
统一 wire、CSI 单帧 Source 和 Monitor Host PCAPNG/时钟锚点已编码；[能力与队列/owner/命名丢弃诊断](investigations/2026-09-10-w03-monitor-diagnostics.md)已接入；[HE-layout HT-SIG 解码](investigations/2026-09-10-w03-monitor-ht-signal.md)已接入 JS/wire；[共享 VHT SIG 解码](investigations/2026-09-10-w06-vht-signal.md)修正 CSI 宽度与 MU/SU 并接入 Monitor；[共享 HE SIG 与统一 PHY snapshot](investigations/2026-09-10-w06-he-signal.md)已接入，完整 PHY/time、共享预算与运行验收仍待完成。
legacy/HE callback 读取跨度已有内部契约；不能将
helper 编译算作 RF/GC/共存验收。当前公开单帧接口见
[接入记录](investigations/2026-09-09-w03-monitor-public.md)。见[MAC parser](investigations/2026-09-08-w03-rx-header.md)
和[target span/原生过滤](investigations/2026-09-08-w03-rx-target.md)、
[registry 分发保留](investigations/2026-09-08-w03-rx-registry.md)、
[driver 事务/共享 enable 与 CSI 清理](investigations/2026-09-08-w03-promiscuous-driver.md)、
[RX Radio 事务与未运行竞争测试](investigations/2026-09-08-w03-rx-radio.md)、
[Monitor 资源所有权与待运行测试](investigations/2026-09-08-w03-monitor-resources.md)、
[EventQueue bridge 与上下文寿命](investigations/2026-09-09-w03-monitor-queue.md)、
[原生 capture 与 Radio 生命周期](investigations/2026-09-09-w03-monitor-capture.md)、
[Session owner/reaper 与 runtime 边界](investigations/2026-09-09-w03-monitor-session.md)、
[参数捕获与待运行 VM 测试](investigations/2026-09-09-w03-monitor-options.md)。

**代码**：`wifi_common`、`wifi_monitor`、promiscuous broker；拟新增文件见第 29 节。

**工作**：无分配 bounded 802.11 parser；按 callback type 验证可读数据；normalized WiFiRxInfo；bounded filters；Frame/Batch/View；filter union/last-owner cleanup；native wire Source。

**验收用例**：0～64 字节短输入、可变 MAC header、Addr4/QoS/HT Control、metadata-only callback、unknown subtype、snapLength/requireComplete、pool 与 queue 饱和、两 subscriber 关闭顺序、retained view 后 close。

parser 安全边界用 provenReadableLength，不用猜测的 driverLength。

### W-04：Raw TX 工作包

本文其他位置的 W-04 是下列 W-04A/W-04B 的统称。按用户最新安排先完成全部 Wi-Fi API，集中测试时先验证 one-shot 回调隔离，再验证队列与周期发送。

### W-04A：Raw TX one-shot 与可靠 completion

**当前状态**：已编码 [Raw TX 输入校验与 callback 快照](investigations/2026-09-09-w04-raw-tx-input.md)及 [单 in-flight 原生 broker](investigations/2026-09-09-w04-raw-tx-broker.md)，包含精确 token、提交/完成分离、超时保留和注销后 deinit 隔离。后续已接入 [Radio 发送/退休与通道保留](investigations/2026-09-09-w04-raw-tx-radio.md)和正常 shutdown 的 broker 清理。后续已接入 [ByteSource/Future 与公开 one-shot](investigations/2026-09-09-w04-raw-tx-public.md)，包含固定原生清理槽和 runtime teardown 等待；C5 已链接实际发送路径。显式故障恢复仍待实现，生产竞争/GC/OOM 用例已编码但未执行；公开 API 保持 Candidate，不把编译当作可靠 completion 验收。

**工作**：strict validator、单 in-flight broker、无指针 tx snapshot、输入 ownership、timeout quarantine、driverAccepted/driverCompleted 结果区分。

**验收用例**：无效 frame 不触发 driver；connected sequence 限制；callback 中地址指针返回后失效；timeout 后旧 completion 与新请求交错；callback 缺失/注销失败；不能为了恢复而重启别人的 STA/ESP-NOW。

必须取得双板真实接收证据；本机 driver success 不等于对端应用收到。

### W-04B：Raw TX queue/batch/periodic

**当前状态**：已编码 [原生 FIFO、整批准入与有界 flush 账本](investigations/2026-09-09-w04-raw-tx-queue.md)，包含已开始 batch 保护、逐 payload 移交、关闭保留 active 和精确 ticket/watch 身份。已将 [共享 FIFO 发送仲裁](investigations/2026-09-09-w04-raw-tx-lane.md) 接入 one-shot 与原生退休槽；已连接 [原生 Session/worker/Radio 与关闭引用](investigations/2026-09-09-w04-raw-tx-session.md)，grant 保留至 completion 和必要清理完成。已补 [逐次结果登记/注销](investigations/2026-09-09-w04-raw-tx-results.md)，结果与 queue admission 同时提交，等待方注销不影响 native ownership。已接入 [公开 Session/queue/flush/send Future](investigations/2026-09-09-w04-raw-tx-session-public.md)，open、send、flush、close 使用真实 Future driver，enqueue/enqueueBatch 使用原子队列准入；关闭后缓存状态并释放原生 caller owner，全局状态覆盖未返回 JS 对象的失败打开。Station rate lease 已接入 pre-start/恢复与 owner/grant 保留，AP 临时 rate、显式 Raw TX 故障恢复与集中验收仍待完成；普通 queue 由 runtime poller 触发原生 worker，周期任务另有自治 timer。已补 [原生周期调度账本](investigations/2026-09-09-w04-raw-tx-periodic-ledger.md)，覆盖绝对 deadline/迟到合并、busy skip/stop、精确 ticket 和未知完成保留；已接入 [原生周期 owner/Session/自治 timer](investigations/2026-09-09-w04-raw-tx-periodic-job.md)，包含 child close fence、单个未完成包、timer callback 有界 worker 准入与 stop/delete 后缀；已接入 [公开周期 API/Future 与全局诊断](investigations/2026-09-09-w04-raw-tx-periodic-public.md)，正式类型和 callable 已同步；实际 timer/GC/OOM/竞争/RF 验收仍 not-run。

**实施依赖**：先完成 W-04A 的 completion 隔离实现；按用户最新安排，API 实现期间继续 B 的代码工作，集中阶段测试先验证 A 的竞争场景再验证 B。未通过前保持待验证状态，不声明可靠 completion 或稳定等级已通过。

**工作**：native FIFO、admission fence、batch atomic admission、flush 终态账本、受限 drop-oldest-batch、native periodic worker、busy skip/stop、rate lease。

**验收用例**：部分 batch 空间不足；尚未发出的 batch 被丢弃；flush 期间并发 enqueue；定时器触发时前包未结束；session close 自动停止其 periodic；恢复 rate 失败；callback/结果观察队列饱和。

### W-05：CSI correlated packet

**前置**：Radio/broker/parser 已稳定。

当前增量：CSI 的 `source` 对象、`buffering.poolCapacity/queueCapacity` 与
`maxCsiBytes` 唯一 v1 契约已接通；实际 pool 按请求容量在 Radio acquisition 前
分配，batch 不超过当前 queue。后续已将数据池接入跨代 registry，所有代共享原 build 的总槽预算；Radio 清理完成后可在剩余预算内重开。
[固定 SDK packet span 核对](investigations/2026-09-11-w05-csi-packet-span.md)
确认 published payload 固定为 hdr+24，不能直接据此拼接可变头/body。当前已接通四个
原生 RX 来源的完成复制 receipt、同次 callback identity、none/header/full、required/
requireComplete、同槽 packet 存储、Frame/Batch/View/Source、统计和 packet wire。
生产语法、ROM 和 pinned archive 核对不代表运行/RF 验收；仍为 Candidate。BSSID/type/subtype
过滤、公共接收 metadata 与 slot/sequence identity 耗尽已接通；未知 PHY 字段仍按可用性
返回 null。CSI 跨代 pool 已接入，保留 owner 不影响预算内新池重开。完整 PHY/time 验收、
共享预算的全部分配入口覆盖继续待完成。

**工作**：保留 legacy/HE adapter；建立各 target capture contract；同 callback 同 observation 复制 CSI 与可证明 packet；支持 none/header/full、required/requireComplete；更新 Frame/Batch/Source 方法和统计。

**验收用例**：hdr/payload 缺失、不可信布局、短 header、packet 截断而 CSI 不截断、CSI oversize、未知 FCS/加密表示、同 channel Monitor+CSI、旧 pool 被 view 持有时关闭和重开。

C3/S3 legacy、C5 HE/相关 band 只按实际支持的组合测试。不支持/尚未验证的字段不能因为结构成员存在就标 true。

### W-06：统一 wire、时间戳与 Host 工具

当前已编码[共用 envelope 基础](investigations/2026-09-09-w06-rx-wire-layout.md)和[256-byte metadata encoder](investigations/2026-09-09-w06-rx-wire-metadata.md)，共用既有 CSI layout 类型，并补了延后执行的生产实现测试。已补 [Monitor 快照 adapter](investigations/2026-09-09-w06-monitor-wire-snapshot.md)，公开 info 与 wire 共用 PHY 映射；已接入 [CSI-only snapshot、Batch Source 与 Host parser](investigations/2026-09-09-w06-csi-wire.md)，设备/Host/fixture 同步切换 32/40/256 唯一 v1；CSI 使用 callback-time 和原 capture Radio generation。已接入 [Monitor Batch wire Source 与 Host JSONL](investigations/2026-09-09-w06-monitor-source.md)，wireSource 为 true；已接入 [CSI 单帧统一 Source/sampleSource 与参数关闭边界](investigations/2026-09-09-w05-csi-frame-source.md)；已接入 [Host PCAPNG/显式时钟锚点](investigations/2026-09-09-w06-monitor-pcapng.md)；CSI packet 的同槽存储、snapshot 和 wire Source 已接通；完整 PHY/time 与导出运行验收仍待完成。无旧 192-byte reader 或兼容分支。

**工作**：实施第 9.4/12 节；writer/parser/fixture 同步替换唯一 v1；严格 canonical layout；新增 callback-time 时间标志；相对时间/UTC anchor/boot 身份处理；Monitor JSONL/PCAPNG 转换与 CSI packet 对照。

**验收用例**：checked arithmetic、重叠区、非零 reserved、未知必需 flag、错误 enum、目录/metadata 重复值不一致、零 section、padding、截断输入；时间回绕/长空窗/restart；旧 192-byte metadata 明确拒绝，无 fallback reader。

### W-07：完整 Driver typed control

**当前增量**：[内部 restart 全局 checkpoint](investigations/2026-09-09-w07-restart-globals.md)已编码国家/MAC/省电/安全 event mask 的捕获、恢复和最终读回；已追加[启动后 TX power 精确恢复](investigations/2026-09-09-w07-restart-tx-power.md)，stopped driver 的提前捕获仍待公开协调器接线；[interval 原值/revision 恢复](investigations/2026-09-09-w07-restart-interval.md)已接入内部路径；[Station/AP TX rate 恢复](investigations/2026-09-09-w07-restart-rate.md)也已编码；[PHY 全字段恢复主体](investigations/2026-09-09-w07-restart-phy.md)已编码；[重建端 Station/AUTO/STOP 准备](investigations/2026-09-09-w07-band-prepare.md)及[SDK 内部重启事件屏障](investigations/2026-09-09-w07-band-cycle-events.md)已接入；[捕获端单频准备与部分快照保留](investigations/2026-09-09-w07-band-capture.md)已编码，最终交接要求原 band/channel 精确读回；[原 band mode 与 Station channel 恢复](investigations/2026-09-09-w07-band-restore.md)已编码；[restart 启动前配置核对](investigations/2026-09-09-w07-restart-prestart.md)已接入，其原有 AP 信道相等限制已由后续 CSA 协调替换；[restart 存储策略最终提交](investigations/2026-09-09-w07-restart-storage-commit.md)已将 FLASH 恢复移至 START/完整核对之后，失败保留 unknown 且不交接 owner；[AP-only CSA 后启用 Station](investigations/2026-09-09-w07-ap-activation.md)已接入内部 AP/APSTA 最终启动信道协调；公开 restart/helper 集成与高级控制仍待完成。C5 编译和契约检查通过不代表运行/实机验收。

**工作**：补齐第 8/27 节各 public setter/getter、target-specific schema、band/protocol/bandwidth、天线、inactive time/RSSI/TSF、PMF/coexistence/wakelock、restore/restart。

**验收用例**：前置状态、单位转换、权限/秘密读回、unsupported option、有效参数 readback、无 getter 的配置恢复拒绝、shadow state revision、feature-disabled 构建无残留对象。

### W-08：高级功能逐模块扩展

按依赖与明确契约分批：Vendor IE/Action/ROC → FTM/TWT → Roaming → Enterprise/WAPI → WPS/DPP/SmartConfig → NAN → Mesh。

2026-09-12 当前增量：NAN 的 USD 公开入口已与 NAN-Sync gate 解耦，USD-only
不注册 DataPath。C3 USD-only 与 C5 Sync+USD 的受影响生产单元及局部链接通过；
完整镜像、S3/disabled 矩阵和运行/RF 验收尚未执行。Pairing 已补内部精确服务/
凭据选择、服务独立的派生密钥缓存和 Auth/follow-up 共享发送回收；公开确认与
Radio/Future 后续已接通显式 PIN 的 preparePairing/confirm/ready/close；PIN bootstrap
请求、接收与显式确认已编码；缓存元数据和显式重验证已接通，凭据仅在协议/TX/回收完成后提交。WAPI 实际生命周期控制后续已接通；Mesh 及剩余
恢复边界继续实施。当前范围统一见[剩余工作清单](investigations/2026-09-08-wifi-api-remaining.md)，
下列逐批记录保留历史证据，不应把后续已接通的旧缺口重复列为待实现。

TWT 增量：[管理帧 TX 存活观察](investigations/2026-09-10-w08-twt-tx.md)已接入
C5 output/ROM recycler 函数表与内部 SDK snapshot。输入/原生 lane/timer 顺序
helper 已编码；probe TX、完整 SDK 退休、Radio/Agreement/Future 和公开控制仍待
完成。无新占位 API，运行验收 not-run；测试仍按全部 Wi-Fi API 完成后集中安排。
随后已补 [probe TX 范围与共用回调审查](investigations/2026-09-10-w08-twt-probe-tx.md)：
原生 task/Probe Request 过滤及调用存活计数已编码，普通连接 probe callback
可能进入当前 TWT pending 的身份隔离缺口仍待处理，不能据此标记 TWT 已完成。
后续已编码 [probe TX callback 身份修复](investigations/2026-09-10-w08-twt-probe-callback.md)：
build-local 调用传递 EB、精确一次交付、提前完成保留、probe 发射前身份要求及
原生 BUSY 准入；RX/timeout/完整退休与公开 API 继续保留，动态验收未执行。
[probe timeout 参数修复](investigations/2026-09-10-w08-twt-probe-timeout.md)已接入
Wi-Fi timer 函数表及原生 handler，数字 identity 隔离旧 node/阶段，保留 post
错误；RX/完整联合退休/故障恢复和公开接入仍未完成，动态验收继续后置。
[probe 原生结果与观察队列](investigations/2026-09-10-w08-twt-probe-result.md)已接入
原生完成先保存和零等待投递，避免事件队列饱和挡住清理；RX 已核对为关联
AP Beacon/Probe Response 存活语义。完整退休/故障恢复和 Radio/Future/公开
TWT 继续保留，新增生产 fixture 仅 AST，动态验收未执行。
[probe timer 错误返回/清理](investigations/2026-09-10-w08-twt-probe-timer-errors.md)
已覆盖首次毫秒和 ACK 后微秒计时，create/start 错误不再 abort，delete 失败
保留 handle；原生 finish 按当前 node/phase 清理，不伪造 RF timeout。完整
退休、故障恢复及 Radio/Future/公开 TWT 仍待完成，运行验收继续后置。
[probe 唤醒引用归属](investigations/2026-09-10-w08-twt-probe-wake.md)已独立于
timer presence，六处精确 SDK relocation 与 framework abort 共用记账，避免
保留 handle 后 stop 再次释放共享 PM。取消/联合退休和公开 TWT 仍待完成，
新增生产与 archive fixture 仅 AST，运行未执行。
[probe 精确取消与存活状态](investigations/2026-09-10-w08-twt-probe-cancel.md)
已接入私有原生队列的 identity 校验、timer 失效/删除后缀重试和独立 active/PM
释放；取消结果与 TX 存活状态分开观察。完整 timer/TX/native/event 退休、故障
恢复和 Radio/Future/公开 TWT 仍待完成，阶段运行与实机测试继续后置。
[probe 原生 owner/联合回收](investigations/2026-09-10-w08-twt-probe-retire.md)
已补托管提交的 identity/pin、原生 quiescence/release 与既有 Radio boot
事件确认；新协调器串联取消、TX、timer/native 和精确 event fence，尚待
Radio/Future caller。其余 TWT agreement/恢复与公开 API 不记为完成。

后续 [公开 probe](investigations/2026-09-10-w08-twt-probe-public.md)已接入独立
Radio/Future 与 boot cleanup；[iTWT setup timer 身份](investigations/2026-09-10-w08-twt-setup-timer.md)
已保护响应和 dwell 两阶段的 callback/native queue 复用。Agreement 公开控制、
完整 TX/RF/事件退休和故障恢复仍保留；这些静态/构建证据不构成运行验收。
[setup 原生结果](investigations/2026-09-10-w08-twt-setup-result.md)随后已补八槽
身份与值记录，并接入原生事件源的零等待发布；公开 Agreement 与完整退休
仍未完成，fixture 仅 AST，未扩大公开能力声明。
[setup 调用与身份交接](investigations/2026-09-10-w08-twt-setup-submit.md)已补
公开 SDK 调用桥接及 native 阶段确认，异常保留稳定存储；公开 Agreement
caller 和完整退休继续保留，桥接尚未进入当前 ELF。

每个模块必须先提交完整 option/event/result/timeout/ownership 子规范和 API map 条目；原文 `object` 与未定义类型不得直接进入正式发布声明。每个模块单独编译 gate、资源预算、错误注入和硬件证据。

没有目标设备时保留 `not-run`；公共底层不支持时记录有证据的 target-unsupported；仅因暂未实现则保留 planned，不删掉总目标。

### W-09：内存预算、旧 generation 与关闭

2026-09-11：CSI 数据池从单会话控制中分离，旧 Frame/Batch/View/Source 通过精确
代号访问。所有 active/retained/allocating/retiring 池共用现有 build 总槽数，最多
八代；分配前预留、实际 free 返回后归还，代号耗尽不回绕。driver 清理后保留数据
不再阻止有预算的新 open。诊断逐代列出预留字节与 owner；集中运行与实机待验收。
CSI 总槽限制继续作为子预算。共享 memory manager 已新增 internal/PSRAM 总额和
control reserve 的原生准入，并接入 CSI/Monitor、ESP-NOW 收发缓冲与 worker stack、
既有 BLE 扫描/订阅池。allocator 前预留 payload 与跟踪节点，实际 free 返回后归还；
CSI/Monitor 退休池只改分类，runtime restart 不清除仍存活的固定存储额度。
`memory.manager.wireless` 与 Wi-Fi snapshot 已接通角色小计和峰值。后续已接通无线
EventQueue 完整 RTOS/原生存储及 receive state/buffer、显式无线 driver 的 Future
参数 roots/公开句柄/捕获状态，以及 Raw TX control/queue/周期副本/broker 隔离存储。
模块控制/配置凭据/复制结果、CSI/Monitor 的 Frame/Batch/View/Source/读取租约、
既有 BLE 原生池和复制数据及 BLE/ESP-NOW 数组输入临时副本也已接入；分配、转移和
最终释放共用账本，凭据保留 secure-zero。Wi-Fi helper 同步/完成队列和 watch ingress
也已接入；无线启用构建将共享 Future service 的 runtime/registry/slot/queue、所有
公共 handle/参数及组合 roots、后台 worker stack/TCB 计入 `wireless.runtime`，包含
generic receive 和该共享服务处理的非无线调用。worker 创建前预留全部栈；部分创建
失败保留已启动任务及存储，只重试未创建后缀，已创建 pool 在 runtime restart 后
仍保持 boot 额度。driver/SDK/NimBLE/JS/TLS/静态段按 26.5 独立列出，不冒充完整堆。
完整 build profile 约束和
诊断 reset/dump/coverage 仍待完成，不将局部接入标为 W-09 已通过。

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
wifi.start();
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
    station: { ghz2MHz: 40, ghz5MHz: 20 }
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
| TWT/WAPI/WPS/DPP/SmartConfig 的辅助类型 | option/result/event schema、取消时机、credential owner；FTM initiator Session 与可读来源 recover 已在第 14 节展开并注册，其余恢复来源仍待实现 | 未展开部分 contract-pending，不注册空对象 |
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

W-07 最新增量：[connectionless interval/ESP-NOW 接入](investigations/2026-09-09-w07-interval-integration.md)：公开 setter、共享精确租约、前值恢复及诊断已编码并通过 C5 生产编译；运行/竞争/其他目标验收 not-run，W-07 整体仍未完成。

停机恢复增量：[事件 mask](investigations/2026-09-09-w07-stopped-mask.md) 在固定 SDK C3/S3/C5 保留 STOP 观察，restart 捕获最新 mask；失败回滚仍受故障准入约束。实现为 Candidate，运行测试 not-run，其他恢复来源与 W-08 新操作 API 仍待完成。

[iTWT teardown TX 身份与 PM 引用](investigations/2026-09-10-w08-twt-teardown-tx.md)已连接原生 callback 18、
现有 TX 账本及两个精确 SDK PM 调用点；output 返回和 recycler 均携带身份，
缓冲地址复用不作为旧请求完成。完整 timer/native/event 联合退休、信息定时器、
Radio/Future/Agreement 和物理恢复仍待完成；本批新增 fixture 仅 AST，运行未执行。

[iTWT information timer 参数与身份](investigations/2026-09-10-w08-twt-information-timer.md)已接入现有 OSI/native
分发：异步队列携带数字，参数在取消或原生消费时精确释放，stop/delete 失败
保留后缀；单 flow/all-flow 核对不同请求集合。信息 TX 身份、完整联合退休、
Radio/Future/Agreement 与阶段运行仍待完成，新增 fixture 仅 AST。

[单 flow teardown 本地联合回收](investigations/2026-09-10-w08-twt-teardown-retire.md)已扩展现有执行器：
核对 TX/setup/information/teardown 四个 revision，沿用 TASK/native/event 顺序，
结果先释放、TX 后释放并保留失败后缀。共享 information timer 不取消其他 flow。
公开 Radio/Agreement caller、信息 TX 身份、RF/bTWT 与物理恢复继续待完成。

[information 提交失败/观察队列](investigations/2026-09-10-w08-twt-information-submit.md)已编码无 output 的 PM 引用
回滚（archive-only，待 suspend caller）；event 31 的归一化零等待观察已链接，
避免观察队列饱和阻塞原生清理。
信息 TX 身份、结果/Future 关联、公开 Agreement suspend/恢复与 RF 仍待完成。

[information TX 身份与 PM 回收](investigations/2026-09-10-w08-twt-information-tx.md)已编码：
复用现有 TX 槽，按 node/flow/request ID 核对完成回调；回收后尚欠的 PM 引用
由 Wi-Fi task 归还。关闭检查纳入该保留状态。无 cookie 路径仍依赖原生 callback
先于回收的顺序，公开 Agreement、操作结果/Future 与完整恢复尚待接入。


[individual Agreement 公开接入](investigations/2026-09-10-w08-twt-individual-public.md)：
setupIndividual/agreements 与 WiFiTwtAgreement.status/close 已接入八个独立 Radio
owner、真实 SDK submit、精确 teardown/联合退休和 GC/runtime 清理。所有权不再
依赖 Future/JS 存储。Candidate，集中运行验证未执行；bTWT、suspend/resume、
RF 晚到帧与物理恢复仍未完成，不将 individual 接入标记为整个 W-08 完成。


W-08 最新增量：[individual Agreement suspend](investigations/2026-09-10-w08-twt-suspend.md)
已接入正式 v1 类型、注册、Radio/native/Future 与关闭流程。信息 TX 在输出前绑定
独立操作身份，原生回调完成副作用后记录终态并零等待发布观察；公共超时不撤销
已提交暂停，原生 TX 退休前不复用 lane。仅单 Agreement，durationMs 0 为无限期、
1..4294967 为定时暂停；显式 resume、广播协议、物理故障恢复与阶段运行仍未完成。


W-08 最新增量：[individual Agreement resume](investigations/2026-09-10-w08-twt-resume.md)
已接入公开 Future。恢复使用带最近合法 wake TSF 的 Information 帧，等待精确
新 timer 的原生处理与本地 flow 状态核对，旧 queued timer 不构成完成。替换
发送成功前保留旧自动恢复 timer；PS NONE 准入与执行检查防止 SDK 隐式启用
共享省电。广播协议、完整物理恢复及 Wi-Fi 阶段运行/实机验收继续保留。

广播 TWT 后续增量：[定时器完成与观察事件](investigations/2026-09-10-w08-btwt-event.md)
已接入原生处理后的精确结果保存和零等待发布，缺失/冲突、清理错误与观察
错误分开记录。TX/RX 关联、定时器外托管结果、Agreement 与联合回收仍待
完成；没有新增 setupBroadcast 占位 API，运行和实机验收继续后置。

广播 TWT 后续增量：[setup TX 身份与关闭撤销](investigations/2026-09-10-w08-btwt-tx.md)
已接入共享 TX ledger、精确完成和原生连接关闭；与 information 共用可选身份
数组，不增加其最大预算。RX dialog/请求关联、托管结果与公开生命周期仍待
完成；运行与 RF 验收仍 not-run。

广播 TWT 后续增量：[RX 长度/响应关联与保护布局](investigations/2026-09-10-w08-btwt-rx.md)
修正广播实际 15-byte 发送长度，并用同步值副本适配原广播 PMF 完成；已接入
收包长度校验、当前 timer/dialog 关联和旧 timer 清理失败保护。RF dialog
重用后的隔离、Agreement owner/公开生命周期与完整联合回收仍未完成。

广播 TWT 后续增量：[wire dialog 不回绕分配](investigations/2026-09-10-w08-btwt-dialog.md)
已在发送前为每广播 ID 分配本 boot 唯一的非零 dialog，最多 255 次；发送错误/
关闭/重连不退回 token，耗尽不阻断其他 ID。公开广播 API 接入时必须同步暴露
上限和错误语义；结果 owner、Agreement 生命周期、完整回收与 RF 验收仍待完成。

W-08 最新增量：[广播稳定 setup 结果](investigations/2026-09-10-w08-btwt-result.md)
已接入发送前 TX identity、TXFAIL scope、response/dwell 稳定读取与独立
output/关闭错误。复用 timer entry 增加 256 B 延迟 INTERNAL，控制结果先于
观察发布。公开提交身份交接、Agreement owner、teardown/联合退休仍待完成；
精确 reader 尚无 Future caller，集中运行/实机测试仍未执行。

W-08 最新增量：[广播提交与结果持有](investigations/2026-09-10-w08-btwt-submit.md)
已补 boot config、公共 SDK operation 117/native wrapper/发送前身份交接与
held 准入。完成或断连不自动释放结果；同 ID 复用须等后续联合退休与精确释放。
worker helper 尚无公开 caller，Radio/Agreement/Future、teardown 与运行验收
继续保留，未把内部提交 helper 注册成可调用广播 setup。

W-08 最新增量：[广播 pending 精确取消/静止检查](investigations/2026-09-10-w08-btwt-cancel.md)
已接入 private native ioctl、精确 TX 撤销、timer 清理后缀和 held/cancelled
结果。已建立协议不会被 pending cancel 删除；取消不等于 TX buffer 归还。
顺序屏障后的联合释放、建立后 teardown、Radio/Agreement/Future caller 和
集中运行/实机验收仍未完成，未扩大公开稳定性声明。

W-08 最新增量：[广播 pending 联合退休/held 释放](investigations/2026-09-10-w08-btwt-retire.md)
已连接取消/静止、timer/native marker、Radio control 数字事件确认及原生精确
释放。状态变化撤销旧证明，失败保留未完成后缀；结果池为 3328 B（+256 B）。
worker 协调器尚待公开 owner 接入，已建立协议 teardown/TX/PM、Radio/Agreement/
Future 和集中运行/实机验收继续保留，未声明完整广播生命周期完成。

W-08 广播 teardown 增量：[原生提交/TX/PM](investigations/2026-09-10-w08-btwt-teardown.md)
已接入真实 output/callback/recycler 与 private ioctl，复用单例增加 12 B，
原生连接关闭永久撤销旧 setup node；pending 回收不能越过未退休 teardown。
完整 teardown 联合回收、公开广播 Agreement/Future 与阶段验收仍未完成。

W-08 广播关闭回收增量：[teardown 联合回收](investigations/2026-09-10-w08-btwt-close.md)
已接入精确结果/TX/PM 静止与 timer/native/event 顺序；cut 纳入 teardown revision，
释放单例后再撤销 setup held，结果保留在 owner 存储。没有自动 RF 重试。
公开 Radio/Agreement/Future、显式失败恢复策略与集中运行/实机验证仍未完成。

广播 TWT 公开增量：[Radio/Agreement/Future 接入](investigations/2026-09-10-w08-btwt-public.md)
已编码并通过五种目标构建。`setupBroadcast` 复用 Agreement、Future 和 boot cleanup，
同 ID 精确 owner 保留至完整退休；255 次 boot dialog 预算、关闭错误和原生资源保留
已公开。完整显式物理恢复、all-flow 策略及阶段运行/RF 验收仍未完成。新增 fixture
仅 AST，不能将本批构建与静态证据视为动态竞争验证。

TWT 恢复前置增量：[individual 原连接关闭与清理](investigations/2026-09-10-w08-twt-native-close.md)
已接入原生 close-all、结果永久撤销、setup/information 数字回调撤销及原失败
结果保留。真实连接关闭后仍要求 TX/PM、timer/native/event 排空；不把缺失的
RF 成功事件当作资源占用，也不以空位图替代退休证明。完整物理恢复与阶段运行
仍待完成；本批五目标构建与静态证据不代替竞争测试。
