# W-08 TWT probe 的 Radio、Future 与公开入口

基线 firmware `d7db8d1`、SDK `fff9895c82`；接续
[托管 owner/联合回收](2026-09-10-w08-twt-probe-retire.md)。本次接入 C5 HE 的
Candidate `wifi.twt.capabilities/status/probe`。正式契约见 [API](../api/wifi-twt.md)。
完整 Wi-Fi 目标仍有效，Agreement setup/teardown、suspend/恢复和物理故障恢复
尚未完成；运行、实机及长 soak 沿用用户的后置安排。

## Radio 与身份

提交要求框架已拥有且已启动的健康 Radio、有效 storage、当前已关联 Station；
不隐式初始化、连接、改变 mode/channel 或覆盖 PS policy。APSTA 可共享当前
关联信道，其他固定信道冲突、Raw TX in-flight、Radio operation/lifecycle
和既有排他控制阻止准入。

boot-scoped uint64 TWT identity 域与 Radio operation/lease identity 分开校验，
耗尽明确失败，不回绕。新增 TWT client 计数；`wifi.status().radio.clients`
中的 `wifiTwt` 包括退休中的占用，并计入 total。driver 调用在 mutation mutex
下串行，未放入 snapshot critical section。SDK submit 不自动重试；在原生
接受后返回错误也保留非零 token，调用者仍须退休它。

Generic end/release 不得绕过专用退休路径。联合回收记录内嵌在稳定的 Radio
owner 中；只有其 poll 确认 native pin/marker 已释放，才清除 operation 并
释放精确 lease。失败保留占用和原始阶段，只重试未完成后缀，其他 owner 的
计数不会因本 probe 退休而减少。不能证明归属/排空时仍需后续物理恢复设计。

## Future 与 runtime

Public options 严格接受整数 `responseTimeoutMs`/`timeoutMs`（1..60000，默认
5000/6000）。输入验证和属性 getter 在有 GC root 的 JS 线程完成，再分配
native Future state。SDK 时间是估计值，Public deadline 包含调度/等待时间。

同一 Future resource lane 串行提交；上一请求已结束但 native cleanup 未结束
时，新请求仍等待。提交 worker 最后用 release store 发布完成；poll/cancel
在看到 acquire publication 前不读并发结果、不释放 state。timeout 构造错误
只使用本地空快照，避免与 worker 写结果竞争。

Public Future 结束后可释放 JS roots；仍引用 native owner 的 token 转交
boot cleanup。后台队列满或联合回收失败时保留同一 token，每 100 ms 尝试未
完成步骤。现有 Wi-Fi poller 推进服务，没有新占 async poller 槽；runtime
销毁先请求 Future 取消，再推进 TWT/其他 owner 清理，并等待全部 gate。

原生结果在可丢弃的 watch 发布前保存。控制层以 native snapshot 完成 Future，
不依赖观察队列。结果区分 SDK/control error、RF failed/timeout/disconnected
终态和 public deadline。超时/取消只终止本 probe 的权限与资源，不主动断开
Station。RX 成功保留 AP Beacon/Probe Response 存活语义，无精确 RF cookie，
不能证明 TWT Agreement 或 TSF 同步。

## 本轮发现并修复的接入问题

- 初次 C5 编译暴露新源文件缺少 FreeRTOS include；补齐后重新编译。
- 第一份可链接镜像的生成 ROM 表缺少三个 TWT 方法。原因是主机 stdlib 工具
  未收到 C5/HE 宏，而目标模块按这些宏编译，造成两边 gate 不一致。
  CMake 已显式传递两项宏；修复前生成头/ELF 符号与构建日志保存在 build。
  修复后检查真实 ELF 的 `js_c_function_table` 指针、三个方法符号以及 native
  Future driver 九个 callback 指针，不以构建成功替代实际注册证据。

这是编译/静态链接层面的失败与修复证据；未声称重现过设备运行失败。

## 验证

通过 immutable Build Context 的生产构建（没有烧录）：

| Context / build directory | 镜像字节 | 相对上一批 |
| --- | ---: | ---: |
| c5-roaming / wireless-c5-roaming | 2,972,144 | +8,992 |
| c5-no-softap / wireless-c5-no-softap | 2,848,544 | +8,976 |
| c5-disabled / wireless-c5-disabled | 459,024 | 0 |
| c3 / wireless-c3 | 2,665,696 | 0 |
| s3 / wireless-s3 | 2,570,768 | 0 |

命令从 firmware 运行：
`source /home/zach/esp/esp-idf/export.sh` 后，执行
`.venv/bin/python scripts/remote.py --build-context build/wireless-contexts/<context> --build-dir <directory> --assume y build`。
没有修改 Context 或共享 SDK archive；原 SDK 工作区仍干净。

C5 HE 两份镜像的三个公开方法真实进入 ROM function table；C3/S3/Wi-Fi disabled
的 table 和 ELF 均无这些入口。生产对象与 archive member 一致，链接调用链
覆盖 Public Future、Radio submit/retire、联合 poll、timer callback-exit wait、
native fence 和 copied-identity event fence。现有 public `wifi.capabilities()`
也在同一 gate 下列出 `twt` namespace；Agreement flags 保持未实现状态。

C5 新增静态 SRAM 216 B：Radio probe owner 160 B、TWT identity 16 B、boot cleanup
32 B，Radio client 数组及结构对齐增加 8 B。其余 29 项已跟踪 framework 静态
对象、EAP stop 和先前 probe TX/timer/result/wake 对象尺寸不变。每个已捕获
Future state 为 128 B；marker 的 SDK timer 分配和既有 lazy TX ledger 512 B
继续单独计费。这些是静态尺寸，不能替代同等预热静止状态的实机 heap 比较。

manifest 52 classes/513 functions、feature 文档 27、SDK schema STA35/AP21、严格
TypeScript、MQuickJS 61 sources/60 snippets、whitespace 检查通过。
证据：`build/w08-twt-probe-public-evidence.json`，包括 hash、ROM 指针、调用链、
构建日志及修复前 gate 缺口；摘要在同名前缀的 evidence-summary.txt。

新增 `test_wifi_twt_radio.py` 与 `test_wifi_twt_future.py`：调用生产 registry、
Radio glue、capture/conversion/worker callback，注入 SDK/联合退休边界和 worker
调度。覆盖身份耗尽、共享 owner 保留、SDK 早退、generic bypass、native error、
观察丢失、取消/publication、后台队列饱和、runtime gate、GC 与第 N 次分配失败。
仅 AST 检查，未 import、编译或执行；joint retire 的真实内部步骤由上一批生产
fixture 单独覆盖，完整 Future core 调度与 SDK/RF 仍需阶段验收。

not-run：Host C/Python/VM 运行、设备 GC/close/reopen/runtime restart、Wi-Fi
对端/RF/共存、实际 internal/PSRAM free/largest block、完整 feature matrix。
长 soak 在 BLE API 完成后再做。没有串口/烧录/擦除 workspace、前端构建、提交、
推送或根仓库 gitlink 更新。功能等级维持 Candidate。
