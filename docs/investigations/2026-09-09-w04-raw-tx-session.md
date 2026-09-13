# W-04B 原生 Session、worker 与关闭所有权

firmware `d7db8d1` 工作区增量；SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批连接 [FIFO/flush 账本](2026-09-09-w04-raw-tx-queue.md)、[共享发送仲裁](2026-09-09-w04-raw-tx-lane.md)
及已有 Radio/broker。公开 `open`、Session 方法、per-send Future result、periodic
和 rate lease 仍未注册。正式类型/manifest 没有新增占位类或未展开的接口。

## 有界控制与引用

生产 header/source 为 `esp32_mquickjs_wifi_raw_tx_session.h/.c`，分别位于
`components/esp32_mquickjs/internal` 与同组件 `src/modules/wifi_raw_tx`。
最多 8 个 Session 控制，含关闭后仍被 caller/flush 引用的控制。每个 Session
持有一个静态 task mutex、一份独立 options、1–128 个 queue slots，以及既有
最多 8 个 flush watcher。control/mutex 使用 internal 8-bit heap；slots 单独
分配，不分配常驻 packet payload。W-09 仍需覆盖 control/capture/queue/SDK copy
和退休预算，不能用这里的数量上限代替完整预算。

Session generation 使用独立 boot monotonic counter，到 UINT32_MAX 后拒绝
继续创建。registry 只发布完全初始化的控制，失败已消耗的 generation 不复用。
创建先校验 options/target，再分配 Session/slots/mutex；任一失败回收已完成部分，
没有 Radio I/O。成功创建只代表 native storage 已建立，Radio opening 由 worker
完成；后续公开 open 必须等待 opening 结果，不能直接把 new 成功当作已打开。

初始 caller 和 native cleanup 各持一份引用；registry 指针受短临界区保护。
worker、临时 snapshot、flush watcher 各持独立引用。registry remove 与最后引用
释放在同一临界区判定，free/mutex delete 在临界区外；最终 free 要求 closed、
所有 active/queued packet 已退休及所有 watcher 已释放。公开 wrapper 的关闭/GC
必须释放其 caller 引用；后续 adapter 仍需明确关闭后 status 的保存方式，不能让
旧 JS owner 无故占满 native reopen 配额。

原生控制不保存 JS root、JSContext、runtime 或 task wake 指针。worker 通过
runtime-free background queue 执行；调度方保留自己的 snapshot 引用直到
xSemaphoreGive 返回，worker 有另一份引用，避免被唤醒的 worker 立即关闭后
释放仍被调度方使用的 mutex。不是把同一个引用提前转给 worker。

## 打开与发送

Session 的 task mutex 保护队列和可变记录；SDK/Radio 调用使用 worker local
lease/token 副本，全部位于 mutex 外。worker_busy 排除第二个 worker，完成一个
步骤后在 mutex 内发布记录。request_close 是 atomic flag，不等待 driver 返回；
status 的 active sequence 直接读取受 mutex 保护的 queue active slot，driver
调用正在运行时仍能观察到该包。其他 driver-owned 字段在 worker 发布后可见。

打开先请求共享 grant，再调用实际 `radio_raw_tx_acquire`，因此固定 channel
仍受统一 Radio owner 约束。成功后保留 Session lease；空闲时归还 grant。
numeric channel 的 fixed lease 持续至 close；current channel 在每次真实提交时
重新观察。每包都走现有 Radio 动态 association/DS/sequence 约束和 broker，
Session capture 不允许 caller 伪造连接状态。

admit 在所有 captured payload 完成 MAC validation 后，才锁住 queue 做整批
准入。输入已由 caller 独立持有，成功全部转入队列，eviction payload 转交给
caller 在锁外 free；错误第二帧不能导致第一帧先提交。尚未 open 或 close_requested
后拒绝新准入。公开 ByteSource/ArrayLike 捕获和 moving-GC owner 交接仍待 adapter。

只有取得共享 grant 后才 queue.take，不会先移走 packet 再因另一 Raw TX producer
占用 broker 将它记成失败。每个 Session 同时只有一个 native request/active packet。
完成后归还 grant，下一包重新排队，因此另一 Session 已准入的请求保留优先次序。
已开始 batch 的剩余帧仍由 FIFO 核心保护，不因暂时让出 grant 变成可 overflow
evict 的完整未开始 batch。

SDK 成功接受后更新 submitted；原生快照确认 exact token、driverCompleted 和
callback drain 后才尝试 Radio retire。必须先观察到完成再 retire，避免 callback
恰在旧 pending snapshot 与 retire 之间完成，导致归还操作却把旧结果当作 unknown。
Radio/broker retire 仍重新检查实际终态，新增调度 identity 不是 SDK cookie。
Session 只保留一条最后完成快照，不存无限历史，也不能凭这条快照实现任意
per-send Future；对应的精确 result watcher 仍待实现。

## 关闭、错误与 flush

