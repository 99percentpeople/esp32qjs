# W-02：保留 Station 的 AP 关闭原生边界

基线 firmware `d7db8d1`，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
上一批公开 configure/APSTA 已注册，本批新增其 AP-only 关闭的 Radio 核心。
本页记录原生核心批次的历史状态；后续[公开 stopAP 接入记录](2026-09-08-w02-ap-stop-public.md)已完成 AP helper、netif 退休和 runtime 交接，运行验证仍待执行。

## 已编码的原生顺序

1. begin_ap_stop 只接纳健康运行的 APSTA，要求实际 Station/AP lease 及可选
   Application lease 的精确 identity/role。Station/AP 的原生 required mode 也须
   分别为 STA/AP；仍拒绝 operation、wake、promiscuous 和其他 owner。准入本身
   不调用 driver、不释放 lease，也不隐式断连。
2. quiesce_ap_lifecycle 验证精确 token 和仍存活的三 owner，读取旧 mode 必须为
   APSTA；先登记专用 AP_STOP 事件阶段，再调用 esp_wifi_set_mode(STA)。没有
   esp_wifi_stop/deinit/start，也不创建另一组 Station/Application owner。
3. 等待 AP_STOP 后要求 event_live 仅剩 STA，再等待默认事件队列 marker。该
   阶段使用原有 STOP 的 1000 ms 有界 native wait，没有 JS poller。原有完整
   STOP 仍要求所有接口都停止；两种条件不混用。
4. 再读回 native mode 必须为 STA，进入 QUIESCED。新的 exact-token 检查只在
   该状态及 Station 仍 live 时允许 caller 退休 AP netif。当前尚未把这个检查
   接入真实 AP netif adapter；caller 必须先完成退休才可调用 finish_ap_stop。
5. finish 只释放 AP，保留 Station/Application 的 identity，把 Application 原生
   APSTA requirement 缩减为 STA，避免下一次 ensure_started 又启用 AP。然后
   清除此操作的专属故障和 token；started/storage/Station helper/IP 均不重建。

所有 driver 调用在 Radio operation mutex 下，但不放在临界区；回调仅使用原有
短临界区，不等 runtime mutex。新增状态是有界 boot metadata，没有新 pool 或
预算器。事件 identity/revision 仍不回绕复用。最终 Station 的无线连接是否保持
必须由 SDK/实机阶段证明，源代码没有调用全局停机不是 RF 连接证明。

## 失败与重试

原生状态细分为 READY、ATTEMPTED、SUBMITTED、EVENTS_DONE、QUIESCED，避免
最后一次 get_mode 失败后重新等待已经消费完的 AP_STOP/marker。

- 模式调用已返回成功：只重试事件/读回后缀，不重复 set_mode。
- setter 返回错误：记录原始错误，后续只读 mode；若已为 STA，可继续等待原生
  终止；若仍 APSTA，不重放不确定的 mutation，保留 token/三 owner。需要所有
  owner 显式退出之后走完整 quiesce/finish_lifecycle 清理。
- 事件迟到、队列满、readback 失败或 Station 出现 STOP：保留 owner/netif 义务，
  不发布已关闭结果。Station 不再 live 时不能以 AP_STOP 单独满足完成条件。
- 完整 STOP 成功后也清除 partial metadata，但仍沿用原有故障/清理策略，不把
  未经 deinit 的旧原生故障宣称恢复。

诊断增加 eventPhase=ap-stop 和 ap-stop-state/mode-snapshot/mode/events/
mode-readback 的 fault/cleanup stage，正式 status 类型同步。没有增加新公开
namespace、force 或未实现的占位 API。

## 验证与下一步

Host C 新增 partial-ap-stop 用例，直接编译生产 Radio，只替换 SDK/RTOS 边界。
覆盖精确角色/required-mode、旧 token、全程保留 lease、无全局 stop/start/deinit、
延迟 AP_STOP、队列拒绝、setter 部分成功后报错、setter 未改变 mode、snapshot/
事件后 readback 失败、Station 意外停止、原始错误及完整显式清理。**未执行**。
SDK set_mode 替身按新增/移除接口发事件，不能作为真实 ESP-IDF 行为的证据。

必要 C5 immutable context 编译和 manifest/features/raw schema/live SDK/
recorded map/MQuickJS 语法/whitespace 检查记录于
`build/w02-ap-stop-native-evidence.json`。新函数当前没有 JS caller，链接器可裁剪；
object 编译证明类型一致，不能写成公开 stopAP 已支持 APSTA。Host/Python、C3/S3/
disabled 矩阵、真实模式切换与 AP netif 清理的竞争和实机功能全部 not-run；长时间
soak 仍放到 BLE API 也完成之后。未刷写、操作串口、擦除 workspace、构建前端、
提交、推送或更新父仓库 gitlink。

下一步必须把该 token 和 AP netif 退休接入 AP helper：失败期间保存 partial 意图，
并让 public stop 与 runtime teardown 处理同一未完成后缀；不能误转入独占 AP 的
完整 shutdown，也不能在 Station 尚存活时清除其默认 netif/事件 handler。固定
SDK wifi_default.c 的 clear_default_wifi_driver_and_handlers 按 netif 断开对应
对象，只有全部 Wi-Fi netif 指针都清除才注销共享 default handlers；仍须以实际
helper、事件和硬件测试验证调用顺序。完整剩余范围见[当前清单](2026-09-08-wifi-api-remaining.md)。
