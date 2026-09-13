# W-08：销毁流程推进已准入的恢复清理

在 `d7db8d1` 的未提交 Wi-Fi 工作区继续修复；SDK 为
`fff9895c82d744c7237be8847347bdd1b07c6643`。这是
[共享 FTM recover](2026-09-10-w08-ftm-recovery-public.md) 收尾，不新增公开 API。

## 原因与证据边界

原 `esp32_mquickjs_destroy_internal` 在 Future prepare 返回 false 时立即返回。
Future prepare 本身会遍历所有 slot，请求取消并轮询；一个原生 Future 仍 pending
就阻止执行后面的 Action/ROC/Raw TX/FTM 后台清理。即使 Future 都完成，原 FTM/
ROC owner 未退休也会再次提前返回；Wi-Fi runtime deinit 位于这些检查之后。

已取消/超时的公开恢复把精确生命周期留给中央清理，而原 owner 又可能需要中央
STOP、timer/SDK barrier、deinit 的物理证据才能退休。因而既有顺序存在相互等待。
证据是生产源码中的返回分支及所有者依赖；阶段性测试尚未允许执行，本批没有
声称已取得运行失败或完整 runtime restart 的验证结果。

## 修复

- Core 保留 Future prepare 的结果，先让它请求取消，然后调用新增内部
  `esp32_mquickjs_prepare_wifi_recovery_runtime_destroy()`，继续服务全部原 owner，
  最后汇总 recovery、Action/ROC、Raw TX、FTM、Future 的排空条件。
- 新入口只处理 `s_wifi_configuration_cleanup` 中已有且经 Radio 精确 lifecycle
  确认的物理恢复。它调用现有中央清理执行器；不准入新的恢复、不复制/释放原
  owner 的 lease，不重放已取消恢复的配置。
- 成功 STOP/helper 退休等阶段不会因后续排空失败而重新执行成功的 SDK 后缀；
  原操作的 Future 或后台 worker 仍是唯一退休方。队列满或暂未调度时继续保留
  原 native storage 和中央生命周期，后续销毁重试继续推进。
- 如果中央清理先观察到原 owner 尚在、而该 owner 在本轮服务中才退休，本轮仍
  返回 false；下一轮销毁完成中央后缀。没有在单次调用中循环等待后台任务。
- 所有 drain gate 通过后才运行余下销毁、释放 JSContext 和 runtime 资源。
  其他 Future/Raw TX 仍未完成时，即使恢复清理已成功，也不能释放 runtime。
- 原 Monitor 先请求关闭的顺序保留。没有物理恢复准入时，新入口直接成功且不
  触碰 driver；普通配置清理继续走原 Wi-Fi runtime deinit。

## 延后运行的生产用例

`tests/c/integration/wifi/lifecycle/test_wifi_recovery_teardown.py` 在修复前建立，修复后仅做 AST 解析。
它组合实际 core destroy、Future prepare/slot clear/scheduler 判定、共享 recovery
poll/cancel/dispose、runtime/AP cleanup。VM/队列存储、无关原生 Future 和 Radio/
worker 完成是注入边界；真实物理证明由已有 Action/FTM Radio fixture 单独覆盖。

用例包含两类恢复、准入后六个阶段的取消、无关 Future 阻塞、原 worker 暂不运行、
不短路调度其他 native 清理、STOP/AP detach/STA detach/lifecycle finish 失败、
已成功后缀不重复、原 owner 排空后的中央清理、所有 gate 前不释放 VM，以及无
准入恢复时不隐式 STOP。没有 import、C 编译或执行这些 fixture。

## 构建与静态检查

- immutable C5 FTM enabled：2,875,808 bytes，相比前一批增加 64 bytes。
- immutable C5 FTM disabled：2,842,496 bytes，相比前一批增加 80 bytes。
- 两个最终 ELF 都链接内部恢复销毁入口，core destroy 实际调用该入口及 native
  清理准备；FTM 关闭时不链接 FTM 接口。此前跟踪的静态 native 账本尺寸不变。
- Manifest 51/492、features 27、配置 schema STA 35/AP 21、严格 TypeScript、
  SDK map、MQuickJS 61 sources/56 snippets、whitespace 与 8 份恢复 fixture AST
  检查通过。详情与 hash：`build/w08-recovery-teardown-evidence.json`。
- C3/S3/role matrix、Host/Python/VM、native 竞争/队列、实机/RF、GC/堆与
  largest block 比较均 `not-run`；长 soak 仍推迟到 BLE API 完成。
- 未操作串口、刷写、擦除、提交、推送、根 gitlink、共享 SDK 或前端构建。

本批修复此处的排空顺序，不意味着已覆盖不可读/已故障的恢复来源、全部 native
调用阻塞或 netif detach 失败，也不提升 Candidate 稳定等级。
