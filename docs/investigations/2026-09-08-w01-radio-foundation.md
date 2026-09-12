# W-01：Radio 内部生命周期、共享 lease 与清理接入

基线为 firmware `d7db8d1`，F-CORE 收尾已提交。固定 ESP-IDF
`fff9895c82d744c7237be8847347bdd1b07c6643`。截至第九批，现有 feature 范围的
**W-01 原生收尾已实现，W-02 首批 start/stop 已接入**。下面各批保留当时证据，
最后一批和后续清单代表当前状态；不宣称长期硬件 Gate 已通过。

## 已实现

- 复用原有 boot-lived mutation mutex 和 16 项 identity registry，删除 init/start
  的 once 成功缓存。生命周期记录 driver ownership、storage configured、started、
  stop required、故障原始阶段及 cleanup suffix；NVS 自身的 boot helper 保留。
- 内部 `wifi_radio_stop()` / `wifi_radio_shutdown()` 必须在所有 lease 退出后运行。
  stop 保留 driver，后续 acquire/start 可再次启动；shutdown 完成 stop/deinit 后
  才清除 ownership 并推进 generation。存活子 owner 导致无副作用拒绝。
  lease identity 始终 boot-scoped、不回绕；重复 release/shutdown 幂等。
- stop/deinit 失败保留准确已完成步骤，禁止新 acquire/start/mutation。重试 deinit
  不重复成功的 stop；promiscuous restore 失败仍保留原 lease，成功后解除对应故障。
- 每项 lease 保存 required mode。按存活集合计算 `requestedMode`；STA/AP 合并成
  APSTA，正在运行时延后破坏性降级，在下一次 stopped start 精确收敛。未审查的
  native mode 不参与隐式位合并。
- 固定信道约束移入每项 lease。primary/secondary 相同允许共享，不同要求拒绝，
  A 退出不影响 B。SDK 提交失败不发布新约束。connected STA 和运行 AP 的 home
  channel 不可被固定采集请求覆盖。
- 通过生产 `get_channel` 观察到 driver 自发改变信道时推进 channelGeneration，
  锁存旧 fixed lease 的 conflict；不会把 driver 强制切回旧信道。这里是主动查询
  观察，尚不是完整事件 broker 或 RF callback admission 处理。
- `wifi.status().radio` 增加实际 `driverState`、`requestedMode`、`cleanupStage` /
  `cleanupError` 和 fixed/conflicted owner 数量。类型与 API 文档同步，唯一 v1；
  没有注册尚未接入的 `wifi.start/stop/driver.restart` 方法。

## SDK 失败边界

固定 IDF 的 `components/esp_wifi/src/wifi_init.c` 中，`esp_wifi_init()` 的 `_deinit`
分支调用 `wifi_deinit_internal()`，清理失败只记录日志，返回原始 init error；公开
`esp_wifi_deinit()` 又依赖 `s_wifi_inited`，该标志在成功 init 结尾才设置。因此
原生 init 返回失败不能证明其内部资源已经干净。本批不在该状态盲目重试 init：
`restartRequired` 仍要求设备重启。NVS boot helper 的失败也不能用 runtime restart
恢复。storage/mode/start 失败则有已确认 driver ownership，允许所有 owner 退出后
走显式 shutdown 重建。该边界是 SDK 证据支持的保守处理，不宣称所有 init 错误均
已具备在线恢复能力。

## 并发顺序

正常修改顺序为 Radio mutation mutex → 短 snapshot lock。registry 验证、SDK
mutation 与 release 在同一个 mutation mutex 期间完成；NVS、SDK、等待与分配不在
短 critical section 中。SDK/事件回调不得在持有子模块锁时等待 Radio，再反向等待
该子模块退出。本批生命周期入口要求零 lease，因而不在 Radio 锁下清理子模块。

初始化、启动、停止或 cleanup-pending 期间，status 直接复制短锁快照，不等待正在
执行的 SDK 调用。稳定状态下的 driver 查询经过 mutation mutex，避免与 deinit
交错。所有跨任务状态都是 native 数据。

## 验证

`tests/c/test_wifi_radio.c` 直接编译生产 Radio，只替换 SDK/RTOS 调用。先把同信道
共享期待加入原有测试，取得修改前失败 `build/w01-before.txt`，然后修改生产实现。

