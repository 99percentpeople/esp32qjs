# W-07 合格 STOP 来源的重启准入

firmware `d7db8d1` 工作区增量，SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`。承接
[停机捕获](2026-09-09-w07-restart-stopped.md)与
[停用接口恢复](2026-09-09-w07-inactive-deferred.md)。本批接入内部零 owner
准入与 runtime/AP helper 转交，没有注册公开 `wifi.driver.restart()`。

## 同一把锁内检查和占用

新增 `esp32_mquickjs_wifi_radio_begin_stopped_restart(token, mode)`。调用者
提供全零 token；输出 mode 由 Radio 解析，调用者不能通过参数更换恢复目标。
取得 mutation mutex 后，依次核对同代、同 STOP event identity、完整无写入失效
标记的 STOP 观察、driver ownership、已知 RAM/FLASH storage、健康 STOPPED
状态和原 mode 一致。没有合格历史、仍运行、停机后写入、故障或清理未完成均拒绝。

准入还拒绝临时 TX rate/interval owner 及其未完成恢复、旧配置 checkpoint 或
策略 checkpoint owner。停用接口的 pending inactive-time 意图仍按前批独立
规则保留，不把它误判为其他 owner 的临时借用。

随后在同一把 mutex 内调用生产 `wifi_radio_begin_lifecycle_locked`，三个允许
owner 参数全部为 NULL。真实 registry 的所有槽（包括 Application、Station、
AP 和其他 feature）必须为空；wake lock、operation、promiscuous claim、已有
lifecycle 均拒绝。identity 耗尽明确失败；成功只分配一个新的 token 并返回
保存的 STA/AP/APSTA 模式。SoftAP-disabled 构建拒绝 AP/APSTA。

函数没有 SDK 调用、分配或 owner release。driver 状态读取和占用之间没有解锁
窗口，不以调用前的 status 快照授权。无效输入保持输出；状态/owner 拒绝清空
mode，保留全零 token，不消耗 identity，也不覆盖原生故障诊断。

## Runtime 与 AP 转交

原内部 managed-owner executor 和新增
`esp32_mquickjs_wifi_restart_stopped_interfaces` 共用实际 checkpoint、helper
退休、物理 rebuild、helper 重建、replay、resume 和中央失败清理。旧内部入口
的授权范围保持原契约；新入口仅使用上述零 owner 准入。

运行任务先拒绝 Station 连接/异步操作/Future 和中央清理状态，再通过
`esp32_mquickjs_wifi_ap_begin_stopped_restart` 拒绝 AP 的独立 lifecycle、已有
coordinator、lease、cleanup-pending 和 netif detach error。AP 检查在 runtime
task 上执行；Radio registry 仍由 mutex 内的原子检查决定。成功把 AP 的退休
义务绑定到同一 token，未借用旧 lease，也未越权释放 owner。

SoftAP-enabled 时在原子选择 mode 前预分配一份有界 AP 验证缓冲区，即使最终
解析为 Station 也会暂时保留它；避免准入后才遇到该分配失败。所有出口安全清零
并释放。未新增常驻账本或 JS roots。准入后的失败保留 token 与中央清理状态，
后续调用不会重新执行已失败的重建；清理仍只推进未完成后缀。

这只完成已知健康 STOP 来源的内部准入。未知隐藏配置、停机写入/无 STOP 历史
来源、RSSI 一次性通知语义、完整故障恢复与公开参数/错误/状态绑定仍未完成。
没有将任意 `restartRequired` 故障描述为本入口可恢复。

## 检查和待执行用例

C5 immutable Context `build/wireless-contexts/c5` 构建 exit 0，binary
2,783,824 bytes，比前批增加 256 bytes。11 个既有静态账本大小不变；其中
Radio 824、restart control 40、STOP 24、inactive history 24 bytes。新增入口
已编译进对应 object，但无公开调用方，最终 ELF 未保留这些函数；编译不等于
功能可调用或运行验收。本批没有实机 heap/碎片测量。

新增 deferred `test_wifi_restart_admission.py`，提取生产 STOP predicate、
lease validation、lifecycle claim 和新准入函数。编写所有 registry 槽/角色、
状态/历史失效、恢复义务、身份耗尽、SoftAP gate、输入拒绝，以及在注入 mutex
入口改变 owner/generation/写入标记/mode 的用例。没有替代生产准入的测试状态机，
也未提供 SDK 函数；原生状态和调度入口是隔离边界，不证明真实 FreeRTOS 并发。

扩展 deferred `test_wifi_restart_runtime.py`，提取生产公共执行主体、两种内部
入口与 AP 转交函数，编写 AP pending/解绑错误/旧 token/lease 拒绝、预分配
失败、解析后模式恢复、临时 Station 准备失败及中央清理转交。Radio 重建和 SDK/
netif 仍为独立注入边界，不能替代真实生命周期证明。两份 fixture 只做 AST，
未导入、编译或执行。

manifest 49 classes/469 functions、features 27、live SDK schema 35 STA/21 AP、
MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map 和 whitespace 通过。
证据与 hash：`build/w07-restart-admission-evidence.json`。

Host/Python/VM/竞争、C3/S3/feature-disabled 构建、完整恢复、实机/RF/NVS 和
soak 均 **not-run**。全部 Wi-Fi API 完成后统一阶段/实机功能测试；长 soak 留到
BLE API 完成。未刷写、串口操作、擦 workspace、构建前端、提交、推送或更新根 gitlink。

后续 [公开 Candidate restart 接入](2026-09-09-w07-public-restart.md)已将本批内部准入连接到 JS wrapper 和真实重建路径；其余来源与运行验收仍未完成。
