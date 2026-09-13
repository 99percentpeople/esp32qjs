# W-03：Monitor Session owner、reaper 与 runtime 边界

## 实际实现

新增 `wifi_monitor/esp32_mquickjs_wifi_monitor_session.c` 与内部头文件。它创建
stopped Session 的 native storage/resources/EventQueue/capture，通过上一批 capture
执行显式 start/stop/close，并连接已有 async poller/reaper。JS queue 在构造期间
使用 JSGCRef；native owner 不长期持有任何 JS roots。

本批尚未新增公开 Monitor JS 类、方法、options parser、Frame converter 或正式
类型占位。构造入口接受后续实际 Frame converter；该 converter 必须按已有契约
接收 PUBLIC root 并保留自己的 Session context，失败时由 queue bridge 回收 root。

## 所有权与有界登记

最多 8 个 native Session control，共用有界登记表；已关闭但仍被队列、Future、
Frame 或 View/Source 持有的 control 继续计入容量。generation 从 boot 级单调
计数器分配，UINT32_MAX 后失败，不在 runtime restart 时回绕或重置。

构造先注册每 runtime 唯一 poller，再分配 control、slot/payload 和 EventQueue；
所有失败路径都在 Radio 启动之前。初始两个引用分别属于调用方和 native cleanup。
queue context、reaper callback、poller/teardown 快照、Frame 与 retained ref 各自
需要独立引用。只有确认原生控制权已关闭后，才释放 cleanup 引用并清空 runtime
指针；最后一个引用释放 payload pool 和 control，并归还登记 slot。

快照在短 critical section 内取得临时引用，payload/JS/SDK 操作及 free 在锁外。
release 不调用 JS、不访问 runtime，可用于后续 Source/worker 的 native 释放。
调用方须先释放 Frame/lease，再释放其 context 引用。最后引用发现 control 未
关闭或 pool 仍有 owner 时保留 control 并锁存 retirement_blocked，避免释放仍在
使用的内存；这是 invariant 故障保留，不是正常关闭成功或完整诊断 API。

登记容量提供当前控制数量上限，不等于 W-09 的 active/retired/control-reserve
字节预算。未新增第二套 allocator/budgeter；完整统一预算仍须实施。

## 关闭、调度与线程

所有生命周期调用限定于创建 Session 的 runtime task，不引入另一个任务或 SDK
工作队列。JS finalizer/EventQueue close 只设置关闭请求、停止 pool admission 并
通知 runtime；RX 信道冲突也通过新增 capture notify hook 唤醒已有调度。

poller 为需要停止/关闭的 control 注册已有 reaper，每个 reaper 自持一个引用。
SDK cleanup 或 dispatch drain 未完成时返回 false，原 token、queue 和 control
保持存活。注册容量不足时保留 cleanup 引用与登记项，记录 reaper_full；已有
poller 后续重试，runtime teardown 也可直接推进同一 cleanup，不分配替代 owner。

stop 的 reaper 完成后释放其引用并退出，Session 留在 STOPPED；close 完成则
释放 Radio 和 producer queue，detach runtime，再归还 cleanup/reaper 引用。
已经交付的 Frame/View/Source 继续持有独立 context 和 pool，不等待 JS 后续语句
才结束原生关闭，也不把 payload owner 的存在误报成 Radio cleanup 失败。

## runtime teardown 的实际顺序

检查原 core 可见：destroy_internal 先处理 Future，再检查 reapers pending，之后
才执行各模块 deinit。新增 prepare_wifi_monitor_runtime_destroy 在这些等待之前
调用，按精确 runtime 找到并临时 retain Session，先请求其原生关闭。

任一 native close 未确认则返回 false，禁止释放 runtime。原生关闭完成后，已有
Future/queue/reaper/JS finalization 顺序继续清理剩余引用。已退休 control 的
runtime 指针为 NULL，旧 Frame/Source 的最终释放不会通知新 runtime；其他 runtime
的 Session 不在这次快照内。

这不宣称验证了所有模块的 teardown，也不改变 CSI/BLE/ESP-NOW 的控制逻辑；
完整真实队列、GC、runtime restart 与共存回归仍待阶段测试。

## 测试源码与验证边界

新增 `tests/c/integration/wifi/monitor/test_wifi_monitor_session.py`。它拼接实际 Session/capture/
resources/queue bridge、EventQueue native lifetime/Future finish/destroy 与 reaper
registry；Radio、JS object/GC、SDK allocation、task notification、queue 字节传输
为明确边界。复用 queue fixture 的 production_queue_code，并增加第 N 次 allocator
故障注入；没有使用另一个 Session 状态机代替生产实现。

已编写构造/poller/原生分配失败，非 owning task 拒绝，reaper 满后保留/retry，
JS owner 释放后的延迟 cleanup，retired control 容量归还，runtime 隔离，关闭后
Future 转换失败、Frame/View 独立保留，generation 耗尽及 core teardown 顺序检查。
所有这些用例未导入、编译或执行；仅两份 Python AST 通过。

C5 immutable Context `build/wireless-contexts/c5` 初次与最终编译通过；日志为
`build/w03-monitor-session-c5-build.txt` 和 `build/w03-monitor-session-final-c5-build.txt`。
最终 app `0x284c40`，余量 16%，较上一批增加 0x6c0。nm 确认 Session 构造/start/
stop 入口存在于目标 object，当前无 JS caller，未保留到最终 ELF；core 的 prepare
destroy、Session close/retain/release 已链接。登记指针数组为 32 字节；没有当前
生产 Monitor control/pool 分配，也没有实机 heap/largest-block 比较。

MQuickJS 59 sources / 48 snippets、manifest 44 classes / 398 functions、feature
文档 27 项、config schema 35 STA / 21 AP（live SDK）、recorded map、strict
TypeScript declaration、两份 Python AST 和 whitespace 检查通过。hash 与范围见
`build/w03-monitor-session-evidence.json`。

Host/Python、C3/S3/disabled、实机功能/GC/RF/runtime restart 均 not-run。Wi-Fi
APIs 完成后统一阶段和实机功能测试；BLE 在相关测试后开始；长 soak 待 BLE APIs
完成。未刷写、操作串口、擦除 workspace、构建前端、提交、推送或更新根 gitlink。

下一步仍是 Frame/Session JS 绑定、公开 options capture 和真实 native converter。
AP 共存、持续 powerSavePolicy、PHY/time、Batch/Source、统一 wire、W-09 预算
及其他 W-01～W-12 剩余范围继续有效，不能将本批记为 Monitor 或全部 Wi-Fi 完成。
