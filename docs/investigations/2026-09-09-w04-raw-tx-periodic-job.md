# W-04B 周期任务 owner、Session 与自治 timer 接入

firmware `d7db8d1` 工作区增量；SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
新增生产 `wifi_raw_tx_periodic_job.h/.c`，连接上一批周期 ledger 与实际原生 Session/
queue/result/broker。已加入 CMake 和 Raw TX 原生 poller；还没有 JS 创建入口，
`startPeriodic`/周期对象及 capabilities.periodicTx 仍为 contract-pending/false。
不能把本批内部 worker 链接或 C5 编译写成周期 API 已提供、timer 精度或 RF 已验证。

## 原生 ownership 与调度

boot registry 最多 8 jobs，包含 retired 但 caller 仍保留的 job。generation 精确、
单调，UINT32_MAX 使用一次后失败，不通过 Session/runtime restart 回绕。new 只
分配 internal 控制、校验模板 MAC、预留 Session child 和转移已捕获 frame，不调用
SDK/timer；失败保留 caller frame/output。成功返回 caller reference，另有独立
cleanup hold。frame 是 native 唯一 ownership，后续 JS capture 必须先复制输入。

Session child 准入和 close 共用 Session task mutex；每个 child 单独保留 Session
reference。close 已开始则拒绝 child，已预留但尚未发布 registry 的 child 也计入
Session close 条件。job 每次观察实际 Session close/fault，停止未来机会；最终
释放 timer/worker/template 控制前，Session 不会发布 closed。公开 Session.status
增加 periodicJobs 控制数量；retired job 的 caller 不再保留 Session child。

初次服务在 background worker 创建 ESP_TIMER_TASK timer，周期为 1000 us，开启
skip_unhandled_events。actual interval/startDelay/count 由绝对 deadline ledger
控制。timer callback 只通过 CAS 提交至已有有界 background queue，每 job 最多
一个 queued/running worker；不分配、不等 mutex、不调用 Radio/JS，不保存 runtime
或 JS pointer。队列满时恢复 worker flag 和 worker ref，下次 timer tick 重试。
新增 worker 在获取 busy 后重查 retired，封闭旧 worker 退休与新调度的竞争窗口。

启动后 timer 唤醒 worker，worker 推进实际 Session service 与结果观察，不要求 JS
runtime poller 来推进每一包。poller 继续负责初次 admission，以及 timer 已 disarm
后的失败清理重试。调度精度、任务竞争、light sleep 和持续吞吐尚需运行/实机证据；
1000 us 是内部调度下限，不是每毫秒一定实际发包的承诺。

## 周期准入与结果

每个 job 有一份 template 和至多一个待终结 ticket/result record。ISSUE 后在任何
mutex 外分配并复制单包，随后在 job mutex 内重查 stop，再进入 Session mutation。
stop 与此最终 admission 串行；stop 返回后不产生新的周期 queue admission，但
此前已准入包仍可发送。Parent close 在 Session mutex 的最终检查阻止新增包。

新增 Session admit_periodic 复用真实 result admission，但要求 Session queue/active
和 worker 均空闲；不会驱逐其他生产者的 batch。多个周期 job、one-shot 和其他
Session 仍共用既有 arbiter。观察到 busy 则按 skip/stop；在实际 admission 发生
竞争时，将同一次 ISSUED 机会重新计为 skippedBusy，不增加 scheduled、不保留
packet、不把 busy 当成已发送。copy OOM 和 SDK 前拒绝有独立 no-submit 终态。

Session 在 SDK 接纳后，将 driver_submitted 写到同 mutex 保护的精确 result。
这让周期账本能区分尚在 queue 和已经接纳的包，不把 pending 快照中的零值误认为
最终未提交。实际 completion 仍由 Session worker 原生 retire 后发布，周期 observer
读取同一 record；unknown 与 failed 各自记账。其他 enqueue 驱逐尚未开始的周期
单包时记为 dropped。stopOnError 对 rejected/failed/dropped 停止并 fault。

结果 ledger settlement 和 record release 是两个后缀，后者失败不会再次统计终态。
uncertain 继续保留 result、ticket、Session、template 与 cleanup hold，不伪造完成
或强制重启 Radio；显式故障恢复仍待实现。该路径会阻止 parent/runtime 退休。

## timer 与关闭

stop、close、有限 count 结束只停止未来调度，已有 packet 保留到真实终态。timer
暂时继续做观察/清理唤醒。ledger drained 且 exact result 已注销后，worker 在全部
mutex 外调用 esp_timer_stop_blocking(timer, 1 tick)，确认无在途 callback，再 delete。
stop timeout 不释放 callback arg；stop 成功/delete 失败则保留 timer_stopped，后续
只重试 delete。清理失败保留原始 error 和单独 cleanup error/stage。

timer 已删除后才转移 template/session 以在锁外释放，发布 retired，并释放 cleanup
hold 和最后 worker reference。timerPresent/timerQuiesced 是已发布的控制事实，timerTransition 标记 SDK 步骤
正在执行，过渡期间不声称 quiesced，避免把
“stop 返回超时但已 disarm”错误描述成 timer 仍 active。保留的 retired job 可以读取
native 状态；未来 JS close 将缓存状态并释放 caller，避免占用 registry slot。

## 用例、验证及缺口

新增 deferred test_wifi_raw_tx_periodic_job.py，实际组装生产周期 ledger/job、Session、
queue、arbiter 和 broker，仅注入 allocator、task lock/queue、timer/clock、Radio/SDK
边界。准备覆盖自治 timer 驱动、前包未完成时 busy、parent close child fence、timer
删除失败仅重试后缀、timer start 失败、stop-before-start、worker 队列满、分配/mutex/
模板错误、8 jobs 限额及 generation 耗尽。outer job mutex -> Session mutex 的测试
边界保留全部 SDK/分配在锁外的断言，未用替代状态机驱动发送。

现有 ledger 用例增加 admission race reclassification 和 dropped。one-shot fixture
增加空 periodic registry stub，其范围仍为 one-shot；完整周期 stack 使用新 fixture。
17 份相关 Python 仅 AST parse，没有导入、编译 fixture 或执行测试。

C5 immutable Build Context 编译 exit 0。ledger/job worker/timer 与 Session service
已链接，但 new/status/stop 等尚无 public caller 的入口可能被裁剪。binary 为 `0x296590` / 2,712,976 bytes，app 空余 14%；新增 s_jobs 静态
registry 32 bytes，其他既有原生表大小未变，不等于 SRAM/heap 峰值已验收。
manifest 48 classes/427 functions、feature 27、schema 35 STA/21 AP（live SDK）、
strict TypeScript、SDK map、MQuickJS 61 sources/50 snippets 和 whitespace 通过；
固定 SDK 干净。实际符号/hash 见 `build/w04-raw-tx-periodic-job-evidence.json`，
这些证据不构成 concurrency/GC/heap/RF 或硬件 qualification。

后续仍需：公开 startPeriodic/status/stop/close 及 Future/GC/OOM capture、全局失败
job 诊断、timer callback/worker 真实并发与所有异常后缀、停止时已提交 RF、其他生产者
竞争、count/实际发包间隔、rate lease/restore、显式故障恢复与 W-09 预算。
C3/S3/disabled、Host/VM、实机/RF/heap/soak 均 not-run。全部 Wi-Fi API 完成后统一
阶段和实机功能测试；BLE 在相关测试后，长 soak 留到 BLE API 完成。
未刷写、串口操作、擦 workspace、构建前端、提交、推送或更新根 gitlink。
