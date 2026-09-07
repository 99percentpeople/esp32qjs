# ESP32QJS 第一阶段：现有问题修复与无线底座加固

> 本文是实施任务书，不是已完成的修复报告。范围为本轮评审涉及的 Wi-Fi、BLE、ESP-NOW、CSI 及其共享运行时，不代表对整个仓库所有模块的缺陷审计。

| 项目 | 约定 |
| --- | --- |
| 文档日期 | 2026-09-07 |
| 目标仓库 | `99percentpeople/esp32qjs` |
| 审查基线 | `9a74f1197d53863e079c30f8559ccd5b6cd60b42`（历史评审）；本次实施 `e1b861c` |
| ESP-IDF 基线 | `v6.1`，实际 checkout `fff9895c82d744c7237be8847347bdd1b07c6643` |
| 本文状态 | F-CORE 实施记录见[证据表](investigations/2026-09-07-wireless-core.md)；F-HARDWARE 单独验收 |
| 版本策略 | 唯一开发版本 v1；不增加 v2、兼容别名、旧格式 reader |
| 实际仓库路径 | `docs/01_esp32qjs_existing_fixes.md` |
| 后续文档 | [第二阶段：Wi-Fi 重构与新功能](02_esp32qjs_wifi_refactor_and_features.md)、[第三阶段：BLE 重构与新功能](03_esp32qjs_ble_refactor_and_features.md) |

## 0. 执行方式、证据等级与边界

本文基于两份用户设计文档、上一轮源码评审，以及固定提交的仓库并发契约、Radio 实现、贡献说明和功能稳定性记录整理。所有新增验收规则都是**本轮建议的工程要求**，不是对既有实现已通过测试的声明。

实施每个任务前，先检查工作分支相对基线的变更。已由后续提交修复的项目，不重复修改，补充定位和测试证据即可。不要为了复现旧行为而回退用户的新代码。

### 0.1 证据等级

| 标记 | 意义 | 实施要求 |
| --- | --- | --- |
| `CONFIRMED-BEHAVIOR` | 已从基线源码或仓库文档确认的行为；不一定是 bug | 先判断是否违反现有公共契约 |
| `RISK-TO-VERIFY` | 有明确风险路径，但没有完成故障复现 | 先写测试；失败后才按缺陷修复 |
| `PRESERVE-INVARIANT` | 现有实现已经建立的正确机制 | 保留并增加防回归测试，不另起一套 owner |
| `DESIGN-GAP` | 两份新增设计中的缺口，不能归咎于尚未存在的代码 | 转到第二、三份文档修订，不在本阶段铺开新功能 |
| `HARDWARE-PENDING` | 需要真实 RF、对端或长时间运行证据 | 不用 mock、编译成功或文档生成代替 |

**没有复现证据时，不把“可能有问题”写成“已经发生内存泄漏/死锁”。** 如果新增测试通过，该任务可以以“验证通过，无须改代码”结束。

### 0.2 三份文档的依赖

```text
01：现有缺陷复现、最小修复、公共生命周期与回归基线
 ├── 02：共享 Wi-Fi Radio 重构 → Wi-Fi 新能力
 └── 03：BLE 机械拆分 → BLE 新能力
                         ↓
             两条分支汇合后的无线共存验收
```

第二、三份文档可以在第一份的公共契约稳定后并行实施，但不能分别实现两套 Future、EventQueue、ByteView 或 generation 基础设施。共享文件的变更必须由一个明确的提交序列管理。

### 0.3 本阶段做什么、不做什么

本阶段处理现有打开/关闭、错误恢复、异步回调所有权、队列饱和、输入验证、秘密数据、文档契约和测试证据。只做能缩小风险的必要内部重构。

本阶段不新增 `wifi.monitor`、`wifi.rawTx`、完整 `wifi.driver`、CSI correlated packet、Extended/Periodic BLE、L2CAP、ISO、CTE；不在修复提交里改 CSI wire layout；不进行 BLE 全文件拆分；不改变现有单固定信道 owner 的共享政策。它们分别归后续文档。

## 1. 基线已经具备的能力与必须保留的机制

基线无线并发文档规定：JS 运行在 runtime task；NimBLE/Wi-Fi callback 不创建 JSValue、不执行 JS；数据复制到有界原生池；跨任务标量使用 atomics 或明确的锁域；EventQueue、Future 和 payload 都有独立所有权。[R-CONCURRENCY]

需要保留的具体机制：

