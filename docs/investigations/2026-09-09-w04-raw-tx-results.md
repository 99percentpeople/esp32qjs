# W-04B 逐次发送结果与等待方存储注销

firmware `d7db8d1` 工作区增量；SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批扩展 [原生 Session](2026-09-09-w04-raw-tx-session.md)，为后续 Session.send
Future 增加精确结果记录。公开 open/enqueue/flush/send 等 Session 方法仍未注册；
不能将内部 result helper 当作已经提供公共 API。

## 准入与结果是同一提交

生产文件仍为 `esp32_mquickjs_wifi_raw_tx_session.h/.c`。新增 admit_result、
result_status、result_release 三个原生入口。单包 MAC validation 通过后，先在
Session mutex 内检查 result slot 和独立 identity 空间，再调用真实 queue admission。
只有 queue admission 成功，才登记结果 pointer/token、转移 payload 并持有独立
Session 引用；全部提交过程不分配、不调用 SDK，不会先驱逐旧包再因结果 slot
不足只留下半次准入。普通 enqueue/batch 继续使用共用 locked admission helper。

每个 Session 最多 8 个结果记录；已完成但尚未释放的记录也占用名额。token 包含
Session generation、独立 monotonic result identity 和 slot index；结果绑定的
sequence 是 queue admission sequence。它们与 native broker operation identity
分开。UINT32_MAX 可使用一次，之后拒绝登记，不回绕。失败保持新 frame、queue、
admission、token 和 record 不变；validation 仍可报告输入拒绝原因。

等待方提供独立、零初始化且地址稳定的 native record，例如未来 Future state 的
成员；不能与其他 input/output/control/payload 重叠。native helper 不额外分配
结果块。成功登记后不得移动、直接读写或释放该 record；读取一律通过受 Session
mutex 保护的 result_status。worker 只在同一 mutex 内访问记录，从不把 pointer
传给 SDK，也不在解锁后继续持有它。

result_release 按 exact token 从表中移除 pointer、清空 record/token，再在 mutex
外释放独立 Session 引用。因此注销返回后，等待方可立即释放原生 state/JS roots，
即使已准入 packet 仍排队或在 SDK 中执行。注销不是 RF cancel，不能承诺该包不再
发送；公开 timeout 文档与实现必须说明这一副作用。队列 payload、SDK copy、lease
和 grant 仍按既有 native ownership 路径处理，不能从结果记录是否存活推断它们可释放。

## 逐次终态与不确定结果

结果 kind 有 pending、completed、rejected、dropped、aborted、uncertain。
completed 携带该包原始 native completion 快照、实际 channel 和 admission sequence；
MAC success/failed/unknown 由 native snapshot 表达，不代表对端 ACK。
每条记录只从 pending 发布一次，后续包完成不能覆盖前一条结果。pending 中的
native 零值不是 driverAccepted=false 的观察证据；公开 converter 必须按 kind
判断哪些字段已有事实来源。

overflow 的完整 batch eviction 与 close 的 queued drop 都在 queue 变化同一个
mutex 边界内标记相应结果。使用仍被 queue 持有的独立 payload identity 匹配
removed 输出；这些 payload 在发布结果之后才由 caller/worker 在 mutex 外 free。
一旦记录完成，清掉 payload pointer，避免后来 allocator 地址复用污染旧记录。

SDK 前拒绝且没有 native token 时，先写入 queue rejected 和 Session fault，再
发布 rejected。已经发布 native token 后出错，则先写入 Session fault/close，再
发布 uncertain；保留原始 error/stage 和匹配该 token 的 native 快照。发现 token
不匹配时不把另一操作的快照当作自己的结果，只保留原 token。不确定结果可以让
公开等待结束，但不会把 queue/flush 账本的 active packet 伪造为 settled。
实际 SDK ownership、grant 和 runtime cleanup 仍可能保持 pending。

close 在 SDK 提交前被观察到时，明确没有 submit 的 active packet 可以 aborted。
若 close 已经与正在进行的 submit 交错，仍按实际 native 回调和 cleanup 路径处理。
结果记录、flush watcher 与 Session caller 分别持有引用；closed Session 可以在
caller 释放后继续由结果读取者保留，最后一个 exact watcher 释放后才回收控制。

## 用例和证据范围

新增 `tests/python/test_wifi_raw_tx_results.py`，复用提取出的 production_session_code
来直接编译实际 Session/queue/arbiter/broker/validator。覆盖 8 个结果上限、slot
满时不驱逐 queue、旧 token/slot 复用、独立序号、不同 packet 的 completion 不互相
覆盖、queue overflow/close dropped、UINT32_MAX 耗尽、SDK 前 rejected 和 SDK 后
uncertain。注销后用固定字节覆盖 caller record，再继续真实生产 worker/callback
路径，检查旧记录存储不再被访问；这不等于完整 Future GC 或设备内存竞争验收。

既有 Session 用例只抽取共用 fixture 组装函数，原场景保留。两份用例均未编译或
执行；12 份 Raw TX/相关 Python 仅通过 AST。后续还需实际 Future capture/finish/
cancel/destroy、ByteSource 第 N 次分配失败和 moving GC、JS close 后 reopen、原生
periodic trigger/rate restore，以及 W-09 control/active/retired 预算。

C5 immutable context `firmware-ci-esp32c5-representative` 编译 exit 0；新增三个
result export 已存在目标对象，实际 worker 的结果发布路径已编译/链接。尚无公开
Session 创建/登记入口，未使用的 admit_result/status/release 在最终 ELF 被裁剪，
不能称为可从 JS 调用。binary `0x291b90` / 2,694,032 bytes，比前批增加 480；
app 空余 14%。没有新增 boot 静态 registry 或常驻 result payload；每个创建的
Session 增加 8 个 pointer 和一个 identity 字段，实际 Future record/Session heap
与并发峰值仍待阶段测量。

MQuickJS 61 sources/49 snippets、manifest 47 classes/419 functions、feature 27、
schema 35 STA/21 AP（live SDK）、strict TypeScript、recorded SDK map、AST 与
whitespace 检查通过。SDK 干净；证据为 `build/w04-raw-tx-results-evidence.json`。
Host/VM、C3/S3/disabled、实机/RF/heap/soak 均 not-run。未刷写、串口操作、擦除
workspace、构建前端、提交、推送或更新根 gitlink。全 Wi-Fi API 后执行阶段与
实机功能测试，BLE 在相关测试后，长 soak 留到 BLE API 也完成。
