# W-07：Dynamic CS、11b Rate 与 Coexistence Power 控制

firmware HEAD d7db8d1，固定 SDK fff9895c82d744c7237be8847347bdd1b07c6643。
本批编码三个公开唯一 v1 boolean 控制；完整 Driver/恢复与 Wi-Fi 总目标仍未完成。

## 实现与状态契约

setDynamicCarrierSense：固定 esp_wifi.h 要求 started。中央 Wi-Fi helper 准入后，
Radio mutation mutex 下复用精确 Application/Station/AP owner 检查，可用于已有
Station/AP/APSTA；排除其他 feature owner、wake、promiscuous 与临时 rate obligation。
configure11bRate：固定 SDK 要求 init 后/start 前，仅用于确需禁用 11b 的场景。
true=禁用/false=启用；不是协议 bitmap 或显式 TX-rate 的别名。要求 fully stopped、
stop_required=false、零 live owner、无 wake/promiscuous/临时 rate，接口已启用且
SoftAP gate 满足。setCoexistencePowerManagement：SDK 要求 initialized；已运行时
同精确 owner 规则，停止时同零 owner 规则，另有 CONFIG_ESP_COEX_POWER_MANAGEMENT。

三者先严格 boolean/arity/interface 验证，再检查 helper scan/connect/drain/cleanup，
释放 helper mutex 后进入 Radio。SDK 不进入 IRQ critical；不隐式 init/stop/disconnect/
mode mutation 或 owner 退休。coexistence 是全局策略，不声称不影响 BLE 等参与者。

SDK 无公开 getter，每次显式调用单次 setter，包括重复相同值；成功返回被接受的
boolean，false 不是失败。没有猜测默认值/读回/去重/临时可逆 lease。SDK error 可能
已经生效，保留原始错误并 fault Radio，无自动重放或虚假 rollback，当前需设备重启。
公开完整 restart 和无 getter 写入状态/revision/恢复仍待实现，未伪装已具备恢复能力。
共用 WIFI_DRIVER_WRITE_FAILED；11b interface 为实际接口，其余为 null；Native 事务
复用 Radio.configuration，无新常驻状态表、JS roots 或后台 job。

## 条件覆盖缺口

固定 esp_coex/Kconfig 默认关闭 ESP_COEX_POWER_MANAGEMENT，依赖 software coexistence。
当前 C5 representative sdkconfig 和已记录 inventory variants 均未开启，所以
esp_wifi_coex_pwr_configure 不在记录符号表中。adapter 已按公开 header 条件编写，
C5 当前只编译 NOT_SUPPORTED 分支；不声称 SDK enabled 分支构建通过。SDK map 在
unexpandedHeaders 明确登记这项 conditional gap，没有凭空补一个未生成的 variant。
后续需合法 coex-enabled Build Context、inventory 扩展与真实共存测试。

## Connectionless interval 的实际依赖

当前 ESP-NOW 在 open、setPowerSave、恢复与 close 四处直接调用全局 wake interval
setter；SDK 明确所有 connectionless modules 共用一个 interval。因此先保留该新
Driver setter 未注册，后续必须统一原生写入/lease/关闭恢复边界，不能新增并行写入
路径。该项及失败后窗口/interval 配置一致性继续在 Wi-Fi 总范围，不归为 unsupported。

## 延后测试准备

test_wifi_driver_policy.py 提取生产 public wrapper、runtime helper、精确 registry/
owner validator、Radio policy dispatch 和 fault converter。只注入 SDK/原生状态/锁，
准备三目标、AP on/off、coex on/off（注入声明，不等于真实 SDK enabled build）、
started/stopped/stop suffix、其他 owner/旧 token/helper drain、单次写与错误后无重放。
VM 覆盖 bool/type/arity/NUL interface、原始错误/global null interface 和 GC/OOM；
native SDK 与 VM 分配计数独立。仅 AST parse，未导入、编译或执行测试。

Host C/Python/VM/故障注入、C3/S3/disabled/coex-enabled 构建、真实任务/SDK 状态、
实机/RF/动态内存均 not-run。Wi-Fi API 全部完成后集中阶段和实机功能测试，长 soak
留待 BLE API 完成；不提升 feature 稳定等级。

## 本批构建与契约检查

C5 immutable Build Context firmware-ci-esp32c5-representative 构建 exit 0；ELF 链接
三个 public wrapper、中央 coordinator 和 Radio policy 核心。coex SDK enabled 分支
未编译，仅其 NOT_SUPPORTED 分支。binary 0x29fa30 / 2,751,024 bytes，较前批增加
2,368 bytes，app 空余 13%。静态账本仍为 s_radio=824、s_raw_tx=136、s_tx_rates=68、
s_tx_rate_lease=36、s_sessions=32、s_jobs=32、s_retired=56、s_lane=44 bytes。

MQuickJS 61 sources/53 snippets、manifest 49 classes/463 functions、strict TypeScript、
feature docs 27、config schema 35 STA/21 AP（live SDK）、recorded SDK map、Python AST
和 whitespace 通过；SDK 工作区干净。原始日志/符号/source hashes 在
build/w07-policy-evidence.json。没有把 AST/injected declaration 视为生产 enabled
构建或运行验收。未刷写、操作串口、擦 workspace、构建前端、提交、推送或更新
根仓库 gitlink。下一步继续 connectionless/ESP-NOW 共享边界与其余 Driver/恢复。