| 模块 | 现有机制 | 本阶段不能退化为 |
| --- | --- | --- |
| BLE | callback 引用计数、generation、原生 GATT cache、Host stop/deinit 后释放回调可见资源 | callback 直接执行 JS；只凭 `volatile` 同步 |
| ESP-NOW | 关闭 admission/注销 callback 与实际清理分离；queue native retain；显式 recovery pending | 超时后立刻重用 send state 或全局重启 Wi-Fi |
| CSI | event owner 向 Frame/Batch 转移；View/Source 额外 retain；旧 pool 延迟销毁 | 关闭 parent 就释放仍被 view 使用的 storage |
| Radio | 精确 lease identity；固定信道冲突前检查 | 只按 client kind 判断是否拥有资源 |
| 运行时 | Cooperative Future、受控 worker、显式 close、finalizer 兜底 | 在 finalizer 中无期限等待、跨线程访问 JS heap |

`docs/feature-stability.json` 在基线将 Wi-Fi、BLE、ESP-NOW 标为 Candidate，将 CSI 标为 Hardware pending。BLE 已有部分 S3/Pi、S3/C5 端到端证据，ESP-NOW 已有部分 S3/C5 加密链路证据；这些是仓库的记录，不是本次重新执行的结果。[R-STABILITY]

## 2. 修复任务总表

优先级：P0 表示内存/状态机/安全边界；P1 表示错误语义和一致性；P2 表示补充诊断与维护性。优先级是本轮建议，不代表所有项目都是已证实缺陷。

| ID | 任务 | 分类 | 优先级 | 主要交付 |
| --- | --- | --- | --- | --- |
| F-00 | 锁定基线、保存现有行为与命令 | CONFIRMED-BEHAVIOR | P0 | 可复现的基线记录 |
| F-01 | Radio 初始化/启动失败后的状态与资源恢复 | CONFIRMED-BEHAVIOR + RISK-TO-VERIFY | P0 | 故障注入、精确回滚、可诊断状态 |
| F-02 | lease 的身份、幂等释放与并发变更校验 | RISK-TO-VERIFY | P0 | stale/duplicate/race 测试与最小修复 |
| F-03 | 关闭、迟到 callback 与 Future storage 隔离 | PRESERVE-INVARIANT + RISK-TO-VERIFY | P0 | 生产路径竞争测试、准确清理后缀 |
| F-04 | 控制状态不依赖可丢弃观察队列 | RISK-TO-VERIFY | P0 | queue saturation 下终态可达 |
| F-05 | BLE 超时与断连副作用的逐操作契约 | CONFIRMED-BEHAVIOR | P0 | timeout 表、lane 隔离、文档一致 |
| F-06 | CSI Frame/View/Source 生命周期与旧池预算 | PRESERVE-INVARIANT | P0 | parent close 不死锁、不 UAF |
| F-07 | ESP-NOW recovery/close 与共享 Radio 回归 | PRESERVE-INVARIANT | P0 | 保留既有 E2E 和恢复语义 |
| F-08 | 参数、GC rooting、分配失败与数据长度 | RISK-TO-VERIFY | P1 | 输入/转换失败路径覆盖 |
| F-09 | 秘密数据与示例资源释放 | RISK-TO-VERIFY | P0/P1 | 脱敏、清零、所有权完整示例 |
| F-10 | feature、类型、manifest 与文档一致性 | HARDWARE-PENDING + RISK-TO-VERIFY | P1 | 明确 availability 与稳定性证据 |
| F-11 | 跨目标、生命周期、低内存与共存验证 | HARDWARE-PENDING | P0/P1 | 测试记录和未完成项清单 |
| F-12 | 将新设计缺口移交后续功能阶段 | DESIGN-GAP | P1 | 明确归属，不混入修复提交 |

## 3. F-00：冻结可复现基线

### 3.1 必须记录

保存 firmware commit、工作区 dirty diff、ESP-IDF commit/tag、NimBLE 子模块 commit、MQuickJS 子模块 commit、工具链版本、Build Context 路径及 hash、最终 sdkconfig、目标芯片、板卡、PSRAM 配置，以及现有测试结果。

记录现有公共 API 的 native registration、`types/esp32qjs-c-api.d.ts`、`types/esp32qjs-js-api.d.ts`、`api-manifest.json`、`docs/api/` 和 feature catalog。快照用于比较，不作为新增兼容 reader 的理由。

### 3.2 使用仓库既有入口

下列命令来自基线 `AGENTS.md`，须在正确环境和合法 Build Context 下运行：[R-AGENTS]

```sh
uv sync
python scripts/remote.py mcus
python scripts/remote.py show-config
python scripts/remote.py check-js
python -m unittest discover -s tests/python
python scripts/remote.py test --scope c
python scripts/remote.py --assume y build
```

有授权硬件和适当环境变量时，再执行既有 device/network 测试：

```sh
python scripts/remote.py test
```

C3/S3/C5 各使用仓库现存且完整的测试 Build Context，或预先生成的合法 Context。不要虚构 Context 目录，也不要只替换 `IDF_TARGET` 却沿用不匹配的分区、常量或 flash_data。

普通修复不执行 `flash --erase-workspace` 或 `flash-workspace`。这些命令涉及持久数据破坏，不属于普通回归操作。

### 3.3 验收

