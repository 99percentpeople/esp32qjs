# Wi-Fi C5 合并镜像短时验收

本记录是当前结果入口；此前阶段文档中的各批次按原镜像保留。Wi-Fi 功能及本轮可执行的短时验收已收尾，公开能力保持 Candidate / v1。长时间 soak、500 次完整生命周期和 BLE 扩展
按用户安排后置；缺少对端的 RF 验收没有因此记为通过。

## 镜像与设备

- 实现提交：firmware `70f4e88`，最后的 Station START 并发补充为 `388069d`；
  必要 Host Build Context / CSI 诊断适配为根仓库 `27fa8dd`。
  下述十五项功能和六次 runtime restart 对应合并镜像 `9e697840…`；
  提交镜像 `20f988e2…` 的最终四项定向复测另列，不混算为同一镜像的全量结果。
- 固定 ESP-IDF：`fff9895c82d744c7237be8847347bdd1b07c6643`。
- XIAO ESP32-C5：`hw-10bda3c854e8`，MAC `10:BD:A3:C8:54:E8`；8 MiB Flash、
  8 MiB quad PSRAM。通过后端重新探测 ROM，仅操作此设备。
- 前一验收 Artifact：`9e69784053b43164746280641f9b60b68b7a1cf8c1911881febbf3307566e71c`。
  Build job `493db054fd8b93cb693604d1`、Flash job `b957d583fc0856ea6c8d357b`
  均 succeeded；写入校验、Agent 重连通过。
- app：3,133,312 / 3,145,728 bytes；原功能集、SRAM 优化和分区保留。
  workspace offset 3,735,552、size 4,653,056；刷写使用 preserve。

## 已确认的修复

1. **后创建的 Station IP 接口漏掉启动。** Monitor/Raw TX/ESP-NOW 可以先启动
   物理 Station。随后 `wifi_init_helper` 原来仅同步 started 标志，未执行网络栈
   START。实际 `csi-packet-hardware-51b58537` 崩溃，MEPC=0、RA=0x4215c2ac；
   对应旧镜像 `ae20d5b9…` 的精确 ELF 定位到 `wlanif_input` 调用空 input。
   生产后缀回归先失败；修复先等待 Radio START/fence，补 MAC、netstack buffer
   回调和有返回值的 netif start，再发布状态。已有真实 START 时不重复 netif_add；
   AP-only 不误设 Station started，错误保留实际阶段并走现有清理后缀。
   新回归及相关 29 项通过。原崩溃顺序和独立后创建联网均实机通过。
   提交后的并发审查又补了 START 已发事件但原生调用尚未返回的窗口：早期 started
   快照可为 false，现于最终 Radio START/fence 后再次确保 netif 就绪；失败沿同一
   helper 清理后缀退出。新增生产后缀回归先失败后通过，两个 netif 回归及相关 29 项
   通过；这项新增用例在上述完整 Python 运行之后执行，不计入该次 1252 项。
   见 `late-station-in-progress-before/after/related.log`。
2. **后台 worker 污染 JS wait/watchdog 和串口 RPC。** worker 原来改动 JS deadline /
   native wait，并对未注册任务调用 watchdog reset。旧串口原始数据仅移除十条该
   错误日志后，生产 CRC/reassembly 解码恢复了原请求的成功回复。修复限定 runtime
   wait、JS heartbeat 和 watchdog reset 的调用任务，worker 继续响应停止控制。
   两项生产回归先失败后通过，相关 31 项和 Host C 138 项通过；修复镜像正常回复。
3. **LR / 普通 TX 速率、Driver 配置恢复及 CSI 对应包。** 前批修复保留，见
   [阶段入口](2026-09-12-wifi-stage-entry.md)对应生产回归和 SDK 证据。
   下表给出本次合并镜像的实际复测，不沿用旧镜像通过记录。

## 合并镜像实机结果（9e697840）

原始记录根目录为 `build/wifi-stage-tests/hardware-runs/`。

