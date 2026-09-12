# W-04：Raw TX 物理恢复阶段

本批在 firmware `d7db8d1` 的未提交 Wi-Fi 工作区继续实现，SDK 保持
`fff9895c82d744c7237be8847347bdd1b07c6643`。没有注册新的公开方法；完整
`wifi.rawTx.recover` 及恢复配置快照/重放仍待后续接入。

## SDK 边界

固定 SDK 的 `esp_wifi.h` 明确 TX callback 在 Wi-Fi task 执行。C5 的
`esp_wifi_register_80211_tx_cb` 写 `g_ic + 0x1d0`，C3/S3 写 `g_ic + 0x1c0`；
这只是函数指针写入。`ieee80211_freedom_inside_cb` 可能已读取该指针，再
构造 TX 信息并间接调用；因此 unregister 返回且 broker 活跃 callback 为零，
均不能单独证明没有尚未进入 broker 的旧回调。

三个目标的 `ieee80211_freedom_init` 将 inside callback 注册到 TX callback
表，deinit 清空指针并注销；`ppTask` 包含发送完成处理，成功 driver deinit 的
caller-task 后缀会删除 Wi-Fi task。恢复使用已有、经固定 SDK 审查的同步
Action SDK queue fence：注销之后、deinit 之前等待旧 task work 返回。这里
需要队列屏障，不把“off-channel context 全零”当作 Raw TX 完成条件。

原 archive/object/section/hash 与反汇编在 `build/w04-raw-recovery-sdk/index.json`。
SDK 原仓库没有修改。源码/对象审查并非中断、调度、RF 或完整生命周期实测。

## 原生实现

内部头 `esp32_mquickjs_wifi_raw_tx_recovery.h` 提供 begin/active/stop/
check-stopped/shutdown/finish，实际实现位于生产 Radio。

- 准入要求精确 broker sequence、physical generation 和原 Radio lease identity；
  已返回的原生 submit、可信健康 STARTED driver、无 competing lifecycle/operation、
  wake/promiscuous/interval/Vendor IE 冲突，以及可列举的 managed Wi-Fi owners。
  未完成 submit、过期 token、无关 owner、已故障/不可读来源不准入。
- 可保留原 Raw TX 临时速率 lease，但必须精确归属当前 owner，当前速率接受
  记录可信、write identity 一致、前值有效且没有 restore_pending。失效/失败的
  临时速率恢复仍是后续故障来源缺口。
- boot 生命周期 identity 不回绕。24-byte 恢复记录仅含精确 token/lifecycle
  与 STOP/SDK-fence 完成标志，不存 Session/JS/runtime 指针，不分配第二份 payload。
- STOP 的 Raw TX 例外只属于该精确恢复 owner。常规临时速率恢复的原约束继续
  生效；无关 owner 和 Raw TX pin 不能借用此例外。成功 STOP/event fence 才
  记录 stopped，不能由先前未启动状态推断物理终止。
- Raw TX 原 owner 在恢复期间即便收到普通完成也不能先 retire。关闭阶段
  quiesce 精确 broker，完成 SDK 队列屏障、注销/排空 channel callbacks，然后
  deinit。只有成功 deinit 后调用原 broker termination handoff，并清除精确
  临时速率所有权；SDK 错误或 STOP 不产生该证明。
- 原 Future/Session worker 独立读取 native_terminated，退休 broker/pin、释放
  原 lease。协调器持有的栈上 identity-only 引用仅用于验证 STOP/shutdown，不
  获得原 mutable lease 的释放权限。
- broker 退休之后但 lease 尚未释放时仍返回 raw-tx-retire timeout，保留物理
  generation。所有原 owner 排空后才推进 generation，最后 finish 清除恢复记录。
- 后续 suffix 重试不重复已接受 STOP、成功 callback 注销、已完成 SDK fence 或
  成功 deinit。始终保留原发送结果/归属诊断，不伪造 TX success 或对端送达。

## 尚需接入的部分

本批专用原生入口在 C5 Radio object 中编译，但因为没有协调器消费者，未链接
到最终 ELF。共享 STOP/shutdown/retire 保护与恢复记录已链接。没有将新 kind 放入
公共 Future 路由或 manifest，也没有注册空壳 recover。

下一步应将 Raw TX 恢复接到共享配置 checkpoint/runtime/Future：临时速率必须
冻结原 lease 的前值，不能在重建后把临时设置当作永久配置；物理销毁后已没有
旧 driver 可用于执行通常的速率恢复。成功重放、超时中央清理、Session/periodic
终止通知，以及失败恢复来源均需继续完成。当前 native begin 对健康 temporary
lease 的接纳不等于已完成其配置恢复。

## 验证

- FTM enabled C5：2,876,624 bytes；普通 FTM disabled C5：2,843,296 bytes。
  都使用既有 immutable Build Context，构建通过。
- 新 `s_raw_tx_recovery`：24 bytes；之前跟踪的 Radio/FTM/Raw TX 等静态账本尺寸
  不变。不是实机堆、largest block、PSRAM 或 GC 生命周期验证。
- 新 deferred `test_wifi_raw_tx_recovery.py` 调用真实 Radio begin、registry、STOP、
  shutdown、retire、finish；SDK/broker snapshot/rate storage 为注入边界，真实
  broker/rate ledger 另有已有 fixture。覆盖精确准入、普通完成不得提前退休、
  STOP/event/SDK fence/deinit 失败、原 owner 排空、临时 lease 物理清除及旧 token。
- 同步补齐现有 Action/FTM/Raw TX Radio/临时速率 fixture 的无关恢复边界；本批
  全部 fixture 只做 AST 解析，未 import、编译或运行。
- Manifest 51/492、features 27、STA/AP schema 35/21、严格 TypeScript、SDK map、
  MQuickJS、whitespace 检查见 `build/w04-raw-recovery-evidence.json`。
- Host/Python/VM、C3/S3/full gate matrix、实机/RF、GC/队列/runtime restart 与内存
  比较仍 `not-run`。长 soak 延后到 BLE API 完成，不提升 Candidate 稳定等级。
- 无 flash、serial、erase、commit、push、根 gitlink 更新、共享 SDK 修改或前端构建。

后续：[公开 Raw TX recover 与临时速率前值重放](2026-09-10-w04-raw-recovery-public.md)已接入；以上原生批次的构建尺寸和未链接状态为历史证据。