- [x] 已执行命令记录输出与退出码；not-run 单列，见实施证据。
- [x] 原有失败、回归失败和环境失败分开记录。
- [x] 未取得硬件结果的项目明确标注 `not-run`，不记为 pass。
- [x] 工作分支相对实施基线 `e1b861c` 的差异已确认。

## 4. F-01：Radio 初始化/启动错误的恢复边界

### 4.1 已确认的代码事实

`esp32_mquickjs_wifi_radio.c` 使用 `s_init_state`、`s_start_state`、`s_init_result`、`s_start_result` 记住首次初始化/启动结果。初始化路径包含 NVS 初始化、`esp_wifi_init()` 和设置 storage；结果包含失败也会进入 once complete 状态。[R-RADIO]

这说明当前 Radio 不是任意可重启 Driver 状态机。它**不等于**“现有 `wifi.stop()` 已经有 bug”，因为完整公开 stop/restart 是第二份文档要新增的能力。

### 4.2 先复现的故障矩阵

| 注入点 | 关注行为 |
| --- | --- |
| NVS 初始化失败 | 没有启动 Wi-Fi 的情况下是否可以明确恢复或报告稳定故障 |
| `esp_wifi_init()` 失败 | 不把部分初始化误报成完整可用 |
| init 成功、set_storage 失败 | 已获取的 driver owner 是否保留或准确 deinit |
| `esp_wifi_start()` 失败 | 再次调用不会报告虚假的 started；失败结果和恢复要求可见 |
| 两个 caller 同时 ensure_started | 只有一个初始化者；等待者可在合理契约下退出 |
| runtime teardown 与初始化交错 | 不在未完成 init 上执行无条件清零或重复 deinit |

### 4.3 修复规则

只在验证存在缺陷后调整。增加明确的阶段记录，至少区分“未拥有 driver”“已初始化但未启动”“启动失败”“清理尚未完成”。如果现有接口没有安全的在线重试契约，本阶段可以明确返回故障并要求设备重启（runtime restart 不会重置 boot-scoped Radio once 状态），不得伪造恢复成功。

不能简单把 once state 写回 NOT_STARTED：先证明上一轮拥有的 driver/event/netif 资源已释放，并确认不存在仍可访问旧状态的任务。记录失败的原始 `esp_err_t` 和 stage，但不能记录凭据。

涉及完整 start/stop/restart、mode aggregation 和新 Radio policy 的重构，移交 W-01；本阶段不双线开发同一状态机。

### 4.4 验收

每个失败注入后，要么能通过已有受控路径恢复，要么稳定停留在可诊断、禁止重复占用的故障状态；不得出现 started=true 但 driver 实际未启动，或遗留资源被第二次 init 覆盖。

## 5. F-02：lease identity 与副作用原子性

### 5.1 检查重点

基线 `wifi_radio_lease_valid()` 检查 generation、acquired、非零 identity 和相应 client 计数；固定信道 owner 另外使用精确 identity。[R-RADIO][R-CONCURRENCY]

需要验证内部调用者不会把已释放 token 的复制品当作有效 lease。此处是原生生命周期风险，不是已证实的 JS 可利用漏洞。

### 5.2 必须加入的测试

1. 同一个 lease 重复 release，不重复减少 client count。
2. A 释放后，同类型 B 仍存活，A 的旧 token 不能修改 B 的 radio state。
3. 重开后的 token 不能因 slot 复用或 ID 回绕匹配旧 owner。
4. 同一 lease 重复申请同一固定信道保持幂等。
5. 本阶段仍保持基线行为：另一个 lease 申请固定信道，即便信道相同，也按当前契约冲突。
6. 验证通过与 driver mutation 之间发生 release/close，不造成未受保护的 driver 修改。
7. driver mutation 失败时，原有 owner 和有效配置保持一致。

### 5.3 最小修复方向

在有界 owner registry 中确认精确身份，或证明现有 token 的唯一持有约束并补齐失效检查。不要只把计数判断改成另一个不完整的布尔标记。

generation 和 identity 在仍有旧引用时不得重用。达到可表示范围时允许明确拒绝分配，不能绕回到一个仍可能被旧 handle 持有的值。

同信道多 owner 共享、AP/NAN/Mesh mode 合并和 rate lease 归 W-01；F-02 只建立它们可复用的身份验证基础。

## 6. F-03：callback、Future 与关闭的共同安全规则

### 6.1 四个不同的问题必须分别回答

| 问题 | 不能替代它的判断 |
| --- | --- |
| callback 入口上下文是否仍有效 | 在已经失效的指针上才增加引用计数 |
| 当前是否有 callback 正在执行 | 未来是否还可能到达旧 callback |
| JS Future 是否已超时/取消 | native operation 是否已经结束 |
| driver 是否不再产生回调 | JS 侧 Frame/View 是否已释放 |

