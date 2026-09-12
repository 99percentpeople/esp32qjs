# W-08 Action/ROC runtime 协调与公开恢复

接续[恢复 helper 清理](2026-09-10-w08-recovery-helper.md)。已接入可读配置来源的
`wifi.action.recover` Candidate，包括精确准入、分阶段物理恢复、原 owner 排空和共享
重建/重放。不可读/已故障来源和完整运行验收仍未完成。firmware HEAD `d7db8d1`，
固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。

## 实现与边界

公开 options 为必填 `sequence`、`radioGeneration`，可选 `allowDisconnect` 和
`timeoutMs`（默认 10000，范围 1–60000）。框架 identity 范围 1–UINT32_MAX；原生
8-bit operationId 不是恢复身份。全部字段严格预验证并捕获后才可能占用生命周期。
排队期间目标退休/变化会被原子准入拒绝，不转而恢复新的 Action/ROC。

AP admission 先检查独立清理/协调器，再把其 managed lease 与 application/Station
lease 一并交给真实 Radio 精确准入。成功后绑定同一 lifecycle 并释放 AP managed lease；
原 Action/ROC owner 仍由其自身持有。SoftAP-disabled 分支不构造 AP helper/lease。
AP 配置验证 storage 在准入前分配，失败不会留下部分交接。

runtime 使用调用者拥有的原生进度记录；唯一恢复权威仍是已有中央 lifecycle。
每一步验证 identity，公开 stop 或 runtime 清理若已消费它，旧 step 返回
`recovery-lifecycle-lost`，不碰随后取得的新生命周期。

步骤为授权 Station 断连/排空与配置 checkpoint、STOP、AP 退休、Station 退休、
物理 shutdown/原 owner 排空，最后进入既有重建/重放。每次 step 返回调度器，原
Action Future/ROC worker 继续消费物理终止证明。shutdown 的 TIMEOUT 保留当前步骤，
不重新 capture、释放原 token 或提前重建。SDK 调用与 mutex 不可抢占，单步骤仍可能
占用 runtime；只在已完成的步骤间让出，不把 timeout 声称为硬执行上限。

原 owner 退出并且 Radio 确认 UNINITIALIZED、无 driver/leases/operation/未完事件后，
新增 `finish_action_recovery` 只撤销恢复特例，保留 lifecycle 和两份冻结快照。
随后使用从现有 restart 提取的同一个 `wifi_restart_restore_interfaces`：rebuild、
helper prepare、原配置/策略 replay、启动前后读回和最终 lease 发布。撤销特例使
后续新 driver init/replay 失败回到普通中央清理，包括普通 FAULTED 状态的清理权限。
未复制另一套配置重放，也未通过丢弃 owner 满足准入。

公开方法直接调用时等待 Future；也可用 `Future.call`。恢复 Future 与 Action send
使用不同 resource key，避免原 Future 还未退休时恢复阻塞它。它不持有 worker/JS
pointer；capture 后的 caller state 在 timeout/cancel/destroy 可释放，已准入的物理
清理由中央 lifecycle 保留。取消/超时后不继续成功重放；可检查 status 后完成 stop
清理。排队取消无 driver 副作用。

恢复重启 managed Wi-Fi/AP，AP 客户端会断开；已连接 Station 需要显式
allowDisconnect。ESP-NOW/Monitor/CSI/Raw TX 等外部 owner 不被接管。成功仅返回
sequence、previousRadioGeneration、radioGeneration；不重发 Action、不改写原结果、
不自动重新连接 Station。返回对象 OOM 可能发生于已成功恢复之后，仍须先读状态。
可读来源包括原 checkpoint 支持的已知配置/策略；未知隐藏 PHY/无可信 getter/已经
faulted 的源没有被包装成“恢复成功”。公开契约见[API](../api/wifi-action.md)。

## 检查

- immutable C5 context `build/wireless-contexts/c5` 的内部协调、首次公开绑定和最终
  编译均 exit 0。日志分别为 `build/w08-recovery-runtime-c5-build.txt`、
  `build/w08-recovery-public-c5-build.txt`、`build/w08-recovery-public-c5-build-final.txt`。
- 最终 ELF 已链接公开 recover/Future 注册、runtime begin/step/dispose、AP handoff、
  Radio admission/checkpoint/STOP/shutdown/finish 和共享 restore；入口不再被裁剪。
- API manifest 50 classes/482 functions，feature docs 27，live SDK schema STA/AP
  35/21，strict TypeScript、MQuickJS 61 sources/55 snippets、SDK map、whitespace
  检查通过。Radio action-retire 与中央恢复阶段补入公开 status 类型。
- 此前跟踪的 28 个无线静态对象尺寸不变。C5 DWARF 显示新增 recovery Future state
  56 bytes，内嵌 runtime state 28 bytes；SoftAP build 准入前临时 wifi_config_t
  184 bytes，失败/完成后 dispose 使用 secure-zero/free。复用原全局凭据 checkpoint；
  不把静态尺寸比较当作运行 heap/largest block 或 stack 测量。
- 证据、最终 binary 尺寸和源/产物 hash 见 `build/w08-recovery-public-evidence.json`。
  初次 pyelftools 读取可重定位 object 遇到 unsupported relocation，随后改读最终
  ELF 的同一编译单元取得上述类型尺寸；未运行目标代码。

七份 fixtures 只做 AST 检查，未导入、编译、执行：

- 新 `test_wifi_recovery_runtime.py` 组合真实 runtime stepper、AP handoff 和共享
  restore，注入 Radio 物理阶段/owner 消费边界。写入准入/OOM/旧 identity、STA/AP/
  APSTA、owner-drain 多次让出、逐阶段 dispose、重建失败转普通清理与旧 step 拒绝。
- 新 `test_wifi_recovery_future.py` 将真实公开 Future start/poll/cancel 接到上述
  runtime 生产函数，注入时钟与 wait scope，覆盖截止时间、原 owner 退出前不重建、
  queued cancel、准入后取消和等待预算。不是完整 Future-core 调度验收。
- 新 `test_wifi_recovery_capture_gc.py` 提取真实 MQuickJS options/result/error
  converter，准备 identity 边界、非法参数、getter 异常、移动 GC 和第 N 次分配失败。
- `test_wifi_action_recovery.py` 补实际 Radio finish handoff：deinit/owner 排空前
  拒绝、旧 token 拒绝、成功仅结束恢复特例而保留生命周期，普通 finish 仍可完成。
- 既有 `test_wifi_restart_runtime.py` 更新生产共享 restore 的提取，普通执行路径
  保留；共用 configuration cleanup 与 Action capture fixtures 纳入 AST 核对。

## 尚未完成

本批证明代码接线、编译和契约一致，未证明原生事件排序、GC/OOM 运行行为、实机
关闭/重开或 RF 恢复。已故障/不可读来源、Raw TX 恢复、高级 Wi-Fi 模块、统一预算与
诊断仍保留在[完整剩余清单](2026-09-08-wifi-api-remaining.md)。不提升稳定等级。

全部 Wi-Fi API 完成后统一 Host/Python/VM、C3/S3/feature-disabled matrix 与实机
测试；长时间 soak 留到 BLE API 完成后。本批无刷写/串口、擦 workspace、前端构建、
提交、推送或根 gitlink 更新。
