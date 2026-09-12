# W-07 显式 storage 选择与 SDK 缺口核对

firmware `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批先核对 AP 启动与尚缺 Driver API，再接入 `wifi.driver.setStorage()`；完整目标
仍按 [Wi-Fi 剩余清单](2026-09-08-wifi-api-remaining.md)，没有缩减高级模块范围。

## 已实现的公开接口

`setStorage("ram" | "flash")` 要求 driver 已初始化、完全停止、零 owner，且没有
lifecycle/operation/wake/promiscuous/临时 rate 恢复义务。输入必须恰好一个小写字符串，
NUL 后缀、大小写差异、转换对象和额外参数均在调用 SDK 前拒绝。

它沿用 Radio mutation mutex，每次显式调用执行一次 SDK setter，不隐式初始化、
启停接口或复制凭据。成功返回 SDK 已接受的选择；没有 storage getter，也没有把
内存配置 flush 到 NVS 的保证。绑定保留输入 GC root 并直接返回原字符串，成功后
不再分配结果对象。

SDK 返回失败时保留原始错误，`storage_configured=false`；既有 status 转换因此输出
`radio.storage=null`、`initialized=false`，实际 driver ownership 仍为 true。
`storage-write` 故障阻止其他 mutation。显式重新选择只有满足相同停止/排他条件且
没有其他 fault/cleanup 时才允许修复；SDK 接受后才重新标为 known，并只清除此
特定故障。没有猜测前值、自动回滚或重试，没有增加持久状态或常驻 RAM 字段。

注册、内部声明、正式类型、manifest、Driver 文档和 SDK map 同步更新。错误继续
使用 `WIFI_DRIVER_WRITE_FAILED`，operation 为 `wifi.driver.setStorage`，interface
为 null，包含当前调用的 stage/espCode/mutationAttempted。

## 本批确认的 SDK 边界

固定 SDK 公共 `esp_wifi.h` 将 `esp_wifi_clear_fast_connect` 明确标为 stub。C5
`libnet80211.a/ieee80211_api.o` 的实际函数仅调用 `wifi_init_completed`，然后返回
ESP_OK 或 0x3001，没有清除缓存的操作。证据为
`build/w07-storage-clear-fast-connect-disassembly.txt`。因此当前不注册
`clearFastConnect()`，不通过断连/清配置/private API 替代原语义；SDK map 记录明确
SDK 限制。其他 target 的二进制函数体本批未检查，公共头的 stub 声明适用于固定
SDK，不能因为 C5 返回成功就宣称某个缓存已清除。

C5 `wifi_softap_start` 调用 `chm_set_home_channel` 和 `chm_set_current_channel`，
且启动路径包含从原生配置和 Station 状态选择信道的分支。仅在临时 Station 设好
信道再启用 AP，不足以证明 AP 会保留它。反汇编存于
`build/w07-storage-sdk-wifi_softap_start.txt`。本批未改变 AP 启动路径；AP 配置信道
与原实际 home 不同的恢复协调、APSTA Station 关系和广播/客户端竞争仍待实现及
实机验证。此静态证据不等于复现了任一实机启动故障。

## 验证范围

C5 immutable Context `build/wireless-contexts/c5`（8 MB/no PSRAM）生产构建 exit 0：
`build/w07-storage-c5-build.txt`。binary 2,764,496 bytes，比前批增加 992；记录的
Radio/ESP-NOW/Raw TX/restart control/policy/interval/rate 静态大小不变。公开绑定与
原生 setter 已链接。没有 live heap/回收证据。

新增 deferred `test_wifi_driver_storage.py` 提取实际 production Radio/绑定/error
实现，注入 SDK、锁、storage 和 VM 分配/GC 边界。覆盖停止/owner/故障准入、未知
枚举、SDK 部分写入错误、不自动重试、storage unknown、重复失败、显式修复、无关
cleanup 不被清除、精确参数个数/NUL、原始 SDK 错误、SDK 边界触发 GC 与逐次错误
分配失败。只做 AST 解析，未导入、编译或执行用例。

manifest 49 classes/465 functions、feature 文档 27、STA/AP schema 35/21（live SDK
header）、MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map 与 whitespace
检查通过。证据汇总 `build/w07-storage-evidence.json`。

Host/Python/VM/GC/OOM/故障注入、C3/S3/feature-disabled、完整 restart、RF/共存、实机
均 not-run。全部 Wi-Fi API 完成后集中阶段及实机功能测试；长 soak 放到 BLE API
完成后。未刷写、串口操作、擦除 workspace、构建前端、提交、推送或更新根 gitlink。