`activeCallbacks == 0` 只证明某一时刻没有正在运行的 callback。注销、停止、驱动终止或完成事件是否构成可靠屏障，必须逐适配器以固定版本公共契约及测试证明。延迟若干毫秒不能单独充当证明。

### 6.2 操作所有权要求

每个 operation 至少记录 resource generation、operation identity、native started、public settled、cleanup stage 和 callback/native references。引用与状态发布必须使用同一同步域，不能先公开裸指针再补 retain。

- Public Future settle 不得自动释放仍被 driver 引用的 state。
- callback 能携带独立 arg/cookie 时，cookie 必须绑定该次原生操作，而不是回调到达时才读取“当前 operation”。
- callback 没有 request cookie 时，清理完成前禁止重用容易发生混淆的 lane/slot。
- 超时后等待旧结果的隔离状态是 native owner，不需要继续保留失效的 JSContext。
- worker 只处理原生清理；JS root 的创建和删除必须由允许访问 JS heap 的上下文完成。
- 新 runtime 不接收旧 runtime 的事件或唤醒。

### 6.3 关闭采用“准确后缀保留”

清理每一步都记录是否完成。再次 `close()` 只重试尚未完成的必要后缀，不重复 free，不因为前一个操作已成功就重新注册 callback 或重新启动 driver。

关闭失败时保留 callback-visible storage；先关闭新 admission，使其他方法返回 closing/faulted 错误。不得“返回失败，同时把底层内存释放掉”。

**逻辑关闭、原生操作清理和数据内存回收是三个阶段。** 已关闭 handle 不能继续发起操作；仍持有数据 lease 的 ByteView/Source 可以按其原契约继续读取。

### 6.4 生产路径竞争测试

测试不能只有独立 toy refcount。应调用生产 helper，提供可控 callback/worker 调度点，覆盖：

| 时间线 | 期望 |
| --- | --- |
| callback 进入 → close → callback 退出 | 资源保持到 callback 退出 |
| timeout → 新 operation 请求 → 旧 completion | 新操作未被旧 completion 完成 |
| callback 事件已排队 → runtime teardown | 事件释放 payload；不转换为旧 JSValue |
| Future dequeue 后未进行 JS conversion → Future destroy | 槽仅归还一次 |
| unregister/stop/deinit 逐步失败 | 保留准确所有权并可重试 |
| worker 提交失败 | 不丢失 cleanup obligation，不假装 close 完成 |
| callback entry 与 subscriber detach 交错 | 入口 context 不 UAF |

## 7. F-04：状态机不能依赖可丢弃事件

### 7.1 分层规则

本轮要求将“原生完成结果”和“提供给 JS 的观察消息”分开。可以复用现有 Future/状态结构，不要求新增一个通用消息框架。

| 数据类别 | 处理 |
| --- | --- |
| Scan Report、CSI、Notification 等高频数据 | 可按已声明策略丢弃；归还槽并独立统计 |
| connect/disconnect、send complete、close complete | 先更新原生状态和 Future；观察队列满不影响终态 |
| Pairing input、incoming connection 等需要应用处理的资源 | 在原生表中登记并保留可恢复入口；容量不足按既定策略拒绝/结束 |

本阶段审核**现有**事件路径。新 GATT `event-only` 的背压、全量 `wifi.watch()`、Raw TX 完成 broker，归 B-07、W-02、W-04。

### 7.2 验收用例

主动填满每一个现有公开观察队列，再触发连接成功、连接失败、对端断开、发送完成和关闭。Future 必须 settle，状态可查询，清理最终完成。观察事件可以有 drop counter，但不得依赖“用户先取走一条消息”才能让 driver 关闭。

终态不得被后续低优先级事件覆盖成 running。发生资源丢失时，必须能从 snapshot 或资源表发现，不制造无人可关闭的连接。

## 8. F-05：BLE 超时行为契约化

### 8.1 已确认的现有行为

基线 `ble_future_on_timeout()` 对已启动的扫描、广播、连接和若干 GATT/Pair/MTU/RSSI 操作采取不同处理，其中若干分支会终止相关连接。因此不能笼统说明“BLE timeout 从不影响连接”。[R-BLE]

这不意味着所有断连都是错误；在原生协议无法安全取消时，关闭本连接可能是保持状态机一致的必要处理。要修复的是错误关联、资源提前释放、非目标连接受影响，以及文档与实际副作用不符。

### 8.2 逐操作必须填写的表

