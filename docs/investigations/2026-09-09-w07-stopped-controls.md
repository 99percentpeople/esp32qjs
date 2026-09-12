# W-07 停机后的 storage/RSSI 写入与恢复

firmware `d7db8d1` 工作区增量，SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`。承接
[公开 Candidate restart](2026-09-09-w07-public-restart.md)。本批扩大可恢复来源：
固定 SDK 的 C3/S3/C5 在健康全局 STOP 后，允许 storage/RSSI 写入保留已有 STOP
观察。其他配置写入、未知来源与完整故障恢复继续留在原目标中。

## 先核对固定 SDK 的真实写入范围

从当前三目标 libnet80211.a 重新提取 ieee80211_api.o、ieee80211_supplicant.o
和 ieee80211_ioctl.o，并对 setter/writer 做带 relocation 的静态反汇编。

| SDK 操作 | C3/S3 | C5 | 已观察到的操作范围 |
| --- | --- | --- | --- |
| esp_wifi_set_storage | g_ic + 0x215 | g_ic + 0x235 | init/参数校验、global lock、写一个 storage byte、unlock；不读取/重写配置或 NVS |
| wifi_set_rssi_threshold | g_ic + 0x264 | g_ic + 0x288 | 读取消息 offset 12 的 threshold、写一个 int32 字段、返回成功 |

三目标 RSSI public wrapper 均检查 init、分配 24-byte 消息，消息 id 83，handler
为 wifi_set_rssi_threshold，经 ieee80211_ioctl 提交；未见其直接调用 RF/config
setter。storage 的两个 OS adapter 间接调用 offset 84/88 对应 mutex lock/unlock，
固定 header 的布局及各目标 esp_adapter.c 均绑定到 recursive semaphore take/give。

这些操作不改写 STOP snapshot 保存的 TX power、band/mode、current/home channel
与 inactive time。因此它们不必作废这些历史观察；storage 作为后续写入策略
由 checkpoint 另行捕获最新已接受值，RSSI 仍按一次性请求契约排除自动 replay。
这不是 RF/事件交付或 SDK init/deinit 整体无副作用的证明；没有推断 armed 状态、
默认阈值或停止后 getter 可用性。

源库、object 和 disassembly hashes 位于
`build/w07-stopped-controls-sdk/evidence.json`。二进制/源码审查覆盖 C3/S3/C5，
不等于这三个目标的 firmware build 或运行测试已经执行。

## 生产失效边界的修订

原 Radio-local header 仍列出全部 27 个 SDK writer/lifecycle alias，避免新 writer
漏入。仅 storage 与 RSSI 标为 WIFI_RADIO_STOP_NEUTRAL；在已审查的 C3/S3/C5
保留 candidate bit，在其他 target 回退到原 invalidation。其余 25 项仍在 SDK
尝试前作废，包括失败与同值/回滚调用。SDK 参数和返回值不改变，参数只求值一次。

该标记只保留原观察，不改变原子准入中的 registry、operation/wake、generation、
STOP identity、driver ownership、fault/cleanup、known storage 等前提。未知或
已经失效的历史不会因中性写入变为有效，也不能越过其他 owner。

storage setter 失败仍设置 storage unknown 与原 storage-write fault，此时 STOP
谓词和 restart 准入都拒绝。后续显式成功选择 storage 只清除其原有可修复故障；
若 STOP 历史的其他条件仍满足，才重新满足准入。冻结的是本次最新接受的 RAM/
FLASH 策略，重建结束恢复该值。没有使用旧枚举猜测失败写入结果或回滚 NVS。

RSSI setter 无论接受或返回 SDK error，都不改变这份 RF 历史；真实请求/error/
generation 记录保持原契约，失败并不证明没有事件。重建不重新发送旧请求。
公开类型/API 文档已明确两项例外，方法仍为 Candidate。

## 检查与待执行用例

新增 deferred test_wifi_stopped_controls.py：连接生产 storage/RSSI setter、实际
mutation alias、STOP predicate、完整 registry lifecycle admission、checkpoint、
pre-/post-start、replay 和 commit。编写最新 storage 捕获/恢复、失败 storage
先拒绝再显式修复、RSSI 失败保留历史/不重放、不能生成/恢复无效历史、不同 STOP
identity、新 owner 拒绝，以及 unreviewed-target 仍失效的用例。SDK、物理重建/
START、锁与 native storage 是注入边界，不代替真实 helper/event/RF 证明。

共享 config fixture 增加实际 target define；RSSI 生产函数在本新 fixture 中
移到真实 macro 边界之后，避免绕过需要检验的 alias。旧 STOP 测试改为明确
中性写入与其他 setter 的区别，inventory 仍要求 writer 集合完整、neutral 集合
精确为这两项。四份 fixture 仅 AST，未导入、编译或执行。

C5 immutable Context `build/wireless-contexts/c5` build exit 0。binary
2,797,840 bytes，比前批减少 32 bytes。既有 12 项静态账本大小不变（Radio 824、
restart control 40、STOP 24、inactive history 24、RSSI 16 bytes）；没有新常驻
分配、owner 或 JS root。公开 restart、两个写入核心、原子准入与配置 capture 均
链接到 ELF。未做实机 heap/碎片或 stack 峰值测量。

manifest 49 classes/470 functions、features 27、live SDK schema 35 STA/21 AP、
MQuickJS 61 sources/54 snippets、strict TypeScript、SDK map 与 whitespace 通过。
本批证据：`build/w07-stopped-controls-evidence.json`。

Host/Python/VM/竞争、C3/S3/feature-disabled firmware builds、实机/RF/NVS/完整
restart 运行测试均 **not-run**。依安排待全部 Wi-Fi API 完成后集中执行，长 soak
留到 BLE API 完成。未刷写、串口操作、擦 workspace、构建前端、提交、推送或更新
根 gitlink。其余停止状态、未知隐藏配置和完整故障恢复没有标为完成。