新增覆盖：共享/退出/复制旧 token、不同 secondary 冲突、重复 stop/start/shutdown、
重新初始化后旧 generation、所有 init/start 阶段失败后的清理、stop/deinit 失败后缀、
mode union 与安全降级、STA home-channel/driver drift、stop 和 deinit 中并发 acquire。
竞争以 pthread condition/mutex contention 明确握手，不靠 sleep 猜测时序；每个 SDK
调用均断言未处于 critical section。初始化暂停时 status 可读取 initializing。

最终复核补充了正常 promiscuous release 的状态断言，复现本批引入的错误：成功
cleanup 也写入 cleanup-pending（`build/w01-promiscuous-before.txt`，78/79）。
已修正为仅在 SDK 返回错误时记录 cleanup fault，并验证正常退出及失败重试后均
回到 started，清空对应 cleanup/fault 字段。

Host C 79/79，Python 407/407。实际 MQuickJS 状态转换的新增字段继续接受逐 API
失败与移动 GC 回归。C3/S3/C5、C5 disabled/NAN-Sync 构建、语法及生成物检查的
最终结果与源码/image hash 见 `build/w01-evidence.json` 和 `build/w01-*` 日志。
没有刷写、擦除 workspace、前端构建或更新父仓库 gitlink。已有 SRAM payload
优化保留；新增的只是有界控制状态，没有新增 payload pool。设备预热后 heap/PSRAM
比较、真实 RF 与长时间验收均为 not-run。

## 第二批：helper 清理与现有全局 setter

继续接入前确认：`wifi_cleanup_failed_init()` 忽略 timer delete/event unregister
的返回值，仍释放 callback 可见的队列、锁与 netif。生产函数失败注入先取得
`build/w01-cleanup-before.txt` 的断言失败，再修复。现在失败立即停止，保留精确
剩余句柄；timer stop 完成后 delete 失败不会重复 stop，已成功 unregister 的
前缀也不会重试。固定 IDF 的 `esp_timer_stop_blocking()` 提供 TASK timer callback
退出屏障；仅在该屏障成功后提交 delete。所有外部调用都在 Wi-Fi 短状态锁之外。

后续 Wi-Fi 操作遇到残留 cleanup 直接返回原始错误；runtime teardown 与下一次
runtime attach 是清理后缀的重试入口。attach 清理失败时不注册新消费者。未初始化
runtime 的 teardown 也会清空 runtime 指针。`wifi.status().cleanupStage/cleanupError`
暴露 helper 层的失败阶段，与 `.radio` 的 driver cleanup 区分。该实现还不是健康
Wi-Fi runtime 的全量物理 teardown，也没有放宽 native init 的设备重启边界。

现有 `wifi.setPowerSave` / `wifi.setTxPower` 已改为生产 Radio helper 提交。参数、
精确 lease、driver 状态、其他存活 owner 与 promiscuous 约束通过后，才在同一个
mutation mutex 内写入和读取实际值。拒绝其他 ESP-NOW/CSI owner；release/shutdown
不能夹入写入和 readback。SDK write 失败不执行 getter，getter 失败不假装回滚已
提交配置。暂未开放 rate/bandwidth 等临时覆盖，因此不新增虚构的恢复默认值机制。

本批新增 Host C 四项：参数/共享 owner/旧 token、SDK 写入和读取失败、两个 setter
分别与 release 竞争。Python 增加六阶段 cleanup 失败与后缀重试、runtime attach
拒绝残留 cleanup；状态转换继续经过真实 MQuickJS 的分配失败/移动 GC 测试。
Host C 83/83、Python 409/409 通过；C3/S3/C5、C5 disabled/NAN-Sync 五项构建、
MQuickJS 语法（59 sources / 47 snippets）、API manifest（43 classes / 384 functions）、
feature 文档（27 项）与 Wi-Fi coverage map（1,267 项）检查均通过。
最终结果以 `build/w01-followup-*` 日志及 `build/w01-followup-evidence.json` 为准。
第一批 `build/w01-evidence.json` 保留历史源码 hash，不作为第二批当前源码证据。

## 第三批：scan 原生终态与结果 ownership

确认并复现的竞争：旧 `wifi_future_cancel()` 在 `esp_wifi_scan_stop()` 返回后直接
清空 `scan_in_progress`，而 `SCAN_DONE` 是异步到达的；随后新 Future 可占用同一
slot，旧事件被贴上新 generation。修改前生产 cancel + event helper fixture 在
`build/w01-scan-before.txt` 失败，断言取消后原生扫描仍应 pending。

