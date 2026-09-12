# W-07 无 getter 策略记录与 restart 前提

本批是 firmware HEAD `d7db8d1` 上的工作区增量，SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`。前置为
[策略 setter](2026-09-09-w07-policy-controls.md)及
[共享 interval](2026-09-09-w07-interval-integration.md)。目标仍是全部 Wi-Fi API 与最后实机功能验收。

## 现状与实现

现有 `esp32_mquickjs_wifi_radio_restart_lifecycle` 是原生 shutdown/init/mode/start 路径，
尚无完整 driver 配置快照与重放，不作为已实现公开 `wifi.driver.restart()` 的证据。
原动态 CS、Station/AP 11b、共存 power setter 没有 public SDK getter，也没有
保存其成功写入值。直接依赖 SDK 默认值会丢失显式配置。本批补足 W-07 的 shadow revision 前提。

新增 `wifi_policy` 原生模块，四个固定记录槽、一个 boot 内非回绕 revision。每次 SDK
尝试前记录 generation/revision/requested 并置为 unknown/uncertain。成功保存
accepted revision/value；失败保留最近接受值作为历史，不能继续呈现为当前值。
严格输入/feature/owner 准入失败不消耗 revision；revision 耗尽在 SDK 之前拒绝。

生产 Radio writer 是唯一 SDK 调用边界，继续持 mutation mutex，SDK 不进入 IRQ
critical section。原 setter 的 started/stopped、精确 owner、SoftAP/coex gate 和
失败 Radio fault 行为保留。成功物理 deinit 后清除 live known/uncertain；失败 deinit
不清记录。generation 改变而旧记录仍 live 时，原生 ledger 拒绝写入。

现有 `wifi.status().radio.policies` 增加独立 mutex 快照，源类型与 API 文档同步。
转换中父/子对象分别 rooting，逐项异常回收；没有 native/JS 指针驻留。四个状态槽
不表示四项 capability 都可用。accepted value 是框架接受历史，不能从 known 推断
跨 stop/start、mode/protocol 修改后的实际 RF 策略；这一问题由后续 restart 重放逐项证明。

## 验证与未完成项

C5 immutable Build Context `build/wireless-contexts/c5`，8 MB/no PSRAM 生产编译通过，
日志 `build/w07-policy-record-c5-build.txt`。新增核心已在生产 setter/deinit 路径调用。
最终校验、静态存储和 hash 见 `build/w07-policy-record-evidence.json`。

`test_wifi_policy_record.py` 调用真实核心，覆盖初始 unknown 与已接受 false 的区别、
SDK 先变更后失败、历史保留、跨 generation 拒绝、物理失效和 revision 耗尽。
`test_wifi_driver_policy.py` 接入真实 ledger/writer/admission，仍保留 coex/AP gate fixtures；
`test_wireless_status_gc.py` 接入真实新 converter，补 null 与历史 false 的检查。
本批只做 AST 检查，不导入、编译或执行以上测试。

上批 Radio baseline stage 20 的 Host fixture 需要显式启用 ESP-NOW 编译分支，本批
在该测试 target 的定义中补齐。未执行该 target，不声称 stage 20 故障已复现。

完整 Host/VM/GC/OOM/竞争、C3/S3/feature-disabled、coex-enabled 实际 SDK 构建、
实机 RF/关闭重开/队列/运行时重启均 **not-run**。C5 gate-off 编译不证明 coex-enabled 路径。
未刷写、串口操作、workspace 擦除、前端构建、提交、推送或更新根 gitlink。

后续继续完整配置快照（包含敏感 config 的受控原生存储）、pre-start/post-start 重放、
失败后缀和公开 restart；不能将本批 ledger 当作重放完成。Wi-Fi API 完成后集中测试和
实机功能回归，BLE API 完成后再做长时间 soak。