| 操作组 | 未启动时 | 已启动时 | native 清理完成的证据 | 允许影响的资源 |
| --- | --- | --- | --- | --- |
| Scan start/stop | 取消排队项 | 对应 GAP cancel/stop | 已核验的停止/屏障 | 本 Scanner/现有扫描 owner |
| Advertise start/stop | 取消排队项 | 对应 GAP stop | 停止/终态 | 本 Advertiser |
| Connect | 取消排队项 | 取消 connect；成功竞态下结束新连接 | 连接结果或取消完成 | 本次新连接 |
| Discovery/Read/Write/CCCD/MTU | 取消排队项 | 按 Host 可取消能力或受控断连 | 最终 GATT/断连结果 | 本连接 |
| Pair | 取消排队项 | 拒绝当前请求/结束过程；必要时断本连接 | 安全终态/断连 | 本连接与本次请求 |
| Server Notify/Indicate | 取消排队项 | 检查现有广播/定向语义 | 对应完成/断连 | 本次请求明确涉及的连接 |
| Adapter close | 不适用 | 继续准确后缀清理 | Host stop/deinit 屏障 | 本 Adapter |

实施者需将 native API、实际返回码和 callback 事件补入同一测试记录。上表不是声称所有操作都能无损取消。

### 8.3 验收

- 超时不自动降 MITM/SC 要求，不自动重连。
- 连接 A 的 GATT 超时不能清理连接 B 的 state。
- native operation 尚未终止时，ATT lane 不分配给新 operation。
- late connect success 不遗留无人持有的连接。
- 必要断连在错误 details/文档中可见；不得修改其他无线模块以“帮助恢复”。
- 当前未开始操作的取消语义保持明确，不通过提前释放 state 实现伪取消。

## 9. F-06：CSI 数据 lease 与关闭

### 9.1 保留现有正确设计

基线 CSI 将 public close 与 pool 的最终销毁分开：停止 driver/关闭 session 后，仍被 View/Source retain 的 storage 延迟回收。不要把新设计的“等待 retained leases”理解成公开 `close()` 必须阻塞到应用释放所有数据。[R-CSI][R-CONCURRENCY]

### 9.2 核心测试

使用当前基线已有的方法名编写测试，不在 F 阶段提前引入 W 阶段的 `sampleSource()` 重命名。

```text
receive Frame
取得一个 retained view
关闭 Frame
关闭 CSI session
确认 session 不再占用采集 driver/Radio
确认 view 仍按既有数据契约可读
关闭 view
确认旧 pool storage 最终归还
```

如果 pool 上限或清理状态不允许立即重开，必须明确失败，不能通过复用旧池达到表面成功。

还需覆盖 Batch、多 View、Source 被 RPC/Stream 保留、独立 copy、队列丢弃、JS conversion 失败和 finalizer fallback。独立 copy 不应继续占用 capture pool；保留的 view 不应阻止停止采集。

### 9.3 当前单 pool 的有界行为

当前实现只有一个未释放的 `s_wifi_csi.resources`。`js_wifi_csi_open()` 在
`resources.slots != NULL` 时拒绝 reopen；保留的 View/Source 因此会阻止创建下一代
pool。关闭采集不等于释放 retained storage，最后一个 owner 释放后才归还 pool。

F-06 验证这条已有边界、槽账本、关闭后仍可读和最后 retain 的释放；不新增多代
pool、retired-storage budget 或另一套预算器。多代数据与跨模块预算属于 W-09/B-10，
如后续确需允许旧 view 存活时 reopen，必须先提出新的总预算和关闭契约。

## 10. F-07：ESP-NOW 回归与恢复

保留原来的顶层 `espNow`、peer、PMK/LMK、native TX queue、显式恢复及错误语义，不把 ESP-NOW 搬入 `wifi`。基线的两阶段关闭和 `ESPNOW_CLEANUP_PENDING` / `ESPNOW_RECOVERY_PENDING` 是需要保留的机制。[R-CONCURRENCY]

重点测试：

- send timeout 与 callback active/late callback 交错；
- 关闭期间 EventQueue JS object 被释放，但原生 retain 仍保护 callback-visible storage；
- peer removal 与在途 send 的串行关系；
- 加密 peer 配置失败和重建失败的精确回滚；
- 恢复时不重复注册 callback，不遗留旧 generation 的 RX payload；
- recovery 只触及其有权处理的 ESP-NOW 资源，不擅自停止其他 owner 的 Wi-Fi；
- Radio token 的新验证不得破坏已记录的 S3/C5 链路和排队发送功能。

重复发送、分片、业务 ACK、自动 mesh 和无条件 Wi-Fi restart 均不进入本任务。

## 11. F-08：参数、GC、分配与长度

### 11.1 参数与转换

检查当前开放方法的 plain object、unknown-field、整数上下界、单位转换、buffer length、offset、空数据和 typed-array view 偏移。参数失败必须在 driver mutation 前返回；无法完全预验证的 driver 错误保留 stage。

JSGCRef/rooting 审核重点是：任何会触发 GC 的 JS 调用前，后续仍需使用的 JSValue/字符串/对象必须按项目机制保活；不把 raw JS storage 指针放进 native callback。不要仅靠“这个测试没触发 GC”证明安全。

### 11.2 分配与构造

