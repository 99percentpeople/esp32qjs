# W-08 Action public send/Future 与退休责任

本批接续 Action Radio 记录，firmware HEAD `d7db8d1`、固定 ESP-IDF
`fff9895c82d744c7237be8847347bdd1b07c6643`。既有工作区增量保留。

## 已编码

- `wifi.action.send(options)`、`status()`、`capabilities()` 正式注册 sole v1 Candidate；
  send 注册现有 native Future driver，支持直接 cooperative wait 和 Future.call。
- 固定 SDK request 的 payload 是 Action body，公开参数据此定为 destination/bssid/payload，
  不把完整帧传给 SDK 后静默丢掉调用者的 MAC header。frame 草案直接替换，无 legacy alias。
- 所有 enum、MAC 字符串、时间、channel、bool 与 ByteSource 捕获在 Radio mutation 前完成。
  根引用跨 getter 保持，ByteView read lease 完成复制后归还；固定 1476 上限与 size_t 加法检查。
  捕获失败释放部分 native allocation；SDK 同步复制完成前 request 留在 Future state。
- 普通结果等待 duration/cancel 原生终态，TX_DONE 独立记录；无 TX observation 时返回 unknown。
  不将 driver success 提升为空口对端交付。SDK 错误保留 raw espCode 与当前调用 stage。
- 一个 Future resource key 串行化发送；有界 boot-owned 退休槽保留精确 token，不保留
  JS/runtime/task 指针。新的 send 在其 deadline 内等前一个退休槽排空。
- Worker 最后用 release store 发布 worker_done，poll acquire 之后才读 token/error/request；
  timeout 只写 atomic cancel flag，不读 worker-owned 字段。公开完成/GC 不提前回收 worker storage。
- timeout、cancel、提交错误进入退休后的 native cancel。已接受 cancel 由 Radio 记录，重试仅
  未完成后缀。每 100 ms 调度一个 runtime-free background worker；队列满保留退休状态。
  terminal + SDK ioctl fence + default-loop marker 仍是释放 Radio 的必要证据。
- runtime teardown 在 Future driver 退休之后驱动 Action 与 Raw TX 的清理，再允许销毁 runtime。
  双清理入口分别调用，避免一个 pending 时短路另一个的进度。
- `wifi.status().radio.action` 与 `wifi.action.status()` 暴露原生与退休诊断，不泄露 payload/MAC。
  两个锁下的快照并非跨记录原子快照，文档明确 observation 边界。
- 总异步 poller 的固定容量仍为 8；当前源中 8 个不同注册函数（含可选 watch、Monitor、
  USB、BLE、WebSocket），本批未增加通用表容量。Future 注册上限仍为 128。

## 检查结果及证据边界

C5 合法 immutable Build Context `build/wireless-contexts/c5` 编译通过，日志
`build/w08-action-public-c5-build-verified.txt`。此前两次实现中间编译分别因新增
stdlib 表缺 JS_PROP_END、MQuickJS 不提供 JS_ToBool 而失败；均已修正，失败日志保留，
不计作验证通过。未更改 SDK 或 vendored VM。

最终 ELF 的 `esp_wifi_action_tx_req` 在 ioctl message +4 写入
`__wrap_wifi_action_tx_process`，证据 `build/w08-action-public-linked-sdk.txt`。
发送、取消、退休、快照、两级 fence、wrapper 现在均实际链接。公开 ROC submit 仍未引用，
不能将取消路径中链接的 ROC wrapper 冒充 ROC Session 已实现。

C5 binary 从 2,811,520 增至 2,826,112 bytes（+14,592），含此前未引用的 native/SDK
代码。新增 `s_action_retired` 32 bytes；`s_action` 84、`s_radio` 832 与上批无线
静态状态大小不变。新增 readonly Future driver 36 bytes/resource key 1 byte。
这些是 ELF 尺寸，不是 live free/largest block/worker stack 或 RF 负载下内存证明。

Manifest 49 classes/477 functions；send 标为 nativeFuture。类型、feature 27 项、
STA/AP schema 35/21 与固定 SDK、SDK-map、MQuickJS syntax 和 whitespace 一致性检查
另见 `build/w08-action-public-evidence.json`。

`test_wifi_action_capture_gc.py` 已编写：调用生产 capture/converters、真实 VM/ByteView，
逐次分配失败与移动 GC、最小/最大 payload、溢出、未知参数、NUL 后缀、单次 getter、
getter 异常和关闭 ByteView。仅 AST 解析，**未导入、编译、执行 fixture**。
既有生产 Action lane/Radio/SDK fixture 同样留至 Wi-Fi 阶段集中执行。

## 未完成

- Action Future scheduler/worker/timeout 与真实 Radio 的组合竞争、队列饱和、取消失败、
  teardown 运行证据；本批 capture fixture 不提供这些证明。
- ROC public Session、关闭与独立 operation identity 交接。
- missing/ambiguous terminal 或 SDK submission error 无终态时的物理 recovery coordinator。
  当前保留 token/Radio owner，可能阻止 runtime restart；不能声称 runtime restart 可恢复，
  必要时需设备 reboot。普通 public completion 也不保证退休槽已排空。
- C3/S3/feature-disabled 完整构建、Host/Python/VM 运行套件、实机/对端/RF、live heap。
  Wi-Fi API 全部完成后做阶段与实机测试，长时间 soak 留到 BLE API 也完成。

未刷写、访问串口、擦 workspace、构建前端、提交、推送或改变 root gitlink。
