# W-03：Monitor 原生 capture 与 Radio 生命周期

## 实际代码与状态

新增 `wifi_monitor/esp32_mquickjs_wifi_monitor_capture.c` 和内部头文件，连接现有
resources、queue bridge 与 Radio RX subscribe。control 由调用者所有，本层不
分配 Session、不注册 JS 类、不创建全局 reaper，不释放旧 Frame/View 数据。
完整 Session owner/JS/finalizer/reaper 仍待接入。

Radio 新增真实 MONITOR client，`wifi.status().radio.clients.wifiMonitor` 报告其
计数并纳入 total；类型、API 文档和 status GC fixture 同步更新。它是 owner
诊断字段，不是 Monitor 公共 namespace 或功能稳定等级的声明。

## 启动和停止

初始化要求有效 filter、未关闭的 queue、已初始化且空闲的 resources，以及未耗尽
的 identity。启动先获取独立 Monitor Radio lease，再 ensure_started、读取省电
状态、可选固定信道 claim、实际 channel/generation，最后启用 pool 并提交共享
RX subscribe。start 在运行状态幂等，但有待处理停止/关闭或信道冲突时拒绝。

Radio 原本已允许多个匹配的同信道 fixed owner；这里复用该机制，不引入私有
SDK setter。callback 检查精确 lease 的信道状态和可用 RX primary，冲突锁存
channel_conflicted 并停止新帧；不在 callback 内调用 Radio mutation 或 SDK。
current channel 路径仍跟随 Radio，每帧 driver metadata 保留实际观测。

停止先关闭 resources admission，再 release promiscuous。acquired=true 表示 SDK
cleanup 或已进入的 dispatch 尚未完成，保持 RX、信道、Radio、queue 与 control。
普通 drain 返回 ESP_ERR_TIMEOUT 且不伪造 driver error；cleanup 故障保存独立的
原始 ESP 错误和阶段。重试只推进现有 token 的后缀，不能重新申请替代旧 claim。

RX token 返回后才释放信道并 discard 排队 EVENT，进入 STOPPED；Radio lease
继续保留以支持显式 start。旧 PUBLIC Frame/retained ref 不被 discard，停止后可
在同一有界 pool 的空闲 slot 继续捕获。启动失败保存 last_error/last_stage，并
尝试同一停止后缀；即使 cleanup 成功也不会覆盖原始错误。

close 请求不可逆，先完成 stop，再释放 Radio，最后 detach queue 的生产者
retain。CLOSED 只说明这些原生控制权已释放，不等于旧 queue/Future/Frame/
View 或 payload 已销毁；调用者必须继续持有必要 context。任何 cleanup 未确认
都保留相应资源。这里不调用全局 Wi-Fi stop/restart，不重建 NVS 或重置 identity。

## 并发与剩余边界

生命周期操作与状态读取要求 Session mutex 外部串行化，控制内存从 subscribe
到 Radio 排空期间保持稳定。request_stop 允许 callback/reaper 调用，原子置位
请求并立即停止 pool admission；start 在 subscribe 前后检查关闭/停止，sink 也
检查请求，避免启动中收到关闭后再次放行新帧。完整 owner/refcount、mutex、
reaper 注册失败处理与 runtime teardown 仍由下一批 Session 接入完成。

require_power_save_none 目前与现有 CSI 一样，只检查启动快照，不强制设置
Wi-Fi 省电，也不保证其他 owner 随后不能更改。持续 require-none lease 尚未完成，
不能把该 helper 写成完整 powerSavePolicy 验收。当前 Radio acquire 仍拒绝存在
AP helper owner 的新 claim；AP/APSTA 共存准入留在 W-01/W-02 范围继续实施。
完整 PHY/time、configure、Batch/Source、W-09 预算也没有因本批而完成。

## 测试源码与已执行检查

`tests/python/test_wifi_monitor_capture.py` 编译真实 capture/resources/NativePool/
NativeLease/filter/target adapter 源码，Radio 与 queue lifecycle 为明确替身。
已编写各启动步骤失败、失败且 acquired=true、cleanup 错误与普通 drain 区分、
重试不提前释放信道/Radio/队列、stop/start 复用、关闭后 Frame/ref 保留、固定
信道冲突与启动同步关闭。底层真实 Radio 事务已有前批独立集成 fixture；本批
不能当作 SDK/Radio/Monitor 全链路运行或 RF 证明。

两份 Python 文件仅 AST 检查；没有导入、编译或运行 fixture。Host/竞争、真实
GC、FreeRTOS 调度、AP 共存与完整 teardown 的实际结果均 not-run。

C5 immutable Context `build/wireless-contexts/c5` 构建通过，日志
`build/w03-monitor-capture-c5-build.txt`；app 为 `0x284580`，余量 16%，较上一批
增加 0x30。capture 入口编译到目标 object，尚无生产调用者，因此未保留到最终
ELF；新的 Radio owner 计数/status 路径已链接。没有新增常驻 Monitor pool 或
进行设备 heap/largest-block 比较。

MQuickJS 59 sources / 48 snippets、manifest 44 classes / 398 functions、feature
文档 27 项、config schema 35 STA / 21 AP（live SDK）、recorded map、strict
TypeScript declaration、两份 Python AST 和 whitespace 检查通过。hash/未运行
边界见 `build/w03-monitor-capture-evidence.json`。

Wi-Fi APIs 全部完成后统一 Host/矩阵与实机功能测试，BLE 在相关测试后开始；长
soak 待 BLE APIs 完成。没有刷写、串口操作、workspace 擦除、前端构建、提交、
推送或根 gitlink 更新。W-01～W-12 的全部剩余功能目标继续有效。
