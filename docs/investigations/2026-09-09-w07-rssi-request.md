# W-07 RSSI 一次性请求与重建语义

firmware `d7db8d1` 工作区增量，SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`。承接
[显式阈值控制](2026-09-09-w07-connection-controls.md)与
[重建准入](2026-09-09-w07-restart-admission.md)。本批明确 RSSI 请求不参与
配置自动重放，加入真实请求历史及公共诊断；公开 restart 的其他恢复来源与故障
协调仍未完成。

## 依据与契约

固定 `esp_wifi.h` 的 `esp_wifi_set_rssi_threshold` 说明要求：低 RSSI 通知后，
再次调用 setter（允许同值）才能请求下一次通知。当前公开 SDK 没有阈值/armed
getter 或完成 cookie。框架只有显式 setter 调用这一可证明的请求边界，无法
根据 watch 回调、queue drop 或相同阈值推断请求是否仍 armed。

因此 restart 的“配置恢复”不包括一次性 RSSI 请求的自动重新提交。物理重建后
需要通知的调用者显式设置阈值；正常同代 STOP/START 也不提供 armed 保留保证。
已排入观察队列的事件仍可能交付，不声称 STOP/deinit 会同步清空所有 JS 观察。
事件没有 request revision，不能将迟到事件标为新请求完成，也不会据此自动 rearm。
本批没有推断 SDK 内部默认阈值、STOP 的阈值重置值或 RF 触发时序。

## 生产记录和公共状态

新增固定 16-byte `s_rssi_request`，只由 Radio mutation mutex 访问，字段为
generation、revision、requested dBm 和原 SDK error。先通过原输入/owner gate，
再检查非回绕 revision 容量；每次真正 SDK 写入前递增 revision，调用后保存结果。
失败写入也保留记录，因为 SDK 可能已经改变阈值。耗尽返回 ESP_ERR_NO_MEM，
阶段 rssi-threshold-identity，无 mutation，不覆盖上次请求、不清除 RF owner。

记录在物理 deinit 后作为历史保留，revision 生命周期为本次设备启动；它不进入
配置 checkpoint、重建自动 replay 或 JS root。新原生 getter 在同一把 mutex
内复制记录并判断它是否属于当前仍 owned 的 physical generation，没有 SDK 调用。
Driver 模块转换器使用 GC root 创建对象，接入 `wifi.status().radio.rssiThreshold`。

正式 v1 `WiFiRssiThresholdStatus` 包含 revision、identityExhausted、generation、
generationActive、requestedDbm、accepted、espCode、espName。初次写入前 revision
为 0，generation/request/result 为 null，两个状态布尔均 false。accepted 只指
最近 SDK 调用返回 ESP_OK；generationActive 只指物理 generation 仍对应，均不
代表当前阈值、armed、连接或正在运行。完整字段契约见 [driver 文档](../api/wifi-driver.md)。

物理重建后，旧 accepted:true 可以继续作为历史展示，但 generationActive:false。
新的显式 setter 使用新 generation 和下一个 revision 替换该历史。JS runtime
切换本身不作为物理 generation 变化的证据。无新增自动任务、callback cookie、
事件归属、持久化字段或公共方法占位符。

## 验证和剩余范围

C5 immutable Context `build/wireless-contexts/c5` 构建 exit 0。binary
2,784,656 bytes，比前批增加 832 bytes；新记录 16 bytes，原 11 项静态账本保持
不变（Radio 824、restart control 40、STOP 24、inactive history 24 bytes）。
原生请求 getter、状态转换器和控制核心均已链接到最终 ELF。没有实机 heap、
碎片或低 RSSI RF 测量。

共享 connection-control fixture 增加真实请求类型、静态 storage 与 getter。
新增 deferred `test_wifi_rssi_request.py`，编写生产 setter 的同值请求、错误后
历史、输入/owner 拒绝不消耗 revision、generation 变化和耗尽用例；状态边界
独立注入，不等于真实 deinit/init 测试。另连接生产 config checkpoint、pre-/
post-start、replay、commit，计数 SDK RSSI 调用，要求恢复流程不重新提交请求；
物理重建与 START 仍为注入边界，不证明整个 helper/event/driver 生命周期。

VM 用例使用生产状态转换器、原生 getter 与真实 MQuickJS，编写初值 null、接受/
失败、已退出 generation、identityExhausted、逐分配失败/GC/root 回收，以及
读取不写 SDK/不改变请求记录的断言。两份 fixture 本轮仅 AST，未导入、编译或执行。

manifest 49 classes/469 functions、features 27、live SDK schema 35 STA/21 AP、
strict TypeScript、SDK map、MQuickJS 61 sources/53 snippets 与 whitespace 通过。
证据：`build/w07-rssi-request-evidence.json`。

RSSI 的恢复契约与诊断已编码，RF/queue/GC/OOM 运行验收仍 **not-run**。
公开 restart 的未知来源、停机写入、完整故障恢复/清理和绑定未完成；总 Wi-Fi
范围未缩减。Host/Python/VM、C3/S3/disabled 构建和实机/RF 在全部 Wi-Fi API
完成后统一执行，长 soak 留到 BLE API 完成。未刷写、串口操作、擦 workspace、
构建前端、提交、推送或更新根 gitlink。
