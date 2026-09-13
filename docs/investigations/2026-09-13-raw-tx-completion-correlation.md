# Raw TX completion-correlation 排查

**后续已定位并修复 TX cache descriptor 身份转移遗漏。** 用户随后提供了
两组精确帧字节，真实 C5 SDK 的缓存路径在修复前失败、修复后通过；
最终结论与证据见 [TX cache 修复记录](2026-09-13-raw-tx-cache-fix.md)。
以下保留前两轮排查过程，其中“尚缺复现代码”和“根因未确认”描述当时状态。

## 状态与边界

用户报告 `WIFI_RAW_TX_SEND_FAILED`、espCode 259、stage
`completion-correlation`、validationCode 0；rawTx 隔离、清理停在
`native-completion`，lane/client/operation 保留。显式 recover 有效，
radioGeneration 由 1 到 2 到 3。

本次检查 firmware HEAD `a57bafb`。用户随后补充板型为
`seeed-xiao-esp32c5`，hardwareId `hw-10bda3c854e8`，8 MB Flash / 8 MB
Quad PSRAM，芯片 revision 1.0，IDF v6.1，framework 0.1.0，MQuickJS
2025-12-22，bootId `dc910fd436e3bdf6`。这些是用户提供的信息，未进行
现场 ROM 身份验证。实际 Artifact/commit、最小发送代码和原始帧仍缺失；
版本字符串不能证明该 HEAD 与设备映像一致。

补充状态确认 `submitError`、`registrationError` 均为 null，radio started /
station，liveSessions 0，与 one-shot 完成关联路径一致。没有连接串口、
执行设备 JS、恢复设备或刷写。第一轮只新增故障测试；第二轮已补充运行时
诊断、类型与文档，但没有改变完成准入、隔离、retire 或 recovery 条件。
设备触发根因仍未确认。

## 已确认的源代码因果链

- `wifi_raw_tx/esp32_mquickjs_wifi_raw_tx.c:raw_tx_poll` 在读取到 broker
  的 `correlation_fault` 后主动设置 259 和 `completion-correlation`。
  此 stage 不能解读为 SDK 的发送提交返回了 259；validationCode 0
  也不代表 RF 发送成功。
- `wifi_raw_tx/esp32_mquickjs_wifi_raw_tx_broker.c:raw_tx_complete` 用
  descriptor 绑定记录，再校验回调 interface、可用的源/目的 MAC。
  无效 metadata 或失配导致完成不被接受；精确回调路径随后解除 descriptor
  绑定并设置 correlation fault、quarantine 和 registration uncertainty。
  未绑定的 descriptor 也会设置 registration uncertainty；该状态会把其他
  尚未完成的 active records 一并隔离。因此故障可能来自同代的其他回调。
- `broker_retire` 仅在 native termination 成立，或者 submit 已返回、完成
  已接受且无 correlation fault 时允许回收。`raw_tx_cleanup_worker`
  在不能 retire 时报告 259 / `native-completion`；token 未退休前不释放
  radio lease 和 lane。这解释了用户报告的资源保留，不证明另有 lane 泄漏。
- Radio recovery 的物理终止路径调用 `broker_reset_after_deinit`，推进
  generation，再允许旧 token 收尾。用户报告的 recover 效果与此一致，
  不足以判断最初发生了哪种关联异常。

## 诊断与现有验证缺口

修改前 broker 已记录 `invalid_callbacks`、`mismatched_callbacks`、
`orphan_callbacks`、`duplicate_callbacks`，但当前公开
`status.radio.rawTx` 未导出这些计数，也没有具体失配原因/当时 metadata
快照。用户提供的旧映像公开字段不能区分这些触发路径。

检查本地 pinned C5 SDK 反汇编：`ieee80211_freedom_inside_cb` 先调用
metadata getter，再调用公开 callback；生产 hook 在 getter 返回之后
采集 snapshot。本地调用顺序不支持“getter 尚未填完 metadata”的猜测。
这不是设备上 metadata 内容正确的证明。

