# W-02：Station 参数、编译 gate 与冲突验证

基线 firmware `d7db8d1`，固定 SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`。本批是工作区增量，
Wi-Fi APIs、configure/APSTA 和阶段运行验收尚未全部完成。

## 生产实现

`esp32_mquickjs_wifi_parse_station_config()` 从既有 Future capture 中提取为共享
native config parser；仍由 connect 的生产 prepare 调用，供后续 configure 复用。
不新增 driver 调用、后台任务、静态 pool 或 Future native config 副本。
新参数先解析到既有 wifi_config_t，所有验证完成后才允许 driver 操作。

| 字段 | SDK 字段/条件 | 当前验证 |
| --- | --- | --- |
| listenInterval | listen_interval / uint16，MAX_MODEM 时使用 | 0–65535，单位 AP beacon interval；0 为 SDK 默认 3，不隐式切换省电模式 |
| failureRetryCount | failure_retry_cnt / uint8 | 0–255，非零仅用于 WIFI_ALL_CHANNEL_SCAN，不延长总 timeout |
| rmEnabled | rm_enabled，11KV + RRM build gate | boolean，true 需对应 gate |
| btmEnabled | btm_enabled，11KV + WNM | boolean，禁止 BSSID/非零 channel hint |
| mboEnabled | mbo_enabled，MBO build gate | 同时启用 RM/BTM，显式 false 依赖或固定 hint 冲突 |
| ftEnabled | ft_enabled，11R build gate | boolean，true 需 gate |
| oweEnabled | owe_enabled，OWE STA build gate | 无 password，强制 OWE threshold 与 required PMF；显式弱 threshold/optional PMF 冲突 |
| saePwe | sae_pwe_h2e，使用 enum 常量而非猜测数字 | hunting-and-pecking/hash-to-element/both；SAE/H2E gate，需 1–63-byte password |
| saeH2eIdentifier | sae_h2e_identifier[32] | 1–32 UTF-8 bytes，无 NUL；H2E gate，不能 hunting-only，省略 PWE 时设 BOTH |

`minimumAuthMode:"owe"` 与 oweEnabled:true 使用同一严格配置路径；显式 false
矛盾时拒绝。WPA3、OWE、WAPI threshold 同样检查实际编译支持。显式 false 的
boolean 是关闭设置，不要求启用相应 build gate。没有注册 roaming/enterprise
高级 namespace，也不把允许连接字段误写成这些管理 API 已实现。

`capabilities().stationOptions` 同步报告各 enable 能力、PMF/PWE/threshold
列表，使用相同编译 gate。完整安全能力矩阵仍需后续 AP/driver 模块的字段审查。

## 已确认的 SDK 语义与 sole v1 改动

证据均来自本地固定版本 `components/esp_wifi/include/esp_wifi_types_generic.h`
和 `components/esp_wifi/Kconfig`，以及 supplicant 的对应 build definitions。

- BTM/MBO 的 SDK 注释明确说明会清除特定 BSSID/channel；框架预先拒绝冲突，
  不把被 SDK 忽略的请求描述为成功固定。MBO 的隐式依赖在 capture 中显式落实。
- PMF 的 `capable` 字段已经 deprecated，SDK 明确说明双方具备能力时始终使用
  PMF。因此此前写 capable=false 不足以支持“关闭 PMF”的契约。当前只正式接受
  optional/required；disabled 报 WIFI_CONNECT_UNSUPPORTED，真正 disable 的
  原生 API 与生命周期事务仍须在 W-07 实现。这里是源码证据，不声称已有实机复现。
- scanMethod 用目标 all-channel 替换 all；pmf 用 optional 替换 capable；没有
  legacy aliases。仓库内 network.js 同步，设备 workspace 未自动修改。
- SSID/password 禁止内嵌 NUL；64-byte password 只接纳 ASCII hex PSK。
  BSSID 拒绝零/组播，channel hint 接纳 0、1–13 或 target-supported 5 GHz
  标准信道，不用 hint 建立 fixed owner，也不绕过 driver country 检查。

不改变密码长度对应的全部 native 认证规则；不同 auth mode 的最终密码合法性
仍由 SDK 验证，完整配置字段审查继续进行。

## 秘密和错误

H2E identifier 不进入 status、capabilities、日志、事件或错误详情。失败 capture
由共享 parser 立即 secure-zero 整个 config；Future prepare 失败和 destroy 的
既有整块清零路径仍保留。JS 调用者自己的字符串不由框架改写，SDK 已接受的 RAM
配置副本也不等同于 Future 的临时副本，不能声称 Future 完成即删除 driver 凭据。

不支持的选项返回 WIFI_CONNECT_UNSUPPORTED / wifi.connect，details 只有
option、espCode、espName，不带选项值或秘密。格式、依赖冲突在 driver effects
前报错；字符串转换的 OOM 保留原异常，不用后续 RangeError 覆盖。

## 验证与未完成验收

必要检查由 `build/w02-station-evidence.json` 记录源文件/产物 hash 与日志：
immutable C5 Context 编译、MQuickJS syntax、manifest、feature 文档与 recorded
SDK map 一致性。没有运行 Host C/Python 全套、其他目标/gate 构建或实机测试。
本批不提交、不刷写、不改父仓库 gitlink 或 frontend。

下列生产路径测试留在 Wi-Fi APIs 全部完成后的集中阶段，均为 not-run：

- 所有新增字段的边界、非整数、类型、稀疏输入、NUL/UTF-8 字节长度，H2E 32-byte
  满长度；第 N 次分配失败、移动 GC、失败/完成/取消/destroy 清零。
- 11KV/RRM/WNM/MBO/11R/SAE/H2E/OWE/WAPI gate 的启用与禁用组合；MBO 依赖，
  BSSID/channel 冲突，fast scan 非零 retry，OWE 无弱认证回退、PMF required 不降级。
- 参数在 capture 后不可被 JS options 修改，SDK submission 配置与 capability
  报告一致；deadline/取消/迟到 completion 仍遵守既有 native 生命周期。
- C3/S3/C5 与 feature-disabled matrix；合适 AP 对端上的真实连接/断连/roaming/
  OWE/SAE/H2E/PMF 功能验证，无对应对端时逐项保留 not-run。

长时间 soak 等 BLE API 完成后执行。完整 connect result、嵌套 driver config、
HE/SAE-PK、AP 参数与 configure/APSTA 继续推进，不能以本批参数增量替代全部目标。
