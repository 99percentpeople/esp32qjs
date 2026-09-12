# W-02：扫描信道列表与结果字段

本轮开始持续目标“完成剩下的 Wi-Fi 功能，最后完成实机测试；长时间 soak 放到
BLE API 也完成后”。目标仍包含 02 全部剩余工作，不以本批扫描完成代替全部
Wi-Fi 完成。firmware HEAD `d7db8d1`，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
前一轮非 goal turn 已产生代码与编译证据；本 goal turn 继续产生实际实现增量。

## 信道列表

正式 scan options 新增 channels: { ghz2?: number[], ghz5?: number[] }。
至少一个非空数组；2.4 GHz 至多 14 个信道（1–14），5 GHz 至多 28 个标准 SDK
信道（36–177 中有定义的离散信道）。两频段内均拒绝重复、非法数字与空数组；
没有 5 GHz 的 target 在参数阶段拒绝 ghz5。JS 数组与读取值都有 GC roots。
显式数字 channel 与 channels 互斥；channel:"all" 可配合列表。

未指定的频段编码为 SDK 的 bit0 bypass，不会因漏填另一频段而扩大为该频段全扫。
用户只提供信道数字，不能直接传 reserved bits 或绕过输入校验的 raw bitmap。
5 GHz 信道到 bit 的转换与 Radio 原有监管验证共用同一个纯函数。

获得精确原生 SCAN operation token 后，在 Radio mutex 内校验 token、状态、国家
快照与当前 band mode；不在 Wi-Fi state critical section 中调用 SDK。2.4 GHz
按 schan/nchan 检查；5 GHz 检查手动非零 mask。非法或当前频段/国家不允许返回
ESP_ERR_INVALID_ARG / ESP_ERR_NOT_ALLOWED；校验失败释放本次 operation，无
esp_wifi_scan_start 副作用。all-channel 也校验精确 operation，但不擅自构造选频表。

### 仍未解决的公开 SDK 信息缺口

固定 SDK esp_wifi_types_generic.h 规定 wifi_country_t.wifi_5g_channel_mask 为 0
时采用隐式本地法规表，mask 仅在 manual policy 生效；esp_wifi.h 的公开 country
getter 不提供该隐式表。没有把 0 当成全部允许，也没有硬编码另一套国家法规。
因此显式 5 GHz 选择在 auto 或 mask=0 时返回 ESP_ERR_NOT_SUPPORTED；无列表的
all-channel 扫描继续由 SDK 选取合法信道。此项属于严格显式选择的信息缺口，
不是芯片不支持 5 GHz，仍需后续解决；不能据此宣称 W-02 全部完成。

检查基于 admission 时的快照。无线端驱动的国家变化不受框架 mutex 阻止，实际
RF 法规仍由 SDK 执行。当前公开接口不暴露任意国家 raw mask 写入作为绕过手段。

## 扫描结果

正式返回类型为 WiFiScanRecord，替换开发期 WiFiScanResult 名称，无类型别名。
保留 ssid/bssid/rssi/channel/authMode/hidden，增加：

| 字段 | 来源与未知值 |
| --- | --- |
| secondaryChannel | SDK second：none/above/below；未知 null |
| band | 标准信道数字映射 2.4GHz/5GHz；未知 null |
| pairwiseCipher/groupCipher | SDK cipher enum 的正式 WiFiCipher 映射；未知 null |
| antenna | SDK ant：0/1；未知 null |
| protocols | SDK 11b/11g/11n/11a/11ac/11ax/lr flags；不推断 negotiated rate |
| country | 有有效代码时返回 WiFiCountryStatus，否则 null；是 AP record，不是本地有效国家 |
| capabilities | SDK wps/ftmResponder/ftmInitiator/he/vht flags；false 表示此次扫描未报告 |

WiFiCountryStatus 字段为 code（两字节）、environment（indoor/outdoor/null）、
policy（auto/manual/null）、startChannel、channelCount、maxTxPowerDbm、
ghz5ChannelMask（没有目标字段则 null）。maxTxPowerDbm 根据 SDK max_tx_power
单位返回，不与 0.25 dBm 的 set_max_tx_power 输入混用。mask=0 不代表无限制。

SSID 转换的原生读取范围明确封顶为 32 bytes。所有对象、数组、中间值都保留
GC roots；数组项构造后才附加到结果。SDK list 提取成功即按既有路径记账释放，
新增字段的 JS OOM 不重做已完成 SDK cleanup 后缀；原有结果数量预算不扩大。

## 验证账本

代表 C5 immutable Build Context 编译通过（退出码 0，日志
build/w02-scan-complete-c5-build.txt）。仅做编译/生成物一致性检查，不以旧 Host
通过数证明本批功能。现有 Host fixture 已同步 native 字段、helper 依赖与
WiFiScanRecord 名称；尚未执行。

集中阶段需验证生产解析、空/重复/越界信道、非 5 GHz target、未指定频段 bypass、
country/band mode/error/旧 operation、拒绝后的 owner 归还、异步 country 变化、
32-byte 非终止 SSID、全部 cipher/PHY/未知值、country raw bytes 与真实移动 GC/
第 N 次分配失败。C3/S3/C5/feature-disabled 矩阵、真实 RF 与实机生命周期仍待执行。
长时间 soak 继续排除在本次 Wi-Fi 阶段验收之外，延至 BLE API 完成后。

未刷写、串口操作、提交、推送或更新父仓库 gitlink。继续按[剩余清单](2026-09-08-wifi-api-remaining.md)
推进常用控制与事件/配置及后续模块；本批没有修改 goal 完成状态。

生成物检查：manifest 43 classes / 390 functions、feature 文档 27 项、recorded
SDK map 与 actual manifest 一致性均通过。源码和 C5 产物 hash 保存在
`build/w02-scan-records-evidence.json`。
