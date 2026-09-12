# W-08 iTWT information timer 参数与身份

本批处理固定 C5 SDK 的八个 individual information timer。完整 Agreement
公开接口、信息 TX callback 身份、teardown 联合退休与物理恢复仍待实现。

## 原生证据与实现

固定 SDK `fff9895c82` 的 `he_twt_information_txcb` 和
`he_recv_action_twt_information` 各分配 9 B 参数并交给同一个 timeout callback。
`itwt_information_timeout_fn` 将原始参数地址交给 native timer operation 29；
`itwt_information_timeout_fn_process` 使用其中的 control/时间值，按 flow
stop/delete timer，最后通过 OSI free 释放该地址。`ets_timer_done` 本身只
delete handle，不释放 callback 参数。以上是原始 archive 与 SDK 源码的静态
所有权证据；取消/迟到消息导致的实际设备症状尚未运行复现。

新增 adapter 复用现有 probe/setup timer 的 OSI 分发入口，不更换公共 timer
服务，也不新增 JS API：

- setfn 只接管精确 information timer 表及已核对的 callback，保存 SDK 转交的
  参数。create/准入失败释放已转交参数；未知 callback 不授权释放未知指针。
- TASK callback 和 operation 29 携带不复用的数字 identity，先锁定本次状态，
  锁外发布。post 失败保留参数供原生清理，不从 timer task 修改 Wi-Fi 状态。
- native 消费时核对关联 node、flow 及请求 ID，然后将参数所有权交给原始处理
  函数，由原始 free 完成释放。重复/取消后的消息不读取旧参数。
- all-flow 信息帧核对整个 established flow/request 集合，不能只检查携带
  timer 的那个 flow；单 flow 不因无关 flow 增加而失效。
- disarm 先撤销 callback 权限；done 只在 stop 完成后 delete。失败保留原
  handle 和参数，并保存最初故障与未完成清理错误。重试不重复已成功的 stop。
- 原始 process 已领取参数时，timer done 不再释放它；busy 状态保持到原始
  process 返回。定时器回调仅携带数字，删除成功后的 queued message 不持有
  参数地址，因此取消路径可释放尚未交付原生 process 的参数。

## 边界与验证

八槽 ledger 延迟使用 352 B INTERNAL，不复制 SDK 的 9 B 参数，不增加另一套
timer task 或公共预算器。快照记录原始错误、阶段、active/payload/busy/cleanup
mask，供后续 Agreement 协调使用。`quiescent_native` 返回对应请求的资源缺席
及 revision；它本身不建立 timer/native/event 顺序证明。

新增 `test_wifi_twt_information_timer.py` 使用生产 adapter，注入原生边界，覆盖
参数交接、取消后 slot 复用、提前消费、post 失败、stop/delete 后缀、create/
ledger 分配失败及关联变化。SDK fixture 另调用生产 capture/match，区分单 flow
与 all-flow。所有 fixture 按用户安排仅做 AST 检查，未导入、编译或执行。

生产构建/链接证据已记录在 `build/w08-twt-information-timer-evidence.json`：

| Build Context | binary bytes | 相对 teardown-tx |
| --- | ---: | ---: |
| C5 roaming | 2,990,160 | +3,344 |
| C5 no-SoftAP | 2,866,560 | +3,344 |
| C5 Wi-Fi-disabled | 459,024 | 0 |
| C3 | 2,665,696 | 0 |
| S3 | 2,570,768 | 0 |

C5 静态状态为 28 B，延迟 ledger 为 8 × 44 B。原 TX/setup timer/result
账本及前批 teardown 静态状态保持原尺寸；共享 SDK 和 build-local 修补 archive
hash 均未变化。最终 ELF 的 OSI setfn/disarm/done/arm 分发和 native operation
29 已连接新实现，IRAM 分发 helper 无堆操作。请求 quiescent helper 仍只在
archive，等待完整退休调用方；不能把它写成已完成联合释放。

全项目 manifest（52 classes / 513 functions）、27 feature docs、配置 schema、
1,267 项 SDK map、严格类型、MQuickJS 61 sources / 60 snippets 及 4 份 fixture
AST 检查通过。阶段运行、实机与 RF 均为 `not-run`；不以本内部 adapter 完成
代替完整 TWT Agreement 或 Wi-Fi 完成。
