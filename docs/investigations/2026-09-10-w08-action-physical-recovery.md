# W-08 Action/ROC 物理恢复的原生阶段

接续 [SDK 独立退休证明](2026-09-10-w08-action-quiescence.md)，本批增加无法获得该
证明时所需的内部物理清理阶段。基线 firmware `d7db8d1`、固定 SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`。尚无 runtime/public 调用方，不能
称为完整恢复 API；配置 checkpoint/replay、helper 协调与公开契约仍待接入。

## 已确认的交接缺口

普通 stop/shutdown 要求零 Radio owner；未完成的 Action/ROC 又必须保留 owner
直到原生责任终止。直接调用普通 shutdown 无法恢复，提前 release 则会错误地允许
复用 operation/generation。SDK 独立退休证明已覆盖原生全清空的情况，本批处理
取得物理终止证据所需的独占交接，不把 timeout、cancel 或 STOP 接受当作 deinit。

新增内部接口在 `esp32_mquickjs_wifi_action_radio.h`：

- `begin_action_recovery`：同一个 Radio mutation mutex 内验证精确 Action token、
  live lease、submitted/非 dispatch/非 cancel-busy 状态和显式提供的三个 managed
  Wi-Fi leases。拒绝其他 registry owner、wake lock、promiscuous/raw TX 子责任、
  未恢复的借用设置、竞争 lifecycle 和 once 初始化毒化状态。只预留 lifecycle，
  不调用 SDK、不释放任何 lease，不绕过非回绕 identity 容量限制。
- `stop_action_recovery`：managed leases 必须先由协调器退出；仅允许本次精确
  Action/ROC lease 存活，复用真实 STOP 提交和默认事件排空。成功 STOP 不授予
  native termination。SDK STOP 已接受但事件未到时只重试事件后缀。
- `shutdown_action_recovery`：调用者在 STOP 后退休旧 helper/netif，再进入此阶段。
  复用 vendor/raw callback 注销、channel handler 注销/entered callback 排空及
  `esp_wifi_deinit`。只有 deinit 成功才记录 exact Action operation 的物理终止。
  失败保留原始 stage/error、token 与 driver ownership，不伪造终止。

普通 stop/shutdown 仍使用 NULL allowed lease；临时 Raw TX rate 的已有独占 STOP
例外保留。Action 例外必须同时具有当前 lifecycle 和本次恢复 identity。保存的恢复
identity 为 boot 原生标量，lifecycle 结束后即无授权效果；identity 不复用，因此旧记录
不能授权另一个 lifecycle。新恢复覆盖这条记录，不增加列表或动态资源。

成功 deinit 先记录 driver_owned=false，再等待原 Action/ROC owner 退休。等待期间
返回 `action-retire`/ESP_ERR_TIMEOUT，保留原 Radio generation。原 owner 可在
mutex 外的下一次 worker/poll 调度中消费证明；协调器不能代替它丢弃 token。
再次 shutdown 只完成剩余清理，待精确 owner 释放后推进 generation，一次物理 deinit
不会因为退休调度延迟而重复。最后仍须 finish lifecycle 或完成显式配置恢复才能交接。

## 终止证明的消费

Radio Action cancel/retire 在 physical_termination 后不再发 SDK cancel、quiescence
query 或 event marker；证明来自原生 task deinit 和先前 callback 排空，不是补造的
完成事件。Action Future 等 submission worker 发布后以 `native-terminated` 结束，
既有提交错误保留其原因。ROC worker 记录 `roc-native-terminated`，退休 lease 并缓存
最终状态；不填充 terminal status。已结束 Future/已交付结果不会被改写。

Action 和 ROC status 新增 `nativeTerminated`，与 `nativeQuiescent` 区分。源类型及
API 文档同步，仍为唯一 v1；没有新增 callable 或占位 recover 方法。

## 验证范围

- immutable C5 context `build/wireless-contexts/c5` 构建通过；最终日志
  `build/w08-action-recovery-c5-build-verified.txt`，先前两次构建也成功。
- 新三个内部阶段在 Radio 对象中已编译，但因没有 runtime coordinator 调用而被
  最终 ELF 裁剪。最终 ELF 已包含共用 shutdown、termination helper、Action poll 和
  ROC worker 消费路径；不能用这些符号声称内部恢复入口已端到端接入。
- C5 binary 2,834,032 → 2,834,608 bytes（+576）；s_action 84 → 92 bytes，
  增加一个 8-byte 原生 lifecycle identity。前批其余 27 个记录的无线静态对象尺寸不变。
  这不是 live internal/PSRAM free、largest block 或 stack 的实机比较。
- Manifest 50 classes/481 functions、feature docs 27、live SDK STA/AP schema
  35/21、strict TypeScript、MQuickJS syntax 61 sources/55 snippets、SDK map 与
  whitespace 检查通过。TypeScript 首次命令误指向不存在的 web/node_modules 路径，
  修正为 root 已安装的 TypeScript 7.0.2 后通过，未安装依赖或构建前端。

七份 fixtures 仅 AST 解析，未导入、编译或运行：

- 新 `test_wifi_action_recovery.py` 组合真实 Radio registry、admission、STOP/shutdown、
  Action ledger、retire/finish lifecycle。注入 SDK、事件等待、callback drain、旁路 broker
  和配置 storage 边界，覆盖 owner 竞争、旧 token、借用设置、identity 耗尽、managed
  owner 退出、STOP/注销/deinit 失败后缀、物理证明与 generation 延迟退休。
- 新 `test_wifi_action_future.py` 使用真实 Action poll、Radio snapshot/ledger，覆盖
  worker publication 前保持 pending、物理终止及歧义事件诊断、保留原始提交错误。
  不替代完整 Future scheduler/VM/worker 并发集成。
- `test_wifi_roc_session.py` 补原生物理证明消费、无新增 SDK 查询/取消/marker，以及
  final handle/storage 释放；物理证明生产者由上项 recovery fixture 覆盖。
- 临时 rate fixture 补不应进入的 Action 分支边界声明；Action Radio、Action capture
  GC、ROC capture GC fixtures 同步纳入 AST 检查。

## 未完成

恢复协调器须保存可信配置意图、处理尚在运行的 helper/native 操作、精确转交 managed
owners、在 STOP 后退休 helper/netif，再调用 deinit；之后恢复配置和建立新 helper。
后续 [恢复 checkpoint](2026-09-10-w08-recovery-checkpoint.md) 已补在健康可读来源保留 operation 的配置捕获；完整 runtime 协调和配置重放仍未接线。
Raw TX 独立恢复和其他 W-01～W-12 缺口继续保留。全部阶段运行/故障注入、C3/S3/
feature-disabled 完整构建、实机/RF/heap 测试仍 not-run；长时间 soak 留到 BLE API
完成后。未刷写、操作串口、擦 workspace、提交、推送或更新 root gitlink。

日志、源码及 artifact hash 见 `build/w08-action-recovery-evidence.json`。
