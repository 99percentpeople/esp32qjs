# Runtime restart 内存调查

目标：解释 C5 runtime restart 后 internal largest 从 63,488 降至 36,864 bytes
及小幅 free 差值，再修复已确认原因。只操作 `hw-10bda3c854e8`；保留 workspace。

## 已确认的取样缺陷与修复

`sys.status.memory` 是惰性树，region getter 才返回数值对象。旧设备测试保存
`before = sys.status.memory` 和 `after = sys.status.memory`，因此读两者时都是当前值。
同一设备实测 `sameTree=true`；保存的 internal region 保持 87,059，而 CSI 打开期间
通过保存的 tree 再读 internal 为 66,491。没有修改公共 `sys` 语义。

新增 `test_wireless_memory_sampling.py` 执行实际设备测试源码与实际 harness 取样 helper：
旧脚本前值变成后值，并漏报注入的 managed owner 泄漏；修复后两个用例均通过。
测试使用 Node 仅提供惰性 getter 边界，ES5 语法仍由 vendored MQuickJS check-js 验证。
共用 `test.memorySnapshot()` 已用于无线 lifecycle、CSI 生命周期/吞吐/共存/soak
及 socket 的同类 before/after 测试。

当前生产固件上使用修正脚本复测，internal free 95,071 → 95,083，largest 36,864
保持不变，PSRAM free 3,970,224 不变，managed owners 无差值。该单组数据不解释
跨 restart 的碎片，也不构成长期无泄漏证明。先前错误取样结论已在无线核心证据中撤回。

## 临时原生探针（不属于最终实现）

探针用 linker wrappers 记录框架/SDK 的 heap_caps 分配返回地址，并在重启前用
IDF heap walker 记录存活块地址、大小和分配调用位置；不读取内存内容/秘密。
有界 2,048 项记录放在 PSRAM，关闭/释放会移除对应记录。boot 初始阶段及未经过
wrapped 边界的分配 caller 为 0，不据此猜测 owner。探针需在最终修复前移除。

诊断 build `de3e4169974e3a21d7a2567e`，Artifact `63ff07a06018ead38d4fffdec53bc42be0fe5cc7adac177dd440fe4863ee0b63`，
preserve flash `0db97bfd17a5a87472437cc1` 成功并自动重连，bootId `2b11155bd138bf7b`。
初次输出超过原生 64 项日志 ring，前部记录丢失；没有把残缺 dump 当完整证据。
第二版将每条日志打包 12 个块以容纳完整快照，仍保持现有 ring 和 SRAM 优化。

原始证据位于 `build/wireless-core-evidence/heap-probe/`。
第二版 build `e89c79e1a3156a869d6c35a9`，Artifact
`54dc0fdaf6025b521a54001790bd4cdd050d764e2290d09e91745a01210d62f8`，
preserve flash `57bb13b7ec792d5bec31ef34` 成功，bootId `9b234ffd959fae1b`。
探针的 PSRAM 开销不与正式固件直接比较。

## 原因：未到静止状态的任务回收

同一 boot 的两次任务列表复现：关闭 BLE 后立即有 11 个 RTOS task，其中
`nimble_host` 为 `deleted`；100 ms cooperative sleep 后回到 10 个，deleted task 消失。
重复 5 次纯 BLE open/scan/close 均得到：

| 状态 | Internal free | Internal largest |
| --- | ---: | ---: |
| 打开前静止 | 95,051 | 63,488 |
| close + GC 后立即读取 | 88,795 | 49,152 |
| 给调度器 100 ms + GC 后 | 95,051 | 63,488 |

即立即取样暂少 6,256 bytes，调度回收后全部返回；任务列表复现的另两轮同样差值，
其静止 free 为 95,055。这不是用 `gc()` 可以同步释放的 JS heap。

生产 `ble_host_task` 在 `nimble_port_run` 结束后调用 `nimble_port_freertos_deinit`。
固定 NimBLE 的 `esp_nimble_disable` 调用 `vTaskDelete(host_task_h)`；自删除 task
由 FreeRTOS idle task 的 `prvCheckTasksWaitingTermination` 最后释放 TCB/stack。
因此 Host 终止、JS owner 释放、RTOS 堆块归还不是同一个时间点；没有理由在驱动
close 中人为加固定延迟，或把 idle 尚未回收的 stack 误判为 callback storage 泄漏。

完整 heap walker dump 进一步显示，generation 1 的预留 internal 堆被一块 4,096 bytes
任务栈切开：`0x4083754c`，caller `0x42008ad2`，最终 ELF addr2line 指向
`xTaskCreatePinnedToCoreWithCaps` 的 stack 分配。相邻空闲块为 7,740 / 52,876 bytes，
SDK largest 为 51,200。后续 dump 中这块已释放，预留区合为一块 64,720 bytes，
SDK 按 TLSF 可分配尺寸返回 largest 63,488。此块不是上文的 6 KiB NimBLE 栈：
最终 link map 将该 WithCaps 调用的候选限制在 HTTP server/client/socket，后两者
在这个 Context 显式请求 PSRAM；HTTPD_DEFAULT_CONFIG 则为 4,096 bytes / INTERNAL，
且设备 task 列表确有存活的 `httpd`。这是现有 HTTP server 的存活栈在重建后的布局变化，
不是应该强制释放的无线 owner。NimBLE 暂待回收和 HTTPd 存活布局是两个独立因素。
原始 36,864 bytes 那次没有原生块 dump，不能声称已还原它的每个分配位置。
dump 的 `overflow=0`、BEGIN/END 齐全。
这个 64 KiB 预留区来自 PSRAM malloc 的 internal reserve，heap walker 同时列出其
父分配和子 heap，不能把地址嵌套误读为堆重叠损坏。

