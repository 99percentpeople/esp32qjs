# W-07 停用接口的阈值历史与恢复准入

firmware `d7db8d1` 工作区增量，SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`。承接
[活跃接口阈值恢复](2026-09-09-w07-restart-inactive.md)。本批保存 AP 停用前的
阈值历史，并在目标重新启用接口时恢复；目标仍停用该接口时的延迟恢复、未知
来源和公开 restart 仍未完成。

后续 [停用接口延迟恢复](2026-09-09-w07-inactive-deferred.md)已接入，解除本批
记录的目标停用拒绝；下文保留历史增量和验证范围。

## 同代历史记录

新增一个 Radio mutation mutex 保护的有界原生记录：physical generation、两个
uint16 秒数、known/failed mask 和原始错误。没有 JS root、callback pointer、
动态分配或独立 revision 空间。它保存最近成功观察，不能冒充 disabled 接口的
当前 SDK getter。

公开 inactive setter 成功读回时记入新值；失败后 rollback 读回成功记入原值。
前值读取失败或 rollback 失败时清除对应已知值，保留 unavailable 位和原始错误。
不能继续把旧历史当作已确认值。其他接口的成功观察不会清除这个失败位。

原生全局 STOP 的阈值观察和运行来源的 restart 捕获更新同一记录。AP 部分关闭
在确认 APSTA 后、第一次 set_mode(STA) 之前读取 AP 阈值；失败不阻止关闭，只
留下不可用历史。已提交关闭的事件/读回后缀不重复抓取已停用 AP 的输出。原有
精确 token/lease 准入、event fence、失败后缀与 Station 保留路径不变。此增量
位于 FEATURE_WIFI gate 内，未向仅共享 Radio 的禁用构建加入阈值调用。

只有成功物理 deinit 才清除历史。记录 generation 不匹配时，新的实际观察重建
本代记录，不能继承上一代值。普通全局 STOP 与部分 AP STOP 不清除同代历史。

## 进入冻结快照和重放

原启用 mask 保留为捕获核对范围，新增 saved mask 记录真正要恢复的值。在所有
活跃读取核对后，把同代已知的隐藏值合入原快照的两个秒数字段，不另建动态池。
隐藏项存在 unavailable 记录时，以 `restart-hidden-inactive-history` 和原始
错误拒绝；不能自动改用默认值。

目标包含这些接口时，原有 START 后 RAM write/readback 和最终 acceptance 按
saved mask 恢复全部值。例如 APSTA 停用 AP 后，内部 STA→APSTA 重建可恢复
旧 AP 阈值；新的实际读回更新新 generation 的历史。运行来源的原始捕获核对
仍只调用当时启用接口的 getter，不为历史 AP 调用非法的停用 getter。

目标没有启用某个保存项时，在原 driver 任何重建写入前以
`restart-hidden-inactive-admission` 拒绝并安全擦除局部凭据快照。这样不会等到
deinit 才发现无法恢复隐藏值。此限制需要后续延迟恢复机制解除；它不是完整
同模式 restart 已支持的声明。未曾观察过的隐藏来源也未获得完整恢复证明。

## RSSI 调查边界

保存固定 C5 SDK 两个实际函数的反汇编到 `build/w07-rssi-sdk/`：公开
esp_wifi_set_rssi_threshold 分配 ioctl 请求并交给 ieee80211_ioctl；其 handler
wifi_set_rssi_threshold 把请求值写入 g_ic。公开 SDK 没有 armed getter 或完成
cookie；setter 接受不能证明旧低信号通知是否已经消费。该静态证据没有覆盖通知
producer、全局初始化/STOP 清零和跨 event queue 的竞争，因此本批没有新增
猜测性 armed ledger 或自动 rearm，也不宣称 RSSI 重启恢复完成。

## 验证

新增 deferred `test_wifi_inactive_history.py` 调用生产 lease/token 验证、AP
quiesce、历史记录、捕获和重放，编写精确身份拒绝、AP 观察失败仍能关闭、提交后
重试不重读、隐藏历史合入、重建到 APSTA 恢复、保持 AP 停用的提前拒绝、失败
历史与 stale generation 场景。SDK、event fence、物理 rebuild/START 和 owner
退休 storage 是注入边界，不证明完整原生生命周期或 RF。共享 connection
fixture 补记录成功/回滚/不可用用例；config fixture 的物理 deinit 边界清除历史。
本批四份 fixture 仅 AST，未导入、编译或执行。

C5 immutable Context `build/wireless-contexts/c5` 构建 exit 0；manifest
49 classes/469 functions、features 27、live SDK schema 35 STA/21 AP、MQuickJS
61 sources/53 snippets、strict TypeScript、SDK map 与 whitespace 通过。构建
日志、binary/static/DWARF 账本与 source/SDK hash 见
`build/w07-inactive-history-evidence.json`。

Host/Python/VM/竞争、C3/S3/disabled 构建、完整 restart、NVS、实机/RF 均
**not-run**。Wi-Fi API 完成后统一阶段和实机功能验证，长 soak 留到 BLE API
完成。未刷写、串口操作、擦 workspace、构建前端、提交、推送或更新根 gitlink。