原 SDK identity 测试执行了 C3/C5 真实 allocation/completion 调用点，
但 completion hook 用清零 metadata 的 stub；broker 测试则注入准备好的
metadata。这两类测试通过，不能证明 HMAC/RF 之后实际 metadata 与原始帧
一致，也不能代替设备上的回调顺序与 descriptor 生命周期验证。

官方 callback 契约要求 metadata 只在回调期间使用：
[ESP-IDF Wi-Fi API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_wifi.html)。

## 本次验证

使用 `scripts/run_native_tests.py`，结果保存在本地忽略目录
`build/raw-tx-correlation-investigation/`：

- `baseline.json`：现有 broker、Future、recovery、SDK identity 共 5 个
  用例通过，0 skipped。
- `faults.json`：新增生产 broker/hook 故障用例通过，分别使用 C3、S3、C5
  类型定义注入无效 interface、错误 interface、目的 MAC 失配、源 MAC
  失配、未知 descriptor 五种情况。均验证提交 ESP_OK、validation VALID、
  correlation/quarantine、准确的内部计数、拒绝提前释放，以及 deinit
  后回收。三种类型配置均在 host 执行，不是三块板的硬件验证。
- 合计 6 个原生用例通过。新增测试复现的是隔离状态转换，不是用户设备
  实际触发条件；未把注入故障宣称为硬件根因。`git diff --check` 通过。

## 下一步所需证据

仍需取得固件/Artifact 标识、发送参数和最小帧字节，以及发送前后完整
status 与错误对象。诊断构建已导出上述计数与首个故障的 identity、generation
和具体原因；记录应在 recovery 前采集。若仍无法区分原因，再有针对性地
核对期望/实际 interface/MAC 与 SDK descriptor 生命周期。
只有据此确认具体 SDK metadata 或 descriptor 问题后，才选择修复路径。
当前没有证据支持删除 MAC 校验、自动 recover 或直接清零 lane。

## 第二轮：诊断缺口修复与 C5 构建

`wifi.status().radio.rawTx` 新增：

- `correlationFailureReason`：首次异常原因，区分无效信息/接口、接口/源 MAC/
  目的 MAC 失配、孤立/重复回调、完成与提交拒绝矛盾、descriptor 重用、绑定
  异常和回调计数溢出。未发生时为 null。
- `correlationFailureIdentity`、`correlationFailureGeneration`：首次异常来源；
  无法归属到发送时 identity 为 null，不借用当前第一条记录冒充来源。
- `invalidCallbacks`、`mismatchedCallbacks`、`orphanCallbacks`、
  `duplicateCallbacks`：boot 累计饱和计数，跨 recovery 保留。

原因在锁内记录静态字符串和整数，不分配内存、不保存 SDK/JS 临时指针。
后续异常不覆盖第一条；deinit/retire 后仍可读取，到下一次新注册时清空。
旧设备需要运行含此改动的映像才有这些字段；这不是原发送故障已修复的声明。

- `diagnostics-before.json`：真实 MQuickJS status 转换测试先失败，证实旧
  converter 缺少 callback counters。
- `diagnostics-native.json`：Raw TX 专项 39/39 通过，0 skipped，含真实 VM
  分配失败与移动 GC、SDK hook、原生 cleanup/recovery。
- `diagnostics-faults-final.json`：追加 descriptor 重用来源归属及
  callback-before-rejected-return 后，定向故障用例再次通过；不重复计入
  39 个用例。覆盖首错保留、下一次注册清空、计数跨 recovery 保留。
- API manifest 检查通过：62 classes / 663 functions。`git diff --check`
  通过。未新增 callable API 或格式版本。
- C5 使用现有合法 XIAO C5 Build Context 增量构建：
  `ninja -C build/esp32c5/web-e7ae16508cc3 -j 4`，退出 0。
  `c5-build.log` 保存完整输出。app 0x2feb60（3,140,448）bytes，现有
  3 MiB app 分区剩余 0x14a0（5,280）bytes，大小检查通过；没有调整分区。
- 本轮未执行 C3/S3 目标构建、设备 JS 或 RF 验证；未 flash、未提交。
