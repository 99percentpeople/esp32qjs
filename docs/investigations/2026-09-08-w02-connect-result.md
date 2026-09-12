# W-02：连接结果与关联状态

firmware 基线 `d7db8d1`，本批是未提交工作区增量；SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`。仅实现连接结果及状态补充，
configure/APSTA、完整 lifecycle options 与 driver 模块仍在剩余清单。

## 实际代码路径

旧 Future success 调用 `wifi_make_status_object()`，读取交付时的可变全局状态；
源码可确认它没有返回本次关联 BSSID/AID/耗时，也未保留操作独立的关联信息。
本批未执行竞争复现；不能把源码证据写成测试失败或修复验收通过。

- helper 注册 `WIFI_EVENT_STA_CONNECTED` 并在原生锁内复制固定长度
  SSID、BSSID、信道和 AID；不保留 SDK payload 指针。内部 watch control mask
  保证关联状态先更新再发布观察事件，不重复发布。
- GOT_IP 接受本次成功时复制关联快照，原生完成队列携带 generation、timestamp
  和值类型快照。发布在锁内检查当前注册 generation，旧完成不能覆盖新 Future。
  Future 消费匹配 generation 后持有独立副本；后续 disconnect 清空当前状态，
  不修改已经接受的结果。
- `WiFiConnectResult` 包含 connected:true、ssid、bssid、channel、aid、elapsedMs、
  rssi、negotiatedPhy。elapsedMs 从初始 Radio 已启动后的 native connect dispatch
  到终态发布，含旧连接 drain，不含 lane wait/JS 交付等待，不改变原生 timeout。
  缺少/非法关联身份返回 WIFI_CONNECT_FAILED/ESP_ERR_INVALID_RESPONSE。
- RSSI/PHY 是交付时可选读数：Radio task mutex、精确 STA lease、started/mode/
  cleanup/lifecycle 准入后调用 SDK；查询前后 get_ap_info 均需匹配 BSSID/channel。
  失败或变化返回 null，未知 PHY 返回 null。多 getter 非原子，同 AP 的重关联不能
  由 BSSID/channel 排除；这些补充读数不是终态 RF 快照或操作 identity。
- 固定 SDK 有 `esp_wifi_sta_get_rssi` 与 `esp_wifi_sta_get_negotiated_phymode`。
  前者是最后 beacon RSSI；后者提供实际协商模式，不能用 AP 的支持位推导。
  AID 来自关联事件，不采用之后的查询作为 identity。
- `wifi.status()` 增加 associated 与可空关联字段，connected 仍为 IP-ready；
  channel 为关联事件信道，当前 Radio 信道仍读 status.radio.channel。
  状态读取不初始化 driver。SSID 等结果无凭据；所有 JS 对象经 GC roots 写属性。
- 新 handler 沿用失败清理 suffix 与 callback barrier；connected-unregister 失败
  保留原生存储供重试。没有新增独立 pool/长期队列或公开占位 API。

原生连接已经成功但结果转换失败（包括 OOM）时不隐式断开；应用应查 status/net
确认副作用。Sole v1 connect 返回替换为 WiFiConnectResult，disconnect/start/stop
仍返回 WiFiStatus，不保留旧结果格式别名。

## 验证账本

本批只执行 C5 immutable Build Context 编译、MQuickJS 语法、生成 manifest/
feature 文档与 recorded SDK map 一致性、diff whitespace 检查；结果与 hash 记录
在 `build/w02-connect-result-evidence.json`。这不是完整矩阵或运行测试。
首轮编译暴露 Radio 字段名误用，已改为实际 effective_mode 后重新编译。

阶段测试待运行：

| 项目 | 状态 |
| --- | --- |
| 真实连接结果/断开后结果不变（已有 network.js 增补断言） | not-run |
| GOT_IP 后 Future 消费前断连、超时与迟到事件竞争 | not-run |
| 同 generation 首终态保护与新 generation 旧事件隔离 | not-run |
| watch 饱和、迟交付、runtime teardown、handler unregister 失败后重试 | not-run |
| RSSI/PHY 查询失败、AP 切换、未知 enum、同 AP 重关联限制 | not-run |
| 缺少/非法关联 payload，逐次分配失败、movable GC roots | not-run |
| Host C/Python 全套、三目标/feature-disabled 编译矩阵 | not-run |
| 设备连接与功能回归 | not-run，Wi-Fi API 完成后 |
| 长时间 soak | BLE API 完成后 |

无实机、串口、刷写、提交、推送或父仓库 gitlink 更新。
