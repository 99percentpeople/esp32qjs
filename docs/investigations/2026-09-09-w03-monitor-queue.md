# W-03：Monitor EventQueue bridge 与上下文寿命

## 实际实现

新增 `wifi_monitor/esp32_mquickjs_wifi_monitor_queue.c` 与内部头文件。它创建真实
EventQueue，固定使用 DROP_NEWEST 和上一批无指针事件 token，持有生产者 native
queue 引用，并提供 resources publish hook。没有新增公共 JS 方法、类、类型占位
或另一套消息队列。最终 Monitor Session/Frame converter 与 Radio 生命周期调用方
仍待接入，不能将这批对象编译视为 Monitor 可用。

## 为什么 context 不能随 close 释放

检查生产 EventQueue 可见：Future 已 poll 出事件后，事件不再位于 native queue，
但 Future 的 finish/destroy 仍可能调用 `to_js/drop`，并持有 native queue retain。
`close/dispose` 本身不能证明这些引用已经排空。

EventQueue 新增内部 `bind_context_release()`，仅允许在新队列私有且 GC-rooted、
尚未公开或开始 producer/receiver 时绑定一次。成功转交一个 context 引用，失败
仍归调用方；关闭、dispose 或 receiver/reaper 尚持有队列时不释放它。实际 native
队列销毁先释放资源和队列存储，再调用 context release；回调不得使用 JS。
既有调用者的钩子为 NULL，公共 EventQueue API 不变。

Monitor bridge 在创建前 retain context，构造失败逐步归还；成功后由队列最终销毁
释放该引用。`context_owned` 阻止旧队列仍存活时复用同一 bridge。生产者另外持有
native queue retain，防止 JS disposal 后 callback 访问已释放 queue。

Frame converter 接收已转为 PUBLIC 的 root。成功时必须同时持有 Frame 自己的
context 引用，独立于队列；失败时先拆除部分 JS object 的 native ownership，再
由 bridge close root。EventQueue 在调用 converter 之前已置 event_finished，
因此不能依靠后续 Future destroy 替 converter 释放失败的 root。

## 关闭与线程边界

EventQueue close 钩子先停止 resources 接收，再请求 Session cleanup；它也可能在
runtime/reaper 线程执行，只能发起 native 清理，不能等待或调用 JS。dispose 的
discard 使用真实资源 helper 归还 EVENT root；已转为 Frame 的 root 不会重复关闭。

bridge detach 要求调用方已经停止 producer 准入并通过 Radio 排空 callback，随后
close/discard 队列并释放生产者 retain。JS/Future/reaper 仍可保留 context；调用方
在 JS 线程另行 dispose 已 rooted 的 JS queue。detach 不能代替 Radio cleanup，也
不承担其 SDK 调用超时；完整 Session/reaper 仍待实现。

bridge 首次零初始化、构造与 detach 由调用方串行化，callbacks/context 在 native
引用全部释放前保持有效。pool 的 deinit/reopen 限制、Frame/View/Source 引用保留
以及全局 W-09 预算继续适用；本批没有静态全局 Monitor 控制器或常驻帧池。

## 测试源码，尚未运行

`tests/c/integration/wifi/monitor/test_wifi_monitor_queue.py` 使用实际 Monitor resources/queue bridge、
EventQueue 的构造、context bind、close/dispose、retain/release/native destruction
和 Future finish/destroy。JS、SDK resource 创建、队列字节传输与 wake 调度是明确
边界替身；没有声称该 fixture 等同完整 FreeRTOS queue/reaper 或 movable GC。

已编写 context retain/队列分配/资源创建/JS object/lookup/bind 失败，非法容量与
运行 pool 拒绝，queue full，取出事件后 dispose，迟到 Future 取消或转换异常，
Frame 成功后的独立 context，关闭停止新帧、重复关闭，以及旧 context 禁止复用。
前批 resources fixture 提取可复用 production_code，仍调用同一生产源码。

这两份 Python 文件仅作 AST 检查；未导入测试、未编译 fixture、未执行用例。
完整 EventQueue runtime teardown、受控线程、GC/OOM 与 Radio 集成竞争留在阶段
验收；代码存在不能记为测试通过或确认缺陷已经复现。

## 已执行证据

C5 immutable Context `build/wireless-contexts/c5` 构建通过，日志
`build/w03-monitor-queue-c5-build.txt`。app `0x284550`，余量 16%，较上批增加
0x20。nm 确认三个 Monitor bridge 入口存在于目标 object；当前无生产 Monitor
调用者，最终 ELF 未保留它们。共用 EventQueue native destructor 已链接，队列
控制结构增加一个释放函数指针；本批没有实机 heap/largest-block 比较。

MQuickJS 59 sources / 48 snippets、manifest 44 classes / 398 functions、feature
文档 27 项、config schema 35 STA / 21 AP（live SDK）、recorded map、strict
TypeScript declaration、两份 Python AST 与 whitespace 检查通过。hash 和未运行
范围见 `build/w03-monitor-queue-evidence.json`。

Host/Python、C3/S3/disabled、实机功能与 RF 共存均 not-run。Wi-Fi APIs 完成后
集中阶段与实机功能验证；BLE 在相关测试后开始；长 soak 待 BLE API 完成。没有
刷写、串口操作、workspace 擦除、前端构建、提交、推送或根 gitlink 更新。
