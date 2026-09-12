# W-07 inactive time 的 SDK 持久化诊断与恢复缺口

firmware `d7db8d1` 工作区增量，固定 SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`。本批核对 restart 遗漏的
inactive time 时，确认公开 setter 的持久化诊断有误并修正。未增加公开方法；
完整 restart 仍未完成。

## 固定 SDK 证据与修正

固定 esp_wifi.h 说明 inactive time 不存 Flash；但 C5 libnet80211.a 的
ieee80211_api.o 实现不同。重新核对 archive/object 与上一批 hash 一致后，沿
完整调用路径保存反汇编到 `build/w07-inactive-sdk/`：

- `esp_wifi_set_storage` 把 FLASH/RAM 枚举写入 `g_ic+0x235`。
- `esp_wifi_set_inactive_time_local` 先写当前接口值与 g_wifi_nvs，再以 item
  85（Station）或 86（AP）、请求秒数、`storage == FLASH` 调用 wifi_nvs_set。
- `wifi_nvs_cfg_init` 为两个 item 配置 uint16 类型，默认值分别为 6/300。
- `wifi_nvs_set` 根据 NVS enable 与传入 storage 标志决定是否调用 OS adapter
  的 `_nvs_set_u16`。固定 C5 adapter 接到 `nvs_set_u16`，本次 Context 启用 NVS。
  adapter 字段偏移同时由生产 ELF DWARF 核对。

这证明存在 NVS 写入尝试，不能据此宣称每次都持久化成功，也不是掉电重读证据。
setter 在 SDK error 前可能已经修改当前值；回滚读回只核对当前接口值，不读取
独立 NVS 前值。不能将 rollbackComplete 解释成 NVS 恢复完成。

生产 connection control 现在在第一次 SDK 写入前，按框架已知 storage 填写
`persistent_mutation_possible`。FLASH 写入尝试、SDK 失败和后续 rollback 都保留
true；前值读取失败/准入拒绝不会伪报写入；RAM 为 false。它保守表示可能性，
不推断第三方 SDK 的每次提交结果。沿用原错误详情和 status.radio.configuration，
不增加另一种错误、改变 storage 或自动重试。RSSI 单次观察控制保持原语义。
公开文档、类型注释与旧 connection-controls 记录已同步修正。

## 完整 restart 尚缺的状态

本次审计时动态 restart checkpoint 没有 inactive time；因此当时配置重放不等于恢复
全部 driver 控制。固定 C5 `get_inactive_time_local` 同时检查 START 和目标 mode：
Station 要求 STA/APSTA，AP 要求 AP/APSTA。setter 也要求 START/目标接口。
不能直接在停机全局配置捕获/重放中加入两个 getter/setter，不能把未启用接口
读取失败替换成默认值，更不能启用 AP 广播来猜测旧值。

后续必须明确当前活跃接口、STOP 前保存值、已停用接口历史与物理 generation 的
关系，并安排启动后的值重放/读回及失败隔离。仅存一份当前值不足以覆盖隐藏接口
与停机来源；仅依赖 FLASH 重载也遗漏 RAM 配置。RSSI 阈值另有已消费的一次性
观察语义，无 getter，不能当作普通配置盲目 rearm。本批没有放宽 STOP 历史失效
规则，也没有将上述缺口标成已实现。

后续 [原启用接口的阈值捕获与重放](2026-09-09-w07-restart-inactive.md)已接入；
隐藏接口历史与 RSSI 语义仍待完成。下方验证记录保留为本批诊断修正的历史证据。

## 检查与待执行验证

扩展 `test_wifi_connection_controls.py` 的生产 Radio/runtime helper 用例，编写
RAM/FLASH 成功、每个 SDK 步骤失败、回滚失败、AP/Station 与 RSSI 不误标场景。
SDK、锁和原生状态仍为注入边界；它不验证 SDK NVS binary、掉电或 RF 副作用。
本文件及共享它的 public options fixture 仅 AST parse，未导入、编译或执行。

C5 immutable Context `build/wireless-contexts/c5` 生产构建 exit 0；manifest
49 classes/469 functions、features 27、live SDK schema 35 STA/21 AP、MQuickJS
61 sources/53 snippets、strict TypeScript、SDK map 与 whitespace 检查通过。
构建日志、binary/static 账本及 source/SDK hash 记录在
`build/w07-inactive-persistence-evidence.json`。

Host/Python/VM/竞争测试、NVS 故障与重新上电读取、完整 restart、C3/S3/disabled、
实机/RF 均 **not-run**。Wi-Fi API 完成后统一阶段测试和实机功能验证；长 soak
留到 BLE API 完成后。未刷写、串口操作、擦 workspace、构建前端、提交、推送或
更新根 gitlink。完整剩余范围见[总清单](2026-09-08-wifi-api-remaining.md)。
