# W-07：inactive time 与显式 RSSI 阈值控制

firmware HEAD `d7db8d1`，固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批注册唯一 v1 的 wifi.driver.setInactiveTime/setRssiThreshold，公开类型、文档
与 SDK map 同步。完整 Driver、恢复生命周期和 Wi-Fi 总目标仍未完成。

## 精确 owner 与运行时交接

JS 参数预验证后通过原 Wi-Fi runtime coordinator 交接 APPLICATION、Station、AP
三个实际 lease；不隐式初始化 helper/driver。先排除 helper cleanup、scan/connect/
disconnect/Future 排空状态，释放 helper mutex 后才进入 Radio mutation mutex。
AP cleanup 不提供可控制 lease；实际注册表仍有未退休 AP 时，精确 owner 检查拒绝。

Radio 检查 generation/identity/client/acquired 与真实存活 registry，不按 owner
计数猜测权限。inactive time 要求 target helper 已启动、所有其他 lease 均为传入
的精确框架 owner，排除其他 feature、wake、promiscuous 和临时 rate obligation。
允许现有 APSTA 的两个框架 helper 共存，不停机、不改变另一接口的阈值。

RSSI threshold 仅控制观察事件，允许无关 RF owner 与 wake lock，仍验证框架 owner
identity、target mode、生命周期/operation/fault/cleanup。运行中要求 Station helper
精确 owner；initialized/stopped Station mode 可无 helper 配置。没有为共存场景
新增隐式 owner 或通用忽略 lease 的开关。

## 两种不同的完成语义

inactive time：Station 整数 3..65535 秒，AP 10..65535。固定 esp_wifi.h 的“不存
Flash”说明与 C5 binary 不符；[后续 SDK 审计与修正](2026-09-09-w07-inactive-persistence.md)
确认 FLASH 模式可能进入 NVS setter，已补 persistentMutationPossible。缩短阈值
可能使 Station 断连或 AP deauth 客户端。先读并验证前值，再写入和
读回；SDK write/readback 失败或不匹配时重写前值并读回。原始错误与 rollback 错误
分开记录；回滚失败保留 inactive-time-rollback Radio 清理故障。rollbackComplete
只表示配置值恢复，不能撤销已发生的断连/deauth，不自动重连或全局重置其他 owner。

RSSI：整数 -100..10 dBm，每次显式调用恰好写 SDK 一次，包括重复相同值；成功
返回 SDK 接受的阈值。固定 SDK 要求低信号事件后重新调用才能请求下一次通知。
复用既有 wifi.watch 的 WIFI_EVENT_STA_BSS_RSSI_LOW/data.rssi，不增自动 rearm。
没有公共 getter/callback cookie，因此不伪装当前 armed 状态，不做猜测性 rollback。
SDK 错误也可能已写入，保留 mutationAttempted 和原错误，但不因此清除 RF owner
或自动重放写入。用户下一次显式调用就是新的 rearm。队列丢观察事件不触发重试。

两个 API 复用 WIFI_DRIVER_WRITE_FAILED 与事务详情；Radio 结果写入现有
wifi.status().radio.configuration。helper 拒绝发生在 Radio 前，只返回本次
helper-admission 错误，不伪造新 SDK 记录。无新 JS root、native job、原生恢复队列
或阈值常驻表。整数、长度、参数个数、NUL 后缀与类型在 SDK 写入前验证。

## 验证与限制

test_wifi_connection_controls.py 准备使用三目标 inventory 类型、AP on/off，调用
生产 registry 验证、owner gate、Radio SDK/rollback/fault 路径、实际 helpers_idle、
AP lease 选择及中央 coordinator；仅注入 native state、SDK 和锁。覆盖每个 SDK
步骤/回滚后缀失败、setter 写入后报错、精确 token/错误 role、helper 排空、未退休
AP、其他 feature owner、wake、stopped RSSI、同值单次 rearm 和失败后不自动重试。
test_wifi_connection_control_options.py 连接真实 public wrapper/中央 helper/Radio/
共用 error converter，准备覆盖范围/小数/字符串/参数个数及错误分配失败与 GC。
Native SDK 与 VM 分配计数分别注入。两份文件仅 AST parse，未导入、编译或执行。

C5 immutable Build Context firmware-ci-esp32c5-representative 编译 exit 0；ELF
链接两个公开 setter、runtime coordinator、Radio owner gate 和控制核心。binary
`0x29d940` / 2,742,592 bytes，比前批增加 2,416 bytes，app 空余 13%。静态账本未
增加：s_radio=824、s_raw_tx=136、s_tx_rates=68、s_tx_rate_lease=36、s_sessions=32、
s_jobs=32、s_retired=56、s_lane=44 bytes；动态 heap/stack 峰值与实机内存未测量。

MQuickJS 61 sources/53 snippets、manifest 49 classes/452 functions、strict TypeScript、
feature docs 27、config schema 35 STA/21 AP（live SDK）、recorded SDK map 与 whitespace
通过。日志/符号/hash 在 build/w07-connection-controls-evidence.json；SDK 工作区干净。

Host C/Python/VM/故障注入、真实 callback 并发/队列饱和/RSSI rearm、断连和 AP deauth
副作用、C3/S3/disabled 构建、实机/RF/heap 均 not-run。按既定顺序，Wi-Fi API 全部
完成后集中阶段与实机功能测试，长期 soak 留待 BLE API 完成。未刷写、串口操作、
擦 workspace、构建前端、提交、推送或更新父仓库 gitlink。band/其余 Driver、Raw TX
显式恢复、高级模块、资源预算及交付验收继续按[总清单](2026-09-08-wifi-api-remaining.md)推进。

后续 [RSSI 请求历史与重建契约](2026-09-09-w07-rssi-request.md)已加入固定原生请求记录和 status 诊断；明确不在重建时自动 rearm，替代本批“无阈值常驻表”的实现状态。记录不是 SDK 当前值或 armed 证明。
