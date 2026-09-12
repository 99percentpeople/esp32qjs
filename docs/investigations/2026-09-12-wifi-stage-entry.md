# Wi-Fi 实现收尾与阶段测试入口

当前实现、测试与交付状态统一见
[2026-09-13 C5 短时验收](2026-09-13-wifi-c5-short-acceptance.md)和
[当前剩余工作](2026-09-08-wifi-api-remaining.md)。本批 15 项 C5 功能短测、6 次 runtime
restart、workspace 与静止状态资源核对通过；Python 完整执行及旧断言修订证据单独列出。
实现已提交为 `70f4e88` / `388069d`，最终提交镜像 `20f988e2…` 已刷入并通过四项
定向复测、覆盖元数据与 workspace 核对；Host 适配提交为根仓库 `27fa8dd`。
公开能力仍为 Candidate / v1，缺少对端的 RF、BLE 和长 soak 不记为通过。

以下保留实现及测试过程中各批的原始状态；其中“当前”“最新”“未刷写”“待测”均
指该段对应批次，不替代上方最新验收记录。固定 SDK 仍为
`fff9895c82d744c7237be8847347bdd1b07c6643`。

## 历史阶段执行结果


**最新修复批次（2026-09-13）：合并镜像 `ae20d5b9…` 的 LR、Driver lifecycle、普通速率和 Monitor retained 四项实机通过；CSI 联网发现新的 Station 后创建崩溃，修复已编码、待实机复测。**

- 后台 worker 原本进入 JS native wait/deadline，并对未注册的任务调用 watchdog reset。
  实际 USB 数据中十条 `task not found` 日志污染 RPC；离线仅移除这十条日志后，
  原请求的 CRC 合法成功回复恢复。生产修复限定 runtime wait/deadline、JS heartbeat
  和 watchdog reset 只由绑定 runtime 任务操作，worker 仍响应停止控制。
  两项回归先失败后通过，相关 31 项、Host C 138 项及四配置增量构建通过；
  `ae20d5b9…` 实机正常回复，最新原始串口记录该错误为零。
- `csi-packet-hardware-51b58537` 在上述四项之后触发真实 panic，bootId 改变；
  精确 ELF 将 MEPC=0/RA=0x4215c2ac 定位到 `wlanif_input` 调用空 `netif->input`。
  Monitor 已启动物理 Station，但后创建的 IP helper 错过 STA_START，原代码只设
  started 标志。生产后缀回归先失败；修复加入 Radio START/fence、MAC/缓冲回调/
  checked netif start，再发布状态，并排除 AP-only、避免真实 START 已执行时重复 add。
  新回归及相关 29 项通过，新增独立联网实机回归已登记；修复镜像尚未实机验证。
  原失败请求保留 UNCERTAIN_MUTATION，设备重启后仅删除了本次拥有的临时测试文件。
- 下方 `022f083f…` 等结果属于此前批次。最终合并镜像的 CSI/ESP-NOW、runtime
  restart、资源及 workspace 核对仍未完成，Wi-Fi 增量未提交。

**当前（2026-09-13）：已进入合并镜像短时验收。停止态速率、AP 保存配置和 LR 速率缺陷均已修复，Station/AP 原生发送完成与关闭恢复已实机通过。**

- 当前 XIAO C5 Artifact `022f083f…`（app 3133040 / 3145728 bytes）已 preserve
  刷写，写入校验和 Agent 重连通过；保留原功能、SRAM 优化和 workspace 分区。
  刷写前原 `index.js` 的 2752 bytes / SHA-256 与首次基线相同。最终测试后的
  workspace、预热内存和资源账本仍须集中核对。
- `raw-tx-rate-hardware-dc8690ab` 在前一合并镜像 `26a8a443…` 通过：Station/AP
  原生完成均报告临时 9 Mbps，关闭后恢复为 6 Mbps，Session/lease 归还。
  修复涵盖 STOP 后 SDK 接口对象已释放、fresh AUTO 尚无选定 band，以及保存的
  WPA2 AP 带休眠 SAE BOTH 字段。后者只在保存配置校验时保留该字段，用户输入、
  密码、PMF/WPA3 和精确恢复检查没有放宽；相关 18 项生产路径回归通过。
- `raw-tx-lr-hardware-463cfd00` 在当前 `022f083f…` 通过：Station/AP 均完成
  临时 500 kbps 与恢复后 250 kbps 的实际发送，原生完成分别报告 SDK rate 42/41；
  测试结束恢复原协议集和 6 Mbps。没有对端接收/距离/吞吐资格证明。
  固定 SDK 通用速率校验读取了不同于协议 getter 的 LR 字段，并把 LR rate 41/42
  送入 legacy <=15 校验。生产 bridge 对目标接口读取 LR 启用状态并验证频段、
  精确速率和标志，再使用原 SDK writer；不改写请求速率、不代为启用 LR。
  真实 C3/C5 SDK 执行回归先在断言 40 失败，修复后通过；覆盖另一接口启用不足以
  授权、无效速率/标志、5 GHz、getter 失败、STOP/live 和 fresh AUTO。
  见 `tx-rate-lr-before.log`、`tx-rate-lr-after.log`、`tx-rate-lr-related.log`。
- CSI packet 的 ROM 符号覆盖缺陷已修复并实机通过：原 8 个 callback 对应包全部
  unavailable，修复后 `csi-packet-hardware-615299fd` 为 4 accepted、
  packetUnavailable=0，492-byte IQ / 72-byte packet / 892-byte wire，最后 owner
  释放后归还 pool。`csi-batch-transport-hardware-d2dac78e` 在 `26a8a443…` 通过：
  文件 556 bytes、RPC 591 bytes；独占 USB 原始传输未执行，避免占用 Agent 的 USB。
