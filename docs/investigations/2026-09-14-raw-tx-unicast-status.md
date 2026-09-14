# Raw TX 单播完成失败与地址校验的区分

## 用户报告与已确认事实

用户在已刷入扫描／Raw TX 关联预检调整的 XIAO ESP32-C5 上报告：
Station 未关联，channel 4，driver sequence，26-byte deauthentication 帧。
仅把 Addr1 从广播换成单播时，完成状态从 success 变成 failed。
完整七组输入保存在 native SDK 测试的 `address_matrix.inc`。

失败调用没有 throw，`driverAccepted=true`、`driverCompleted=true`、
`rawStatus=1`、`submitError=null`；完成后 correlation、quarantine、cleanup
均正常。这个观察表明 SDK 提交成功，后续发送完成报告失败。它不证明
“目的地址不在关联表，所以同步校验拒绝”，也不证明帧有没有实际上空。

已核对当前 SDK 的 `wifi_tx_status_t`：0 是 `WIFI_SEND_SUCCESS`，1 是
`WIFI_SEND_FAIL`。这是通用完成结果，不是 ESP 错误码或详细失败原因。
框架按该枚举转换 `driverStatus`，没有把单播地址转换为 failed 的逻辑。

## 本次验证

- 只读查看目标 `hw-10bda3c854e8`，bootId `712f0c74eb2d90b0`。
  查询时未关联，rawTx 无活动操作、隔离、关联故障或待清理项。
- 在主机 QEMU 中执行本地 pinned ESP-IDF C3/C5 的真实
  `esp_wifi_80211_tx` 和 `ieee80211_raw_frame_sanity_check`，应用现有
  management-subtype 补丁，替换 OS／HMAC 边界。
- 每目标 240 组组合：STA/AP、有／无连接、beacon/probe-request/plain
  data/deauthentication、三个地址字段、五类地址值。单播、组播、广播、
  全零与非本机地址均到达发送边界，原始帧字节保持一致。
- 每目标额外执行用户七个原样 26-byte 输入；全部提交成功、到达 HMAC
  边界。包含 Addr1 为单播且 Station 未关联的 deauthentication。
- 三项 SDK 测试通过，0 skipped；覆盖 C3/C5 可执行验证，以及 C3/S3/C5
  补丁的指令范围、哈希边界和已有补丁组合检查。

命令（从 firmware 运行）：

```sh
.venv/bin/python scripts/run_native_tests.py \
  --pattern test_wifi_raw_tx_management_sdk.py \
  --result build/raw-tx-address-investigation/sdk-regression.json
```

## 结论与证据边界

没有复现 SDK 入口按未关联单播地址拒绝的限制，因此没有进一步修改
SDK、删除完成匹配校验或重映射 failed。本次只增加回归覆盖和状态说明。

SDK native 测试截止于注入的 HMAC 边界，不模拟真实 RF、ACK、重试或
异步发送调度。广播不需要单播式 ACK；未收到 ACK 是可能原因，但本次
并未证明它就是根因。后续若要区分实际发射、重试及接收响应，应使用
受控测试网络中的独立接收证据，不能由 `rawStatus=1` 单独推断。

没有进行新的设备发送、国家码修改、刷机或 workspace 写入。

SDK 官方参考：
[Wi-Fi Vendor Features](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c5/api-guides/wifi-driver/wifi-vendor-features.html)，
[wifi_tx_status_t](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c5/api-reference/network/esp_wifi.html#_CPPv416wifi_tx_status_t)。
