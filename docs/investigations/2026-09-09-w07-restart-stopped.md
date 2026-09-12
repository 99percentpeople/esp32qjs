# W-07 将合格 STOP 观察接入停止状态的 restart 捕获

firmware `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接 [STOP 有效性边界](2026-09-09-w07-stop-validity.md)。内部配置捕获现在可使用
合格的 STOP 观察；公开 restart、停机后配置变化和无历史观察的完整恢复仍待完成。

## 固定 SDK 与捕获路径

重新核对当前 C5 archive/object hash，并保存 `esp_wifi_get_band_mode`、
`esp_wifi_get_protocols`、`esp_wifi_get_bandwidths` 反汇编至
`build/w07-stopped-sdk/`。band mode getter 初始化检查后在 SDK lock 内读取
g_wifi_nvs 保存值；protocol/bandwidth wrapper 也没有 START 拒绝分支，继续通过
ioctl 读取可见频段字段。后两者的内部 ioctl/实际 target 行为仍需集中验证；静态
wrapper 证据不等于 SDK 实机运行通过。

停止状态的捕获先要求原 STOP 记录满足 generation、event identity、完整观察、
无后续被跟踪写入和健康 STOPPED 条件；不满足时以 `restart-stop-snapshot` 拒绝，
不分配凭据 checkpoint，不调用 START/deinit。原有精确 lifecycle、零 owner、
无 native operation/wake/promiscuous 和 rate/interval 前提继续有效。

随后仍从当前 SDK 读取完整 Station/AP 配置、country/MAC/power-save/event-mask
及可见 PHY 字段，冻结当前框架 rate/interval 记录。新的
`wifi_radio_restart_stopped_observations_locked()` 只提供 STOP 后不能查询的旧
TX power、band/current/home 观察，并直接读回 SDK band mode 核对保存策略。
完成可见 PHY 读取后再次核对，拒绝不一致，包括不能因 uint8_t 截断而接受的非法
SDK enum。不会在停机状态调用 power/current/home getter 来覆盖原值。

C5 原单频模式随后进入既有 RAM/Station/AUTO 捕获准备，读取另一频段；允许从
已停止状态进入，原 STOP 步骤保持幂等。第一次准备写入会使历史记录失效，但
所需原值已进入同一个独立冻结 checkpoint，后续失败/重放不能重新采集受影响的
driver 输出。来源仍为原 physical generation，原 storage、功率、信道和凭据
保留。运行中来源继续原 live getter 路径。

## Runtime helper 顺序

内部 executor 在取得排他 token、释放旧 lease 后读取原生 STOP 资格；满足时先
退休可能残留的 AP/Station helper，再准备旧 generation 的临时 Station helper。
这样 `wifi.stop()` 已释放全部 helper 的情况下，单频捕获所需的临时 START 也有
正确的 netif/handler。完成捕获和最后 STOP 后按原顺序退休临时 helper、物理
rebuild、准备新 helper、replay 和 resume。没有在运行 driver 上套用停止态 helper
准备入口。

提前准备失败仍交给原中央清理，不进入 checkpoint 或 physical rebuild；部分
helper storage 保留至清理后缀完成。原有 runtime/公共 stop 清理 token 和冻结
秘密的顺序不变，未增加 JS Future、长期 allocation 或另一份快照。

## 当前范围与验证

这一分支只支持有合格 STOP 观察且没有后续被跟踪写入的 stopped driver。停机后
setMode/setStorage/其他写入导致记录失效、从未 START 的已有配置、历史观察
失败等仍拒绝，不自动 START 猜测旧功率，也不声称完整 stopped restart 已完成。
冷初始化无旧 driver 的既有路径保持独立。公开 restart 的准入、完整恢复及注册
继续待完成，其他 Wi-Fi API 范围仍见[剩余清单](2026-09-08-wifi-api-remaining.md)。

C5 immutable Context `build/wireless-contexts/c5` 构建 exit 0，日志
`build/w07-restart-stopped-c5-build.txt`。新分支在生产 object 编译，因公开入口
尚未调用，捕获/runtime executor 仍从最终 ELF 移除。binary 保持 2,780,800 bytes；
STOP 记录 20 bytes、完整 checkpoint 688 bytes、control 40 bytes，十项静态
账本不变。没有实机 heap/碎片比较。

新增 deferred `test_wifi_restart_stopped.py` 使用真实捕获/重放与真实 mutation
header，注入 SDK/锁/存储；编写合格 STOP、不可用的停机 getter 不覆盖原值、
各捕获读取失败、OOM、写入/identity 失效、非法 band mode、C5 两种单频到 AUTO
捕获及原冻结值跨重放保留的用例。共享 config fixture 接入真实 STOP observation
helpers，并更新无历史观察时的早期拒绝结果；runtime fixture 增加停机来源的
旧 helper 准备顺序和失败清理。物理重建仍为该隔离 fixture 的 SDK 边界，不能
代替生产 shutdown/init、netif 事件或 RF 的独立验证。

四份 fixture 仅 AST，未导入、编译或执行。manifest 49 classes/469 functions、
features 27、live SDK schema 35 STA/21 AP、MQuickJS 61 sources/53 snippets、
strict TypeScript、SDK map 和 whitespace 通过。源 hash、对象/链接边界和日志见
`build/w07-restart-stopped-evidence.json`。

Host/Python/VM/竞争、完整 restart、C3/S3/disabled、实机/RF/共存均 **not-run**。
全部 Wi-Fi API 完成后统一阶段测试和实机功能验证，长 soak 留到 BLE API 完成后。
未刷写、串口操作、擦除 workspace、构建前端、提交、推送或更新根 gitlink。