close 停止新 admission、丢弃全部未发送 packet，但保留 active payload、broker
副本、exact Radio lease 和 grant。若 close 恰在 Radio submit 调用进行时发生，
该包仍可能 RF 提交；关闭不能声称撤销已经进入 driver 的操作。无 submit 的 active
packet 可明确 aborted；SDK 前拒绝且没有 native token 时记 rejected 并回收。
实际 SDK 已发布 token 后出错则进入 fault/closing，保留原始错误及不确定 ownership。

普通 MAC success/failed/unknown 都属于发送终态；SDK/Radio operational error
使 Session fault，停止其后续队列。首个 operational error/stage 不被 cleanup
后缀覆盖。close 的清理顺序为原生 retire → 本 Session lease release/idle stop →
已有 grant release。stop 失败保留 stop_pending；若 lease 已释放，重试只执行
未完成后缀。已持有的 grant 在这个后缀成功前不复用。原本空闲、没有 grant 的
Session close 只释放自己的 lease；仍由 Radio owner/mutation 边界防止错误停机。

缺失 completion/相关性错误/SDK 不确定失败可能长期保留 Session。当前没有显式
fault recovery，不能用 timeout、GC、runtime restart 或移除 callback 假装原生
终止。现有 Raw TX runtime prepare 会请求所有 Session close 并返回 drained
状态；任何未清理的 Session 都阻止 runtime disposal。

flush_begin 以 queue fence 为范围并持有独立 Session 引用，caller/JS owner 关闭或
释放后，watcher 仍可读取准确累计结果。后来的 enqueue/drop 不改变已建立 fence。
flush_release 只释放 exact watcher 及其引用，不取消发送；后续 public timeout
必须沿用此行为。全部 packet 已进入终态时 flush 可完成，即使 Radio cleanup
仍 pending；它不证明关闭、ACK 或 RF 对端接收成功。

## 调度与尚未提供的能力

Session service 复用现有 Raw TX async poller；不增加 poller entry、task、timer
或 observer EventQueue。每个 Session 至多一个 worker item；service 只尝试零等待
获取 Session mutex，并使用非阻塞 worker queue admission。队列满时恢复 busy 标记
并保留所有 ownership。尚未获得 grant 的等待者，以及未收到 native completion 的发送，
不会反复投递空转 worker。one-shot 退休清理先尝试 admission，避免 Session 请求
挤占其恢复所需 worker queue slot。

当前触发仍来自 runtime poller/prepare；还没有独立 periodic timer，也不承诺在
JS 长时间不让出 runtime 时持续排空 native queue。W-04B periodic 的原生触发、
busy skip/stop、速率约束、rate restore 和实际间隔验证继续待实现。公开 Session
Future、completion watcher、输入捕获和关闭后的 JS 结果保存亦待接入。

## 当前检查及待执行用例

新增 `tests/c/integration/wifi/tx/test_wifi_raw_tx_session.py` 直接包含实际 Session/queue/lane/
broker/validator，只有 Radio/SDK、allocator、task locks、worker queue 与 clock
作为可控边界。覆盖两 Session 排队/逐包公平、flush 之后 enqueue、关闭期间保留
active、迟到 callback、stop 失败后缀、错误第二帧、control 第 N 次分配失败、mutex
失败、worker queue 满、未打开即关闭、最大 Session 数、关闭后存活引用、generation
耗尽、SDK 前分配失败与 SDK 后不确定错误。另用真实 pthread 将 worker 暂停在
Radio submit 边界，主线程观察/关闭/拒绝新准入并释放 caller 引用，再继续完成。

用例未编译或执行；11 份 Raw TX/相关 Python 只做 AST。oneshot fixture 增加无
Session 边界 stub，不能把该 stub 当作真实 runtime teardown 或跨 Session 验收。
完整 Future scheduler、FreeRTOS/NimBLE/ESP-NOW 共存、GC/RF/heap 和关闭后 reopen
用例仍留到 Wi-Fi 阶段集中执行。

C5 immutable context `firmware-ci-esp32c5-representative` 最终编译 exit 0。
Session 对象内 12 个 exported helper 已编译；service/worker/cleanup 已链接到实际
Raw TX poller/runtime prepare 路径。由于尚无公开创建入口，new/admit/flush/status
等未使用 export 仍被最终 ELF 裁剪；不能把 service 链接写成公开 Session 可用。
binary `0x2919b0` / 2,693,552 bytes，比前批增加 4,960；app 空余 14%。最终 ELF
新增 registry 32 bytes；s_lane 44、s_retired 56、s_raw_tx 128、s_radio 824 未变。
实际 Session heap、并发峰值和 RF 时序尚未测量。

MQuickJS 61 sources/49 snippets、manifest 47 classes/419 functions、feature 27、
schema 35 STA/21 AP（live SDK）、strict TypeScript、recorded SDK map、AST 与
whitespace 通过，SDK 工作区干净；证据为 `build/w04-raw-tx-session-evidence.json`。
Host/VM、C3/S3/disabled matrix、实机/RF/heap/soak 继续 not-run。没有刷写、串口
操作、擦除 workspace、前端构建、提交、推送或更新父 gitlink。全部 Wi-Fi API
之后执行阶段及实机功能测试，BLE 在相关测试后，长 soak 留到 BLE API 也完成。
