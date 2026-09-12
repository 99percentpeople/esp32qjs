# W-08：FTM 共享运行时恢复与公开 recover

本次在 firmware `d7db8d1` 的未提交 Wi-Fi API 工作区上继续实施，SDK 保持
`fff9895c82d744c7237be8847347bdd1b07c6643`。没有运行 Host/Python/VM fixture，
没有实机操作；下面区分实现、构建证据与尚缺的验收。

## 实现

- `wifi.ftm.recover(options)` 在 SDK FTM initiator gate 下注册，为实际 native
  Future；严格捕获原操作的 sequence/radioGeneration、allowDisconnect 和
  timeoutMs。完整公共字段见 [FTM API](../api/wifi-ftm.md) 与源 `.d.ts`。
- 原 Action recovery Future 移到 `wifi_common/esp32_mquickjs_wifi_recovery.c`。
  两个 binding 共用捕获、轮询、结果/错误 rooting、取消/销毁和一个恢复资源 key；
  按内部 request.kind 区分 Action/ROC 与 FTM，无后续功能的占位 kind。
- 原 Action 专用 runtime/AP helper 名称改为共享 recovery 名称。配置快照、AP
  校验内存、helper 退休、成功重放和失败后的中央清理沿用原执行器。
- Radio 路由只选择原生阶段，不持有另一份全局恢复状态。原生阶段在自己的
  mutation mutex 内复核精确 lifecycle；选择之后失效的 token 不能取得新操作权限。
- FTM 的 STOP 后新 timer marker、SDK queue barrier、deinit、原 Session worker
  丢弃报告并退休 owner 继续走 [原生实现](2026-09-10-w08-ftm-physical-recovery.md)。
  协调器等待原 owner；只有成功排空后才重建，不重做测距、不重连 Station。
- 审查发现共用错误转换仍写死 Action 方法名/通用错误码；已改为按 request.kind
  返回 `WIFI_FTM_RECOVERY_*` 或 `WIFI_ACTION_RECOVERY_*` 及对应 operation。
  第一次构建的 unused recovery_code 警告是静态发现线索，不是已执行竞争复现。
- 注册器、源类型、唯一 v1 manifest、capabilities、SDK map 和 API 文档同步。
  Manifest 指向实际共享 `recovery_register` 安装函数，不放宽生成器校验。

## 资源与失败边界

可信健康 STARTED Station/APSTA 来源才可准入；已 faulted/不可读配置、未知策略、
其他 owner 或不安全 helper 状态仍拒绝。此范围没有解决所有 Radio 故障来源。

公开 Future 结束不等于物理操作停止。已准入的取消/超时保留中央生命周期和原
Session storage；销毁仅安全擦除/释放调用方 AP 校验副本。用户可通过 wifi.stop()
推进未完成清理，完整成功恢复不自动重放已经结束的公开尝试。

本轮额外确认 runtime teardown 顺序仍需收尾：`esp32_mquickjs_destroy_internal`
先调用 Future prepare，随后要求 Action/Raw TX/FTM owner 全部排空，之后才调用
Wi-Fi runtime deinit。已取消的 recovery 若仍需中央 STOP/deinit，原 owner 又在
等待物理退休，可能无法到达中央清理。此处为源码可达性缺口，未执行竞争复现；
后续应先形成真实销毁调度 fixture，再在不抢原 owner 的前提下推进已准入清理。
不得声称本批已经完成 runtime restart 验收。

## 验证与交接

- 合法 C5 FTM 启用、普通 FTM 关闭两个 immutable Build Context 构建通过。
- Manifest：51 classes / 492 functions；feature docs：27；SDK 配置 schema：
  STA 35 / AP 21；严格 TypeScript、SDK map、MQuickJS 61 sources / 56 doc snippets、
  whitespace 检查通过。
- 最终 ELF 已链接公开 FTM recover、共享 Future/协调器及全部原生 FTM 恢复入口；
  原生 SDK weak `ftm_initiator_cleanup` 保留实际实现。FTM 关闭不链接 FTM 接口。
- 7 份恢复/中央清理 fixture 只做 Python AST 解析。新增内容使用生产 request 声明、
  捕获/转换/执行器/原生 Radio 路由，覆盖双 kind、错误 kind、精确身份、超时/
  取消和原 owner 排空；未 import、未编译、未执行。
- 产物、符号/尺寸、源文件 hash 和检查记录保存在
  `build/w08-ftm-recovery-public-evidence.json`。静态符号尺寸不是空闲堆/PSRAM/
  largest block 或实际生命周期内存证据。
- Host/Python/VM、C3/S3/role matrix、GC/队列竞争、实机/RF、堆比较均 `not-run`；
  长 soak 继续延后到 BLE API 完成。没有 flash、serial、erase、提交、推送、
  根 gitlink 更新、SDK 修改或前端构建。

后续更新：[恢复 teardown 收尾](2026-09-10-w08-recovery-teardown.md)已修复上文的排空顺序，生产调度 fixture 尚未执行；此文构建数字保留为当批证据。