按真实 region 快照并等待 task cleanup 后，诊断固件 generation 3～6 的四组
预热 + 5 次开关循环，各组 internal free before/after 相同：94,911、94,903、
94,903、94,903；largest 全为 63,488。小幅 free 差值没有按重启次数持续累计。
原生 dump 中同一分配调用点的短生命周期 Future handle/timer 块大小会有 4～12 bytes
差别；最后两组存活块的 `(size, caller)` 多重集完全相等。TLSF 无法拆出过小余块时
会把余量留在已分配块中。因此短周期的字节差值也不能独自证明 owner 泄漏。
这些有限样本不是长期无泄漏证明。

## 最终修复

- 共用 `test.memorySnapshot()` 实时物化每个 region/manager，修复所有检出的同类测试。
- 无线 lifecycle 测试在两次取样前给 idle task 运行机会，并检查任务列表中没有
  `deleted` 项。最多等待 1 秒；列表截断或无法清理会明确失败，不把未静止样本记为通过。
- 新增第三个生产脚本回归，确认始终 pending 的原生 task cleanup 会使取样失败。
- 补充 `sys` 文档的惰性 getter 取样示例。公共 API 不变；没有用新增预算器或
  强制 sleep 修改 Radio/BLE 的生产关闭语义。
- 所有临时 wrappers、heap walker 和构建链接参数已移除。最终 link map 无
  `heap_probe` / `__wrap_heap_caps_` 符号；SRAM 优化保留。

Host C 65/65、Python 339/339、MQuickJS 59 sources / 47 doc snippets、manifest
43 classes / 384 functions 均通过。此轮最终 C 实现相对之前通过 C3/S3/C5/disabled
矩阵的实现没有新增差异；变化为测试取样、测试等待和文档。正式 C5 rebuild 通过，去探针的设备复测如下。

## 正式固件复测

Build `a413b18ee84dcb29a4d1cbeb`，Artifact
`5fadea240a380a97b4772f8d144620c166e900d87dec0c4403a3add6f72a8a7a`，
app 2,468,096 bytes；preserve flash `a7623b01ebdd88169835136a` 成功、ROM hash
verified 并自动重连。最终 link map 不含临时探针；生产 C/H 文件 hash 与先前三目标
构建证据相同。没有再次引入内部静态 RPC/log buffer。

确认相同 C5，bootId `f25bacbbce1bffc1`，8 MiB Flash / quad PSRAM，workspace
4,653,056 bytes 保留。每个 generation 都等待 startup healthy、failureCount 0 后才
执行注册测试的原始源码（以有界 operator exec 包装，不写 workspace）。每组 1 次预热、
5 次 BLE/ESP-NOW/CSI open/close、GC，并有界确认没有 deleted task 后取样：

| Generation | Internal free，组前=组后 | Internal largest，组前=组后 | PSRAM free，组前=组后 | PSRAM largest | Managed PSRAM |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 | 94,979 | 51,200 | 3,970,228 | 3,932,160 | 152,397 |
| 2 | 94,919 | 63,488 | 3,970,224 | 3,932,160 | 152,397 |
| 3 | 94,911 | 63,488 | 3,970,224 | 3,932,160 | 152,397 |
| 4 | 94,907 | 63,488 | 3,970,224 | 3,932,160 | 152,397 |

三次 restart 前均确认 BLE scanner.receive 与 CSI receive 两个 Future 为 pending，
Radio clients 为 2；重启后 clients 归零，Future/队列回到 Agent 基线，后续 reopen 成功。
bootId 不变，generation `1 → 2 → 3 → 4`。最终 generation 4 healthy、safe mode false、
failureCount 0、orphans 0，10 个 task 无 deleted。每组 managed owners 相等；跨重启 free
仍有小幅布局差值，largest 没有持续下降，后续均为 63,488。未把有限样本写成长期稳定性证明。

`final/g*-lifecycle.json`、`g*-restart.json`、`g*-healthy.json` 和最终 health JSON
保留原始证据。此前错误的 before/after 内存“通过”结论由本节的真实取样结果替代。
此问题的修复是恢复可信取样与静止条件；没有确认需要修改 Radio/BLE 生产关闭语义的
持续内存泄漏。F-CORE 其余竞争/分配失败覆盖缺口及 BLE/GATT 对端、ESP-NOW 双机、
CSI RF、500 次完整生命周期/共存仍按原任务书单独跟踪。
