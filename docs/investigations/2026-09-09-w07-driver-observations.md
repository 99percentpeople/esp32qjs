# W-07：频段、连接与时间观察接口

firmware HEAD `d7db8d1`，固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批接入九个真实 Driver getter，正式 v1 注册/类型/文档与 SDK map 同步：
getBand、getBandMode、getPowerSave、getTxPower、getRssi、getAid、getNegotiatedPhy、
getTsfTime、getInactiveTime。W-07 和完整 Wi-Fi 目标仍未完成。

## 状态与值的边界

统一 Radio read_driver helper 在 mutation mutex 内调用 SDK；不初始化、不获取
owner、不访问 JS。拒绝 lifecycle/operation、fault/cleanup/restart-required 和
不稳定 driver state。接口型查询要求对应 mode 启用，AP 受 SoftAP build gate。
TX power、RSSI、negotiated PHY、TSF、inactive time 额外要求 started；其余允许
稳定 stopped。SDK 和 IRQ critical section 分离，没有新 callback 或常驻记录。

每次只查询一个 SDK 值，不能把多个返回值拼成原子连接快照。Radio mutex 串行化
框架的 driver mutation，不能冻结 native association/beacon 变化。RSSI 是最近
beacon 的 dBm；AID 0 是未关联的 SDK 观察值，不是 operation/client identity。
negotiated PHY 复用按目标 SDK enum 映射的既有命名 helper，不从 bit 序号推断。

getBand 与 getBandMode 均调用 SDK，即使在 2.4 GHz-only target，也不伪造常量
成功。未知/target 不一致的 band/mode/PHY/power-save enum 拒绝为 decode 错误。
getTxPower 读取 quarter-dBm 并在 JS converter 除以 4，保留 0.25 dBm 精度；该值
是配置最大值，不是即时 RF 发射功率测量。inactive time 只读当前秒数。

esp_wifi.h 的 TSF 契约：Station 未关联或关联后尚无 beacon 时返回 0，power save
可能影响准确性。本接口保留 0，不制造 UTC/Host 时钟锚点；负值或超过 JS exact
integer 上限 9,007,199,254,740,991 时返回 decode 错误，绝不静默舍入。这项读取
没有完成 RX wire 时钟校准、跨关联/restart 时钟或 RF 验收。

前七个方法严格零参数，TSF/inactive time 严格一个 station/access-point 字符串。
所有 SDK 输出先存原生局部变量，成功才发布；失败清零并保留原始 espCode/stage。
共有错误 converter 使用已有 GC ref/native error helper；全局查询 interface=null，
连接查询为 station，按接口查询为用户选定接口。PHY 四个 getter 也复用此 error
converter，原有错误 code 与字段保持同一 v1 契约。

## 验证与后续

新增 test_wifi_driver_observe.py，准备在三目标 inventory 类型/AP on-off 下调用
实际 Radio helper、生产 enum mapper，注入 SDK 与 mutex/native state 存储，覆盖
每个 getter SDK 失败/清零、状态准入、unknown enum、0 sentinel 和 TSF 精度边界。
VM 部分调用九个真实 public wrapper、实际 native helper 与共享错误 converter，
准备验证 arity、SDK error details、单位转换以及逐分配失败和 moving GC。
本批仅 AST parse 此文件，未导入、编译或执行上述测试。

C5 immutable Build Context firmware-ci-esp32c5-representative 编译 exit 0，ELF
链接全部九个公开 getter、Radio read_driver、scalar/error converter。binary
`0x29cfd0` / 2,740,176 bytes，比前批增加 3,024 bytes，app 空余 13%。静态账本未
增加：s_radio=824、s_raw_tx=136、s_tx_rates=68、s_tx_rate_lease=36、s_sessions=32、
s_jobs=32、s_retired=56、s_lane=44 bytes；动态 heap/stack 峰值与实机内存未测量。

MQuickJS 61 sources/53 snippets、manifest 49 classes/450 functions、strict TypeScript、
feature docs 27、config schema 35 STA/21 AP（live SDK）、recorded SDK map 与 whitespace
通过。日志/符号/hash 见 build/w07-driver-observe-evidence.json；固定 SDK 工作区干净。
Host C/Python/VM/故障注入、真实并发、C3/S3/disabled 构建、实机/RF/heap 均 not-run。
Wi-Fi API 全部完成后集中阶段与实机功能测试，长期 soak 留待 BLE API 完成。

本批未刷写、串口操作、擦 workspace、构建前端、提交、推送或更新父仓库 gitlink。
inactive time setter 可能触发 Station 断连或 AP deauth，需独立 owner/副作用契约；
RSSI threshold 无 getter 且事件后需显式 rearm，不能当普通值读回配置。它们与 band
setter、其他 Driver/高级模块、Raw TX 恢复、资源预算仍按[总清单](2026-09-08-wifi-api-remaining.md)推进。
