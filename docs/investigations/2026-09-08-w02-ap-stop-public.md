# W-02：公开 stopAP 的 APSTA 关闭与失败接管

基线 firmware `d7db8d1`，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
接续[原生核心](2026-09-08-w02-ap-stop-native.md)，本批把生产 helper 接入
无参数 `wifi.stopAP()`。保留唯一 v1；没有新 namespace 或占位 API。

## 已编码行为

- APSTA：准入检查允许已建立的 Station，仍拒绝 Future、扫描/连接/断连排空、
  native operation、wake、promiscuous 与其他 feature owner。准入失败不设置
  AP cleanupPending、不释放 lease，也不把健康 AP 误转入独占 shutdown。
- 准入后，中央 `s_wifi_lifecycle` 持有 token，AP helper 只借用精确 identity /
  generation。APSTA→STA、AP_STOP/marker、读回沿用上一批原生后缀状态。
- 只在原生 AP 停止且 Station 仍 live 后退休 AP netif：复用 event-task detach
  和 IP fence；完成后仅释放 AP lease，保留 Station/Application identity、
  Station helper/IP/队列/timer，缩减 Application 的 mode requirement。
- 模式、事件、读回、netif 退休、finish 任一步失败都保留未完成义务；再次
  stopAP 不重复已接受的模式切换，也不提前清除 callback storage。已关闭 AP
  的重复 stopAP 不影响剩余 Station。独占 AP 仍沿用完整关闭路径。
- 固定 SDK 的 netif clear 只断开对应接口，所有 Wi-Fi netif 都为空后才注销
  共用 default handlers。此处复用该机制；源码与编译不是实机连续连接证明。

## 部分关闭失败后整体清理

`wifi.stop()` 在 Station 已断开、所有原生操作及 Future 已排空后，可以把同一
partial token 转为中央配置清理。adopt 只清除原生 AP-only 意图，保留 token、
故障、driver 状态；随后释放 AP，再由现有中央后缀释放其他 owner，完整停止、
退休 AP/STA helper、shutdown。该失败接管不是健康 APSTA 的 stop-only 路径，
也不声称恢复原连接。仍连接或排空中的 Station 会在任何接管前被拒绝。

runtime teardown 先分离 JS consumer，再取消/排空 Station 原生操作；等待
DISCONNECTED 和 IP fence 完成后才执行同 token 接管。缺少终态或清理错误保留
资源，阻止新 runtime 附着。不能通过重启 runtime 冒充设备重启。

永久 netif detach 错误沿用已有隔离：SDK 可能已释放内部 driver，不能盲目重试
该调用；错误详情 restartRequired 保持 true，netif 指针保留至设备重启。
公开错误仍为 WIFI_AP_FAILED / WIFI_STOP_FAILED。Radio 的 ap-stop-*、
lifecycleActive 与 AP 错误详情描述原生后缀；runtime 接管准入失败新增
WiFiStatus.cleanupStage = ap-stop-admission。

## 验证记录与范围

新增 `test_wifi_ap_stop.py` 从生产文件提取公开 stopAP、AP partial helper、
中央 coordinator、完整失败清理和 stop helper，仅注入 Radio/netif 边界。覆盖
成功、各后缀失败重试、忙/foreign 准入、旧 token、永久 detach 错误、整体 stop
在 Station 排空前拒绝以及同 token 接管。`test_wifi_public_lifecycle.py` 新增
生产 runtime teardown 等待断连/IP fence 后接管；原生 Radio 用例补充 adopt
精确 token、保留故障/owner 和不调用 driver 的断言。独占 AP 与旧 coordinator
fixture 同步新调用边界。以上测试源码已增加，**均未执行**。

必要 C5 immutable Build Context 编译、MQuickJS 语法、manifest/features/raw
schema、recorded SDK map、Python AST 与 whitespace 检查记录于
`build/w02-ap-stop-public-evidence.json`。C3/S3/disabled 矩阵、Host C/Python、
实际 AP_STOP/默认 netif 顺序、Station 连续性、RF 与实机功能均 not-run。
长时间 soak 仍放到 BLE API 完成后。不提高 feature 稳定等级。

尚缺共享 AP 重开、完整 status/client IP、start/stop options 与其余 Wi-Fi 模块，
见[完整剩余清单](2026-09-08-wifi-api-remaining.md)。未刷写、串口操作、擦除
workspace、构建前端、提交、推送或更新父仓库 gitlink。
