# W-04B 原生周期调度账本

本批在 firmware `d7db8d1` 工作区增加生产 `wifi_raw_tx_periodic.h/.c`，为后续
ESP timer/native worker 和 Session 接入定义调度及终态账本。它当前尚无 Session
调用方，不能从 JS 发起周期发送；正式 capabilities.periodicTx 仍为 false，
startPeriodic/周期对象仍为 contract-pending。没有把内部 helper 注册成占位 API。

## 调度与有界行为

调用方通过 task mutex 串行访问 ledger；helper 无 SDK、JS、分配器、callback
pointer、observer queue 或 timer。每个 ledger 最多一个尚未终结的 ticket。
定时器只负责有界唤醒，实际参数捕获、模板 ownership、worker、Radio admission
以及停止 timer/callback 屏障仍需后续接入。

内部 interval 为整数 1000–UINT32_MAX 微秒；start delay 为 0–UINT32_MAX；
count=0 表示持续，非零限制调度机会数，包含 busy/迟到丢弃，并不保证成功发送次数。
这不是定时精度或吞吐承诺。init 要求非零 fresh generation，且不能重置存活 ledger；
负责分配 generation 的 owner adapter 尚未实现。timestamps 使用非负 int64 本地
monotonic 微秒；检查 start delay/下一 deadline 的加法及乘法，拒绝时不回绕。

使用绝对 deadline 推进，不按迟到的 now 重新建立周期。一次晚唤醒只保留最近一个
到期机会；更早机会计入 skippedLate，不补发一串积压包。计算为 O(1)，不循环追赶
睡眠期间的所有 tick。当前外部 busy 或已有 active ticket 时计入 skippedBusy，
busyPolicy skip 保持未来调度，stop 停止后续调度。外部 busy 只是 caller 的观察，
实际 queue/Radio 准入仍必须重查，不能把时间账本当作 driver mutation 排他边界。

scheduled = issued + skippedBusy + skippedLate。finite count 包含这些机会，
最终 issue 可在 running=false 后继续等待真实终态。连续模式的 UINT32_MAX
机会可使用一次，之后 exhausted/faulted，计数与 ticket 不回绕。时间倒退或不能
表示下个 deadline 时停止并 fault，不清掉尚未完成的 active ticket。

## 提交、终态与关闭

ticket 包含 generation 与调度 sequence，需精确匹配；旧 generation/sequence、
重复 submitted/finish 和与 ledger 重叠的输出 ticket 均拒绝。ISSUE 只是原生调度
预留，尚非 queue admitted、driverAccepted 或发送完成。adapter 后续需要单独
保留 template/captured packet，直到 queue 和 SDK 各自释放实际 ownership。

submitted 单独记录 SDK 接纳。success/failed/unknown 终态要求已接纳及 caller
提供真实 native completion 事实；rejected/aborted 要求证明 SDK 未提交。
stopOnError 会因 failed/rejected 停止并 fault；unknown 单独统计，不伪造 success。
stop/close 停止以后生成机会，保留 active ticket；public timeout 不能调用 finish
来虚构已取消 RF。drained 只表示 ledger 没有待处理 ticket，不能证明 timer、worker、
callback 或 payload 可以释放。

uncertain 使 ledger faulted 并保留 active，不允许把未知原生 ownership 改成
no-submit rejection/abort，也没有 force clear。未来显式故障恢复需要实际 native
终止证据和独立恢复入口；本批没有实现该协议。

## 验证与下一步

新增 deferred `tests/python/test_wifi_raw_tx_periodic.py`，将实际生产 C 文件作为
独立编译单元，而非用测试状态机替换。准备覆盖绝对 deadline、迟到合并、busy 两种
策略、finite count、停止/关闭时 active 保留、精确/旧 ticket、重复提交、错误及
uncertain、时间倒退、INT64/UINT32 边界与计数不变量。该 fixture 仅通过 AST，
没有导入、编译或执行；不替代将来的 timer/Session/SDK 竞争测试。

C5 immutable Build Context `firmware-ci-esp32c5-representative` 构建 exit 0。
新增 helper 已在目标对象中编译，因尚无调用方，在最终 ELF 被裁剪；binary 仍为
0x295460 / 2,708,576 bytes，app 空余 14%。没有新增常驻 timer/task/registry，
也没有新增公共 class/function。生产调用接入与真实功能验证均尚未完成。
manifest 48 classes/427 functions、feature 27、schema 35 STA/21 AP（live SDK）、
strict TypeScript、SDK map 与 MQuickJS 61 sources/50 snippets 均通过。16 份相关
Python 仅 AST parse；whitespace 通过，SDK 干净。Host/VM、C3/S3/disabled、
实机/RF/heap/soak 均 not-run。证据索引为
`build/w04-raw-tx-periodic-ledger-evidence.json`。

下一步：将 ledger 连接有界原生模板 owner、Session admission/result 与自治 timer
触发；Session close 先停止后续调度，再验证 callback/worker 和已提交包的退休。
随后补公开 startPeriodic/status/stop/close、rate lease/restore 与故障恢复。
所有 Wi-Fi API 后统一阶段和实机功能测试；BLE 在相关测试后，长 soak 后置至 BLE
API 完成。未刷写、操作串口、擦 workspace、构建前端、提交、推送或更新根 gitlink。