对每个现有构造路径注入第 N 次分配失败，包括 queue、pool、Future state、注册表项、GATT snapshot/值副本。所有失败路径遵循逆序 rollback；所有权转移后不能由 caller 和 callee 双重 free。

长度计算使用 checked addition/multiplication/alignment，尤其是 capacity × stride、batch 多段长度和 ByteSource 的 offset+length。此阶段不改变 wire 布局，只检查当前格式的 writer/parser 边界。

### 11.3 回调上下文

根据实际 callback 所在线程选择正确的队列/唤醒 API。不要看到某个 helper 名称包含 ISR 就推断所有无线 callback 都在 ISR；也不要未经核验更换当前经过测试的 publisher。生产 helper 的上下文合法性需要单独记录。

## 12. F-09：秘密、日志与示例

秘密包括 Wi-Fi password、EAP password、private key、PMK/LMK、LTK/IRK/CSRK、OOB 以及 Pairing 临时材料。检查 status、error、日志、RPC inspection、manifest snapshot 和测试输出，不能通过 JSON 序列化间接泄漏。

要求原生 owned secret storage 在释放前使用项目允许的不可被优化掉的清零方法；不要声称能够清零 JS 不可变字符串的所有 GC 副本。JS/RPC 层以避免不必要复制、禁止自动 inspection/日志和访问边界为主。

示例必须在 acquisition 失败时也能清理之前获得的资源；不能只给成功路径加一个 `finally`。存在 cleanup-pending 时不得继续假定 parent 已经物理释放。

对于 Numeric Comparison，示例必须使用用户真实确认的结果；无人确认或超时默认拒绝，不能打印数字后直接 `match: true`。新 BLE 示例的正式替换在 B-12 完成。

## 13. F-10：公共表面与证据一致性

### 13.1 单一真实来源

C registration、feature catalog、`docs/api/docs.json` 选择的文档、`.d.ts` 和 manifest 必须一致。先确认哪些文件是 generator 输出，再修改源定义并重新生成，不手工分别“补几个字段”制造漂移。

基线 `AGENTS.md` 要求框架消费不可变 Build Context，不解析 Board/Library/Agent/workspace/provider manifest。不要在无线重构中新增根据板卡名字猜能力的分支，也不要让固件自行解析产品 Library manifest。[R-AGENTS]

运行中的 MQuickJS 使用 ES5-like 方言。类型声明里的 TypeScript interface/class 是说明，不是可直接烧录的 JS；设备示例不引入 `let`、`const`、箭头函数、async/await 或未经构建链支持的模块语法。

### 13.2 能力与成熟度分开

至少分清：源码存在、当前 build 编译、native 注册、target 能力、Host 测试、硬件测试。只完成 Host 测试不能把 `Hardware pending` 改成稳定。

F 阶段不把后续设计里的方法先塞进正式 `.d.ts` 和 manifest。目标 API 放在设计文档中，实际注册与声明在对应实现提交一起更新。

## 14. F-11：验证矩阵与判定

### 14.1 测试层次

| 层次 | 目的 | 证据 |
| --- | --- | --- |
| Host C | 生产 owner/资源 helper 的状态、引用和失败路径 | 测试命令、退出码、用例输出 |
| Python architecture | callback/registration/生成物和结构约束 | 检查结果；不能替代 C 行为测试 |
| 三 target build | C3/S3/C5 的条件编译和链接 | 完整 Context、sdkconfig、构建日志 |
| Device JS | 当前公共 API 端到端、GC、关闭与重开 | 设备日志、对端配置 |
| RF/共存 | 真实丢包、参数生效、压力下恢复 | 传输统计、内存快照和复现实验 |

### 14.2 最小必测组合

按当前已开放且 target/build 支持的能力执行：BLE scan + STA、GATT notification + TLS、ESP-NOW queued TX + BLE、CSI + STA/ESP-NOW，以及 pending Future 下的 runtime restart。没有编译的能力为 `not-applicable` 并给出理由；有能力但没有设备为 `not-run`，不是 pass。

### 14.3 生命周期与内存

延续原设计的 500 次生命周期要求，但不要一次循环内硬把所有场景混在一起。分别覆盖 Adapter、Scanner、Connection、订阅、ESP-NOW session、CSI session，再做混合场景。

内存判定采用预热后**同等静止状态**的 current free heap、largest free block、活跃/retired 资源数和 pool free slots。历史 minimum free 是高水位压力指标，数值本来只会保持或降低，不能用“必须不下降”作为单独泄漏标准。

仍被测试显式持有的 view 不算泄漏；释放所有测试 owner 后，资源账本必须回到预期状态。容许的缓存差额必须事先列明和有上限，不能事后用任意容差掩盖泄漏。

32 位微秒 RX 时间戳约 71.58 分钟回绕，这是 `2^32 / 1,000,000 / 60` 的计算结果；只做一小时 soak 不能覆盖首次回绕。[U-RX] F 阶段检查当前时间处理；新的统一时间戳契约和长时间无包的处理在 W-06 完成。