固定 IDF 的 `docs/en/api-guides/wifi-driver/overview.rst` 明确列出扫描完成和
`scan_stop()` 的 `SCAN_DONE`，并要求释放结果列表；`station-scenarios.rst` 说明
成功 `get_ap_records()` 会释放列表。实现现在同时等待原生 terminal 与结果 ownership
结束；正在调用 stop 时也不能归还 slot。Future token 可先释放，scan config 存于
boot-lived Wi-Fi helper，取消、Future destroy 和 runtime teardown 都不重用该配置。
成功 stop 不重复提交，失败 stop 仍等待自然 terminal 或明确的 teardown 清理重试。

没有注册公开消费者的 SCAN_DONE 不投递 Future，只交给 runtime task 清理结果；
事件回调本身不调用 driver cleanup。成功读取 records 后标记 SDK 列表已释放，
即使后续 JS 转换失败也不重复该后缀。完成但未 finish 的 Future 在 destroy 时
清理列表。AP-list cleanup 失败保留错误并阻止新 scan/connect；poller 不反复重试
失败，新操作请求或 teardown 才是后缀重试边界。scan generation 耗尽拒绝新操作。

生产回归覆盖取消后新 scan/connect 拒绝、迟到 terminal、AP-list cleanup 失败、
stop 失败、callback 在 stop 返回前到达、旧 generation、不读取结果的 destroy、
start 失败和跨 runtime 迟到 callback。配置和原生状态的存活不依赖 JS 根。
Host C 83/83、Python 415/415、C3/S3/C5 与 C5 disabled/NAN-Sync 五项构建通过；
MQuickJS 语法（59 sources / 47 snippets）、manifest（43 classes / 384 functions）、
feature 文档（27 项）和 Wi-Fi coverage map（1,267 项）检查通过。
当前证据见 `build/w01-scan-*` 与 `build/w01-scan-evidence.json`；前两批 hash
保留历史用途。未刷写、未运行 RF/heap/长周期验收。

本批仅建立受框架控制的单个 scan slot 终态屏障。connect 的旧 DISCONNECTED /
GOT_IP 以及 timer 回调还需单独的终止/关联处理，不能因 scan 测试通过即宣称
完整 Wi-Fi teardown 或公开 stop/restart 可用。严格 fixed-channel owner 与 scan/
connect 的 Radio admission 接入也仍待推进。

## 第四批：connect timeout identity 与回调退出屏障

生产路径复现两处问题（`build/w01-timer-before.txt`，2/2 失败）：超时仅用一个
boot-global bool 传递，旧 callback 已入队时新 connect 的 generation 无法区分；
prepared、尚未 started 的 connect Future cancel 仍调用原生 disconnect。

现在 timer callback 捕获 armed generation，跨 runtime poll 的 native 通知保留该
值。处理时必须匹配当前 connect generation、公开 operation kind 与 in-progress，
旧 timeout 不能终止新连接，也不能误处理 disconnect Future。generation 耗尽拒绝
新 operation，runtime teardown 不回绕该计数。

新 connect 提交 config 之前调用固定 IDF `esp_timer_stop_blocking()`（1,000 ms
上限）。先禁止旧 timer 新通知，再等待已进入 callback 退出，最后清除旧 pending
notification；通过屏障后才为新 timer 绑定 generation。失败时保留原生 timer 和
诊断，不写 config、不提交 connect。runtime teardown 也执行该屏障，不因 public
Future 结束就销毁仍可能被引用的 timer 存储。`wifi.status().connectTimerError`
报告 timer barrier/start 的原始错误；后续显式调用可重试，未做循环自动重试。

prepared cancel 不再断开现有连接，也不使用空 runtime 唤醒；已 started 的连接
取消、timer start 失败后的本连接断连保护保留。原生 helper 生产测试覆盖旧 timeout、
当前 timeout、disconnect kind、prepared cancel、barrier 失败无 config/connect
副作用、barrier 期间旧 callback 发布、deferred poll、timer start 失败。
SDK/RTOS 边界替换，实际 callback、poller、start_connect 与 Future cancel 均被调用。
Host C 83/83、Python 420/420、C3/S3/C5 与 C5 disabled/NAN-Sync 五项构建通过；
MQuickJS 语法、API manifest、feature 文档与 Wi-Fi coverage map 检查通过。
源码/image/检查日志记录在 `build/w01-timer-evidence.json` 和 `build/w01-timer-*`。

