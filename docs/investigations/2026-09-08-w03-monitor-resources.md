# W-03：Monitor 原生帧池与引用生命周期

## 实际范围

新增 `wifi_monitor/esp32_mquickjs_wifi_monitor_resources.c` 与内部头文件，复用
NativePool/NativeLease；没有第二套队列或预算器。原生 sink 可接前批 Radio RX
订阅，发布 hook 供后续 EventQueue DROP_NEWEST bridge 使用。本批未创建实际
EventQueue、Monitor JS Session/Frame/Batch/Source，也未新增正式 API 或类型占位。

帧池容量 1–128，snap length 1–16384。初始化分配 slot 数组和定长 payload 区域，
第二次分配失败归还第一次分配；乘法及加法检查后才分配。size/snapshot 计入这两块
内存，不包含调用方 control、EventQueue 或未来 wire/Batch 资源，不等于 W-09
总预算完成。尚无生产 caller，因此未分配常驻池。

## 发布、复制和所有权

只复制 target adapter 证明的 readable_length，以 snap length 为上限；metadata-only
事件只保存元数据。require_complete 拒绝 metadata-only 或复制截断，不承诺 SDK
交付完整空中帧或 FCS。原生 metadata、MAC header 与 callback_time_us 按值保存；
本层未完成公共 PHY 归一化、timestamp 回绕或 wire 时间契约。

每个事件只携带 pool generation、slot index、lease identity，不携带数据指针。
每池 identity 单调增长，UINT32_MAX 后明确耗尽，不回绕。重用同一 control 必须
使用更大的非零 pool generation；全局 generation 分配属于待接入的生命周期。

所有 pool/lease 操作、owner 转移、最终归还计数及重新 acquire 都在本资源的
短 critical section 内完成。payload 复制、分配、释放和发布 hook 在锁外。
slot 在 WRITING 阶段不可消费；发布前已停止接收则直接回收。hook 成功转交
EVENT root，失败必须不保留 root，资源核心负责 discard 并回收。

消费者将 EVENT 转为 PUBLIC；take/discard/close 验证精确 token 与 owner 阶段，
重复关闭和旧事件不能减少其他引用。Frame 可先 retain 原生引用，再创建 JS
View/Source；转换失败须释放该引用。Frame root 关闭后不再允许从它新建引用，
已有 ref 继续借用不可变数据，直到最后 ref 释放。ref 是线性 native handle，
不能通过复制结构体制造新 owner；同一 handle 的操作由调用方串行化。

发布 hook 允许消费者同步 take/close，甚至同 slot 已被后续帧重用。因此 hook
返回后不再访问旧 slot，也不再读取 callback view；仅使用本地长度/截断快照
记账。publishers 与 leased_frames 共同阻止 deinit，避免 root 已消费但发布函数
尚未返回时提前释放 backing storage。流量统计采用饱和 uint64，不回绕。

## 关闭契约与尚待接入

调用方必须停止接收、通过 Radio detach 并排空已进入的 sink，再 discard 队列。
deinit 仅在 accepting=false、publishers=0、leased_frames=0 时成功；旧 Frame 或
ref 保留时拒绝销毁或重新初始化。control、allocator/queue context 必须存活到
相关调用和引用全部结束；本资源 helper 不替调用方释放 control 或完成 Radio
cleanup。首次 lock 初始化及 control 销毁需要外部串行化。

仍需实现实际 EventQueue 桥接、JS roots/OOM/GC、Frame/Batch/View/Source、Session
关闭/reap、完整 PHY/time、W-09 active/retired/control-reserve 预算。现有 CSI 单池
限制及 SRAM 优化未修改。本批不能标记 W-03 完成，也不能把 retained native ref
用例当作 JS movable-GC、Source send 或 RF 验收。

## 测试源码与执行边界

`tests/python/test_wifi_monitor_resources.py` 拼接真实 target adapter/parser、
NativePool、NativeLease 和 Monitor resources。仅 allocator、发布 hook、FreeRTOS
锁映射为明确测试边界，使用 recorded C3/S3/C5 SDK 类型；不是独立测试状态机。

已编写：两次分配故障、大小边界、pool 满、queue 拒收、复制/截断、metadata-only、
require_complete、双引用保留、关闭后数据、重复关闭、同 slot 旧 identity、重开后
旧 generation、同步消费并重入发布、identity 耗尽、统计饱和，以及暂停发布期间
关闭/deinit。暂停仅用于可控调度，生产 hook 不得等待或分配。

按用户要求，该测试未导入、编译或运行；仅 Python AST 检查通过。所有竞争结果
仍是 not-run，不声明先取得失败或修复已通过运行验证。

C5 immutable Context `build/wireless-contexts/c5` 编译通过；日志为
`build/w03-monitor-resources-c5-build.txt`，最后一次源码算术调整的重编译见
`build/w03-monitor-resources-final-c5-build.txt`。app 为 `0x284530`，余量 16%；
nm 确认本批资源入口存在于目标 object，尚无生产调用者，最终 ELF 无 Monitor
资源符号。固件大小未增加不代表未来接入没有 SRAM/heap 成本。

MQuickJS 59 sources / 48 snippets、manifest 44 classes / 398 functions、feature
文档 27 项、config schema 35 STA / 21 AP（live SDK）、recorded map、strict
TypeScript declaration 和 whitespace 检查通过。hash 与边界见
`build/w03-monitor-resources-evidence.json`。

Host/Python 执行、C3/S3/disabled 构建、实机功能与 heap/largest-block 比较均
not-run。Wi-Fi APIs 完成后集中阶段与实机功能测试，BLE 在相关测试后开始；长
soak 待 BLE API 完成。未刷写、操作串口、擦除 workspace、构建前端、提交、
推送或更新根仓库 gitlink。
