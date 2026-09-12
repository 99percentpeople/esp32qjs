# W-08 TWT probe timer 错误返回与清理

基线 firmware `d7db8d1`、SDK `fff9895c82`。本项继续已接入的数字 timer identity
和原生 probe 结果记录，不注册公开 TWT；阶段运行测试仍后置。

## 确认的问题

固定 `ets_timer_legacy.c` 的 setfn 对 `esp_timer_create` 使用 `ESP_ERROR_CHECK`，
arm/arm_us 对 start 也使用该宏。OOM/启动错误会中止设备，不会返回一次 probe
失败。done 则忽略 `esp_timer_delete` 的错误并清除 handle，删除被拒绝时丢失
后续清理所需的原指针。对应 SDK 源码/ABI 已核对，未执行故障注入复现。

真实路径分别为 Wi-Fi OSI + 224 → 本地 `timer_arm_wrapper` → `ets_timer_arm`，
及 OSI + 240 → `esp_coex_common_timer_arm_us_wrapper`。前者是首次发送后的
毫秒 timer，后者是 ACK 后等待 Beacon/Probe Response 的剩余微秒 timer。
只改 setfn 不能消除后续 arm 的断言/abort。

## 已接入的生产处理

仅精确 `&itwt_probe_timer` 走新的 create/start/stop/delete；其他 timer 保留
原调用、参数和单位。原生 Wi-Fi task 仍是 handle 字段的唯一管理者，timer
callback 只携带数字、更新原生记录及投递数字消息。

- create 直接使用 `esp_timer_create` 返回值，成功才发布 handle；每次 create
  消费不可复用 identity。失败不调用 abort，也不发布空/半初始化 handle。
- 毫秒使用 64-bit 乘 1000，微秒直接传递；保留 SDK 零时长的立即计时语义。
  probe 不接受意外 periodic 设置。两个入口都保留 IRAM 路径。
- stop 先使 callback identity 失效。未 arm/已过期的 `INVALID_STATE` 维持 SDK
  原有语义，不作为 callback 已返回的证明；其他错误保存为 STOP 阶段故障。
- delete 只有被接受后才清除 legacy handle。被拒绝时保留同一 handle，后续
  setfn 不得覆盖；可重复 disarm/done 清理未完成的后缀。
- 第一条故障保存原始 error、stage、identity；cleanup error 单独保留，成功
  删除可清除 cleanup error，但不抹去操作失败。阶段含参数、identity 耗尽、
  create、start、stop、delete、native post；post 另存原始队列错误。

初次 native submit 返回和 TX callback（含提前完成重放）返回后执行原生
finish。它先保存 `probe_result.control_error`，尝试 timer 清理，再核对当前
node 地址值/active/phase，只清除这一次 active 并释放一次 PM wake 引用。
清 active 在 PM 调用前，重试及迟到 TX callback 不能重复释放。旧地址不解引用；
node 已更换或阶段不符时保留未证明完成的状态，不操作新连接。

初次 timer 错误会成为 submit 的返回错误，并阻止提前 TX 完成重放；ACK 后
的 timer 错误写入独立 control error。不会伪造 `ITWT_PROBE_TIMEOUT` 或新的
RF 成功/失败事件。此前已保存的原生 event、观察投递错误仍独立保留。

native post 失败在 esp_timer task 中只记录故障、失效数字身份，不修改 Wi-Fi
node；后续 native finish 可处理它。完整 Radio/请求轮询尚未接入，所以目前
不能声称所有晚到 post 失败都会自动完成 Future 和关闭。

## 证据与限制

`esp_timer.c` 的 create/start/stop/delete/异步删除语义加入只读 SHA-256 gate。
共享 SDK 未改写。C5 最终函数表及链接核对证明毫秒、微秒、disarm/done/setfn
实际到达包装入口；错误 helper 与 start/stop/临界区函数位于本构建 IRAM。
SDK abort helper 的最终调用确实到达 `pm_wake_done`；未跳过原生 TX 回收。

C5 镜像为 2,959,120 字节，比上一批增加 1,152 字节。probe timer 静态记录
28 → 36 字节，probe result 32 → 36 字节，共增加 12 字节；TX 静态 32 字节、
512 字节懒分配 ledger 及已跟踪框架静态对象保持不变。使用的仍是原有每阶段
esp_timer 分配，不新增另一套 heap timer owner。删除被接受不等于内存已释放。

构建、实际函数表/IRAM/对象/符号及源 hash 记录在
`build/w08-twt-probe-timer-errors-evidence.json`。新增及扩展 fixture 调用生产
timer/result/TX/SDK helper，覆盖 create/start/stop/delete/post 失败、旧 timer
与队列消息、提前 TX、重复清理、替换 node、毫秒溢出/微秒/零值、保留 handle
和 foreign timer 转交。仅 AST 检查，未导入、编译或执行 fixture。

仍待完成：完整 timer/native/default-event-loop 联合退休、取消/断连、故障
恢复、Radio owner、Agreement/Future/公开 TWT，以及其余 Wi-Fi 范围。当前
故障不会被 runtime restart 重置；没有借本次局部清理宣称 restart 已可恢复。
全部 Wi-Fi API 完成后集中运行与实机测试；长 soak 留到 BLE API 完成后。
本轮无刷写、串口、擦除、前端构建、提交、推送或父仓库 gitlink 更新。