### 14.4 建议新增/扩展测试

以下名称是**拟新增文件**，不是声称仓库已经存在；已有等价测试时优先扩展。

```text
tests/c/test_wireless_control_completion.c
tests/c/test_wireless_late_callback_isolation.c
tests/c/test_wireless_cleanup_suffix.c
tests/c/test_wifi_radio_failure_recovery.c
tests/c/test_wifi_radio_lease_identity.c
tests/c/test_ble_timeout_lifecycle.c
tests/c/test_wifi_csi_retained_lifecycle.c
tests/c/test_wireless_memory_budget.c
tests/python/test_wireless_public_contract.py
tests/python/test_wireless_secret_redaction.py
tests/js/wireless/lifecycle-existing-api.js
```

### 14.5 测试证据模板

```yaml
schema: 1
phase: F-03
firmwareCommit: <actual-sha>
idfCommit: <actual-sha>
nimbleCommit: <actual-sha>
buildContextHash: <actual-hash>
target: <actual-target>
board: <actual-board>
peer: <actual-peer-and-version>
command: <exact-command>
result: not-run   # pass / fail / not-run / not-applicable
reason: <required-when-not-pass>
artifacts: []
heapBefore: null
heapAfter: null
outstandingOwnersAfter: null
```

不得把模板中的 `null` 或 `not-run` 批量改为 pass 充当验收。

## 15. F-12：新设计缺口的移交表

| 上轮发现 | 不应被误写为 | 归属 |
| --- | --- | --- |
| 新 stop/restart 与当前 once 启动模型不兼容 | 已存在的 stop API 已损坏 | W-01 |
| 同信道多 lease 共享需要 owner 集合 | 当前单 owner 契约天然错误 | W-01 |
| Raw TX callback 结构含指针且无框架 cookie | 尚未实现的 Raw TX 已 UAF | W-04 |
| 新 rawEventData 兜底可能包含变长数据/秘密 | 所有现有 Wi-Fi event 都泄密 | W-02 |
| CSI header/payload 边界、FCS、时间戳需核验 | 当前 CSI samples 一定错误 | W-05/W-06 |
| Repeat Pairing 不能直接挂起等待 JS | 所有当前 BLE pairing 都错误 | B-08 |
| 多扫描器、多订阅者缺少协调政策 | 当前单 Scanner 必须立刻改成多 Scanner | B-03/B-06 |
| 新 GATT event-only/Prepared Write 语义不完整 | 当前 native cache 已丢失事务 | B-07 |
| 公开 close 等待外部 view 会死锁 | 当前 CSI close 已经阻塞 view | F-06 保留机制，W/B 延续 |
| API map 只列函数不覆盖参数、状态和实测 | 当前所有公共 API 都不可信 | W-00/B-00 |

两份功能文档会明确替换这些设计条款，不要求开发者同时遵循互相冲突的新旧说明。

## 16. 提交顺序与交付物

| 建议提交 | 内容 | 禁止夹带 |
| --- | --- | --- |
| F-A | 基线、证据模板、生产 helper 测试入口 | 新公共 API |
| F-B | Radio 错误状态和 lease 最小修复 | 完整 restart/多 owner 政策 |
| F-C | callback/Future/cleanup 后缀与队列饱和修复 | BLE 全文件拆分 |
| F-D | BLE timeout、CSI retained storage、ESP-NOW 回归 | wire 格式替换 |
| F-E | 输入/GC/秘密/示例与声明一致性 | 高级 BLE/Wi-Fi 能力占位对象 |
| F-F | 三 target、硬件结果、残留风险与交接 | 把 not-run 写成 pass |

每个提交说明问题证据、修改的 invariant、实际测试及未运行测试。没有复现的问题，只增加测试和说明即可；不为了“完成工单”强行修改已正确的生产代码。

## 17. 本阶段完成标准

### Gate F-CORE：可以开始后续重构

已新增 BLE timeout 取消失败存储保留、扫描/广播 callback cookie 与退出屏障修复，
以及排队广播请求的 generation 重验。实际 MQuickJS 测试还复现并修复状态转换的
分配失败传播、移动 GC 引用与属性 helper 写入 exception sentinel 问题。
连接交接失败、入站队列丢弃/关闭及未完成 receive 的清理已有生产回归；终止失败保留槽位，显式清理可重试。
已补连接句柄复用竞争、失败状态断连清理及断连/配对先完成 Future 的回归；GATT snapshot 已有实际 GC 测试。
F-08 的 payload、ByteView/Source、CSI 合成转移及 capture 失败测试已经收尾；
F-CORE 通过，W-01 可以开始。见[最终证据与测试边界](investigations/2026-09-08-fcore-closeout.md)。

