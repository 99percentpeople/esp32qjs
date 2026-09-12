# W-07 restart 与 helper/netif 的阶段边界

firmware `d7db8d1` 工作区增量，固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接 [AP/APSTA 启动协调](2026-09-09-w07-ap-activation.md)。本批拆出原生 restart
阶段入口，没有注册公开 `wifi.driver.restart()`，也没有接入 runtime helper 协调器。

## 实现及理由

原 Radio-only restart 连续执行 capture、shutdown/init、replay 和 resume。
这不能直接用于持有 Station/AP netif 的 runtime：旧 helper 必须保留到 STOP
及事件排空完成，随后在物理 deinit 前退休；新 helper 则必须在重放可能产生的
临时 START 前接入。仅在最终 resume 前准备 helper 不够。

新增三个仅供内部协调器使用的入口，共用原 checkpoint 和 lifecycle token：

1. `checkpoint_restart_lifecycle`：冻结策略和完整配置，完成最后一次 STOP，
   核对停止/零 owner/事件边界。捕获仍可能执行原有单频转 AUTO 的临时 Station
   周期；旧 helper 在此返回成功前不能退休。STOP 失败保留快照，允许原捕获端
   已有的安全后缀重试，不重复采集冻结原值。
2. `rebuild_restart_lifecycle`：要求匹配的策略和配置快照、精确 token、已证明
   停止且无 owner；调用原 shutdown/init，返回停止状态。调用方必须先完成旧
   helper/netif 的退休，不能将这一入口当成替调用方停止和退休资源的方法。
3. `replay_restart_lifecycle`：要求新 driver、已知 storage、停止且无故障；
   选择目标 mode 并重放原配置和启动前策略，返回停止状态。调用方必须先接入
   新 helper。即使最终目标 AP-only，双频准备仍可能临时启动 Station，因此
   Station helper 也需要准备，重放后再退休不属于最终 mode 的临时 helper。

三个入口只在 driver mutation 期间持有 Radio mutex；调用方可以在入口之间执行
原有 event-task detach/fence、回调排空和 netif 释放/创建，而 lifecycle reservation
始终保留。最后仍用原 `resume_lifecycle` 发布所有真实 owner；本批没有提前发布
lease、复制额外秘密或增加常驻 phase 字段。helper 是否退休/接入由 runtime
协调器负责，Radio 本身不检查 netif 对象，不能把本批当作 helper 顺序已受验证。

已重建 generation 不能重新进入 checkpoint 并把新 driver 当作原值来源。恢复
失败保留 token/原快照；下一次物理尝试必须显式 quiesce、退休新 helper，再调用
rebuild/replay。Radio-only 便捷入口复用三个 locked 实现，适用于没有 runtime
helper/netif 的首轮调用；Wi-Fi-enabled 路径不通过反复调用该便捷入口自动恢复
失败的新 generation。feature-disabled 路径保留原逻辑，运行验证仍待执行。

这只是协调边界重构，不声称已经复现或修复实机 netif 故障。停止 driver 没有提前
TX-power/channel 快照时依然拒绝捕获，不推测旧值；公开 restart 的停止前快照
保留、helper 接线、中央失败清理及完整配置范围仍需完成。

## 证据边界

C5 immutable Build Context `build/wireless-contexts/c5` 生产构建 exit 0，日志
`build/w07-restart-phases-c5-build.txt`。本批入口在生产对象中编译，但由于没有
公开/runtime 调用，最终 ELF 将其移除；不把编译结果写成运行证据。
binary 保持 2,780,080 bytes；九项原有静态账本不变，checkpoint 688 bytes、
restart control 40 bytes。没有实机 heap/碎片测量。

新 `test_wifi_restart_phases.py` 复用真实配置捕获、策略账本、阶段准入和重放，
编写了旧 token、缺快照、错误 mode、owner/wake 占用、OOM、STOP 失败后保留与
后缀重试、新 generation 禁止重捕获、重放写入失败后禁止重复 mutation 的用例。
物理 shutdown/init 仅设拒绝路径 trap，成功的新 generation 由已有 SDK reset
fixture 提供；没有用该 fixture 声称已覆盖真实物理重建或 netif 顺序。
本批只做 AST 解析，没有导入、编译或执行 fixture。

manifest 49 classes/469 functions、feature 文档 27 项、live SDK config schema
35 STA/21 AP、MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map 和
whitespace 检查通过。完整 hash/对象与链接边界见
`build/w07-restart-phases-evidence.json`。

Host/Python/VM/竞争、物理 restart、helper/netif 故障注入、C3/S3/disabled 构建、
实机/RF/共存均 **not-run**。全部 Wi-Fi API 完成后统一阶段测试及实机功能验证；
长 soak 留到 BLE API 完成后。未刷写、串口操作、擦除 workspace、构建前端、
提交、推送或更新根 gitlink。完整未完成范围继续以
[Wi-Fi 剩余清单](2026-09-08-wifi-api-remaining.md)为准。
