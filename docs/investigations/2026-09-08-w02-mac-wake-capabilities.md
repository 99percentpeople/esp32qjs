# W-02：MAC、唤醒锁与能力发现

基线仍为 firmware `d7db8d1` 与本地 ESP-IDF
`fff9895c82d744c7237be8847347bdd1b07c6643`；本文件记录工作区增量，
不代表提交或 W-02/Wi-Fi 整体完成。正式契约见 [wifi API](../api/wifi.md)。

## 实现与边界

| API | 原生边界 | 失败/关闭语义 |
| --- | --- | --- |
| setMac | initialized/stopped/零 owner，排除 operation/lifecycle/promiscuous/fault；在同一个 task mutex 内检查其他编译接口 MAC、写入并精确读回 | 预验证不写；写成功读回失败的错误保留 driverAccepted=true；不写 base MAC/eFuse，不声称持久化 |
| acquireWakeLock | started 健康 driver，最多 16 个 generation/identity token；无新 data-owner lease | native 失败不建立记录，identity 不回绕复用；不隐式启动/连接 |
| WiFiWakeLock.close | exact token 校验与 native release 同 mutex；成功后清记录/JS opaque | 重复/旧 token 不调用 SDK；失败保留记录和对象可重试；GC 后仍失败的 native 记录留给 runtime retirement |
| capabilities | 静态 build gate/实际 API + 只读 country 快照；不初始化 driver | 无 native country 时静态发现仍成功，channels=null 与 countryError；JS OOM 不改变 driver |

`setMac` 接受精确 17 字节 hex-colon unicast/nonzero 地址，接口名拒绝 NUL
后缀；Station、编译启用的 AP/NAN 地址冲突均先拒绝。AP 编译禁用时不能访问
AP MAC。MAC 的禁用接口要求来自固定 SDK `esp_wifi.h`；SDK eth2ap 示例也在
start 之前设置 AP MAC。仅靠“不连接”不足以满足本契约，必须停止整个 driver。

唤醒锁根据固定 SDK `esp_wifi.h` 的 force-wakeup 成对引用规则实现；driver
必须 started。它只保持 RF 活跃，不产生连接、不固定信道，也不授予其他 mutation
权限。stop、shutdown 及 lifecycle begin 检查 outstanding count。JS/native
分配全部先于 native acquire，native 成功后设置 opaque 不分配 JS 对象。

关闭失败不得无条件清零计数。runtime teardown 先尝试所有存活 wake token，
再清理 AP/STA；成功 token 删除，失败 token 保留。下一次 runtime attach
先重试尚未释放的项，仍失败则拒绝 attach。GC 失败后的 orphan 无公开当前
runtime reaper；它仍阻止 stop，等待 retirement 重试，不能声称 restart 一定恢复。
`status().radio.wakeLocks/wakeLockError` 提供 outstanding 计数和最近 native 错误。

静态新增 wake registry 为 16 × 8 字节，单调 identity 为 4 字节，Radio 计数和
错误为 8 字节；每个活 JS handle 的 native token 为 8 字节，另有 allocator/JS
开销。registry 仅在 Wi-Fi feature 启用时编译；不建立新 pool/budget 系统。
能力发现没有持久队列或 native pool，所有返回对象均是独立、受 GC root 保护的快照。

能力 flags 表示框架公开 API，不能由 SDK symbol presence 或芯片硬件能力填 true。
当前 mode 为 station/编译启用的 softAP；不包含 APSTA。namespace 仅有实际
注册的 csi。物理 5 GHz gate 与 SoftAP 的当前 2.4 GHz 参数范围分别说明；法规
查询失败或 5 GHz 隐式 country mask 返回 null，而非猜测国家表。
scan/AP/wake limits 与 production admission 共用常量，避免复制数字漂移。
完整 security、protocol/bandwidth 与 Build Context identity 仍需对应字段契约。

## 验证与后续阶段用例

本批执行 immutable C5 Build Context 编译与必要语法/生成物一致性检查。
日志和 hash 由 `build/w02-discovery-evidence.json` 记录。Host C/Python 全套、
故障注入、其他 target/feature-disabled 构建及实机功能测试均保留为阶段 not-run。
不把前批通过数用于新 API；无刷写、串口写操作、workspace 改动或提交。

Wi-Fi APIs 完成后集中验证以下 production 路径：

- MAC：格式/NUL/零/多播/相同接口碰撞、其他 owner、running/lifecycle/fault
  拒绝；collision getter、setter、readback 各自失败；接受后 OOM；C3/S3/C5 和禁用 AP/NAN gate。
- Wake：第 17 项、native acquire/release 失败、identity 耗尽、旧 generation/
  重复 close、GC finalizer、teardown suffix 重试、新 runtime attach 拒绝；与
  stop/shutdown/scan/CSI/ESP-NOW 同时操作且不改变其他 owner 引用。
- Discovery：未初始化/正常 stopped/started/retiring/faulted 下状态不变；
  SoftAP/CSI disabled gate、C3/S3/C5 band、无效 country range 和 5 GHz mask；
  每次 JS 分配失败/移动 GC、native read 与 teardown 竞争、返回对象修改不污染后续结果。
- 最后实机确认 MAC 生效、wake 释放及 Radio 关闭重开；长时间 soak 等 BLE API 完成后执行。

这批增量没有完成事件 broker、configure/APSTA、Monitor、Raw TX、CSI 新 wire
或 W-07/W-08 高级模块，完整剩余工作见[清单](2026-09-08-wifi-api-remaining.md)。