- Monitor Batch、关联 CSI、Raw TX 单次/批量/有限周期、SoftAP/APSTA 和 Driver
  短循环已有历史实机通过记录。APSTA 配置替换保持原 Station 连接/IP 与 generation。
  这些结果按各自镜像记录，最新合并镜像的回归仍在推进。
- 最新 `tx-rate-lr-targets/` 的 S3 WPS registrar、C3 NAN/USD、C5 no-SoftAP 和
  C5 全关闭四项增量构建通过，C5 正式 Board 增量构建通过；LR 相关 8 项回归通过。
  Wi-Fi 增量未提交；文档与验收账本收尾、runtime restart/ESP-NOW 共享基础检查继续。
  BLE、长时间 soak 和缺少对端的 RF 验收没有执行，公开能力仍为 Candidate。

以下保留较早的 Driver 修复与各批次证据。

**Driver 实机门槛（2026-09-13）：Monitor 实际收包、饱和及 retained/GC 通过；Driver 重启的 AP 默认配置读回缺陷已修复，完整短循环实机复测通过。**

- 无 probe Artifact `be7d7e3d…` 构建及 preserve 刷写成功，app 3131728 bytes，
  原分区及功能保留。Driver 实机运行 `driver-lifecycle-hardware-5318ecfb` 通过：
  预热 1 次后连续 3 次重建/启动/停止，generation 2→5，扫描参数保持不变，
  owner/active operations/checkpoint bytes 均归零，无 fault；internal 与 PSRAM
  free 各减少 12 bytes，largest block 均不变，无线预算账本不变。
  原 `index.js` 的 2752-byte 长度及 SHA-256 再次与首次刷写前相同，safe mode=false。
  见 `driver-lifecycle-ap-fixed.log` 和 `hardware-preflight/ap-default-fix-postcheck.json`。

- 合并三项修复的 Artifact `c01babe6…` 刷写 job `2766d092714e2dc9071c9259`
  已成功，写入校验和 Agent 重连通过。Monitor retained 实机运行
  `monitor-retained-hardware-04005b55` 通过：2 frames、28 pool drops、72 queue drops，
  Batch/Session 关闭及 GC 后保留 View/Source 有效，848-byte wire 写入成功，
  最后 owner 释放后 pool 归还；自建临时文件已删除。
- 同一镜像的 Driver restart 越过信道和 MAC 原失败点，但在
  `restart-ap-readback` 失败。临时诊断镜像 `f144d485…` 实机确认唯一数值差异为
  默认开放 AP 的 `pmf_cfg.capable` 从快照 true 变成写回后的 false；SSID、密码
  相等，只记录比较结果，未记录秘密内容。诊断原始证据见
  `hardware-runs/restart-ap-readback-probe-evidence.json`。
- 新回归调用生产配置恢复 helper，模拟实际 SDK 规范化，先复现读回失败。
  修复先读取新驱动 AP 配置，若与快照完全相同则避免重复写入；不放宽 PMF、
  认证或密码的精确校验，读取失败不继续写入，临时 probe 已从源码移除。
  相关八项定向回归通过（`restart-ap-default-after.log`），无 probe 镜像的 Driver
  实机复测也已通过。其他短时功能验收及最终受影响配置检查仍需继续，不能据此
  宣称全部 Wi-Fi 或缺少对端的 RF 项目通过。

以下保留前几次构建及故障定位过程；其中“正在进行／尚未刷入”是对应批次的历史状态。

最新修复补充：

- 信道 enum 初始化修复已在实机越过原失败点；随后重放在 mode=off 时写 MAC，
  SDK 返回 `ESP_ERR_WIFI_MODE`。固定 SDK 的 `wifi_set_mac_process` 要求 mode
  包含目标接口，`_do_wifi_start` 另检查物理 START 标志。新增独立 mode 选择/
  readback 后缀，再恢复 MAC；保持物理停止、RAM storage、精确重试和 swapped
  MAC 保护。SoftAP 开/关生产回归先失败后通过，相关七项完整定向回归通过。
- 临时一次性 RX probe 实测 `type=0 prefix=64 sig=372 dump=376 format=0 channel=4`。
  公开 SDK 明确回调 payload 长度由 `sig_len` 描述。适配器现接受已观测的
  `dump_len=sig_len+4`，只读取较小的 `sig_len`，保留原始两个长度，不推断 FCS。
  保护页回归先失败；修复后 RX/filter/Monitor/wire 八项通过，不读取额外尾部，
  其他不一致长度仍拒绝。临时 probe 已从源码移除。
- Wi-Fi network 实机通过：Future 连接、IP readiness、NTP、缺失 SSID 失败后
  重连、断连及完成结果不变。凭据使用现有测试配置，不写入运行请求证据。
- 合并修复 Artifact `c01babe65d20d1478fee4fdc614a3ec8352b91ae3f6eca9edafdcce318334a47`
  构建通过，app 3131680 bytes，分区及功能不变。刷写 job
  `2766d092714e2dc9071c9259` 正在进行；尚未将 Driver/Monitor 实机缺口标记通过。

新证据：`restart-mac-before.log`、`restart-mac-after.log`、`rx-span-before.log`、
`rx-span-final.log`、`hardware-runs/rx-length-probe-evidence.json` 及各实机运行目录。
`rx-span-after.log` 曾误列两个不存在的测试模块，命令失败已保留；正确模块组合
的最终通过证据为 `rx-span-final.log`，不能把前者改写为成功。