| 用例 | 运行目录后缀 | 结果 |
| --- | --- | --- |
| Station/AP LR 临时发送及速率恢复 | `raw-tx-lr-hardware-c793394c` | passed |
| Driver 预热一次、三次重建/停止 | `driver-lifecycle-hardware-b52589b3` | passed |
| Station/AP 普通 TX 临时速率及恢复 | `raw-tx-rate-hardware-5f5ab023` | passed |
| Monitor 饱和、保留视图/Source、GC 与 pool 归还 | `monitor-retained-hardware-7e5417cd` | passed |
| 原组合顺序后的 CSI 对应包 | `csi-packet-hardware-eb71d2d1` | passed |
| CSI Batch 文件与 RPC 传输 | `csi-batch-transport-hardware-c7d9bddd` | passed |
| Monitor 先启动、后创建 Station 联网 | `late-station-netif-hardware-b08e90be` | passed |
| ESP-NOW offline | `espnow-offline-2fc2430a` | passed |
| ESP-NOW / Wi-Fi 共享 Radio | `espnow-wifi-radio-hardware-1477b49a` | passed |
| Wi-Fi offline / 扫描 | `wifi-offline-1eba04e7` | passed |
| Wi-Fi Future 联网、IP 与断连 | `wifi-network-22f93577` | passed |
| Raw TX 单次 / Batch / 有限周期 | `raw-tx-hardware-5be935ff` | passed |
| SoftAP / APSTA 生命周期 | `ap-lifecycle-hardware-8d3124be` | passed |
| Monitor 配置替换 / 关闭 | `monitor-configure-hardware-a5b21346` | passed |
| Monitor Batch / wire | `monitor-batch-hardware-05f6d8b0` | passed |

独立后创建用例首次 `cc56f8b4` 在 finally 清理中直接 stop 已连接 Station，被现有
准入规则拒绝；测试改为先断连再 stop 后通过，没有放宽生产保护。原失败保留。
独占 USB 原始数据传输未执行，以免占用 Agent 串口；文件/RPC 通过不能替代它。

## Runtime restart 与静止状态内存

保留的实机准备程序为
`tests/js/templates/wifi-runtime-restart-prepare.js`。Host 为模板注入唯一全局 owner
key 与 `monitor` / `shared` 场景，确认原生 Future 已进入 pending 后请求 runtime
restart；必须另查实际 generation、bootId 和资源，不能把接受回执视作完成。

- `runtime-restart-pending-61f1b998`：Monitor 两次，CSI+ESP-NOW 两次。Monitor 保留
  View/Source、停止接收但保持 Session 和 pending receive；共享场景保留两个 Session
  与各自 pending receive。四次均完成，runtime generation 1→5。
- `runtime-restart-quiet-feb5dfc9`：另做两次共享场景，generation 5→7；每次待启动状态
  healthy 后按相同静止条件取样。六次 bootId 均为 `aa81f732f2089b17`，没有 MCU 重启。
- 每次完成后无线 owner/active operations 为零、无 fault/lifecycle reservation、CSI
  reservedSlots 为零、Monitor Session 记录清空；旧全局 key 不再存在。
- 两次 healthy 样本：internal free 差值 −12 bytes，PSRAM free −8 bytes；两者 largest
  block 差值均零。完整 memory manager 账本相等：wireless internal reserved 13,496
  bytes、PSRAM reserved 576 bytes。该共享账本也包含 Agent 使用的通用 Future 服务，
  两次均有 2 个正常 pending Future，不能要求整个共享服务归零。
- 较早的 200 ms 样本控制账本相差 48 bytes，未用作“账本完全相等”的证据；后续三个
  独立静止读取相同，且上述两次 healthy 生命周期样本再次相等。短测不证明长时无泄漏。

全部 15 项功能短测结束后再次检查，Radio 健康停止、owner/operation/pool 归零。
原 `index.js` 为 2,752 bytes，SHA-256
`7ebfb188f0fd0c8e7d53e9774aaff2d08d14ea84613cf66d3a804854e1b0e1d4`；刷写前后核对
一致，启动状态 healthy、safe mode=false、failureCount=0。仅删除本次拥有的临时文件。

## 提交镜像最终核对（20f988e2）

- Artifact：`20f988e2d5b192aefe66f63a6168c7c91a882621749cd301440ffc5d52d9f91b`，
  不可变 Build Context 的 firmwareCommit 为
  `388069dc2b63b657cc0a9b497c6e4210f43dbdf5`。
  Build job `0ae56663748152ac542eef0e`、Flash job `72dbe53b96e32564b3c43c90`
  均 succeeded，写入校验及 Agent 重连通过。后端另列的
  `artifactIdentityConfirmed` 为 false，未把重连当成设备独立镜像身份认证。