该批仅解决 timer 的请求身份及退出屏障。DISCONNECTED/GOT_IP 本身没有本批新增
cookie，取消后 native disconnect 终止、IP 事件排空、隐式重新连接的交接还需实现；
不得将本批结果写成完整 connect lifecycle、Radio admission 或 W-01 Gate 通过。

## 第五批：disconnect/IP 原生交接屏障

生产 `wifi_process_driver_event()` 复现 GOT_IP 在注册 DISCONNECT 时仍写入 connected
并投递成功的缺陷，修改前断言见 `build/w01-link-before.txt`（该次 discover 另带入
六项 scan fixture 测试；确认失败的是新增的迟到 IP 用例）。现在接纳 GOT_IP 必须
同时满足当前 CONNECT、公开 token 存活、in-progress 且未处于 disconnect draining。
取消先锁定 native draining 再释放 token，旧 generation 不能取消新的 owner；未登记
但仍 active 的原生 connect 也不能被新 Future 冒领。

没有为无 cookie 的 SDK 回调虚构请求 identity。旧链路必须同时完成 DISCONNECTED、
默认 event loop 的 FIFO marker 和仍在执行的 `esp_wifi_disconnect()` 调用，才可复用。
重连在写 config 之前等待这个屏障，等待上限为该次 timeoutMs；新连接本身的 timeout
另行计时。旧 IP 在此期间被丢弃；scan 也不能穿过该隔离。marker 有独立 boot-scoped
递增 epoch，不回绕；耗尽保留无效状态诊断并要求设备重启。状态转换和 SDK/RTOS
调用分离，所有测试替身均断言外部调用不持有 Wi-Fi 状态锁。

固定 SDK 证据：`components/esp_wifi/src/wifi_default.c` 先注册默认 STA handler；
Wi-Fi helper 在创建默认 STA netif 后才注册自己的事件处理。默认 DISCONNECTED
经 `components/esp_netif/esp_netif_handlers.c` 调用 `esp_netif_down()`；
`components/esp_netif/lwip/esp_netif_lwip.c` 用同步 TCP/IP task IPC 停 DHCP、清 IP
并关闭 link/netif。此前已生成的 IP 事件会排在 `components/esp_event/esp_event.c`
默认 FIFO 上新提交的 marker 前。提交前还检查 netif 已 down；若 roaming/IP-retention
配置跳过 down，则保守保留隔离，不把未验证配置视为已支持。该证明仅适用于固定
SDK 的默认 handler 顺序；并不意味着任意外部 handler 重排同样成立。

公开 disconnect 仍在原生 DISCONNECTED 时完成，IP marker 可以稍后完成；已见到
terminal 的再次 disconnect 不等待第二个不会到来的事件。marker 队列满不妨碍原生
断连状态/Future 完成，但保持 connectDraining；后续显式 connect/disconnect 仅重试
未提交的 marker，不重做成功的 native disconnect。SDK disconnect 失败可显式重试。
NOT_CONNECT/NOT_STARTED 的错误值本身不证明旧事件已排空；terminal 缺失时跨 runtime
保持隔离，不重置共享 Radio 来掩盖问题。

新增 `wifi.status().connectDraining/disconnectCleanupError`，公开类型和 API 文档
同步。私有控制 handler 加入初始化失败的清理后缀，新增 control-unregister 失败点；
失败仍保留原生 handles。没有新增公开 lifecycle API 或 payload pool。

九项生产路径测试覆盖迟到 IP、disconnect 返回前 terminal+marker 到达、marker 队列
满/后缀重试、旧 epoch、SDK 失败/terminal 缺失、netif 未 down、epoch 耗尽、runtime
teardown 后隔离、旧 cancel、Future admission、已完成断连的重试和重连 config 顺序。
既有 timer/scan/cleanup 与真实 MQuickJS 分配失败和移动 GC 回归继续执行。
Host C 83/83、完整 Python 429/429 与新增交接九项回归通过；C3/S3/C5 和 C5
feature-disabled/NAN-Sync 五种合法 Build Context 构建通过。MQuickJS 语法
59 sources / 47 snippets、API manifest 43 classes / 384 functions、feature 文档
27 项及 Wi-Fi coverage map 1,267 项检查通过。当前源码/image hash 记录在
`build/w01-link-evidence.json` 与 `build/w01-link-*`；前四批证据保留历史用途。
本批未提交、未刷写、未修改根仓库 gitlink；硬件 heap/PSRAM 静止态比较、RF、
500 次生命周期与共存验证均为 not-run。原有 SRAM payload 优化未改动。