- 原 Artifact `ac3028c…` 的四个镜像写入校验和 Agent 重连均成功；bootId 为
  `bf4c199a5da7769e`，workspace 挂载、safe mode=false、启动失败计数零。
  原 `index.js` 仍为 2752 bytes，SHA-256 与刷写前相同。见
  `build/wifi-stage-tests/hardware-preflight/flash-postcheck.json`。
- Wi-Fi offline 实机通过，扫描返回 32 条记录；当前 SSID 输入支持文本/ByteSource，
  原测试的“string SSID”错误文案已过时。实机确认数字仍抛 TypeError，Radio
  generation/owner/started 不变；测试同步契约，未放宽生产校验。
- Monitor configure 实机通过（10 次替换/启停、pending receive、pool/lease 归还）；
  CSI offline 通过；实际 Host 生成的 CSI lifecycle 程序预热 1 次后执行 10 次，
  internal free/largest、DMA largest、PSRAM largest 差值均为零，CSI owner 归零。
- 新增 Driver lifecycle 实机在首轮 restart 的 `restart-band-channel-snapshot`
  返回 `ESP_ERR_INVALID_RESPONSE`。已保存实际 ELF 反汇编：固定 C5 SDK 的
  current/home channel getter 均只写 secondary enum 的低字节，原快照局部变量
  未初始化。生产 helper 加单字节 SDK 边界和 pattern 栈填充后先复现失败；初始化
  完整输出后，restart/config/STOP/AP/source 六项回归通过。C5 修复 Artifact
  `ec353db…` 构建通过（app 3131648 bytes），尚未刷入。
- 故障后的 runtime restart 已实际完成：bootId 不变，runtime generation 1→2、
  Radio generation 1→2，原生 owner、checkpoint bytes、lifecycle reservation 归零。
  这证明本次已知故障的 teardown，不宣称所有 fault 或 once-init 失败都能恢复。
- Monitor retained 测试首次因无 Batch 被跳过；进一步实机观测排除了“没有流量”：
  信道 4、1.5 秒内 63 callbacks 全部计入 `filtered.invalidCallback`，accepted=0。
  该项现记录为失败待修复，测试也拒绝把已收到但全部无效的回调记成环境跳过。
  当前正在构建仅一次、只记录长度/格式等非秘密元数据的临时 probe；必须移除 probe
  后再交付。尚未改变读取跨度或放宽长度检查。

本批实机原始证据在 `build/wifi-stage-tests/hardware-runs/`；关联版本不可混用。
联网所需 `TEST_WIFI_*` 使用项目既有配置，联网实机用例已通过，不记录其值。
长时间 soak、BLE 和缺少对端的受控 RF 验收仍未执行。以下为此前软件门槛记录。

**软件门槛（2026-09-13）：八配置完整构建通过，开始保留 workspace 的 C5 刷写。**

- 完整 Python 实际执行 **1241 项**，原终态是 **3 failures / 1 skipped**，失败均为
  `test_wifi_monitor_diagnostics` 的 C3/S3/C5 场景 2。平坦队列对象只有一次对象分配，
  其数字/布尔子值不分配；旧断言却要求转换期间发生移动。现对返回后持有的对象
  进行真实 GC 并核对地址改变；生产转换没有修改。该模块全部场景及数字边界
  已在 `monitor-diagnostics-final.log` 定向通过。原全量失败记录保留，不能改写成
  一次全量绿色运行。运行期间的天线增量另有 12 项回归通过，CI/预算 19 项通过。
- 最新逐模块清单 185/185；Host C 138/138；MQuickJS 61 源码/69 片段；四项生成物
  检查通过。Host Context 与诊断脚本共 9 项通过，16 个实际生成的诊断程序通过
  MQuickJS 语法和 4096-byte 请求大小检查。Host CSI 脚本同步当前 v1 的 source /
  buffering 与 wire Source，内存取样物化惰性 region，生命周期先预热一次再比较。
  正例及注入 10 KiB 泄漏的反例调用实际生成程序；不作为原生/RF 证明。
- C3 NAN/USD、S3 WPS registrar、C5 WPS/band、FTM、roaming/WPS、no-SoftAP、
  ESP-NOW-only、全部关闭八项完整构建通过；最终逐配置记录及原始失败指向
  `build/wifi-stage-tests/matrix-completed.json`。
- XIAO C5 正式 Board 的未优化镜像超过现有应用分区 301456 bytes；该 Board 改用
  `CONFIG_COMPILER_OPTIMIZATION_SIZE=y`，Host 仅放行这个明确的 Board 编译选项。
  选定功能和原分区不变，最终 app 为 **3131632 / 3145728 bytes**（余 14096 bytes）。
  Artifact `ac3028cff8e2a56b55b48880ae799516ee6fa8fbe4f4fbc8309862fc28f05cec`
  核对为 C5 / 8 MiB Flash / 8 MiB quad PSRAM，workspace 仍为
  offset 3735552、size 4653056。原 `index.js` 的长度及 SHA-256 已保存。
  刷写 job 为 `2a6834095c4ac13921d73f25`，只使用 `preserve`；终态、重连和后续
  功能证据见 `hardware-preflight/`，本段不预先宣称实机通过。

上述软件收尾证据和改动 hash 汇总在 `stage-closeout-current.json`。外部 tshark、
缺少对端的 RF/共存及长时间 soak 仍分别为 not-run/deferred。以下保留各批次过程。