- [x] F-00 已保存可复现基线。
- [x] 已确认的 P0 缺陷完成修复；风险项有测试或明确阻塞说明。
- [x] callback admission、Future settle、native cleanup 和数据 storage 的边界明确；转换失败的 API/allocator 边界注入见最终证据。
- [x] 现有控制状态在观察队列饱和时仍可达终态（生产分支测试与完成路径核对；真实 RF 压力单列）。
- [x] Radio lease 和 BLE operation identity 有生产测试；耗尽不回绕。
- [x] CSI 原生及实际 VM retained storage 测试通过；真实 RF 仍单列。
- [x] BLE timeout 逐操作副作用已与源代码同步记录。
- [x] ESP-NOW 现有 recovery/queue retain 的 Host 回归通过；双机 RF 未运行。
- [x] Host C 65/65、Python 407/407、三 target 与 C5 disabled/NAN-Sync build 有记录。
- [x] 公共 API 不提前宣称后续新能力已经实现。

### Gate F-HARDWARE：可宣称现有修复完成硬件验收

按用户最新安排，500 次完整生命周期、长时间内存/吞吐与共存测试统一在所有功能完成后集中执行，当前为 deferred / not-run；不阻塞 W-00 输入采集，不改变 feature 稳定等级。短竞争与分配失败测试不在延期范围内。

C5 新固件已完成四组预热 + 5 次关闭/重开与 GC、两次 pending 无线 Future restart
及一次空载对照。惰性 getter 取样和原生任务回收时机问题已修复，去探针正式固件复测通过；
见[内存调查](investigations/2026-09-07-runtime-heap.md)。真实 RF/对端/500 次完整生命周期
仍未执行，其余竞争/分配失败覆盖缺口保留。

- [ ] 当前可用能力的 device/RF 测试有真实结果。
- [ ] 生命周期、低内存、对端消失和共存测试通过。
- [ ] 清理后的资源账本无无法解释的残留。
- [ ] 稳定性文件只按取得的证据更新。

F-CORE 通过而 F-HARDWARE 未通过时，可以继续受控重构，但必须保留各 feature 原有的 Candidate/Hardware pending 等真实状态及尚缺证据，不得宣称无线底座已经完成生产稳定性验证。

## 18. 来源与追溯

**用户提供的设计来源**：

- **S-W**：`esp32qjs_wireless_api_v1_complete_wifi_design(1).md`，重点为原第 4、24～27、30～32 节。
- **S-B**：`esp32qjs_ble_api_v1_development_design(1).md`，重点为原第 4、17、24～30 节和附录 A。

**已读取的固定版本仓库/上游来源**：

- [R-AGENTS]：项目结构、Build Context、命令、ES5-like 方言与唯一 v1 约束。
- [R-CONCURRENCY]：现有 callback、队列、Future、CSI owner 和 teardown 约束。
- [R-STABILITY]：基线的 Candidate/Hardware pending 及证据范围。
- [R-RADIO]：初始化/启动 once 状态、lease 验证和信道管理。
- [R-BLE]：重点定位 `ble_future_on_timeout()` 与 Future storage release；引用基于前一轮源码读取。
- [R-CSI]：重点定位 `wifi_csi_finish_close()`、`wifi_csi_maybe_destroy_resources()`；引用基于前一轮源码读取。
- [U-RX]：固定 IDF 的 RX timestamp、CSI buf/hdr/payload 字段。

本文中的 task ID、优先级、测试文件名、Gate 和新增状态/资源约束均为本轮建议，不是源码现状。

[R-AGENTS]: https://github.com/99percentpeople/esp32qjs/blob/9a74f1197d53863e079c30f8559ccd5b6cd60b42/AGENTS.md
[R-CONCURRENCY]: https://github.com/99percentpeople/esp32qjs/blob/9a74f1197d53863e079c30f8559ccd5b6cd60b42/docs/wireless-concurrency.md
[R-STABILITY]: https://github.com/99percentpeople/esp32qjs/blob/9a74f1197d53863e079c30f8559ccd5b6cd60b42/docs/feature-stability.json
[R-RADIO]: https://github.com/99percentpeople/esp32qjs/blob/9a74f1197d53863e079c30f8559ccd5b6cd60b42/components/esp32_mquickjs/src/modules/wifi_radio/esp32_mquickjs_wifi_radio.c
[R-BLE]: https://github.com/99percentpeople/esp32qjs/blob/9a74f1197d53863e079c30f8559ccd5b6cd60b42/components/esp32_mquickjs/src/modules/ble/esp32_mquickjs_ble.c
[R-CSI]: https://github.com/99percentpeople/esp32qjs/blob/9a74f1197d53863e079c30f8559ccd5b6cd60b42/components/esp32_mquickjs/src/modules/wifi_csi/esp32_mquickjs_wifi_csi.c
[U-RX]: https://github.com/espressif/esp-idf/blob/v6.1/components/esp_wifi/include/local/esp_wifi_types_native.h
