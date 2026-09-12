# W-04B 公开 Raw TX Session 与 Future 生命周期

firmware `d7db8d1` 工作区增量；固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批将已有 queue、共享 arbiter、原生 Session 和逐次结果记录接入 JS。
`wifi.rawTx.open` 与 Session `send/enqueue/enqueueBatch/flush/status/stats/close`
已正式注册；完整参数、结果、错误及 timeout 副作用见 [API](../api/wifi-raw-tx.md)。
periodic、rate lease、显式故障恢复仍待实现，相关能力保持 false/contract-pending。

## 实现与所有权

新增生产 `esp32_mquickjs_wifi_raw_tx_public_session.c`，open/send/flush/close
使用真实 Future capture/start/poll/finish/cancel/destroy；同步调用走现有合作等待。
没有新增 JS 定时器、独立执行任务或观察队列完成通道。queue 由 runtime poller
触发已有 native worker，当前仍要求 JS runtime yield，不能称为自治周期发送。

open 在 capture 内校验全部参数，start 才创建原生 Session 并排队获取 Radio。
JS class 只在打开完成且未 fault/close 后发布。参数 getter 异常按原对象传播，
每项长度只读取一次；共享 one-shot ByteSource capture 与结果 converter，保持
24–1500 字节、整数输入、ByteView read lease 和现有 payload 分配策略。
共享捕获函数失败时的部分 buffer 仍由 caller 释放。

enqueueBatch 在变更队列前捕获全部 frame；准入失败不驱逐旧包。成功后全部
payload 移交 native queue，eviction payload 在 task mutex 外释放。receipt 的
JS 分配可能发生在准入之后，OOM 不能撤销已准入包；API 要求先查看账本再决定重试。
getter 可重入并关闭 Session，捕获期间独立 native 引用防止 use-after-free，最终
native admission 拒绝已关闭 owner。该交错已审查，尚未执行完整 VM 用例。

Future core 在 capture 后释放 receiver/arguments roots。send/flush 因此各自持有
native Session 引用与精确 result/fence token；记录注销后 native worker 不再引用
Future storage，即使包仍排队或 in-flight，也可销毁 Future。close 另外持有 JS
包装器的 C 引用，finalizer 不会先释放仍供 close.finish 使用的控制对象。

send timeout/cancel 只结束观察，已准入包仍可能发出。flush timeout 仅释放 watcher。
close timeout 即使发生于 Future start 分发前，也保留原生 close 请求；显式
cancel-before-start 可以取消该次关闭意图。open timeout/cancel 请求关闭未发布的
native Session。实际 native completion/Radio lease/grant 仍按原生账本清理，
不以 Future 终态推断 RF 已终止，不强制重启共享 Radio。

close 成功或 status/stats 观察到 closed 时，包装器缓存无 JS 指针的最终 native
快照并释放 native caller owner。保留关闭后的 JS 对象不会占着旧队列和 registry
slot，正常 close/reopen 不依赖 GC。尚未释放的 result/flush 引用仍有独立上限。
最终 JS 包装器本身与峰值 capture 内存仍属于后续 W-09 预算工作。

结果/flush 注销若出现内部 identity 不一致，destroy 防御性保留原生 state，避免
释放登记表仍可访问的存储；没有保留 JS roots。该异常路径没有新增自动恢复器，
仍需阶段故障注入；不可把这项防御处理描述为所有清理错误都已可恢复。

## 公共契约和诊断

增加 WiFiRawTxSession class ID、构造器拒绝直接 new、prototype 方法、4 个 Future
注册，以及 manifest generator 的 owner/source/doc 映射。正式 TS 增加对应参数/
结果/status/stats 和 prototype-only global declaration。唯一 v1，无占位 callable。
capabilities 的 nativeQueue/batchAdmission 为 true；最多 8 native Sessions，
每 Session 128 packets、8 results、8 flushes。已完成但未注销的 watcher 也占名额。

`wifi.status().radio.rawTx` 增加 native Session 数量、关闭/故障/登记引用计数和
首个 Session error/cleanup 诊断，覆盖失败 open 未返回 JS handle 的保留对象。
汇总先 retain registry 快照，再分别在短 task mutex 下读取；无 SDK/JS 调用。
它不是跨 Session/Radio/broker 原子快照，已经回收的失败对象不属于永久历史日志。

## 验证范围

固定 C5 Build Context `firmware-ci-esp32c5-representative` 编译 exit 0，实际 JS
方法、Session new/admit/result/flush 与 worker 路径均已链接最终 ELF。
binary `0x295460` / 2,708,576 bytes，比上一批增加 14,544；app 空余 14%。
原生静态 s_lane=44、s_radio=824、s_raw_tx=128、s_retired=56、s_sessions=32
bytes，未增加这些常驻控制表；这不是设备 heap/碎片或 SRAM 峰值验收。

manifest 48 classes/427 functions、feature 27、schema 35 STA/21 AP（live SDK）、
strict TypeScript、SDK map、MQuickJS 61 sources/50 snippets 和 whitespace 均通过。
15 份 Raw TX/相关 Python 仅 AST parse，没有导入、编译 fixture 或执行测试。

新增 test_wifi_raw_tx_public_session 使用真实 production start/poll/cancel/destroy
及实际 Session/queue/arbiter/broker，准备覆盖 Future 释放后原生包保留、flush、
关闭缓存/重开和关闭分发前 timeout；generic Future scheduler/JS converter 不在
该 C fixture 范围内。新增 test_wifi_raw_tx_session_options 准备使用实际 MQuickJS
执行生产参数捕获的默认值、边界、getter-once、异常保留及 moving-GC/Nth allocation。
既有 ByteSource/status GC fixture 同步共享 helper 与 Session 汇总字段；共享 VM
fixture class count 更新为生产的 USER+50。这些用例全部 **not-run**。

完整 JS enqueueBatch/capture/finish OOM、reentrant close/GC、Future 调度饱和与
runtime teardown、C3/S3/disabled 矩阵、实机/RF/heap 均 not-run。证据索引为
`build/w04-raw-tx-session-public-evidence.json`。未刷写、操作串口、擦 workspace、
构建前端、提交、推送或更新根 gitlink，固定 SDK 工作区保持干净。

继续推进周期发送、rate lease/restore、故障恢复与剩余 Wi-Fi API。全部 Wi-Fi API
完成后统一阶段及实机功能测试；BLE 在相关测试后；长 soak 留到 BLE API 也完成。
