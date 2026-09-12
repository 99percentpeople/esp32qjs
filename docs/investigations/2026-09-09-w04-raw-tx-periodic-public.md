# W-04B 公开周期发送、Future 与原生诊断

firmware `d7db8d1` 工作区增量；固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批正式注册 `WiFiRawTxSession.startPeriodic` 和 `WiFiRawPeriodicTx.status/stop/close`，
连接已有原生 periodic ledger/job、timer、Session、queue/result 与 Radio broker。
capabilities.periodicTx 为 true，minimumPeriodicIntervalUs=1000、maximumPeriodicJobs=8；
rateLease 仍为 false。稳定等级 Candidate，新增功能尚未运行或 RF 验收。

## Capture 与 Future

新增生产 public_periodic.c。startPeriodic 与 close 使用真实 native Future driver，
同步调用复用合作等待，也支持 Future.call。只在实际 timer ready 且没有 startup
fault/close 时创建 JS class；没有 new/占位 constructor 创建路径。

参数 intervalUs 必填，count/startDelayUs/busyPolicy/stopOnError/timeoutMs 有明确
默认和整数/枚举范围，严格拒绝未知字段、非 boolean、NUL 后缀。所有 policy getter
完成后才读取 frame getter，并复用现有 ByteSource capture；length 只读一次，
ByteView read lease 在 capture 退出前释放。getter 错误保留，partial native buffer
由 Future destroy 释放。capture 再按 Session interface/sequence 执行纯 MAC 校验。

捕获前先从实际 Session JS 包装器取得独立 native reference 和 immutable options，
避免 getter 重入 close 后访问已释放 Session。真正 child admission 仍在原生 Session
mutex 内重查 open/close/fault。Future state/SDK/timer 不保存 JS roots/runtime pointer；
frame copy 在 native job new 成功后才转移，JS结果构造 OOM 不能撤销先前已入队/RF。

startup timeout/cancel 请求关闭未发布 job，独立 native cleanup hold 保留 timer arg、
template 和未完成 packet；Public Future storage 可释放。close Future 另外保留 C
包装器和 native job 引用，即使 JS finalizer 或另一 status 调用发生，也能安全 finish。
close timeout 在 start 分发前仍保留 close intent；显式 cancel-before-start 只撤销该次
close 操作，已经开始的 close 与 GC owner 关闭不会被撤销。

## 状态与退休

count 是调度机会数，包含 skippedBusy/skippedLate；原生 deadline 不积压补发。
stop() 只停止以后的周期 admission，已入队包仍可发出；close() 以默认 1000 ms 预算
等待真正 native retirement。状态含实际 ledger counters、active identity、原生错误、
timerPresent/timerQuiesced/timerTransition、workerBusy 与 cleanupPending。
明确 timer精度/实际吞吐/对端收到不能从 intervalUs 或本机 completion 推断。

status/close 观察 retired 后缓存无 JS pointer 的 native snapshot，释放 caller job
引用。保留的 JS 对象不再占 native registry slot 或 Session child；close 幂等，停止
或 finite completion 可以是 retired/stopped，显式 close 后报告 closed。

原生退休仍先释放 template 与 Session child。补齐 retired 和 worker_busy=false 在
同一 job mutex 内发布，使公开缓存不会永久保留过渡期 workerBusy=true；调度的
retired 再检查继续防止旧对象被再次排队。没有提前释放 callback arg 或改变 native
in-flight 的所有权。保留父 Session 和周期 JS 对象可避免 GC 提前请求关闭。

新增全局 periodic registry 汇总：live/retired/faulted/cleanup 数量、identity exhaustion
及首个被保留 job 的错误代次/error/stage/cleanup。汇总先持有 registry 快照引用，
再逐 job 短 mutex 读取，不含 frame/凭据/JS pointer，不是跨 producer 原子快照。
它能暴露未返回 JS 对象的失败启动或待清理 job；已完全回收的对象不构成永久日志。
完整参数/结果/副作用见 [Raw TX API](../api/wifi-raw-tx.md)，正式 TS/manifest 同步。

## 检查和阶段测试

新增 deferred test_wifi_raw_tx_periodic_options.py：真实 MQuickJS 执行生产参数与
ByteSource capture、status converter，准备验证 false/default boolean、整数边界、
getter-once/原始异常、getter 关闭 view、moving GC 与第 N 次分配失败。

新增 deferred test_wifi_raw_tx_public_periodic.py：调用生产 start/poll/cancel/expire/
destroy 与 handle retain/cache helper，连接真实 native periodic/Session/queue/arbiter/
broker，准备覆盖 Future 销毁而 packet 保留、关闭分发前 deadline、显式 cancel 和
retired handle detach。成功 JS finish 的所有权移交在该 C fixture 手工建立；完整
JS class attach/finish OOM 和 generic Future scheduler 不在其证明范围，仍需阶段集成。

原 native periodic fixture 提取共用 assembler，增加真实全局状态/identity exhaustion
断言；原 one-shot capture/状态 GC fixture 同步 periodic capability 和全局汇总；共享
VM class count 同步生产 USER+51。19 份相关 Python 仅 AST parse，未导入、编译或执行。

C5 immutable context `firmware-ci-esp32c5-representative` 编译 exit 0；公开创建/方法、
Future registration、native new/worker/timer 均已进入实际链接路径。具体符号、binary、
静态表和检查/hash 见 `build/w04-raw-tx-periodic-public-evidence.json`。API manifest
49 classes/431 functions；feature 27、schema 35 STA/21 AP（live SDK）、strict TypeScript、
SDK map、MQuickJS 61 sources/51 snippets 与 whitespace 均通过。binary `0x2984c0` /
2,720,960 bytes，比前批增加 7,984，app 空余 14%。s_jobs=32、s_lane=44、
s_radio=824、s_raw_tx=128、s_retired=56、s_sessions=32 bytes；未增加这些原生静态表。
新增 class/Future/包装器的 heap 和峰值仍待 W-09/阶段测量，不能据此声称 SRAM 已验收。
固定 SDK 工作区保持干净。

仍缺完整 Future capture/finish/finalizer/GC/OOM 与 timer/worker 真正并发、全 registry
限额、启动结果失败后的诊断、parent/runtime close、其他 producer 竞争与 RF 间隔
证据；C3/S3/disabled、Host/VM、实机/RF/heap/soak 均 not-run。W-04 继续 rate lease/
restore、显式故障恢复；其余 Wi-Fi 范围保留在总清单。全部 Wi-Fi API 完成后统一阶段
与实机功能测试；BLE 在相关测试后，长 soak 后置至 BLE API 完成。
未刷写、操作串口、擦 workspace、构建前端、提交、推送或更新根 gitlink。
