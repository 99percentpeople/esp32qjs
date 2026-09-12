# W-07 公开 Candidate restart 接入

> 2026-09-12 更新：下文保留最初公开批次的证据。当前 restart 已扩展到健康、已初始化、
> 完整停止但无有效 STOP 历史的 STA/AP/APSTA；源 START/实际值捕获与清理语义见
> [当前 API](../api/wifi-driver.md#driver-restart)和[剩余工作表](2026-09-08-wifi-api-remaining.md)。
> 原“停机后写入/从未启动一律拒绝”不再表示当前实现；未初始化/off/故障来源仍待核对。

firmware `d7db8d1` 工作区增量，SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`。承接
[原子准入](2026-09-09-w07-restart-admission.md)和
[RSSI 请求契约](2026-09-09-w07-rssi-request.md)。本批新增真正 callable 的
`wifi.driver.restart(options?)`，连接已实现的物理重建与恢复执行器。完整 Wi-Fi
目标和 restart 的其余恢复来源没有删减；本方法仍为 Candidate，未做运行验收。

## 当前可调用范围

零 owner、健康 owned STOPPED driver、完整同代 STOP 观察且后续 tracked write
未使其失效，是当前公开准入条件。mode 从保存的 STA/AP/APSTA 来源在 Radio
mutation mutex 内解析，不能指定新模式或强制退休其他 owner。AP helper 的旧
coordinator/lifecycle/lease、pending cleanup 和 detach error 也会拒绝。

通过准入后，调用真实 runtime executor：预分配 AP 验证缓冲区、绑定中央/AP
清理 token、准备旧代临时 Station helper、捕获配置/最终 STOP、退休旧 helper、
物理 shutdown/init、安装新 helper、重放、启动核对和 owner 交接。最终恢复原
storage；成功返回真实 WiFiStatus。这里没有用于占位的成功返回或替代状态机。

当前主动拒绝冷/从未启动、停机后写入、无合格历史和故障来源。某些未知隐藏值
还可能在捕获阶段拒绝，留下已取得的清理义务。完善这些来源与完整故障恢复仍是
任务，不能把拒绝路径的存在写成恢复目标已经完成。没有自动连接 Station、恢复
AP 客户端、重开已关闭 session 或 RSSI rearm。

## 参数与可诊断失败

唯一 v1 `WiFiDriverRestartOptions` 只接受 timeoutMs，整数 1..60000，默认
10000 ms。共用的 rooted timeout capture 同时用于 stop，stop 默认仍为 1000。
未知参数、非 plain object、NUL key、小数、越界、额外参数均在 native admission
之前拒绝。不存在 force、requireExclusive:false 或隐式目标模式。

公共 wrapper 建立原生共享 wait scope，执行完后先结束 scope，再创建 JS 状态/
错误。该预算用于跨阶段 event/netif 等待，不能抢占同步 SDK 调用或 mutex 获取，
不声称硬实时 deadline。没有新增 Future/native job 或跨回调保存的 JS roots。

WIFI_RESTART_FAILED 保留原始 espCode/espName、执行 stage、是否取得 lifecycle、
是否进入 checkpoint/replay/resume、cleanupPending、restartRequired、当前
Radio fault 和 restartSnapshotBytes。checkpointAttempted 只代表进入捕获/STOP
阶段，不代表实际已经调用 SDK STOP。配置详情来自本次调用，checkpoint 之前为
null；原生 checkpoint wrapper 先建立新的 admission 记录，使早退不会误用旧
configuration 结果。Radio fault 字段明确是当前状态，可能早于本次准入失败。

错误对象分配由 GC roots 保护，不暴露凭据。失败后的清理保留同一 token，不
自动重试物理重建；wifi.stop/runtime cleanup 推进未完成后缀。清理成功可能丢弃
原来源并 deinit，不保证再次 restart 可恢复。若成功重建后返回对象分配失败，
driver 仍可能已经运行，调用者应先读 status。完整故障恢复协调仍待完成。

正式类型、registration、manifest、capabilities.features.driverRestart、API
文档和 SDK map 已同步。能力标志只表示方法 callable，不表示任意状态可重启或
硬件验收通过。WiFiStatus.cleanupStage 也补齐实际 executor 的 restart 阶段。

## 检查与证据

C5 immutable Context `build/wireless-contexts/c5` 构建 exit 0。binary
2,797,872 bytes，比前批增加 13,216 bytes。公开 wrapper、原子 Radio/AP 准入、
runtime executor、checkpoint capture、physical rebuild 和 replay 均链接到最终
ELF；此前仅存在于 object 的部分内部路径现已由公开调用保留。原 12 项静态账本
大小不变（Radio 824、restart control 40、STOP 24、inactive history 24、RSSI 16
bytes）。没有实机 heap/stack 峰值或碎片测量。

新增 deferred test_wifi_public_restart.py，提取生产 timeout capture、公开 wrapper、
配置/错误转换器，编写参数/默认预算/额外参数、wait 拒绝、准入与后续阶段错误、
旧配置记录隔离、逐 VM 分配失败/GC/结果 OOM 后不得重复 dispatch 的用例。这里
native executor 与 wait scope 是隔离边界；真实 Radio gate、executor、phase 和
wait 的用例分别在已有 fixture 中，不能以此替代完整重建证明。另扩展生产 phase
fixture 的早退新记录断言，stop capture fixture 接入同一共用 helper。三份文件
仅 AST，未导入、编译或执行。

manifest 49 classes/470 functions、features 27、live SDK schema 35 STA/21 AP、
MQuickJS 61 sources/54 snippets、strict TypeScript、SDK map、whitespace 通过。
证据与 hash：`build/w07-public-restart-evidence.json`。

Host/Python/VM/竞争、C3/S3/feature-disabled 构建、真实 STOP/rebuild/APSTA/helper/
NVS/RF/GC 与实机功能测试均 **not-run**，依用户顺序在全部 Wi-Fi API 完成后集中
执行。长 soak 留到 BLE API 完成。未刷写、串口操作、擦 workspace、构建前端、
提交、推送或更新根 gitlink。

后续 [停机后 storage/RSSI 写入边界](2026-09-09-w07-stopped-controls.md)已根据固定 SDK C3/S3/C5 证据允许两项写入保留原 STOP RF 历史；其他失效/故障/未知来源保护仍保留。