**2026-09-13 构建与 USB 准备更新：** 最后一个配置快照回归模块已通过；最新
逐模块台账为 185/185，完整 Python 仍在运行，两者不能等同。S3 WPS registrar
完整镜像已通过。C3 已完成编译/链接，但 3 MiB 测试应用分区不足；C5 完整链接
先暴露下述 GPIO 表问题，修复后同样达到分区大小校验。CI 模板的应用分区现为
4 MiB，storage 保持 512 KiB，余下空间为测试 workspace；不用于设备刷写。
模板/预算相关 19 项检查通过；其余功能组合与扩大分区后的构建仍在进行，见
`matrix-final-retry/`、`matrix-link-retry/` 和后续 `matrix-layout-final/`。

- **C5 天线寄存器访问修复：** SDK 的公共头文件声明 `GPIO_PIN_MUX_REG`，但 C5
  没有该表的定义；此前定向编译没有发现最终链接缺口。C5 改用实际
  `IO_MUX.gpio[pin].val` 保存/恢复，C3/S3 保留各自 SDK 路径。新增回归调用生产
  寄存器 helper，检查每个模拟 pad 的完整还原、相邻 pad 不变、禁止占用时拒绝，
  并保留 USB pull bits；连同原有事务/输入测试 **12/12 通过**。
  原始链接失败和回归前后日志分别在 `matrix-final-retry/c5-wps-band.log`、
  `antenna-mux-before.log`、`antenna-mux-final.log`。
- **Host Build Context 衔接：** `web/backend/src/firmware/board-store.ts` 生成无线
  Context 时缺少新固件要求的显式预算。生产 Context + 固件实际校验器先复现失败，
  现生成 internal/control 的 131072/16384-byte 上限，以及不超过物理容量的最高
  4 MiB PSRAM 上限。无 PSRAM 时为零，不预分配这些容量。Host 整组 **5/5 通过**，
  含 8 MiB/2 MiB/无 PSRAM 和不可变 Context 重现；原有 CSI 文档快照测试同步唯一
  v1 的 source 对象契约，核对完整原文，不再把单次读取上限当作文档大小上限。
  证据在 `host-context-budget-before-validation.log`、`host-context-budget-final.log`。
- **实机前置：** 仅操作 `serial-0b57b8d7de6a`。新 ROM 探测确认
  `hw-10bda3c854e8` / ESP32-C5 / 8 MiB Flash。ROM 未识别外接 PSRAM；随后 Agent
  自动重连并确认 8 MiB quad PSRAM、Board `seeed-xiao-esp32c5`、workspace 挂载、
  safe mode=false、启动失败计数零。原始取样、请求及身份记录保存在
  `hardware-preflight/`。没有刷写新固件，没有对另一块 USB 设备执行探测或操作；
  这些结果不属于新 Wi-Fi 实机验收。实际 Board Artifact 与保持 workspace 的布局
  仍需核对，不能刷写上述扩大分区的 CI 镜像。

最终 Python 运行期间新增了天线修复和对应测试，另调整 CI 模板。该进程不重启；
完成后保留 sourceBefore/sourceAfter 的差异，并用上述定向回归补足改动范围，
不能把它描述为源码全程不变的单次完整通过。

集中运行已经开始，以下是本轮实际执行结果：

- Host C：**138/138 通过**，`build/wifi-stage-tests/host-c-final.log`。
- Enterprise/restart 生产回归：**22/22 通过**，涵盖实际安装、精确身份、跨 generation
  失败清理、AP 协调、重试、runtime closing 与租约交付顺序；
  `build/wifi-stage-tests/enterprise-third.log`。
- MQuickJS 语法：**61 个源码／69 个文档片段通过**，
  `build/wifi-stage-tests/mquickjs-first.log`。
- 全部 Python 首轮：**1134 项，457 failures、27 errors、33 skipped，未通过**。
  `build/wifi-stage-tests/python-full.log` 与 `python-full.json` 保存终态；失败数
  包含不同配置的重复子用例，不能直接当作生产缺陷数量。
- C3/C5 的最新 Radio 生产文件重新编译通过；完整三目标、disabled、实机仍未执行。

### 2026-09-13 Driver / 恢复 / 配网回归继续收敛

**最新补充：C5 快照重试已修复，最终软件回归和镜像矩阵已启动。**
下面的 184/185 是修复前批次结果。`capture-retry-after.log` 中
`test_wifi_restart_configs` 的两个测试均通过，包含 C3/S3/C5 与 SoftAP 开/关；
同次另有 8 项相关测试通过。该命令误列不存在的 `test_wifi_mesh_radio`，整体退出
仍为失败，不能作为整个命令通过的证据；随后正确的四个 Mesh 模块在
`capture-mesh-after.log` 中 **24/24 通过**。

确认的生产原因是：捕获配置期间 STOP 失败后，精确后缀重试保留首个 fault，
临时 START 却进入普通扫描参数/TWT 等设置恢复，触发正常 fault 准入拒绝。
现在只让该内部临时 START 延后设置恢复；完整 checkpoint 已持有设置，pending
标记和首错继续保留，实际 driver 重建后再重放。普通 START、最终恢复和未知失败
的准入不变。新增生产回归验证 STOP 失败、首错保留、无提前设置写入、重复完成
不重做原生操作，以及物理重建后的一次设置重放。