## 第六批：scan/connect 与固定信道 owner 的双向准入

确认缺口：原生 scan/connect 直接提交 SDK，只在 fixed owner 的 channel 设置路径
做一次信道检查。现有 owner 无法阻止新扫描/连接，接纳异步操作后也没有持续约束来
阻止新的固定 owner。修改前生产 Wi-Fi helper 注入 Radio 拒绝边界，scan 仍提交、
connect 仍进入旧连接交接，两项失败见 `build/w01-admission-before.txt`。

现在 Radio 有一个有界 scan/connect reservation，绑定 radio generation、精确 lease
identity、独立 operation identity 与 kind。Wi-Fi 在 scan SDK 提交或 connect 的
旧链路断连/config 写入之前取得 reservation。任意 fixed owner（含冲突锁存 owner）
存在时拒绝；不会暂停旧 session、断开现有链路或改其信道。当前保守拒绝指定同信道
scan，没有把未验收的 same-channel 例外提前放开。仅 following 的 lease 不因共享
而拒绝；连接成功后归还 reservation，原有 home-channel 共享规则继续生效。

反向约束同样由 Radio mutation mutex 串行化：reservation 存活期间拒绝固定信道
申请、mode 实际变更和现有 power-save/TX-power 全局 setter。所属 lease 的 release
保留原 token，不能让 shutdown 越过 active operation。独立 identity 不回绕，耗尽
明确返回 NO_MEM；重复/旧/伪造 token 的 end 不能归还另一项 reservation。

该 reservation 不随公开 Future 结束而归还：scan 需 terminal、SDK 调用返回和结果
列表释放；connect 需成功或 disconnect/IP 交接结束。提交期间的 active 标志阻止
嵌套 poll/早到 callback 提前释放。取消后的 connect 交接复用同一 reservation，
避免旧链路排空与新配置之间被 fixed owner 插入。失败的 native cleanup、缺失的
terminal 以及 runtime teardown 都保留这一原生义务；runtime poll/下一次操作请求
才检查释放条件。所有 Radio begin/end 从 runtime task 在 Wi-Fi 状态锁外调用，
event callback 不等待 Radio mutation mutex。没有在短 critical section 内调用 SDK。

固定 SDK `components/esp_wifi/include/esp_wifi.h` 的 `esp_wifi_set_channel()` 明确
禁止在 STA scanning/connecting 时调用；`esp_wifi_types_generic.h` 的 STA channel
字段只是优先扫描提示。不能用传入相同 channel 推导 connect 没有 off-channel 副作用。
新增 `wifi.status().radio.activeOperations`（当前 0/1）只统计这一 native reservation，
不是所有无线 feature 的操作数。类型、API 文档与实际状态转换同步。

生产 Host C 新增五项，覆盖双向冲突、持有 owner、旧 identity/耗尽、全局 mutation
与 pthread 可控调度的 channel-write/admission 竞争。Wi-Fi 生产 helper 新增八项，
覆盖无副作用拒绝、scan 取消/失败结果清理、SDK 提交失败和早到 callback、connect
超时/IP fence、config 失败/成功边界及跨 runtime handoff。Radio/SDK 是 fixture
边界，不用另一个测试状态机代替生产实现。状态对象继续经过真实 MQuickJS 的
分配失败和移动 GC 回归。

Host C 88/88、Python 437/437 通过；C3/S3/C5 与 C5 feature-disabled/NAN-Sync
五种合法 Build Context 构建通过。MQuickJS 语法 59 sources / 47 snippets、API
manifest 43 classes / 384 functions、feature 文档 27 项及 Wi-Fi coverage map
1,267 项检查通过。

验证日志及当前源码、Build Context、image hash 见 `build/w01-admission-evidence.json`
与 `build/w01-admission-*`。前五批证据保留历史用途。未刷写、未提交、未更新父仓库
gitlink；原有 SRAM payload 优化和 workspace 保留。RF、静止态 heap/PSRAM 比较、
500 次生命周期与共存仍为 not-run，按用户安排集中后置。

## 第七批：home-channel 事件与 CSI/ESP-NOW 准入

确认并复现：CSI RX callback 未读取已锁存的 Radio 冲突，仍投递数据；ESP-NOW
已排队 packet 在 worker 提交时不复核信道，仍调用 `esp_now_send()`。修改前两项
生产 helper 回归失败见 `build/w01-channel-before.txt`。两者现在都使用生产 Radio
的精确 lease channel snapshot；不通过一个独立测试状态机替代真实处理。

