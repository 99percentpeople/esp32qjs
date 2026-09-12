# W-04：Station 临时发送速率租约与保留 owner 的恢复

继续完整 Wi-Fi API 目标，firmware HEAD `d7db8d1`，SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`。本批建立在
[速率写入记录](2026-09-09-w07-tx-rate.md) 上；W-04/W-07 和总任务仍未完成。

## 准入与公共接口

wifi.rawTx.open 增加 rate: WiFiTxRateConfig，复用实际 driver rate capture；所有
嵌套 getter/enum/boolean 在原生操作前验证并保持原始异常。Session native new
再次验证配置与 interface，后台 Radio open 在 mutation mutex 内核实实际状态。

临时 rate 必须满足：Station、initialized、fully stopped、零 Radio owner/wake
lock/operation/lifecycle/promiscuous 使用者，无 driver fault，前值来自本 Radio
代次的成功框架写入。须至少留有本次写入和一次恢复的 identity 空间。首次未知或
跨代记录不伪装成默认速率。没有为临时 rate 隐式停机/断开其他 owner。

AP Raw TX 目前要求 AP 已运行，SDK rate setter 要求 start 前；AP rate 请求明确
拒绝。这不是 target-unsupported，也没有通过重启 AP 假装支持。AP 临时 rate 的
兼容生命周期契约留在总任务内。无 rate 的 Session 和 one-shot 保留原有共享方式；
one-shot / Session.send 没有另增 rate 选项。

capabilities.supports.rateLease=true，新增 rateLeaseInterfaces:[station]；正式类型、
任务书及 [Raw TX API](../api/wifi-raw-tx.md) 同步。稳定等级仍为 Candidate。

## 原生所有权与恢复

唯一临时记录保存 Radio generation/lease identity、最新写入 identity、前值、接口
与恢复错误，无 JS/Session/runtime 指针。先取得精确 Radio lease，再发布记录并写入
速率，最后启动；整个过程由 Radio mutation mutex 串行化。SDK 调用不在 IRQ
critical section。部分打开失败仍将已取得的 lease 交由原生 Session 清理。

临时记录存在时，新的 Radio acquire 和 wake acquire 被拒绝，普通 release 不得
释放该 owner。Session 保留共享 Raw TX arbiter grant，即使暂时无包；同接口其他
发送者因此等待自己的期限。原 service work 判定同步排除“仅持有独占 grant 的
空闲 Session”，避免每毫秒反复调度空 worker；有包/关闭/未完成结果仍正常处理。

close、GC、打开失败、runtime prepare 复用真实 Session worker：先退休 SDK packet，
再通过只允许此精确 owner 存活的 stop helper 完成 stop/event fence，随后重写已
捕获的前值。普通 stop/shutdown 仍要求零 owner，不开放通用忽略 owner 的开关。

恢复失败时保留 Session cleanup hold、Radio owner、前值及 arbiter grant，不允许
lane 复用；下一轮只重试未完成的后缀。stop 已接受但事件未排空不重复 SDK stop。
恢复使用最新写入 identity 核对记录，原值固定，不会在重试时改成当前临时速率。
重写前值失败而回滚到临时速率成功，仍属于恢复未完成，不能释放 owner。
所有写入 identity 永不回绕，持续失败耗尽后保持可诊断资源，不能假装恢复完成。

恢复完成后先解除临时记录，再释放 lease/grant；其他生命周期故障不被清除。
成功借用后若 driver start 失败，可恢复速率并退休 Session，但仍保留原 start
故障；这不是运行时重启已恢复 driver 的证明。

wifi.driver.txRateStatus 的 temporaryLease 在同一 Radio mutex 内快照，含
radioGeneration、radioLeaseIdentity、writeIdentity、previous、restorePending、
restoreError；未返回 JS 对象的失败打开也可诊断。restorePending 是原生恢复已进入
清理的状态，不等同于刚发出的 JS close 意图。完成后为 null。

TX completion 的 rate 现在映射目标 SDK 实际 enum 名，只有 native completed 时
才转换；未知值仍 null，rawRate 原样保留。没有推断 PHY/GI 时长、吞吐或 RF ACK。

## 验证边界

新增 test_wifi_raw_tx_rate_lease.py，准备在三目标 SDK inventory 类型上调用生产
Radio acquire/lease registry/stop/restore/release 和 rate helper，注入 SDK、event wait
与锁。用例覆盖未知前值/运行中/其他 owner/AP/identity 空间拒绝、旧 token、generic
release/stop 不能绕过、未退休 packet、stop fence 超时、恢复失败回滚及后缀重试、
初次写入和回滚双失败、借用后 start 失败仍恢复并保留原错误。

原生产 Session/queue/arbiter/broker fixture 增加独占 idle grant 不调度、恢复失败
保留 owner/grant 的断言；其 Radio 是注入边界，上述新 fixture 另测真正 Radio。
共享 VM capture 增加 rate 嵌套参数与 getter 异常，rate status converter 增加保留
前值/恢复状态的 moving GC 和第 N 次分配失败路径。适配现有 one-shot/Session
fixture 的 native signature、真实 SDK rate 类型及共用 decoder。8 份改动 Python
仅 AST parse，未导入、编译或执行。

C5 immutable Build Context firmware-ci-esp32c5-representative 编译 exit 0。
实际 ELF 已链接 stop-with-owner/restore helper、rate capture、Radio Raw acquire 与
Session new。binary `0x29a940` / 2,730,304 bytes，比前批增加 2,880 bytes，app 空余
13%。s_tx_rate_lease 新增 36 bytes，s_tx_rates=68、s_radio=824、s_lane=44、
s_raw_tx=128、s_retired=56、s_sessions=32、s_jobs=32 bytes；原有表未扩容。Session
及 JS 包装器的 options 增加原生配置值，动态 heap/峰值尚未测量，不能声称已做
实机内存验收或取消 W-09 预算工作。

MQuickJS 61 sources/53 snippets、manifest 49 classes/433 functions、strict TypeScript、
feature docs 27、config schema 35 STA/21 AP（live SDK）、SDK map 与 whitespace 通过。
证据/hash/符号见 build/w04-rate-lease-evidence.json。固定 SDK 工作区保持干净。

Host C/Python/VM/故障注入、真实任务并发、完整 Future open/close/GC/runtime teardown、
跨模块生命周期竞争、C3/S3/disabled 构建、实机/RF/heap 均 not-run；长 soak 待 BLE
API 完成后。缺少这些验证不提升稳定等级。后续仍有显式 Raw TX 故障恢复、AP 临时
rate、driver 其余 API、CSI correlated packet、Monitor 完整元数据、高级模块、资源
预算及最终 Wi-Fi 阶段和实机功能验证，详见[总清单](2026-09-08-wifi-api-remaining.md)。
本批未刷写、串口操作、擦 workspace、构建前端、提交、推送或更新父仓库 gitlink。