最终运行记录在 `build/wifi-stage-tests/software-final/`：Host C **138/138**、
MQuickJS **61 源码/69 文档片段**及 manifest、feature 文档、SDK 覆盖、配置 schema
检查已通过；完整 Python 仍在运行，以该目录的终态记录为准。首轮完整矩阵在进入
有效编译前被错误 Python 环境和旧 Context 缺少显式预算挡住，失败保留于
`matrix-final/`。固定 IDF Python 环境，并在 `contexts-final/` 创建补齐预算的独立
Context 后，已重新启动 `matrix-final-retry/` 的八配置矩阵，每次两个编译任务。
实机仍未执行，Wi-Fi 增量未提交；长时间 soak 延后至 BLE API 完成。

最新逐模块台账为 **185 个模块中 184 个通过、1 个失败**。本批先重新运行旧台账
中的全部 24 个失败模块，再完成修复及依赖复查。`closure-rechecks/` 的 35 模块
合并运行为 33 通过、2 失败；随后 `radio-admission-ninth.log` 的 9 项通过，
使该批各模块最新结果成为 34/35。不能把这些分时结果写成完整当前代码全量通过。

本轮确认并修复三项生产问题：

- `wifi.start({mode: 1})` 等非法 mode/storage 值被拒绝时没有设置 JS 异常。
  `start-capture-before.log` 定位到该具体输入；现在设置 TypeError，并保留已经存在
  的异常。`test_wifi_start_options` 还覆盖嵌入 NUL、未知键、getter 抛出原值、getter
  触发 GC、逐分配失败及拒绝后原生 selection 清零；最新全部 6 项通过。
- Action/Raw TX/FTM 恢复在原生 owner 已退休后，stepper 留下的
  `runtime_cleanup_pending`/cleanup stage 会使 `wifi_init_helper` 拒绝重建。
  `recovery-handoff-before.log` 在生产恢复步骤复现；现在只在精确 lifecycle 仍有效、
  原生清理已完成后清除上一阶段诊断。中央配置 reservation 继续保管清理责任，
  重建失败重新记录 pending。`recovery-handoff-after.log` 的 3 项通过，覆盖成功、
  等待原 owner、取消不恢复、失败移交中央清理；runtime teardown 的组合回归也通过。
- BSS color collision detection 设置遗漏 Radio 私有 mutation 调用边界。新增用例
  在 `stop-mutation-before.log` 证明失败调用仍留下 `unchanged` STOP 快照；现在按
  未列入中性例外的 setter 政策，在尝试前使快照失效，保留 SDK 返回码。实际快照、
  setter 失败与回滚测试，以及调用清单一致性均通过。

其余修订属于测试证据修复：补齐实际 SDK/内部类型和 native wrapper helper；
对象字面量按表达式求值；内存清零观察接到当前 `memory_payload_free`；Future
注册表使用生产分块布局；FTM 的内部 `_locked` 调用在 operation mutex 内执行；
SmartConfig 事件屏障回调只要求 critical lock；广播协议定时器和清理屏障定时器
分别校验原本的 12345/1 微秒。连接同步 callback 注入改用准确函数提取，避免旧
字符串替换失效而漏跑完成事件。没有修改 SDK、MQuickJS 或已有 SRAM 优化。

唯一剩余失败模块为 **`test_wifi_restart_configs` 的两个 C5 子配置**（SoftAP 开/关）。
注入 capture STOP 失败后，精确快照的内部后缀重试仍保留旧 fault；临时 START
触发 TWT policy 恢复，其 fault 准入返回 invalid state。已定位调用链和失败阶段，
但重试与显式清理边界尚待确认，不能计作修复完成。C3/S3 子配置的冷 driver 场景
原夹具只清 `started`、未清 `stop_required`；改为完整停止状态后已通过。

C3/C5 既有不可变 Build Context 下，`wifi.c`、`wifi_config.c`、`wifi_radio.c` 三个
受影响生产单元均编译通过，记录在 `controls-recovery-compile/`。373 个 Python
文件 AST 与 `git diff --check` 通过。台账、885 个源码/夹具 hash、原始失败日志及
编译证据汇总在 `build/wifi-stage-tests/controls-closeout-evidence.json`。
本轮未执行完整 Python/Host C/MQuickJS/生成物最终回归、完整 C3/S3/C5/disabled
镜像构建或实机测试；未提交增量。长时间 soak 继续延后至 BLE API 完成。

### 2026-09-13 集中回归修订

- 合并定向复测 **30/30 通过**，见 `build/wifi-stage-tests/gc-ap-final.log`：
  AP 状态与重开准入、事件掩码、天线事务、CSI Frame/Batch/View/Source、通用
  ByteView/Source、Wi-Fi/ESP-NOW 状态与 scan、BLE payload，以及共享测试工具。
  CSI 包含合成 callback 进入实际 pool、旧 retained view/source 跨新 generation、
  迭代期间关闭、逐分配失败与真实 MQuickJS 移动 GC；这些不是 RF 或实机证据。
- 已修正共享测试的函数提取、SDK 类型/feature gate 和 allocator 边界组合。
  数字构造的故障注入现在遵循所链接 VM 的实际表示：短整数、预分配的负零及
  64-bit Host 的 inline float 不触发虚构的 GC/OOM；可分配数字仍逐次注入。
  独立用例先失败再通过，`numeric-injection-before.log` 保存失败。
- AP 状态的 SSID 字节转换原实现正确；错误来自对无分配的小整数构造强制 GC。
  CSI Source 的活跃 read lease 按现有通用契约返回 busy，测试确认拒绝 close 后
  仍可读取，iterator 退休并释放最后 owner 后旧 pool 归还。没有改动公共关闭契约。
- 事件掩码分别覆盖暂时读回错误后的成功回滚，以及持续读回错误时保留首错、
  回滚错误并拒绝后续写入。原测试持续破坏回滚读回，却预期 rollbackComplete。
  天线/AP 测试构造补齐实际 SDK 类型和安全配置默认字段，保留生产准入检查。
