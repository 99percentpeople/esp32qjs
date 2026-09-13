# W-04B 有界 FIFO、整批准入和 flush 账本

firmware `d7db8d1` 工作区增量；SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本轮只实现 Raw TX queue 的原生 ownership/ledger 核心。Session/open/enqueue/
enqueueBatch/flush/periodic 尚未接入；公开 capabilities 的 queue/batch/periodic
仍为 false。one-shot 的当前实现见[公开路径](2026-09-09-w04-raw-tx-public.md)。

## 原生所有权和准入

生产文件为 `internal/esp32_mquickjs_wifi_raw_tx_queue.h` 与
`src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx_queue.c`，位于
`components/esp32_mquickjs`。纯 C helper 无 SDK、分配、free、回调或自身锁；后续
Session 必须使用 task mutex 串行化全部访问，不能把批量 alias/preflight 扫描放进
ISR 或禁中断临界区。queue capacity 为 1–128，flush watchers 最多 8。

caller 在准入前完成整批捕获和 MAC 校验，持有每份独立 payload。核心检查 descriptor
范围、24–1500 长度、指针溢出、重复/重叠 ownership、输出/control/payload alias、
容量和 sequence 空间；失败不改队列、输入、输出或 receipt。所有可失败检查都在
eviction 前完成，提交阶段不分配、不调用外部函数，因此不会先丢旧 batch 再因新
payload 分配失败只提交一部分。

成功准入转移全部 payload 并清零输入 descriptors。overflow=reject-newest 容量不足
时保持原状态；drop-oldest-batch 只移除完整、尚未开始的排队批次。已开始 batch 的
剩余帧在两个 packet 之间也受保护。新 batch 若无法靠完整可丢弃批次腾出空间，
保持原状态失败。被移除的 payload 转交给 caller 提供的空输出数组，在 mutex 外
释放；核心本身不执行 allocator，也不暴露 JS root。

每个 packet ticket 包含 Session generation 和单调 admission sequence，batch
identity 取该批首个 sequence。框架 owner 必须从不复用的 Session registry 提供
generation；queue helper 只校验非零，并不自行证明外部 generation 的唯一性。
sequence 到 UINT32_MAX 后拒绝继续准入，不能回绕。它不同于 one-shot broker 的
driver-operation identity，后续公开 receipt/result 必须明确区分。

## 在途 packet、关闭与终态

take 只移交 borrowed payload，queue 保持 owner；同一 queue 只有一个 active。
accept 记录 SDK 已接受，重复 accept/错误 ticket 不增加计数。finish 使用精确 ticket
归还 active payload；success/failed/unknown 需要 prior accept，rejected 需要未
accept，aborted 只用于调用者已证明原生终止的显式清理。timeout 不是 finish 证明。

close 可以丢弃全部未发送帧，包括已开始 batch 的剩余部分，但不会丢弃 active。
active payload、尚存 flush watch 或未 close 状态都会阻止 deinit。SDK completion
证明、不同 queue 共享的唯一 Radio/broker lane、NativeSession 引用和 worker 调度
仍由待接入的上一层负责，不能把这个 FIFO 当作 native callback 隔离实现。

## flush 的固定范围

flush_begin 在互斥边界捕获 last admission sequence，并复制当时的累计统计。最多
8 个 watcher 分别保留 fence、pending 和 totals；之后的 accept/terminal 只更新
sequence <= fence 的 watcher。后来 enqueue 的 packet 不延长已有 flush，后来的
丢弃/完成也不会污染它的统计。溢出和 close 丢弃都计入 fence 内的终态。

totals 为 Session 起点至 fence 的 admitted/submitted/settled/succeeded/failed/
unknown/rejected/aborted/dropped；pending 归零代表这个范围都进入原生已确认的终态，
不代表全部成功或对端收到。已完成的结果在 watcher release 前仍保持稳定，不存
无限历史列表。watcher 使用独立 monotonic identity，满额/耗尽明确失败，旧 token
不能释放已复用 slot 的新 watcher。release watcher 不取消发送，也不释放 payload。

现有 ESP-NOW FIFO 已作为设计参照检查；它没有携带本次所需的 fenced terminal
统计和逐 payload 移交回执。这里没有改动 ESP-NOW 的 queue/recovery 行为，也没有
引入面向公开 API 的兼容别名。后续 W-09 仍需覆盖 queue/control/capture/driver 副本
的实际预算；这个无 allocator 核心不构成预算实现。

## 待执行用例与当前检查

新增 `tests/c/integration/wifi/tx/test_wifi_raw_tx_queue.py`，直接包含生产 header/source；只提供
payload allocator/释放追踪和队列不变量审计，不用另一个 FIFO 状态机替代实现。
覆盖整批失败无副作用、descriptor/payload alias、指针溢出、活跃批次保护、整批
eviction 回执、flush 期间继续 enqueue、关闭保留 active、各终态、旧 packet/watch
token、watcher 容量/身份耗尽、packet sequence 耗尽和 128 个 packet 的上限。

用例尚未编译或执行，不能据此声明 ownership/flush 竞争已验收。原生 Session 的
batch capture 第 N 次分配失败、并发 producer/worker、真实 Future flush timeout/
close、共享 arbiter、定时器 busy skip/stop 和 rate restore 测试仍待实际集成后补齐。

C5 immutable Build Context `firmware-ci-esp32c5-representative` 编译通过，binary
仍为 `0x28fe80` / 2,686,592 bytes，app 空余 15%。queue 核心存在于目标对象，尚未
链接到 Session 调用路径；没有新的公开注册或静态 queue/payload 分配。
MQuickJS 61 sources/49 snippets、manifest 47 classes/419 functions、feature 27、
schema 35 STA/21 AP（live SDK）、strict TypeScript、SDK map、九份相关 Python AST
与 whitespace 检查通过。SDK 工作区干净。证据前缀 `build/w04-raw-tx-queue-`。

Host/VM、C3/S3/disabled、实机/RF/heap/soak 继续 not-run；全 Wi-Fi API 完成后做
阶段和实机功能测试，BLE 在相关测试后，长期 soak 留到 BLE API 也完成。没有刷写、
串口操作、擦除 workspace、前端构建、提交、推送或父仓库 gitlink 更新。
