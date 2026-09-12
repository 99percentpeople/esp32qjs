# W-04A Raw TX one-shot 公开路径

firmware `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
已注册 `wifi.rawTx.capabilities()` / `send()`，并链接实际 Radio/broker/SDK 发送
路径。当前等级 Candidate；Host/VM/竞争和实机 RF 验收尚未执行。

## 捕获、Future 与 native retirement

生产模块 `src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx.c` 位于
`components/esp32_mquickjs`。公开 send 使用注册的 native Future driver，可通过
同步调用或 `Future.call(wifi.rawTx.send, wifi.rawTx, [frame, options])` 使用。

capture 不启动 I/O，先读取严格 options（getter 异常保留），再捕获 ByteSource。
ArrayLike length 只读一次且在 payload 分配前限制 24–1500；各 byte 为 0–255 整数。
ByteView 在 options getter 后重新取得 read lease，复制完成即释放；没有把 JS
数据指针保存到 worker。捕获副本使用 `wifi.raw-tx.capture` 的现有 memory allocator。
Mandatory MAC validator 在任何 Radio 副作用之前执行；native admission 再补实际
关联路径约束。strict/basic 都保留这些强制检查，不宣称识别任意 body、容器或 FCS。

一个 Future resource lane 串行化 one-shot。I/O 使用现有 background worker pool，
工作项不含 runtime 指针，不在 native 完成后额外触发 runtime wake。worker 对
Future state 的最后一次访问为 release-store `worker_done`，poll acquire-load 后
才读取结果/销毁；通用 Future 的常规 polling 观察完成，不依赖 EventQueue 发布。
这里没有修改其他模块的 Future worker wake 机制，也不证明那些模块的生命周期。

取消/timeout 在 SDK submit 前被 worker 观察到时避免发送；SDK 调用已经在途时
不保证阻止 RF。公开 Future 可以结束，但它的 native state 等 worker 返回才销毁。
destroy 释放 capture 副本，并把精确 Radio lease/token 转交给一个 boot-lived 清理
槽；driver 的独立副本仍由 broker 保留。清理槽不含 JSValue、runtime 或 task 指针。
结果对象转换失败也走这一销毁路径，不会重复提交数据。

清理槽由共享 background pool 执行原生 retire/release/stop，所有 SDK 操作仍经
Radio mutation mutex。回调未完成、队列饱和或清理失败都保留槽；100 ms 之后重试
未完成后缀。成功 release 后 stop 失败不再重复 release。后续 one-shot 等待这一槽
完成，不能越过已结束 Future 的 native quarantine；它自己的总 deadline 仍有效。
没有为清理增加独立任务/定时器，也没有强制重启共享 Radio。

runtime destroy 在 Future driver 清理之后检查该槽；未排空时返回 false，保持
上下文可诊断而不销毁仍需推进的 native ownership。迟到 callback 可使下次清理
继续；没有 completion/发生 correlation fault 时可能一直阻止 teardown。
当前没有显式 native fault recovery，不能声称 runtime restart 能恢复该状态。

## 公开契约与尚缺范围

`wifi.status().radio.rawTx` 暴露 active identity、quarantine/correlation、清理 stage/
error、submit/registration error、identity exhaustion；不包含 frame 字节或凭据。
capabilities 报告当前 target/SDK/interface、allowlist 和 24–1500 上限；queue/batch/
periodic/rate lease 为 false。send result 使用 driver/MAC completion 语义，不报告
应用 ACK；rate 暂为 null，并保留 rawRate/rawStatus。完整参数/副作用见
[Raw TX API](../api/wifi-raw-tx.md)。

新增绑定、类型、API docs/目录、manifest 与 SDK coverage 同步到唯一 v1。
W-04B Session/open/enqueue/enqueueBatch/periodic/flush 和 rate lease 尚未注册。
显式故障恢复、完整 target matrix、RF 与长期验收仍待完成；本记录不关闭 W-04A
竞争验收门槛，也不将 Host 后置测试写成通过。

## 新增的待执行用例

- `test_wifi_raw_tx_capture_gc.py`：实际 MQuickJS、生产 options/capture/validator/
  result/capabilities；第 N 次分配失败、moving GC、ByteView 被 getter 关闭、一次
  length/options getter、原始 getter 异常、数值/枚举/长度边界。native error formatter
  是 fixture 边界；不以其代替实际 API 异常详情验收。
- `test_wifi_raw_tx_future.py`：生产 worker/poll/cancel/destroy/native reaper 与
  broker；注入 Radio 和 bounded worker queue，验证队列拒绝不发送、acquire 后取消、
  timeout 后保留 driver buffer、后继 Future 等待、迟到完成、release/stop 后缀、
  部分 acquire 失败和同步 callback。它不运行真实 Future scheduler/Radio；对应
  scheduler/runtime teardown 和全 Radio 竞争仍需集中集成测试。
- 既有 Wi-Fi status GC 与 init-cleanup fixture 增加 Raw TX 外部边界，保留原测试范围。

这些文件只做 AST 检查，未导入、编译 fixture 或执行。没有拿独立测试状态机作为
生产实现证据；尚缺的完整调度/关闭/SDK 行为明确保留为 not-run。

## 当前检查与证据

C5 immutable Build Context `firmware-ci-esp32c5-representative` 构建通过，binary
`0x28fe80` / 2,686,592 bytes，app 空余 15%。最终 ELF 链接公开 send、Radio
acquire/submit/retire 及 broker callback；这只证明编译/链接，不证明实际已发包。
首次编译发现 JS_IsString 缺 ctx 参数，已修正并重建。

MQuickJS 61 sources/49 snippets、manifest 47 classes/419 functions、feature 27、
schema 35 STA/21 AP（live SDK）、strict TypeScript、SDK map、Raw TX/相关 fixture
AST 与 whitespace 检查通过。SDK 工作区干净。详细 hash/账本/验证范围保存在
`build/w04-raw-tx-public-evidence.json`，日志前缀为 `build/w04-raw-tx-public-`。

Host C/Python/VM、C3/S3/disabled、实机/RF/heap 与长期 soak 均 not-run。阶段测试
仍安排在全部 Wi-Fi API 实现后，BLE 在相关测试完成后，soak 留到 BLE API 也完成。
没有刷写、串口操作、擦除 workspace、前端构建、提交、推送或父仓库 gitlink 更新。
