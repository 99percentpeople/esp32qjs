# W-02：Station PHY 与 SAE-PK 输入

基线 firmware `d7db8d1`、SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批沿用完整字段映射，补充生产 Station capture 的 14 个 PHY/安全选项。仍使用
既有 `wifi.connect` Future，不新增 driver/configure 占位入口。

## 实现

`wifi_parse_station_phy` 捕获 8 个 HE 字段与 3 个 VHT 字段。先将 JS 值解析到
本地标量，再写 native bitfield，避免对 bitfield 使用 offsetof/指针强转。
options/property 都保持 GC root；未知选项仍由外层 plain-options validator
拒绝，非布尔、非有限整数和范围外值在 Radio/driver 操作前失败。

HE 使用实际 `CONFIG_SOC_WIFI_HE_SUPPORT`，VHT 使用固定 SDK 的 5 GHz target
gate。该 SDK 下 C5 的 5 GHz driver 支持 11ac；这不是从公开 enum 符号存在推断
所有 target 支持。任何显式不受支持的 PHY 字段（包括 false）都失败。
DCM constellation 为整数 0..3，显式 TX/RX 要求 heDcmSet=true；启用时未提供的
TX/RX 各取 SDK 文档默认 3。未启用时保留零初始化/SDK 默认语义。不自动切 band
或协议，也不把配置成功当作实际速率/beamforming 证据。

Station extensions 同时新增 transitionDisable、disableWpa3CompatibleMode、
saePkMode。前两项为有 gate 的显式布尔，false 可用于没有对应安全特性的 build。
禁用 WPA3 compatible 会影响 RSN override，不能描述成必然加强安全的开关；
transition-disable 也不是初次认证的强制安全门限。

saePkMode 用长度感知的字符串比较，只接受 automatic/only/disabled。显式
automatic/only 要求 SAE-PK build；disabled 不需要。Only 要求 1..63-byte
密码、WPA3 threshold、required PMF 与 H2E；拒绝显式更弱的 threshold、optional
PMF、hunting-only PWE、OWE 和 64-byte raw PSK。没有显式设置这些依赖时设置
安全默认值，不在失败时以更弱配置重试。

SDK `esp_wpa3.c` 在 PK-only SAE 路径中验证密码与 AP 的 PK/H2E 支持，不满足
就失败；完整握手不属于 capture。这里不调用私有 supplicant API，也不复制
SAE-PK checksum/认证实现。最终协商与无降级证据仍需阶段安全/RF 测试。

capabilities.stationOptions 新增 he/vht、两个安全 flag 和 saePkModes 列表。
正式类型和 API 文档同步；完整字段映射的对应 gate/约束同步，生成的完整 raw
config 声明仍为 contract-pending。嵌套 driver 输入、二进制 SSID 和高级认证
owner 尚需在各自实现中接入。

## 验证

C5 immutable Context 构建通过：app `0x27b680` bytes，分区空余 17%。语法和
生成物检查及源码/日志 hash 在 `build/w02-station-phy-evidence.json`。

新增 `test_wifi_station_phy_security.py` 编译完整生产 Station parser、真实
MQuickJS、production options helper 和 secure-zero，仅注入 SDK类型/边界及 VM
property OOM/移动 GC。包含三种编译 gate、DCM 默认/范围/依赖、HE/VHT field
类型/未知键、PK-only 与弱配置冲突、NUL 后缀、无支持目标和每个失败点的整个
native config 清零。既有 rssi preference fixture 同步新的生产 helper 依赖。
用例已写、未运行；捕获用密码不是经过原生验证的 PK 凭据。

按用户安排，完整 Host C/Python、C3/S3/feature-disabled 与实机功能在全部
Wi-Fi API 完成后集中测试；长 soak 放到 BLE API 完成后。无串口、刷写、提交或
父仓库 gitlink 更新。完整 raw config、公开 configure/APSTA 与后续模块未完成。
