# W-08 FTM 原生物理恢复阶段

承接 [FTM 公开 Session](2026-09-10-w08-ftm-public.md) 与
[offset 恢复](2026-09-10-w08-ftm-offset.md)，本批补原生 Radio admission、checkpoint、
STOP、helper-retirement guard、shutdown 和 restore handoff。公开 recover Future、
runtime/AP helper 协调及中央 cleanup 分派尚未接入，不新增占位 JS 方法。
firmware HEAD `d7db8d1`，固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。

## 固定 SDK 的释放边界

C3/S3/C5 的 `wifi_station_stop` 在发布 STA_STOP 之前调用真实
`ftm_initiator_cleanup`。该函数 disarm/done 两个 legacy timer，释放 initiator
context，并在 context 已为空时仍检查、释放 report storage。因此不能把 Action
恢复后的普通 report getter 套到已 STOP 的 FTM；无 getter 报告也可能早已被 SDK
释放。真实归档 object、stop/cleanup/ioctl wrapper 反汇编在
`build/w08-ftm-recovery-sdk/esp32*/`。这属于固定 SDK 控制流证据，未执行 SDK/RF。

沿用已经审查的 ESP_TIMER_TASK 与 native ioctl queue 边界。STOP 完成后，先等
可能残留的旧 timer marker 返回，再强制创建一个在 STOP 之后 armed 的新 marker。
旧 token/revision 相同的已完成 marker 不能冒充这个新证明。新 TASK marker 返回
后，通过既有 Action SDK fence sentinel 排空 timer callback 可能提交的 native
queue work；这里不要求 off-channel record 已为空，后续 physical deinit 才负责
终止 driver。timer 和 queue 成功后各自记账，失败只重试未完成后缀。

随后复用原 shutdown 的 callback unregister/drain 与 deinit。只有实际 deinit
成功才记录 physicalTermination、reportConsumed/reportDiscarded，并清零复制条目
数。不伪造 FTM report/status，不清除既有 ambiguous 或原始错误。C5 ELF 仍需核对
实际链接的弱 cleanup 实现，不能只根据 public 头文件断言释放成功。本批最终 ELF
已确认 `wifi_station_stop` 调用该实现，`ftm_initiator_cleanup` 为实际 154-byte
弱符号函数；反汇编另存为 `build/w08-ftm-recovery-linked-*.txt`，不是空 stub。

## Radio 与原 Session 的权责

native begin 要求精确 submitted FTM token，健康可读且已运行的 Station/APSTA，
无 SDK dispatch、另一 lifecycle、wake/borrowed setting/其他 owner。由同 mutation
mutex 检查传入的 framework application/Station/AP lease；准入本身不做 SDK 调用。
开始恢复后，普通 end、collect、retire 暂停 SDK/退休路径，避免 STOP 清理报告后仍
读取 native storage。已进入的 SDK 调用先退出 mutation mutex 才能准入恢复。

复用原 Radio stop/shutdown、配置和策略 checkpoint：只有这个 FTM lease 可以
跨 STOP，helper 资源仍由调用者在对应阶段退休。capture 复用同一份秘密 snapshot、
home-channel/saved-PHY 查询与 offset pre-start replay，没有引入另一恢复状态机或
猜配置默认值。未知/已故障来源仍待完整恢复工作处理，不能绕过已有 capture gate。

physical deinit 后保留原 generation 和 FTM lease，shutdown 返回 TIMEOUT 等待
原生 Session worker。只有该 worker 调用 production retire 后才释放 exact lease；
随后 shutdown 的剩余后缀才能推进 generation。finish 只移除恢复例外，保留中央
lifecycle 和 checkpoint，供后续普通 restore 或清理使用。

Session 最后一次 native snapshot 若含 physicalTermination，会产生明确的失败
状态、禁止 reportReady，并释放自己的条目分配。这个归一化在所有 Radio 调用后
执行，覆盖第一次 status 与 collect/retire 之间才发生 deinit 的竞争。旧 JS handle
仍可读取诊断；关闭/GC 不会返回或复用已丢弃报告。

## 检查与剩余工作

C5 启用构建 `build/w08-ftm-recovery-radio-c5-build.txt` 通过；禁用构建为
`build/w08-ftm-recovery-radio-disabled-c5-build.txt`，同样通过。启用 binary 为
2,872,416 bytes（上一相同 context 为 2,871,216），禁用保持 2,842,144 bytes。
`s_ftm` 从 100 增至 112 bytes，其余 tracked wireless static 不变；DWARF 的
Session 仍为 136 bytes、native state 为 64、report entry 为 48。未测运行 heap
峰值或 largest block。新增 recovery entry functions 仅编译进 Radio object，因
尚无 runtime/public consumer 而被最终链接裁剪；共用 shutdown/Session 的物理
退休分支已链接，不能把它们的存在当作公开恢复已经可调用。

manifest 51/491、feature docs 27、live SDK schema STA 35/AP 21、strict
TypeScript、MQuickJS 61 sources/56 snippets、SDK map、whitespace 通过。
具体 ELF、静态尺寸、生成物
检查和 hash 见 `build/w08-ftm-recovery-radio-evidence.json`。新增
`tests/c/integration/wifi/ftm/test_wifi_ftm_recovery.py`，组合真实 Radio/Session/worker、STOP 与
shutdown helper，注入 SDK/event/timer/allocator/锁和无关 broker storage 边界。
覆盖 missing/ambiguous report、旧 timer、STOP 失败/迟到事件、SDK fence 和注销/
deinit 失败后缀、物理终止后原 worker 释放、报告丢弃与 generation 顺序。仅 AST
解析，未导入、编译、运行。完整 checkpoint/RF/runtime 竞争另留阶段验收。

尚需 runtime/AP helper 协调、中央 timeout/取消清理分派、public FTM recover
Future/诊断，以及完整恢复来源、角色/目标和 RF 验收。现有 FTM 运行时关闭仍不能
自行触发物理恢复。其余 W-01～W-12 目标保持原范围，Wi-Fi API 全部完成后集中
测试并实机验证，长期 soak 在 BLE API 完成后。未刷写、串口操作、提交/推送、
更新根 gitlink、改共享 SDK 或构建前端。
