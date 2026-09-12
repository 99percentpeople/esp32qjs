# W-07 显式 PMF 控制与前值恢复

firmware `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批新增 `wifi.driver.disablePmf(interface): void`，不新增格式版本或参数别名。

## 固定 SDK 依据

`components/esp_wifi/include/esp_wifi.h` 要求专用 disable API 在 set_config 后、
START 前调用，并限制 WPA3/OWE 与兼容模式。`pmf_cfg.capable` 已弃用，输入 false
不能等价表达实际 PMF disable。C5 `ieee80211_api.o` 将调用交给 ioctl；
`ieee80211_ioctl.o::wifi_disable_pmf_config_process` 按接口分别清除两个配置标志，
调用 wifi_nvs_set / wifi_nvs_commit，任一步可能返回错误。不能将失败当作未修改，
也不能把它声称为不涉及持久性。该反汇编不证明其他 target 的实现或真实 RF 行为。

原始证据保存为 `build/w07-pmf-sdk-disable-disassembly.txt` 和
`build/w07-pmf-sdk-wifi_disable_pmf_config_process.txt`，对象与 SDK archive 的一致性
记入 `build/w07-pmf-evidence.json`。生产代码只调用公开 API，不引用私有符号。

## 实现契约

绑定要求恰好一个 station/access-point 字符串，拒绝 NUL 后缀、别名、coercion 和
多余参数。成功返回 undefined，没有 SDK 成功后的结果分配；错误复用
WIFI_DRIVER_WRITE_FAILED 与原始 stage/espCode/interface。

Radio 要求初始化完成、storage 已知、完全停止且零 owner，无 lifecycle/operation/
wake/promiscuous/临时 rate/故障/cleanup/restart-required。AP 由 SoftAP gate 限制。
整个读取、安全检查、写入和读回沿用 mutation mutex，不在临界区调用 SDK。

先读所选接口的完整 SDK config，允许已审查的 OPEN/WEP/WPA/WPA2 personal、
WPA/WPA2 mixed、WPA/WPA2 enterprise family；Station 必须禁用 WPA3 compatible，
AP 必须未启用 compatible。WPA3、WPA2/WPA3 transitions、OWE、DPP、WAPI 和未知/
reserved 编码全部在 SDK 写入前拒绝。这不构成高级认证或 credential owner 的实现。
调用本身是显式关闭请求，可清除符合条件配置的 required；不偷偷修改 auth/transition
来让请求成功，也不影响别的接口。

符合条件且两个标志都已关闭时只读返回。否则调用专用 API 后读回完整 config，
要求除 capable/required 均为 false 外其余语义字段完全不变。两份临时凭据结构
在所有退出路径 secure-zero。阶段为 pmf-admission/snapshot/security/write/readback。
写入或读回失败保留 pmf-write/readback fault，阻止继续启动；不自动重试、不猜测
回滚。FLASH 下记录 persistentMutationPossible，失败不代表 NVS 未变化。

## 恢复路径

后续显式 set_config 可以重新启用 PMF，disable 不是覆盖未来配置的永久开关。
内部 restart replay 与复合 configure rollback 则需要保留 SDK 已观察到的前值。
两条路径均在 set_config 后复用 `wifi_radio_restore_disabled_pmf()`：若原值两个
标志均 false，读取当前标志，只有 SDK 重新启用后才调用专用 disable；随后仍执行
完整 config readback。普通新输入不进入此“保留前值”路径。

恢复沿用既有 RAM storage、冻结原配置和故障后缀；没有增加常驻 PMF 账本，没有
把 NVS 恢复当作已证实。helper 的临时读回结构也 secure-zero。复合 rollback 单独
记录 rollback-station-pmf / rollback-ap-pmf 错误，失败仍保留 cleanup。

connect/AP 参数中的 `pmf: "disabled"` 及完整公开 restart 仍待集成；本批只完成
独立 Driver 操作和它必需的已有配置恢复边界，不提升 feature 稳定等级。

## 验证边界

C5 immutable Context `build/wireless-contexts/c5`（8 MB/no PSRAM）生产构建 exit 0，
日志 `build/w07-pmf-c5-build.txt`。binary 2,770,208 bytes，比前批增加 2,272 bytes；
常驻账本、符号和输入 hash 见 `build/w07-pmf-evidence.json`。首次构建后改进恢复
helper：若 SDK 本身已保留 disabled，则不做多余安全变更；已完成最新源码重建。

新增 deferred `test_wifi_driver_pmf.py`，提取真实 Radio/绑定/错误/PMF 恢复函数，
覆盖所有 SDK auth 编码、compatible gate、逐状态/owner 准入、SDK 部分修改后失败、
完整读回不匹配、重复 disable、其他接口不变、FLASH 不确定、严格输入及 JS GC/OOM。
`test_wifi_restart_configs.py` 补实际 checkpoint/replay 中 SDK 重新启用 PMF 的恢复；
`test_wifi_config_controls.py` 补实际事务 rollback 的同类恢复。仅 AST 解析，没有
导入、编译或运行这些 fixture，不能将已编写用例计为通过。

manifest、feature/schema、MQuickJS 语法、strict TypeScript、SDK map 和 whitespace
检查记录在 evidence。Host/Python/VM/故障注入、C3/S3/feature-disabled、完整重启、
RF/共存与实机均 not-run。全部 Wi-Fi API 完成后集中阶段和实机功能测试；长时间
soak 放到 BLE API 完成后。未刷写、串口操作、擦除 workspace、前端构建、提交、
推送或更新根 gitlink；完整 Wi-Fi 剩余范围继续保持。
