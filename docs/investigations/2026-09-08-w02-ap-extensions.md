# W-02：AP SAE-EXT、compatible 与 BSS idle

基线 firmware `d7db8d1`，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批补齐当前 AP config 的四个扩展 leaf，继续使用已实现的独占冷启动 startAP。
公开 configure/APSTA、5 GHz 与完整 raw driver config 仍待完成。

## 固定 SDK 依据与输入

固定 `esp_wifi_types_generic.h` 明确 WPA3 compatible 会覆盖 auth/cipher；
`esp_hostap.c` 为该模式的 RSN override 选择 SAE、CCMP、required PMF，同时
保留基础 WPA2 配置。该源文件仅在纯 WPA3 + GCMP-256 + sae_ext 下选择
SAE_EXT_KEY。BSS idle 的 public period 单位为 1000 TU（1.024 秒），0 禁用，
非零最小 10；实际编译 gate 为 `CONFIG_ESP_WIFI_BSS_MAX_IDLE_SUPPORT`。

- `saeExt` 默认 false。True 要求 SAE/SoftAP SAE、H2E 和 GCMP 的实际 target/
  build gate；缺省 auth/cipher/PMF/PWE 分别设为纯 WPA3、GCMP-256、required、
  hash-to-element。显式冲突被拒绝，PWE both 允许，不能与 compatible 同时启用。
- `wpa3CompatibleMode` 默认 false。True 是对 WPA2/CCMP base override 的显式
  授权，不声称纯 WPA3。允许 WPA2/WPA3/WPA2-WPA3 输入，password 8..63 bytes、
  cipher CCMP；要求 SoftAP SAE/compatible build。需要 PMF 的请求不会因
  SDK 规范化而被降为 optional。高级 peer 协商能力仍需 RF 验证。
- `bssMaxIdlePeriod` 为整数 0 或 10..65535；protected keepalive 为布尔。
  True 要求非零 period 和加密 AP（包括 OWE），不替代 PMF 设置。不支持的
  build 拒绝非零/true，保留 0/false 的禁用语义。

所有输入由已有 rooted capture 解析，纯 native validator 在分配 Radio owner
前和内部 admission 再验证。没有新增 native storage、callback 或任务。
失败沿用整个 config 的 secure-zero，未把密码加到结果、能力或错误中。

## 读回与结果

配置事务的 AP accept helper 增加四个字段的精确比较；driver 忽略 extension、
compatible、idle period 或 protected flag 时拒绝发布可用 AP。Compatible
要求 readback 的 base auth 为 WPA2、cipher CCMP 且 compatible flag 仍为 true；
其余模式保留请求 auth。PMF/transition 和已有凭据规则继续生效。失败沿用
原生快照回滚和中央清理，不用未验证 readback 掩盖 SDK 差异。
SDK 在 compatible 下的实际 getter 规范化形态仍需实机确认；若返回值不符合
上述可诊断契约，当前实现会拒绝启动，不把输入 auth 误报为实际基础认证模式。

startAP 返回四个已读回字段，compatible 的 PWE 信息也保留。capabilities 中
新增 saeExt/wpa3CompatibleMode/bssMaxIdle，正式 types 与 API 文档同步；完整
raw config 生成声明仍为 contract-pending，不能据此增加 configure 注册。

## 验证账本

C5 immutable Context 构建通过，app `0x27baa0` bytes，分区空余 17%。MQuickJS、
manifest/feature、schema/map 一致性、文件语法及 hash 见
`build/w02-ap-extensions-evidence.json`。

用 `test_wifi_ap_config.py` 替换旧 AP VM fixture 中已失效的简化 parser 提取。
新 fixture 使用记录的 SDK typedef（包括 bitfield），完整生产 AP parser、
validator、readback accept、result、options helper 和 secure-zero，加真实
MQuickJS 的 property OOM/移动 GC。覆盖三种扩展 build gate、默认值、参数冲突、
单位/长度/数值边界、新字段被 native readback 忽略、结果无密码和失败 config
清零。Readback 测试由 fixture 注入 SDK 边界值，不是实际 SDK/协商证明。

新增/更新用例未运行。集中 Host C/Python、其他 target/feature-disabled、实机
SAE-EXT/RSN override/BSS idle 行为、关闭/GC/runtime restart/RF/内存仍待全部
Wi-Fi API 完成后执行。长 soak 放到 BLE API 完成后。没有串口、刷写、提交、
推送或父仓库 gitlink 更新；当前功能仍处于待验证状态。
