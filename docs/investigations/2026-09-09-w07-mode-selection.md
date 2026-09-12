# W-07 显式停止后 mode 选择

firmware `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
继 [storage 选择](2026-09-09-w07-storage-selection.md)后新增 `wifi.driver.setMode()`。

## 已实现

正式参数沿用 `getMode()`/Radio status 的 `WiFiRadioMode`：off、station、softAP、
station+softAP。要求恰好一个字符串，拒绝 coercion、NUL 后缀、大小写变化和额外参数。
Driver setter 不接受 start/configure 的 ap/apsta 参数作为别名。NAN mode 继续归
W-08 的专属生命周期，不提前注册绕过资源管理的原始 mode。

原生路径要求 initialized、完全 stopped、零 owner，无 lifecycle/operation/wake/
promiscuous/临时 rate/cleanup 义务。AP/APSTA 由实际 SoftAP 编译 gate 限制。整个
SDK 操作与回滚都在既有 Radio mutation mutex 内，driver 调用不放在临界区。

先读取 SDK mode，验证编码及与框架缓存一致。未知编码或不一致在首次写入前报
`mode-snapshot` fault；普通 getter 错误不触发 setter。已匹配请求只返回验证结果。
否则写入目标 mode 并再次读回，成功才更新框架 mode。setter 错误或读回不匹配时
恢复刚验证的前值并读回，保留原始 error，另记 rollback stage/error。

RAM 策略下，运行态回滚完成允许后续操作；回滚失败保留 `mode-rollback` cleanup。
FLASH 策略下，失败的变更即使运行态回滚成功，也保留 fault 和
`persistentMutationPossible`，不声称 NVS 已恢复。失败后 Radio mode 可能仍是此前
框架记录，使用前需检查 fault/configuration；正常 getter 不越过该故障边界。

不初始化、启停 Wi-Fi、创建 netif/helper、消耗新 lease identity 或断开连接。
off 只选择 SDK NULL mode，driver 仍 initialized；后续 start/configure 继续执行
各自配置与启动准入。此接口不构成 AP live activation 或公开 restart 的完成。

绑定在 SDK 操作期间保留输入 GC root，验证成功后直接返回该字符串，不再分配结果。
错误复用 `WIFI_DRIVER_WRITE_FAILED`，operation=`wifi.driver.setMode`，interface=null。
内部声明、注册、正式类型、manifest、API 索引/说明与 SDK map 同步更新；补齐新增
mode/既有 storage-write 的 fault 类型，以及 storage unknown 的 null 注释。

## 验证范围

C5 immutable Context `build/wireless-contexts/c5`（8 MB/no PSRAM）生产构建 exit 0：
`build/w07-mode-c5-build.txt`。binary 2,765,824 bytes，较前批增加 1,328；记录的
Radio/ESP-NOW/Raw TX/restart control/policy/interval/rate 常驻大小不变。绑定和原生
setter 已链接，没有 live heap/回收证据。

新增 deferred `test_wifi_driver_mode.py`，提取真实 native transaction、绑定和错误
转换，仅注入 SDK、storage、锁和 VM 分配/GC。包括各合法模式组合、AP gate、逐项
admission、每次 SDK 调用失败、setter 部分生效、错误/不匹配读回、每次 rollback
调用失败、未知/外部 mode、FLASH 持久性不确定、严格字符串/arity 和 SDK 边界 GC。
只做 Python AST 解析，未导入、编译或执行 fixture。

manifest 49 classes/466 functions、feature 文档 27、STA/AP schema 35/21（live SDK
header）、MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map、whitespace
检查通过。证据 `build/w07-mode-evidence.json`。

Host/Python/VM/GC/OOM/故障注入、C3/S3/feature-disabled、AP/APSTA live activation、
完整 restart、RF/共存、实机均 not-run。所有 Wi-Fi API 完成后集中阶段及实机功能
测试，长 soak 留到 BLE API 完成后。未刷写、串口操作、擦除 workspace、构建前端、
提交、推送或更新根 gitlink。完整剩余范围仍见 Wi-Fi 清单。