- 前一批架构/契约检查 **137/137 通过**，见 `architecture-final.log`；它们是
  静态约束检查，不与上述动态用例相加宣称完整覆盖。

首轮 discovery 已导入修改前的测试构造，首轮失败记录不能代表修订后的终态。
首轮未设置 `IDF_PATH`，33 项 skip 分布在 27 个模块，已纳入固定 SDK 的补跑。
失败分布在 152 个模块，汇总保存在 `python-initial-failures.json`。去除已完成
定向复测的模块后，158 个模块使用三个独立测试进程并行复查，约 220 秒完成，
其中 71 个通过、87 个仍失败；每模块的日志、进程、时间和测试输入 hash 在
`build/wifi-stage-tests/rechecks/`，本批结果不是最新代码的全量 Python 通过证明。

该复查后继续定向修复：

- `sdk-lifecycle-second.log`：**11/11 通过**，含实际 SDK EAP worker 退出/存储
  回收/失败重试、AP WPS 原生命令与退休，以及共享 C 提取器的注释边界回归。
  EAP 的文档注释中出现函数名，旧提取器把注释尾部当作函数开始；用例先失败后修复。
- `twt-third.log`：13 项通过，另有 probe Future 的构造错误；后者补齐生产
  information 状态转换与 suspend 上限定义后，`twt-future-fifth.log` **2/2 通过**。
  TWT 多 owner、取消、queue 饱和与原生退休使用生产 Radio/Future helper，SDK 和
  worker 调度仍为注入边界，不代表完整 ESP32 任务调度或 RF 验收。
- `nan-mesh-second.log`：**27/27 通过**，含 NAN security/vendor/USD 参数转换，
  Mesh 原生生命周期、控制、手动父节点/scan、retained receive 和 Session 清理。
  修正测试 allocator 签名和 USD 分支误检查 Vendor 输出；保持生产安全策略。
- Driver/PHY、NAN query/pairing/status 的输入构造已在 `input-vm-second.log` 通过。
  缺失 `JS_EVAL_RETVAL` 的测试之前丢弃表达式值；不支持的数组空位字面量改为
  显式 undefined 元素，继续验证拒绝非法值。EAP 还需把对象字面量作为表达式
  求值，并将清零观察接到当前 `memory_payload_free` 边界；修订后
  `eap-input-fourth.log` 通过全部参数/getter/逐分配失败场景。没有更改生产凭据策略。

前一批结束时，`build/wifi-stage-tests/triage-current.json` 汇总所跟踪 185 个模块
各自最后一次已结束的运行：111 个通过，74 个失败，无剩余 skip。该清单包含
后续修复结果，保留每项原始日志；既不是 74 个确认的生产缺陷，也不是整个
Python suite 的最新通过结果。以下记录该批之后的实际修复和复测。

高级功能剩余失败继续按依赖分组处理。完整 Python、最新镜像矩阵与实机验收均
未通过；长时间 soak 继续延后至 BLE API 完成。

### 2026-09-13 Monitor / Raw TX / NAN 与恢复回归收敛

上一批结束时逐模块台账为 **185 个模块中 161 个通过、24 个失败**，从本次开始的
111/74 收敛而来；这是各模块最后一次运行结果，不是完整当前代码的全量通过声明。

- Monitor、Raw TX、NAN 及关联 RX/离信道模块统一复测 **43 个模块全部通过**，
  每模块 argv、结束状态、测试输入 hash 与日志在 `build/wifi-stage-tests/family-rechecks/`。
  Monitor PCAPNG 的外部 tshark 检查因工具未安装跳过 1 项，外部格式资格仍为 `not-run`。
- Monitor 关闭/重开测试不再提前销毁生产资源继续使用的 pthread 锁；C5 组合测试
  使用既有 SDK 枚举并补入实际 VHT/HE 解码 helper。wire 测试区分可读长度与驱动
  报告长度：复制全部可读字节仍可能截断原始帧；保留错误标记的拒绝、原子输出、
  Host 解码与 PCAPNG 长度检查，未降低生产校验。
- Raw TX/NAN 测试补齐真实类型、当前 SDK feature gate、共享 owner 和 allocator
  边界。NAN 收尾显式调用队列关闭回调后再核对引用账本；Raw TX 周期测试分别验证
  非空输出 token 拒绝和时钟倒退。离信道未知 producer 始终保持 fault，不能因一个
  buffer 回收而释放 owner。FTM Future 测试处理取消/销毁已排入的清理任务，再推进
  原生结果和事件屏障；没有替换生产生命周期状态机。
- FTM Radio/Session/恢复、watch 原生转换/GC、TWT probe/setup result、Driver
  状态读回及 HE 统计快照的定向复测已通过。详见 `radio-watch-second.log`、
  `watch-third.log`、`driver-state-second.log` 及后续分组日志，逐模块归属由台账记录。
  MQuickJS 不执行该测试构造中的数组下标 getter，读出的 undefined 按非法事件项
  拒绝；真实 options getter 和数组读取仍进行 GC/逐分配失败验证。
- 扫描参数 ASAN 负对照增加 `-fno-builtin-memcpy`，使 SDK 的完整 44-byte 读取
  经过 sanitizer interceptor；原编译器内联读取没有报告跨越的栈 redzone。
  `scan-asan-second.log` 两项通过：未填充版本确实报告 stack-buffer-overflow，
  生产 44-byte zeroed union 通过且额外字节均为零。未改动原生产填充优化。

