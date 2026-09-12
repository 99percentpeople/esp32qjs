# Wi-Fi C5 合并镜像短时验收

本记录是当前结果入口；此前阶段文档中的各批次按原镜像保留。本记录随 Wi-Fi 功能增量提交，公开能力保持 Candidate / v1。长时间 soak、500 次完整生命周期和 BLE 扩展
按用户安排后置；缺少对端的 RF 验收没有因此记为通过。

## 镜像与设备

- 验证基线：`d7db8d1` 加 Wi-Fi 工作区增量；上述镜像之后仅同步覆盖证据元数据及测试断言。
- 固定 ESP-IDF：`fff9895c82d744c7237be8847347bdd1b07c6643`。
- XIAO ESP32-C5：`hw-10bda3c854e8`，MAC `10:BD:A3:C8:54:E8`；8 MiB Flash、
  8 MiB quad PSRAM。通过后端重新探测 ROM，仅操作此设备。
- Artifact：`9e69784053b43164746280641f9b60b68b7a1cf8c1911881febbf3307566e71c`。
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
2. **后台 worker 污染 JS wait/watchdog 和串口 RPC。** worker 原来改动 JS deadline /
   native wait，并对未注册任务调用 watchdog reset。旧串口原始数据仅移除十条该
   错误日志后，生产 CRC/reassembly 解码恢复了原请求的成功回复。修复限定 runtime
   wait、JS heartbeat 和 watchdog reset 的调用任务，worker 继续响应停止控制。
   两项生产回归先失败后通过，相关 31 项和 Host C 138 项通过；修复镜像正常回复。
3. **LR / 普通 TX 速率、Driver 配置恢复及 CSI 对应包。** 前批修复保留，见
   [阶段入口](2026-09-12-wifi-stage-entry.md)对应生产回归和 SDK 证据。
   下表给出本次合并镜像的实际复测，不沿用旧镜像通过记录。

## 当前镜像实机结果

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

## 软件与剩余门槛

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
- 剩余为最终覆盖元数据镜像核对；主体实现、测试及必要 Host 适配分别提交。C3/S3 实机、配套对端要求的
  Enterprise/DPP/WPS/NAN/Mesh/FTM/TWT 等 RF、ESP-NOW 双机、受控 CSI RF/共存、
  独占 USB 和外部 tshark 资格仍为 not-run；BLE 和长时间 soak 后置。