共享 Radio 现在自行确保默认 event loop 并注册一个 boot-lived WIFI listener，监听
HOME_CHANNEL_CHANGE、STA_CONNECTED 和 AP_START，不依赖 Wi-Fi JS feature 开启。
固定 SDK `esp_wifi_types_generic.h` 明确 HOME_CHANNEL_CHANGE 不由 scan 触发。
listener 没有 session/JS 指针，物理 shutdown/reinit 不重复注册；没有在本批声称
完整公共 Radio teardown 已实现。event-loop/channel-handler 失败记录原始阶段，
零 owner 后 shutdown 可清除此类 pre-driver 故障，下一次 start 重试。

事件 callback 在 Radio 锁之外读取 SDK 当前信道，而非重放可能迟到的 payload；
不等待 Radio mutation mutex。driver generation 与 observation revision 防止旧
查询跨 deinit 或覆盖较新事件结果。实际信道变更推进 channelGeneration 并锁存每个
fixed owner 的冲突；following owner 获得当前值。只读操作不反向改信道。观察失败
通过 `radio.channelObservationError` 暴露，准入保守拒绝，成功观察可清除此读取错误。
fixed conflict 即使 driver 回到旧信道也不自动解除。channel write 被事件抢先时拒绝
提交过时的 owner claim；已发生的 SDK 写入不被虚构为已回滚。

CSI callback 只读短锁 snapshot，并核对 fixed session 的帧内 primary；即使 RX
先于默认 event loop 的 home-channel 事件到达，也不接纳其他信道数据。冲突后关闭
accepting 并将 running 转为 faulted，
不会覆盖已经进入 stopping/closing 的状态。status() 刷新 effective 信道及
radioGeneration，并报告 WIFI_CSI_CHANNEL_CONFLICT/radio-channel-change；保留既有
cleanup 错误的优先级。Frame/Batch/View/Source 所有权与 pool 不变。显式 stop、
可选 configure、start 才重新验证，不自动断开 STA 或强制切回旧信道。事件队列已有
数据和 pending receive 保持原有消费/取消语义，不虚构自动完成。

ESP-NOW RX 在分配 slot 之前复核；公开 send/enqueue 与 worker 真正提交时均检查。
冲突时 tracked Future 或 queued packet 写入原生失败终态，既有 worker 完成/flush
账本继续处理；已经提交的 native send 仍遵守 callback/timeout/recovery 屏障。
following session 的 channel/generation 通过原子字段在 RX/status/准入/dispatch 时
更新；fixed owner 冲突后需要 close/reopen 显式重配。peer 显式 channel 约束保留。
这里证明的是准入时序，不承诺 RF 换信道与 SDK 提交之间具有原子性或空口送达。

Host C 新增六项覆盖事件冲突/返回原信道锁存、following/旧 lease、读取错误恢复、
boot listener 跨重启、注册失败、事件抢先于 SDK query/channel write 的 pthread
确定性竞争。Python 新增九项生产 CSI/TX 回归覆盖停止投递、跟随、读错误、关闭
状态保护、tracked/queued 失败终态、成功发送、状态刷新和先于事件到达的异信道帧。
两种 callback 都不调用 JS 或等待
Radio mutation mutex；生产状态转换继续进行分配失败/移动 GC 回归。

除原有 C3/S3/C5 与 C5 disabled/NAN-Sync 五项构建，另建独立 immutable Context
`c5-espnow-only`：关闭 Wi-Fi JS/CSI，保留 ESP-NOW 与其 Radio 依赖（其余代表功能
保留）。该项构建验证 listener 不依赖 Wi-Fi JS feature；不将它写成 RF 硬件验证，
也不冒充 W-00 覆盖清单新增了已审查的 SDK 变体。

最终 Host C 94/94、Python 446/446、六项构建通过。MQuickJS 语法
59 sources / 47 snippets、manifest 43 classes / 384 functions、feature 文档 27 项
及既有五变体 Wi-Fi coverage map 1,267 项检查通过。架构测试已指向重构后的
实际 channel refresh helper，仍检查 SDK 可选 secondary 的初始化。

验证日志及当前源码/Build Context/image hash 记录在 `build/w01-channel-evidence.json`
和 `build/w01-channel-*`。本批未提交、未刷写、未更新父仓库 gitlink，没有扩大 payload
pool 或删除 SRAM 优化。真实 RF/预热静止态内存、500 次生命周期与共存均 not-run。