**确认并修复 DPP 恢复阻塞。** 恢复配置的 START 失败后，显式 `recover()` 会
重新要求 STOP/fence；此前 close 先查询 AP 关联状态，Station 尚不存在时 SDK
错误阻止进入 STOP。固定 C3 SDK 的 `wifi_get_ap_info_process` 存在返回
`ESP_ERR_WIFI_CONN` 的接口不存在分支，反汇编证据保存在
`radio-diagnostic/dpp-get-ap-info-process-esp32c3.txt`。原测试使用 NOT_STARTED，
现按该原生分支注入 CONN，`dpp-recovery-sdk-before.log` 仍复现失败。

生产 `wifi_radio_dpp_restore_locked` 只对**已获准的失败 START 恢复**跳过这次
关联读取，由原 STOP/fence 清理部分启动的 driver；普通关闭仍要求断连，恢复仍
校验精确 owner、故障来源及完整快照。`dpp-recovery-after.log` 通过，覆盖停止
屏障延迟、首错保留、再次 START 失败、显式重试及无额外关联读取。C3/C5 既有
不可变 Build Context 下受影响 Radio 生产单元编译均通过，证据在
`build/wifi-stage-tests/dpp-recovery-compile/`；没有逐修改全量构建。

该批尚余 24 个失败模块，集中于 Driver 配置/恢复、runtime 清理、部分 TWT、配网与
输入构造；最新结果见下面的后续收敛记录与 `triage-current.json`。尚未完成全量当前代码回归、完整
C3/S3/C5/disabled 镜像矩阵或实机测试；本轮增量未提交，稳定等级维持 Candidate。

Host C 首轮先暴露 feature-disabled Radio 无条件引入 Wi-Fi JS 头文件的构建错误，
现按 feature gate 引入高级模块，保留共享 Radio／ESP-NOW 的独立构建。
实际运行又复现部分 AP 转换失败后完整 STOP 等待已停止接口第二个 STOP 的缺陷。
现在以受临界区保护的 `event_live` 选择等待接口，仍要求屏障处整个 live mask 为零；
`test_wifi_radio_partial-ap-stop` 从失败转为通过。原始日志保留为
`host-c-fourth.log`、`host-c-fifth.log`，未把原始失败覆盖为通过。

旧的 `esp32_mquickjs_wifi_radio_start_ap` 已没有生产调用者；正式 `wifi.startAP`
经 `reserve_ap`／`configure_interfaces`、共享 reopen 或 `activate_ap` 进入当前
协调器。删除该废弃内部函数与声明；八项只调用它的旧 Host 用例不再保留，新增
当前生产 AP validator 用例（自动信道、SSID、密码、beacon/DTIM/CSA、安全配置及
零 SDK 副作用）。因此 Host 用例数从这轮首次可运行的 145 变为 138。
公开 AP／完整配置路径继续由 `test_wifi_ap.py`、`test_wifi_public_configure.py`、
`test_wifi_config_controls.py`、配置／激活／重放 fixture 覆盖；这些仍需在完整 Python
批次确认，不因旧 helper 删除而宣称其通过。

首轮 Python 的测试构造问题也已修复：生成 C 的换行、隔离模块的 NAN/Mesh/TWT
边界、SDK 函数提取格式，以及 source prepare／WAPI select 共用观测计数。
没有为了通过用例放宽生产恢复准入或改变凭据安全策略。

## 最后实现项：Enterprise restart

`wifi.driver.restart()` 已接入健康、started、未关联的 Enterprise Station。
Radio 在同一个 mutation mutex 内核对精确 binding、实际安装 profile、原生 enabled、
当前 helper owners、START 事件接口记录和 identity 余量。APSTA 要求显式
`allowApRestart:true`，不接管无关 owner 或断开已关联 Station。

复用 config operation 的 profile retain：清理旧 EAP、释放旧 helper pins、捕获
checkpoint、STOP/helper 退休、重建及重放，最后安装同一 profile 与安全策略，
再交付新租约并释放快照。普通 stop 仍关闭 EAP，后续普通 start 不自动 enable。

恢复安装失败时，尚被 SDK 引用的凭据归原 lifecycle 保管，不 pin 未交付的新租约。
显式重试先清理该 binding 再 STOP；lifecycle 保持原 generation，binding 则必须
精确匹配新 driver generation。SDK clear 与旧 AP-release 是独立完成前缀。
初次 checkpoint 之前允许重试未完成清理；capture 已尝试后仅完整快照允许重建，
不从部分修改后的来源重新捕获。runtime closing 禁止重装并交给同一清理路径。

本轮静态审查修正了新准入检查误把 started 来源要求为 `event_live == 0` 的问题；
真实 START callback 设置 STA/AP 位，现在要求它与 SDK mode 一致，在临界区读取。
修改前源码保存在 `build/wifi-enterprise-restart/admission-before.c.txt`。
对应生产回归已写入，但修复前没有执行动态失败用例，不能记为已复现运行失败。

生产位置：`wifi.c` 的 `wifi_begin_enterprise_restart`、
`wifi_restart_enterprise_interfaces`、`wifi_restart_restore_interfaces`、
`wifi_finish_enterprise_clear`；`wifi_ap.c` 的 Enterprise coordinator；
`wifi_radio.c` 的 EAP begin/resume/clear 与最终 resume；EAP installer 的 exact
restart-source 查询。以上文件均位于 `components/esp32_mquickjs/src/modules/`。

## 故障处理范围已固定

