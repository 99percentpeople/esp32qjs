# W-02：扫描过滤、停留时间与结果上限

承接[客户端/MAC 查询](2026-09-08-w02-client-queries.md)，firmware HEAD 仍为
`d7db8d1`，ESP-IDF 为 `fff9895c82d744c7237be8847347bdd1b07c6643`。本增量
继续实施 Wi-Fi API；按用户安排，Wi-Fi API 完成后集中阶段测试，再开始 BLE。

## 当前 API

wifi.scan 新增以下正式 v1 options，保留现有 channel/showHidden/passive/
dwellMs/timeoutMs 契约；没有增加别名或提前发布目标设计的占位接口。

| 字段 | 实际规则 | 原生映射 |
| --- | --- | --- |
| ssid | 可选，1–32 UTF-8 bytes，无 NUL；省略不按 SSID 过滤 | wifi_scan_config_t.ssid |
| bssid | 可选，严格六组两位 hex + 冒号，非零单播地址 | wifi_scan_config_t.bssid |
| homeChannelDwellMs | 整数 30–150，默认 30 ms | home_chan_dwell_time |
| coexistenceBackgroundScan | boolean，默认 false | coex_background_scan |
| maxRecords | 整数 1–32，默认 32 | get_ap_records 的请求容量与 JS 结果上限 |

SSID/BSSID 可以同时指定。未知字段、错误类型、NUL、越界或非整数值在 driver
操作前拒绝。BSSID 共用解析器现按完整 17 字节解析，connect 的 BSSID 同样拒绝
单位 hex 段、空白与 NUL 后缀；不引入仅为保留宽松旧输入而存在的兼容分支。

SDK 依据为本地 esp_wifi.h 的 scan_start/set_scan_parameters 文档及
esp_wifi_types_generic.h 的 wifi_scan_config_t。coex_background_scan 仅透传 SDK
行为请求，不能证明 RF 共存，也不放宽 Radio 对 fixed-channel owner 的扫描拒绝。
maxRecords 不限制 SDK 内部扫描内存、发现 AP 的数量或扫描时长。框架仍至多
分配 32 个 wifi_ap_record_t，较小上限减少结果转换分配。

## 原生存储与清理

Future capture 使用自己拥有的 33-byte SSID 与 6-byte BSSID 数组，不保留 JS
字符串地址。开始扫描时，在原有 Radio operation reservation 内检查扫描槽准入，
再复制到 s_wifi_state 的独立固定数组并重绑 SDK config 指针。先完成复制，再
发布 scan_in_progress、调用 SDK；不会让 SDK 引用 Future-owned storage。

新增原生过滤数组共 39 bytes（结构对齐另计）；Future state 增加对应过滤数组
及 uint16_t 上限。没有扩大 CSI/ESP-NOW pool 或撤回原 SRAM 优化，也不声称此次
扩展零内存成本。实测预热静止状态内存比较仍待集中硬件验证。

Future 超时/取消/销毁后，现有 scan_draining 隔离继续阻止复用。原生完成及
AP-list cleanup 成功后清空 config 和过滤副本；stop/list cleanup 失败仍保留它们。
SDK scan_start 返回失败的路径按原有提交失败契约清空未接纳的 native config。
runtime teardown/下一次 attach 共用同一原生结束屏障，不用 runtime 重启假装完成。

结果上限随 Future 传入生产结果转换 helper。SDK get_ap_records 成功后即记账
列表已释放；随后 JS 转换失败不会重复清理已完成的后缀。空列表与转换前 OOM
继续由现有 cancel/drain 路径清理。现有测试 fixture 的 native 字段与 helper
签名同步更新，但本轮没有执行测试或引用旧通过数证明新路径。

## 本轮编译与一致性检查

代表 C5 immutable Build Context 编译通过（退出码 0，日志
`build/w02-scan-options-c5-build.txt`）；未刷写。API manifest 43 classes /
390 functions、feature 文档 27 项与 recorded SDK map/actual manifest 结构
一致性检查通过，diff whitespace 检查通过。没有重新采集全目标 SDK inventory，
没有执行 Host C/Python、硬件或完整构建矩阵；前批的历史测试记录不覆盖本增量。
源码与 C5 产物 hash 见 `build/w02-scan-options-evidence.json`。

## 待执行的阶段验证

- 生产 capture：SSID 边界/UTF-8/NUL、严格 BSSID、布尔值、整数范围与未知字段。
- capture 后修改 JS options、Future 结束/销毁、原生迟到完成及 runtime teardown
  下的过滤内容与指针 identity；两个扫描不能共用未退休的存储。
- native start/stop/list-clear 错误，重复取消、队列饱和、原生完成与关闭交错。
- maxRecords 为 1/32、空结果、超过上限的 SDK 列表及真实移动 GC/第 N 次分配失败。
- connect 共用严格 BSSID 解析后的输入回归；C3/S3/C5、feature-disabled 构建矩阵。
- 真实 RF 过滤、后台扫描与家庭信道停留、内存账本及长生命周期。

以上均 not-run。channels bitmap、独立 activeMin/activeMax timing、扩展结果 metadata、
APSTA/configure/watch 与其他 Wi-Fi 新能力仍继续实施；deauthClient 的 AID 复用
缺口沿用前批记录。本轮不操作串口、不刷写、不提交、不更改父仓库 gitlink。

## 后续 timing 增量

后续已用正式 v1 mode/activeMinMs/activeMaxMs/passiveMs 替换 passive/dwellMs，
并支持 channel:"all"。本文件前文及编译证据仅代表过滤参数增量时的快照；
当前契约与剩余工作见[最新清单](2026-09-08-wifi-api-remaining.md)。
