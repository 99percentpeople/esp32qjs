# W-08 TWT probe 发送范围与共用回调审查

基线 firmware `d7db8d1`、SDK `fff9895c82`。本项扩展现有 TX ledger，未注册
公开 TWT API；所有动态与实机用例仍按用户要求集中后置。

## 实际发送类型及实现

`esp_wifi_sta_itwt_send_probe_req(timeout_ms)` 的原生 ioctl handler 调用
`ieee80211_send_probereq`，发送普通 Probe Request，使用 callback index 0。
它不发送 null/data frame，也不经过 TWT Action builder；不能用 bit 18–20 的
管理帧分类替代该路径。过去 TX 记录把尚未审查的 probe 称为 null/data，
以本项实际 SDK 反汇编为准。

新增 `wifi_sta_itwt_send_probe_req_process` wrapper，在原生调用前登记当前
task，保留 `probe_calls` 至整个 handler 返回。`ic_tx_pkt` wrapper 要求此 task
范围、callback mask 恰为 1、至少 24 字节 header、实际 Probe Request frame
control，才把精确 EB 纳入既有 ledger；普通扫描、其他 task、ISR 及其他类型
帧不登记。新 native call/原生返回/回收仍遵循之前的独立 slot 保留规则。

handler 在 send 成功返回后才设置 probe pending 并 arm response timer。
因此提前完成 TX、甚至缓冲已回收时，`probe_calls` 仍不能提前变为零。
异常嵌套/重叠 scope 保留首个 `PROBE_SCOPE` fault，不覆盖外层 task，也不让内层
返回清掉外层记录。原生错误原样传递，scope 收尾不重试发送。

SDK snapshot 的内部字段改为 `tx`，包含管理 Action 和本次 probe EB；同一
512 字节 lazy boot ledger 不扩容。C5 静态记录从 24 增至 28 字节，快照结构
原有 padding 用于 `probe_calls`。原 `management_tx` 内部字段直接替换，无别名。

## 本轮新确认的隔离缺口

`wl_cnx.o:cnx_probe_rc_tx_cb(eb)` 从当前 EB 取发送状态后，无条件调用
`itwt_probe_rc_tx_cb(status)`。后者只看当前 Station node 的 probe-active byte，
没有 EB/cookie 参数。普通连接 Probe Request 的迟到 callback 因而也可进入
当前 TWT probe 的 timer/event 状态路径。此为固定 SDK 源对象控制流证据，
尚未执行受控调度复现或 RF 测试。

此外，TWT handler 设置 pending 在发送后；TX callback 的先后、RX response、
timeout 和普通 connection-probe 的相互影响须一起处理。当前新 ledger 观察
已交出的缓冲，不改变 SDK 完成路由或解决上述隔离缺口。不得把空 ledger 或
单个完成事件当作 TWT probe 已安全退休。后续应在收到 EB 的 callback 边界
识别身份，保留普通连接逻辑，再接入 timer/native/event 联合退休。

## 编译和证据边界

C5 最终 ELF 的 `ieee80211_send_probereq` 确认调用 `__wrap_ic_tx_pkt`；后者
调用原生 `ic_tx_pkt`。该原生函数是 flash-resident，本次没有把带 task/heap
调用的路径误标成 IRAM。共用 recycler 的 IRAM 闭包保持不变。

probe ioctl wrapper 已编入生产 archive；因公开 TWT 尚无 caller，该 wrapper
及 SDK probe submit 入口仍被最终 ELF 的 GC 丢弃。后续公开入口接入时必须
重新验证 SDK ioctl → wrapper 的最终调用关系，不能把 archive 符号冒充该
入口已在最终镜像可达。

C5 roaming 构建 2,955,376 B，比上轮增加 352 B；C5 Wi-Fi 关闭、C3、S3 不增加。
既有框架静态对象不改变。Host fixture 已扩展 task/ISR 隔离、普通 Probe Request、
错误与提前回收、handler 结束前 timer 窗口、嵌套 scope；仅 AST 检查，未导入、
编译或执行。证据清单为 `build/w08-twt-probe-evidence.json`。

完整 probe callback 隔离、timer/native/event quiescence、Radio owner、
Agreement/Future 和公开控制仍未完成。未刷写、串口、擦除、前端构建或提交。