| 来源 | 实际实现与拒绝边界 | 生产证据与阶段用例 |
| --- | --- | --- |
| Driver 初始化、NVS、事件身份耗尽、PHY 天线故障 | `restartRequired` 阻止重建；保留首错。未知 init ownership 不重复 init；需要设备重启，NVS 不自动擦除 | `wifi_radio_initialize`、`wifi_radio_begin_events`、`wifi_radio_begin_stopped_restart`；`test_wifi_radio.c`、`test_wifi_restart_admission.py`、`test_wifi_restart_retry.py` |
| 没有完整快照的 mode/protocol/bandwidth 故障 | `restore` 仅接受已初始化、完整停止、零 owner 的精确故障；仅重试 defaults/readback/storage 未完成后缀。其他 fault 来源拒绝默认恢复 | `wifi_radio_restore_phase_locked`、`esp32_mquickjs_wifi_radio_restore`；`test_wifi_driver_discovery_restore.py` |
| 完整 restart 的中途失败 | 原 lifecycle 与完整 checkpoint 允许显式 retry；未知初始化、未退休 owner、部分 capture、未知必需策略拒绝。stop/runtime cleanup 可退休，不能保证保留被丢弃的配置 | `esp32_mquickjs_wifi_radio_admit_restart_retry`、runtime restart/cleanup；`test_wifi_restart_retry.py`、`test_wifi_restart_runtime.py` |
| Enterprise | 同 profile 重建；失败 borrower 清除前不 STOP/deinit。未知绑定、配置忙、关联中、原生清理错误、身份耗尽拒绝新来源 | `wifi_radio_eap_begin_restart`、`wifi_radio_eap_clear_locked`、`wifi_radio_eap_restart_install_locked`；EAP install/radio、Enterprise stop/restart、policy replay fixtures |
| SmartConfig | 关闭原生 decoder、确认捕获停止、恢复合法 home channel、事件屏障及 release 后才归还 operation；unknown handoff 不释放、不开新 Session，可需要设备重启 | `esp32_mquickjs_wifi_radio_smartconfig_close`、`wifi_radio_smartconfig_restore_locked`、decoder/session worker；`test_wifi_smartconfig_radio.py`、`test_wifi_smartconfig_decoder.py`、`test_wifi_smartconfig_session.py` |
| Station/AP WPS | Session close 重试未完成的 worker/原生退休与恢复步骤；公共 timeout 不丢弃原生 owner。unknown handoff 阻止复用，没有通用强制 recover 承诺 | `wifi_wps_radio.inc`、`wifi_wps_ap_radio.inc` 及 WPS worker/session；`test_wifi_wps_radio.py`、`test_wifi_wps_ap_session.py`、`test_wifi_wps_worker.py` |
| DPP | `Session.recover` 仅接管本 Session 已知的恢复 START 失败；核对实际 fault/error、已退休协议、原 helper 与 RAM storage，先 STOP/fence 后一次 START；unknown handoff/其他故障拒绝 | `wifi_dpp_radio.inc` 的 `esp32_mquickjs_wifi_radio_dpp_recover`、restore helpers；`test_wifi_dpp_radio.py`、`test_wifi_dpp_session.py` |
| Roaming | BTM 为 SDK submit，不虚构取消、超时或恢复成功；已提交但未知结果不自动重发。RRM 请求以原 token 取消/退休后才复用，公共终态不代替原生回调退休 | `esp32_mquickjs_wifi_radio_roaming`、`wifi_roaming_sdk.c`、`wifi_rrm_request.c`；`test_wifi_roaming.py`、`test_wifi_rrm_request.py` |
| TWT | 显式 generation recovery 要求健康可捕获 Radio、精确受管 TWT owners 与断连/全部关闭同意；TX/PM/timer/event/info storage 退休前禁止 deinit。sticky tracking fault、unknown handoff、已故障来源拒绝，可需设备重启 | `wifi_radio_twt_recovery_begin/prepare/checkpoint`、原生退休检查；`test_wifi_twt_recovery.py`、TWT submit/retire/timer/fence fixtures |

表内没有覆盖的 fault 不承诺通用恢复；使用各 API 的显式清理入口，并以实际
native retirement 决定是否归还资源。保留中的原生 owner 或不可证明的 SDK 状态
不会因 JS runtime restart 自动变为可用。竞争、超时、硬件共存和 RF 成功率进入
阶段测试，不继续扩展为测试前的新功能。

## 测试入口与证据规则

- 已审查 Wi-Fi SDK function 缺口已接通；五项 ESP-NOW 新能力按第二阶段既定
  底座对齐范围继续 `contract-pending`，未注册占位 API。
- Enterprise 最终批次进行 C3/C5 受影响生产文件编译、manifest／映射／config
  schema 与 deferred fixture AST 核对；记录到 `build/wifi-enterprise-restart/`。
- 上述批次静态检查完成后进入集中运行：先 Host C/Python/MQuickJS 与竞争、GC/OOM，
  然后一次完整 C3/S3/C5 和相关 feature-disabled 构建，再做实机功能和集成。
- 每项真实运行结果单独记录，不继承旧版本通过数。开始执行前，所有新 fixture
  都仍为 `not-run`；静态编译和生成检查不改变这一状态。
- 实机前重新确认 ROM MCU/MAC 与 Build Context；保留 workspace。资源比较使用
  预热后同等静止状态的 internal/PSRAM free、largest block、owner/pool 账本。
- 缺少对端的 BLE/GATT、ESP-NOW 双机、CSI RF 与共存逐项记 `not-run`；长时间 soak
  和 500 次完整生命周期留到 BLE API 完成后，不阻止本次 Wi-Fi 功能测试开始。