## 第八批：Wi-Fi runtime owner 回收

先用生产 `esp32_mquickjs_deinit_wifi_runtime` 复现：已取得 IP 的连接没有
`connect_in_progress`，旧 teardown 因而不请求断连，helper 的 lease、timer、
事件 handler、队列和 netif 继续存活。失败日志为
`build/w01-teardown-before.txt`。另补生产连接通知 helper 的回归，确认其绕过
Wi-Fi 自己的 runtime 脱离状态、仍使用全局 active runtime；失败日志为
`build/w01-teardown-notify-before.txt`。这证明通知目标选择缺口，不冒充实机 UAF。

当前 teardown 先原子脱离 runtime，等待已经进入的 callback 退出；连接/扫描通知
只使用一次读取的本 helper runtime 快照，不重新获取全局 active runtime。健康 STA
也进入原生断连路径，因此应用在 runtime restart 后需要重新连接。清理仍等待
DISCONNECTED、同一默认 event loop 的 IP fence 和 SDK 调用退出；扫描等待
SCAN_DONE、AP list 清理和 SDK 调用退出。JS token 可以脱离，原生 reservation 与
callback storage 在屏障完成前保留。

teardown 不为完整原生终态持续阻塞；下一次 runtime attach 是一次显式取消重试
边界，再最多等待 1 秒原生终态。等待循环不反复提交 disconnect/scan_stop。
原始 SDK 错误立即返回；缺终态报告 scan-drain/connection-drain 并阻止新 attach。
connect timer 的 stop_blocking 有独立 1 秒预算；已进入 callback 的退出屏障仍必须
完成，不能把这两个预算写成整个 teardown 的硬截止时间。

屏障完成后按后缀删除 timer、注销 helper handler；Radio mutation mutex 串行化
精确 lease 释放与最后 owner 停止。其他 CSI/ESP-NOW owner 存活时保留共享 driver。
持有原生 operation 的 lease 不被释放；最后 stop 失败时 lease 已退出注册表，
保留 raw error 和 helper storage，再次清理仅重试 stop，不重复减少 owner。
成功后归还 netif/队列/event group/mutex，scan/connect generation 与 disconnect
identity/exhaustion 保持 boot-scoped，不因 helper reset 回绕。

本批完成 helper runtime 回收，尚未物理 deinit boot-owned Radio driver/mutex/
channel listener；application/STA/AP owner 拆分及独占公开 restart 仍需下一步。
公开 start/stop/restart 不进入注册或类型，仅同步现有 status 清理阶段和 API 文档。

Host C 98/98、Python 453/453；C3/S3/C5、C5 disabled、C5 inventory、
C5 ESP-NOW-only 六项构建通过。新增四项 Host C 覆盖共享 owner、活跃 operation
阻止释放、stop 失败后缀和 retirement/acquire 确定性竞争；新增七项 Python 直接
调用生产 helper，覆盖健康连接、双终态屏障、有限等待、AP list 失败、callback
退出和通知目标，并扩充八个 cleanup 边界与 native identity 保留的注入测试。

MQuickJS 59 sources / 47 snippets、manifest 43 classes / 384 functions、feature
文档 27 项和五变体 SDK map 1,267 项检查通过。日志及源码、Context、image hash
记录为 `build/w01-teardown-evidence.json` / `build/w01-teardown-*`。没有增加 pool
预算或撤回 SRAM 优化；实机预热静止态内存比较、RF/500 次完整生命周期/共存仍
not-run，按用户安排统一后置。本批未提交、未刷写，父仓库 gitlink 未更新。

## 第九批：内部收尾与首批公开 lifecycle

现有 feature 范围的 W-01 内部收尾完成：新增 boot-scoped 有界 lifecycle identity。
begin 在 mutation mutex 下核验唯一 application/idle STA helper；有其他 owner、
原生 operation 或 promiscuous owner 则无 driver 副作用拒绝。token 在清理 helper
的跨步骤间保持准入关闭；新 acquire、start、scan/connect、channel/promiscuous/
全局 setter 均拒绝。精确旧 token 不可完成新的事务，identity 耗尽不回绕。

