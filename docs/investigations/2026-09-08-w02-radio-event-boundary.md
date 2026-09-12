# W-02：Radio START/STOP 事件完成边界

基线 firmware `d7db8d1`，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
上轮完成中央 AP/STA runtime 清理，本轮将启停事件等待接入生产 Radio。公开
configure/APSTA 仍未完成，完整 Wi-Fi 功能与最后实机目标保持不变。

## 原实现与变化

原 Radio 在 esp_wifi_start/stop 返回成功时立即完成内部状态切换；Station 自己
额外等待 START，AP 启动及 native lifecycle handoff 没有同一完成边界。Station
还有本地 started=true 的提前返回，可跳过共享 Radio 的 owner/fault 检查。

此次复用现有 Radio Wi-Fi event listener，观察 STA/AP START/STOP，并增加一个
boot-owned control marker handler。Radio transition 保存有限的 mode mask、
phase、uint32 identity/revision，不保存 JS/runtime 指针或 SDK payload 地址。

- 在调用 native start 前建立 START phase，完成所需 START 后再投递 marker。
  已运行 mode 扩展也等待新增接口的 START；不把其他 role 的事件当作完成。
- SDK start 接受但事件等待失败时，保留 started/stop_required 与 start-events
  故障，不能再次提交 start 或把迟到事件追认为旧调用成功。
- Stop 先记录 expected mask。SDK 接受后保存 stop_submitted，等待所需 STOP、
  live mask 归零，再等待 marker。超时/队列满后重试不重复 esp_wifi_stop。
- 后续 native START/STOP 会提升 revision，使已排队的旧 marker 失效；新的
  marker 必须排在该 native event 的其余 handlers 后。不同 phase identity 的
  marker 也不能串台。identity 耗尽不回绕，并保留设备重启要求。
- Native event callback 只更新短 snapshot lock 内的状态，不等待 Radio mutation
  mutex。SDK/post/cooperate/wait 均在 critical section 外。marker 不依赖 JS
  event queue 或 watch 消费者。
- Native wait 使用现有 native_wait_begin/end 与 cooperate（当前 runtime hook
  只喂 watchdog/检查 deadline/control，不执行 JS poller），保留中断与 deadline
  语义。START wait 5000 ms、每次 STOP wait 1000 ms；SDK 调用不承诺硬 deadline。
- Station ensure_started 无条件通过 Radio 的精确 lease/fault 验证；本地 START
  bit 不再提前返回。Radio 完成后再确认实际 mode 包含 STA并更新 helper 状态。

该代码已影响 Station、SoftAP、ESP-NOW/CSI 共用 Radio、内部 restart/handoff。
未以 FEATURE_WIFI 禁用来绕过底层事件约束；正式 AP 能力仍受原有 SDK/owner gate。

## 诊断与边界

radio status 新增 eventPhase/eventIdentity/eventExpectedMask/eventSeenMask/
eventLiveMask/eventFencePending。mask 为 STA=1、AP=2；expected/seen 保留最近一次
transition，idle 时可供观察。faultStage 补 lifecycle-handler、start-events、
stop-events 及已有配置事务实际阶段；cleanupStage 新增 stop-events。

这里的 identity 是框架 marker identity，不是 native callback cookie。隔离依赖
原生终止确认及默认事件队列顺序；marker 不能证明未来 producer 已自动关闭。
旧 native 事件/漏事件的实机情况仍需阶段验证，不能只凭本代码宣称无竞争。

START 接受后 wait 失败，started 可以仍为 true，fault/phase/state 共同说明它尚未
形成可用生命周期。成功 stop 不清除旧 start-events 故障，显式 shutdown/reinit
后才能恢复；不声称 runtime restart 总能恢复。IP/关联/射频就绪有各自条件。

## 验证账本

C5 immutable Context 构建通过：app `0x27aa20` bytes，分区空余 17%。MQuickJS
syntax、manifest/feature、recorded SDK map、Python 文件语法与 whitespace 结果
以及 hash 记录在 `build/w02-radio-events-evidence.json`。

新增 Host C cases 调用完整生产 Radio，仅注入 SDK/default-event-loop/RTOS/core
wait 边界：start-event-timeout、stop-event-timeout、stop-event-queue、
event-marker-revision、event-apsta-stop、event-interrupt、event-exhaustion。
APSTA case 验证 Radio event masks/stop 并不等于 SoftAP 参数或 APSTA 公共契约测试。
新 Python test_wifi_start_events 调用生产 Station ensure helper，覆盖 cached
started 下的 Radio 错误、状态读取错误、非 STA/未启动状态以及成功路径。

用例已编写、未执行。完整 Host C/Python、三目标和 feature-disabled 构建、GC/
事件丢失/饱和/协作中断、实机功能和内存/RF，留到 Wi-Fi API 完成后集中测试。
长时间 soak 放到 BLE API 完成后。无串口、刷写、提交、推送或父仓库 gitlink 更新。
