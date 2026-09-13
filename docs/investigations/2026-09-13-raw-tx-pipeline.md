# Raw TX 有界发送流水线与相近 API 核对

## 范围与状态

在现有 firmware 工作区修复之上，实现 `maxInFlight`、队列字节容量、
`waitWritable()` 和不依赖 JS 轮询的原生调度。保持唯一 v1，不修改 workspace、
不刷写设备、不提交根仓库 gitlink。C 提供有界资源与发送能力，业务重试、
流量分配、速率策略、批次构造继续由 JS 决定。

这次的“并行”是多包在途的驱动提交流水线。驱动调用由 Radio mutation mutex
串行化，单个 Wi-Fi 发射器不会同时在多个信道发射。不能把窗口大小换算为
吞吐倍数。默认窗口 1，可显式设为 1–8，且不超过队列包数。

## 实现

- Queue：同时限制包数和 retained payload 字节；乱序完成按 generation/sequence
  回收精确 slot；批次开始后，其未发送余部也不可被 overflow 丢弃。
- Broker：8 个有界完成记录；提交 identity 单调且不回绕；每包独立结果、超时隔离、
  原生终止和回收；描述符在完成时先解除关联，再允许 SDK 重用。
- Radio：lease 保持到该 owner 的最后一个 native token 退休；固定信道和共享
  Radio 的原有隔离继续生效。物理 recovery 终止所有相关在途记录。
- Session：一个 worker 填充配置窗口；`send` 观察精确包结果，`flush` 只观察调用
  时的序号截止点。`waitWritable` 观察可用容量，不预留容量、不取消发送。
- 调度：入队和完成直接唤醒 coalesced background worker。一个 boot-owned
  single-shot timer 为后台队列满、worker 竞争和 cleanup suffix 提供重试。
  无 dedicated task stack；空闲无工作时停止定时器。AP 临时速率 helper 的
  runtime 所有权保留。
- 内存：队列字节容量包括 in-flight slot 的 payload。JS 捕获临时内存、broker
  copy、SDK buffer 和控制结构另行计入现有 wireless 内存账本。未声称零拷贝。

## SDK identity 证据

