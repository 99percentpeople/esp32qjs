# W-02：STA/AP netif 退休与默认事件循环串行化

基线 firmware `d7db8d1`，固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批为工作区增量，公开 configure/APSTA 尚未完成。

## 源码依据与范围

固定 SDK `components/esp_wifi/src/wifi_default.c` 的 clear helper 先清除本角色的
静态 netif 指针，只有所有角色都为空时才 unregister 默认 handler。因此 STA/AP
共存时，runtime task 上的同步 detach 不能保证已经进入的默认 callback 退出。
`components/esp_event/esp_event.c` 默认 loop 使用递归 mutex，支持从 callback 中
注销其他 handler；将 detach 调度到同一 event task 可以串行化该调用链。

SDK `esp_netif_stop_api` 在 TCP/IP task 中移除接口，并可追加 NETIF_DOWN；
只有一个排在 WIFI_STOP 后的 marker 不代表这些派生 IP 事件也已 dispatch。
框架 `net_ip_event_handler` 当前忽略 event_data，使用后续 TCP/IP 快照读取现存
接口；此次未把它错误地标为已确认的 payload 解引用缺陷。

以上是生产源码路径证据，尚未执行运行时竞争复现。

## 实现

新 wifi_netif 生产 helper 被 Station/AP 两个 retire 路径直接调用：

1. 单个 boot-owned slot 保存 netif 和单调 uint32 identity；事件 payload 只复制
   identity，不含 runtime、JS 或 caller stack 指针。identity 耗尽明确失败，不回绕。
2. 默认事件循环执行 SDK netif STOP action，再 clear driver/default handlers；
   实际 SDK 调用不在 spinlock 中，也不等待 runtime caller mutex。
3. detach 成功后，由 caller 追加第二个 fence。到达 fence 后才允许 runtime task
   destroy netif；因此 STOP 同步派生且已经排队的 IP 事件先于释放。
4. queue admission 使用零等待。首步入队失败可重新提交；已接受的 job 等待超时
   保留 slot 与 netif；第二步入队失败只重试 fence，不重复 detach。
5. 同一 netif 的后续调用继续读取原操作。另一个 netif 在 slot 占用时明确失败，
   不抢占资源。caller mutex 防止并发调用相互释放；callback 状态由短 spinlock
   保护，重复/旧 identity 不能操作新 netif。
6. 只有实际 clear 失败才设置 caller 的永久 detach_error。SDK 已无条件释放 driver，
   故此时保留 netif，需要设备重启；排队失败/超时不伪装成永久 detach 失败。

一次 job 轮询等待上限 1000 ms（调度粒度为 tick）；SDK 注册/停止/销毁调用本身
沿用 SDK 阻塞语义，不承诺整个 API 的硬 deadline。handler 与 mutex 按 boot 生命周期
保留，只有一个待处理 slot，不为 runtime restart 分配新的一套服务。

## 仍需解决的边界

- Caller 仍须先终止对应 native 操作、取得必要 Radio 排他并保留至清理结束。
  此 helper 不负责 ESP-NOW/CSI drain，也不替代公开配置协调器的 stop/start 事件
  身份隔离或中央 runtime 清理。没有将公开 APSTA 提前注册。
- Fence 仅排序已经排队的事件，不保证所有未来 producer 已终止。
- 新发现：固定 SDK 的 `esp_netif_ip_lost_timer` 以原 netif 指针为 arg，通过
  `esp_netif_is_active` 检查是否仍在列表；stop/destroy 路径未显式取消该 timer。
  当前 C5 Context 启用 LOST_IP_TIMER，间隔 120 s。销毁后地址复用时，指针检查
  可能把旧 timer 认作新 netif，这是源码推导的风险，尚无运行复现。此批不修改
  外部 IDF；APSTA/快速重建前还需处理这项原生生命周期边界。不能声称此次 fence
  已解决所有晚到 IP 事件或该地址复用问题。

## 验证账本

C5 immutable Context 编译、MQuickJS 语法及生成物一致性结果与源文件/二进制
hash 记录于 `build/w02-netif-retirement-evidence.json`。

`test_wifi_netif_retirement.py` 编译整个生产 translation unit，仅替换 SDK/RTOS
边界，覆盖队列拒绝、已入队超时、迟到完成、fence 后缀重试、并发 caller 准入、
旧 identity、detach poison 与 identity 耗尽。既有 Station/AP cleanup 提取用例
同步新的 helper 边界；没有用独立退休状态机替代生产实现。

这些用例仅完成编写及 Python 语法检查，Host C/Python/故障注入运行均为 not-run。
全 target/feature-disabled 构建、GC/实时并发、设备功能、内存回收与 SDK 延迟 timer
复现仍待 Wi-Fi API 完成后集中执行；长时间 soak 放到 BLE API 完成后。
未刷写、操作串口、提交、推送或更新父仓库 gitlink。

后续：[SDK timer 构建修补](2026-09-08-w02-netif-timer.md)已在 stop/destroy 取消
精确 timer，消除上述未取消路径；运行对比与实机地址复用验证仍为 not-run。
此文件中的原批次验证账本不替代后续修补的证据。