- app 为 3,133,376 / 3,145,728 bytes，剩余 12,352 bytes。原功能集、分区、
  SRAM 优化及 workspace 保留；没有因测试删减功能或扩大应用分区。
- 实机 `wifi.diagnostics.idfApiCoverage()` 的 map SHA-256 为
  `c829806899f77bcd3d10e66557708bf604873bdb59225fdcc8e4cc30294d13b0`，manifest
  SHA-256 为 `239d0fec362f865f58034119feddcf46aef043ae90e72d3da54947687f03fae3`，
  均与当前文件相等，target 与固定 SDK revision 相符。
  合并返回完整 coverage、Radio 和启动信息超过 Host exec 的 16,384-byte 结果上限，
  返回明确 `TOOL_ERROR`；随后仅返回所需字段完成核对，没有调整生产限制。
- 最终四项定向复测全部 passed，原始目录仍为 `hardware-runs/`：

| 用例 | 运行目录后缀 |
| --- | --- |
| Driver 生命周期 | `driver-lifecycle-hardware-a19b6b18` |
| Monitor retained / GC / 饱和 | `monitor-retained-hardware-1a7df86e` |
| 前两项后的 CSI 对应包 | `csi-packet-hardware-e5c1f485` |
| Monitor 先启动、后创建 Station 联网 | `late-station-netif-hardware-64de4354` |

四项都完成临时文件清理，无 cleanupError。bootId 始终为 `5a6624f5569e9b97`，
runtime generation=1，无意外重启；原生并发时间点由生产后缀调度回归证明，
不声称实机强制命中了该时间点。最终 Radio stopped，owner/operation 为零，
无 fault/restartRequired，CSI reservedSlots=0，Monitor Session 数为零。
启动 healthy、safe mode=false、failureCount=0；`index.js` 大小与上述 SHA-256 不变。
最终静止状态 internal free=92,523、largest=63,488 bytes，PSRAM free=3,970,684、
largest=3,932,160 bytes；这次单点读取不作为跨镜像内存差值或长时无泄漏证明。

证据见 `build/wifi-stage-tests/hardware-preflight/committed-fence-*`。
最后的 START 并发补充另经 S3 WPS registrar、C3 NAN/USD、C5 no-SoftAP、
C5 全关闭四项增量构建通过；没有为文档收尾重复全量构建或重跑此前全部短测。

## 软件与剩余资格门槛

- 当前 C5 Board 构建，以及 S3 WPS registrar、C3 NAN/USD、C5 no-SoftAP、C5 全关闭
  四项受影响配置构建通过。此前八配置矩阵证据独立保留。
- 新 netif 回归、相关 29 项、测试选择 2 项、MQuickJS 和四项生成物检查通过。
- 本次完整 Python 执行 **1252 项 / 1730.561 秒**，原终态 **2 failures / 1 skipped**。
  两个失败均为旧的硬件测试断言（CSI 用例数 7→8、USB 分支需明确独占授权），
  已先单独复现再修正，相关 **26/26** 通过。生产行为没有因这两条断言改变；
  不把原全量失败记录改写成一次全量绿色。外部 tshark 缺失的 skip 保留。
- SDK 覆盖表的 165 个条目已按其明确引用、实际执行的 Host fixture 更新证据；
  其中仅 15 个已有 reviewed 契约、字段和实际注册的条目提升为 implemented。
  其他条目保持原审查状态，硬件资格不提升。生成检查及覆盖工具测试通过。
- 必要 Host Build Context 五项测试与 CSI 诊断两项测试通过；不构建前端。
- 主体实现、测试、必要 Host 适配已分别提交，最终提交镜像核对及定向短测完成。
  这完成本轮可执行的 Wi-Fi 功能短时验收，不代表第二阶段所有硬件资格冻结。
  C3/S3 实机、配套对端要求的
  Enterprise/DPP/WPS/NAN/Mesh/FTM/TWT 等 RF、ESP-NOW 双机、受控 CSI RF/共存、
  独占 USB 和外部 tshark 资格仍为 not-run；BLE 和长时间 soak 后置。