本地 ESP-IDF `fff9895c82d744c7237be8847347bdd1b07c6643` 的公开回调不带
caller cookie，`wifi_tx_info_t.data` 也不是原始调用 buffer 的身份保证。
[ESP-IDF 回调契约](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/api-reference/network/esp_wifi.html#_CPPv429esp_wifi_register_80211_tx_cb26esp_wifi_80211_tx_done_cb_t)
说明回调在 Wi-Fi task 中执行，metadata 只在回调中有效。

因此采用 build-local、对象 SHA-256 校验的 relocation hooks。初版仅包含
分配与完成两个 hook；同日的 completion-correlation 排查确认 TX cache
会更换 descriptor，现已补齐中间转移步骤：

1. `esp_wifi_80211_tx` → `ic_ebuf_alloc`：拿到 descriptor 后，在 HMAC enqueue 前绑定。
2. `ieee80211_output_process` → `ieee80211_copy_eb_header`：cache 路径复制
   descriptor 时转移已有身份绑定，在旧 descriptor 被回收前完成。
3. `ieee80211_freedom_inside_cb` → `ieee80211_get_tx_info_from_eb`：在最终
   TX descriptor recycle 前取得完成结果。

初版测试的 HMAC stub 没有执行 cache descriptor 替换，漏掉了此生命周期。
修复与真实 SDK cache 路径回归证据见
[completion-correlation 排查](2026-09-13-raw-tx-completion-correlation.md)。
本文后面的原始验证表是初版流水线证据，不能代替该修复的验证结果。

同时校验 `libpp.a/pp.o` 的 SHA-256，保护 callback-before-recycle 的生命周期前提。
仅改变经过审查的调用重定位，不改变 RF 字节、验证器或 SDK 调度。共享 SDK
目录保持不变；对象变化时构建失败，必须重新审查。C3/C5 的实际 SDK 调用点
在 QEMU 中执行，S3 检查对象链接并通过目标构建验证。该证据不能替代 RF 测试。

## 相近 API 能否统一

| API | 已有能力 / 差异 | 可统一的部分与边界 |
| --- | --- | --- |
| Raw TX one-shot / Session / periodic | 共用 arbiter、Radio、broker；periodic 每个 job 仍最多一个未完成包 | 本次统一到同一多记录 broker；无需三套发送队列 |
| ESP-NOW | 原生发送 worker、批量入队、整批拒绝/淘汰、timeout quarantine/recover；`flushTx` 当前等待队列排空 | 最适合继续采用相同 capacity/backpressure 术语、字节预算、`waitWritable` 和截止序号 flush；多包在途需独立审查 ESP-NOW SDK descriptor 生命周期，当前保持单包 lane |
| UART / Stream / Socket | 字节流、部分写入、驱动缓冲和协议各自的完成定义 | 可统一“可写容量等待”和 timeout 不撤销已提交字节；不应套用帧 batch 或 MAC 完成结果 |
| SPI / I2S / DMA | 事务、DMA buffer、总线或通道所有权约束 | 可复用有界提交/完成/close 账本思路；事务顺序与原生 buffer 生命周期单独保留 |
| BLE GATT | 连接/ATT 次序、操作 cookie 和安全要求 | 保留连接级 lane；完成 BLE 相关验证后再评估窗口，不能直接复制 Wi-Fi 并发数 |
| Monitor / CSI | 接收 pool、Frame/Batch/View ownership | 统一容量诊断术语；接收丢包与发送完成是不同契约 |

Raw TX 与 ESP-NOW 的 overflow 类型已复用 `PacketQueueOverflow`，其两个值和
“已开始批次不能淘汰”的含义相同。其他条目是已核对的后续方向，没有添加
空方法、假能力标志、兼容别名或未经实现的类型。

注意：Session window 连续被补充时会持续持有 arbiter grant，其他 producer
可能等待；当前没有 per-producer 公平性保证。JS 可以用有限批次和 `flush()`
组织实验轮次。

## 多记录 recovery 收尾修复

测试先复现：物理 deinit 后，Session 先回收 recovery 的代表 token，Radio owner
转向同一 lease 的另一 token，此时旧判断误拒绝已经成立的物理终止证明。
`recovery-anchor-before.log` 保存生产 helper 断言失败。修复后在 driver 已释放且
STOP/callback fence 已成立时，以原 exact lease/generation 继续回收；driver
仍存活时仍校验冻结的原 token，不放宽其他 owner 的权限。

## 验证记录

自动化结果见 `build/raw-tx-pipeline/verification.json` 和同目录日志。
以下结果全部为本次最终源代码的自动化/构建证据，不能当成硬件通过声明。

| 检查 | 结果 |
| --- | --- |
| Host C / CTest | 138/138 passed |
| Raw TX 生产 C/SDK/MQuickJS 专项 | 38/38 passed |
| Python tooling/contracts | 388/388 passed |
| MQuickJS 语法 | 71 sources / 73 documentation snippets passed |
| API manifest 与 Wi-Fi coverage/header 一致性 | passed；62 classes / 663 functions |
| C3 representative 合法 Build Context | passed；app 3,167,952 bytes |
| XIAO C5 8 MiB Quad PSRAM Build Context | passed；app 3,139,488 bytes |
| XIAO S3 Sense 8 MiB Octal PSRAM Build Context | passed；app 2,957,776 bytes |
| C3 feature-disabled Build Context | passed；app 442,832 bytes |

C5 现有 3 MiB application partition 余量为 6,240 bytes；S3 为 187,952 bytes。
保留原分区及 workspace。新 broker 的静态 `s_raw_tx` 为 1,216 bytes（C5 map）；
未增加任务栈，boot timer 首次使用后保留。未把构建大小当作稳态 heap/碎片证据。

正常构建使用 `scripts/remote.py --build-context <context> --build-dir <dir>
--python-exe <IDF python> --assume y build`，环境为本地 ESP-IDF export。
最后 Radio 修复仅在已验证的相同 Build Context 构建目录执行 `ninja -C <dir> -j 4`
增量编译/链接，三个目标均退出 0。首次 C3 旧 CI context 缺少无线预算的预检失败，
随后使用当前 `prepare_ci_build_context.py` 在本任务目录创建新 context 完成构建，
没有修改旧 context 或放宽预算校验。

- 生产 Queue：乱序回收、容量与字节上限、旧 token、flush 截止点。
- 生产 Broker：8 个相同帧、逆序完成、descriptor 重用、callback-before-return、
  abandon 后保留 storage、deinit 后全部回收。
- 生产 Session：窗口填充/补充、跨窗口 flush、close 保留在途 payload。
- 生产 Pump + Session + Broker：不调用 runtime service，验证队列饱和重试、
  立即补充窗口、空闲停止、runtime replacement 重用 boot-owned timer。
- Public Future：可写等待、close 失败、取消释放 observer；MQuickJS options
  capture 的 getter、GC 和第 N 次分配失败。
- Actual SDK：C3/C5 调用点执行；C3/S3/C5 hash guard、指令不变、relocation 链接。
- Hardware / RF / 吞吐 / 长时间 soak：`not-run`；当前连接设备仍运行上一轮固件。
