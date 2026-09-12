# W-03：Monitor Batch 接收与原生所有权

在停止后 configure 交接基础上增加 `receiveBatch(options?)` 和实际
`WiFiMonitorBatch.info/bytes/close`。Batch Source 需按目标统一 wire 实现，尚未
注册；未用原始字节拼接或另一套格式冒充 `esp32qjs-monitor/1`。

firmware HEAD `d7db8d1e40ee9f6a522c22806252b2ef712843a7`，本批仍为未提交增量。
SDK `fff9895c82d744c7237be8847347bdd1b07c6643`，只使用既有不可变 C5 Build
Context `firmware-ci-esp32c5-representative`。

## 接收契约

- 完整输入验证在消费队列前完成。maximumFrames 默认 min(32,poolCapacity)，
  限制 1–poolCapacity 且最多 128；minimumFrames 默认 1、不得大于 maximum。
- timeoutMs 仅等待首帧，缺省无期限，0 为轮询；maximumLatencyMs 默认 0，首帧
  获得后开始聚合等待窗口。两时间参数限 0–INT32_MAX 整数毫秒，不隐式类型转换。
- 首帧后先按队列顺序取当前可用帧，最多 maximumFrames。达到 minimumFrames
  即返回；零 latency 不额外等，非零窗口到期或队列关闭可返回部分批次。
- deadline 是协作等待预算，不能抢占 SDK、队列转换或 GC。没有取得首帧时返回
  null；原生接收异常释放已收集的所有 Frame root 后抛出。
- 与当前 CSI aggregation helper 一样，公开 receiveBatch 是直接调用的协作
  helper，自身不登记 Future driver；每次内部等待使用真实 EventQueue Future。
  单帧 receive 仍支持 Future.call，未新增误报的 nativeFuture registration。

参数 capture 期间若公开 Session generation 变化，在消费前拒绝。之后独立 retain
原 native Session 和 native queue，并 root 原 JS queue。等待期间 public Session
可以被 stop/close/configure；Batch 不跟随新 queue slot，不混用两代 pool 的 token。
旧队列关闭后返回其已有部分；已被旧 Future 取得的 EVENT 仍可完成转换。

直接排空队列前检查真实 receiver_pending；与 Future admission 同属一个 runtime
task 的串行边界，不能从正在等待的其他 receiver 手中取走其队列事件。

## Batch/Frame/View 转移

Batch 有界 C allocation 保存一个独立 Session reference、count/capacity 及无指针
Frame event token 数组。最大大小由 parser 限制与 size_t 静态断言共同约束。
首次/后续等待返回的临时 Frame 将其 PUBLIC root 转给 Batch，再清除 JS opaque、
释放临时 Frame adapter 与其 context ref。立即可用的后续队列帧直接 EVENT→PUBLIC，
不生成每帧 JS info/Frame 对象；重复/stale token 不能制造新 root。

`info(index)` 按需转换原始 metadata 快照，`bytes(index)` 保留独立 View；索引严格
为 0–frameCount−1 整数。Batch close/GC 逐项关闭 root，再释放其 Session context。
close 幂等，frameCount 为创建时的 JS 属性仍可读取；关闭后索引访问拒绝。
已经取出的 View 可跨 Batch close、原 Session configure/close 继续读取。

构造 Batch JS 对象或 frameCount 属性失败会释放整个部分批次。单次 info/bytes
转换失败不关闭已有 Batch；共用 ByteView 构造器负责消耗失败的 retain ownership。
当前统计仍为 per-pool counters；完整目标的 delivery/capability/status 字段和 W-09
退休预算仍需继续实现，不能将已有字段视为整个 W-03 状态契约完成。

## 已补测试源码，未运行

- `test_wifi_monitor_batch_options.py`：实际 parser 和 vendored MQuickJS，默认值、
  pool 上限、min/max 关系、时间边界、null/未知字段/非整数/NUL、逐次 JS 属性/
  分配失败和 moving GC；失败输出保持不变。原 open options fixture 抽出共用
  production code builder，没有另一套 parser。
- `test_wifi_monitor_session.py`：生产 Batch adopt/release、原生 Session/resources/
  queue/reaper；重复/stale EVENT、容量、两帧 Batch、View 在 Batch/Session 关闭后
  存活与最后 owner 释放。SDK/JS/task 仍为显式边界，不是完整 VM 生命周期证据。
- `monitor-batch-hardware.js`：已注册到 wifi 模块，要求 network 与
  wireless-hardware。使用配置 AP 所在信道的 Beacon；检查实际 Batch/info/bytes、
  越界拒绝、旧 Batch 跨 configure 及 View 跨关闭/GC 保留。
- `monitor-configure-hardware.js`：增加替换后空队列 Batch 轮询检查。

全链路 public Batch aggregation 的受控 timeout/close/竞争调度和构造 GC/OOM、
实际 RF/堆比较仍需在 Wi-Fi API 完成后集中运行。以上仅源码与语法检查，不称为通过。

## 本批检查

C5 最终 build passed，bin `0x28af30`，相较上批 `0x28a110` 增加 `0xe20`，app
剩余 15%。日志 `build/w03-monitor-batch-final-c5-build.txt`；nm 确认 Batch public
entry、options parser 和 finalizer 实际链接。

MQuickJS 61 sources / 48 snippets；manifest 47 classes / 415 functions；feature
文档 27；config schema 35 STA / 21 AP 且 live SDK 匹配；strict TS declaration、
recorded map、相关 Python AST、whitespace 检查通过。新 Batch class offset 为 48，
总 class count 为 USER+49，stdlib/types/VM fixture 同步。SDK source clean。

Host C/Python/fixture 编译、C3/S3/disabled 构建、运行 GC/OOM/队列竞争、实机功能/
RF/共存和 heap 比较均 not-run。Wi-Fi 功能完成后集中阶段验收，长时间 soak 等 BLE
API 完成后。未刷写、串口操作、workspace 变更、前端构建、提交、推送或更新根 gitlink。