helper 清理不在 Radio mutex 下执行。释放本 runtime 的 application/helper lease
后，quiesce 先 stop driver、保留独占状态，再释放 netif/队列等 storage；finish
最后开放准入。stop 或 helper 清理失败均保留 token，只重试未完成后缀；已释放
lease 不重复减少。`radio.lifecycleActive` 和 top-level radio-stop 阶段可诊断。

内部 shutdown 现在注销默认-loop channel listener，并等待已进入 callback 退出
后才 deinit driver。固定 SDK 的 `esp_event_handler_unregister_with_internal`
删除该注册，生产 callback 计数保护已进入路径；该 callback 从不等 Radio mutation
mutex。注销失败报告 channel-unregister，保留 driver ownership；重试不重复成功
stop。重新 init 注册新的 listener。Radio mutex 用静态存储，保留为 boot-owned
同步对象；物理 stop/runtime retirement 保留 driver allocation 是明确策略，
完整 shutdown/restart 才 deinit。NVS/native init 清理不确定仍要求设备重启。

内部 restart 保持同一独占范围，完成 shutdown、注册、init/storage/mode/start
后才发布新的 application lease；失败保留 reservation 和原始阶段供显式重试。
不把它注册成尚未展开 options/配置恢复的公共 driver.restart。

内部回归通过后，W-02 开始首批公开 `wifi.start()` / `wifi.stop()`。两者无参数，
返回现有 WiFiStatus；start 为 Station/RAM，不连接 AP，重复 start 只有一个
application owner。仍保留单 STA helper owner，AP owner 在 AP API 实施时接入，
不预注册不存在的 AP/Monitor/Raw TX 对象。stop 先检查 connected、native drain、
未读结果和 Future 注册，再进行独占检查；不自动取消或断开任何 child。额外参数
在初始化/driver 调用前拒绝。错误码 WIFI_START_FAILED/WIFI_STOP_FAILED 保留
原始 espCode。返回对象分配失败不撤销已经完成的 driver 效果，可另读 status。
现有 global setters 允许本 namespace 的单 application 加 STA helper，仍拒绝
其他 feature owner 和 lifecycle reservation。

本批新增十二项 Host C（合计 110 项）：跨步骤排他、过期/耗尽 token、注销失败
后缀、六个 restart 启动阶段故障、callback/deinit 竞争、restart/acquire 竞争及
application setter 规则。四项 Python 公开适配器测试补参数预验证、幂等 owner、
所有 busy 标志、清理失败准入/重试；生产 helper cleanup 增加 quiesce/finish 失败
与首次 init 前 stop 的回归。既有移动 GC/第 N 次分配失败覆盖新增 status 属性。
正式类型、唯一 v1 manifest、API 文档和 SDK map 同步，不提升无线稳定等级。

最终 Host C 110/110、Python 457/457；C3/S3/C5 和 C5 disabled/inventory/
ESP-NOW-only 六项 Build Context 构建通过。MQuickJS 59 sources / 47 snippets、
manifest 43 classes / 386 functions、feature 文档 27 项及五变体 SDK map 1,267 项
检查通过。公开方法仅新增 start/stop 两项，唯一 v1，不扩大已声明的能力稳定等级。
源码、Context、构建产物 hash 和日志记录在 `build/w02-lifecycle-evidence.json` /
`build/w02-lifecycle-*`。本批未提交、未刷写、未更新父仓库 gitlink，未构建前端或
擦除 workspace。长期硬件、RF、预热内存和 500 次生命周期仍 not-run。

## 后续新 API 工作

后续独占冷启动 SoftAP 已接入，详见 [W-02 SoftAP 实施与验证](2026-09-08-w02-softap.md)。
以下仍是完整目标的剩余项，不能将部分 AP 实现写成 APSTA/共享 AP 已完成。


1. W-02：完整 start/stop options、SoftAP/APSTA、复合 configure 与 watch/event broker；
   AP lease 绑定真实 AP 资源生命周期。当前实际 start/stop 仅 Station/RAM 无参数。
2. 公开 `wifi.driver.restart()` 需要 options/配置保存与恢复、helper 重建的正式契约；
   内部独占 restart 已具备基础，不以占位 API 冒充公开可用。
3. ROC 和 W-07 rate/bandwidth 等临时覆盖按可信前值/revision 恢复契约推进；
   Monitor/Raw TX/新 CSI wire 继续按 W-03 起的工作包实施。
4. 长期 RF、预热静止态 internal/PSRAM free/largest block 与 owner/pool 账本、
   500 次完整生命周期和共存按用户安排统一后置，当前 not-run。
