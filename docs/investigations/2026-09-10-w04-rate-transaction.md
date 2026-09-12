# W-04：STA/AP 共用的启动前速率借用事务

基于 firmware `d7db8d1` 未提交工作区，SDK 保持 `fff9895c82`。
这是 AP 临时速率的前置实现，**不是公开 AP rate 已完成**。

固定 SDK `esp_wifi.h` 要求 `esp_wifi_config_80211_tx` 在 init 后、start 前调用。
当前 Raw TX AP 路径只允许加入已运行的 AP，启动由 AP helper/configuration
transaction 管理。因此不能直接删除 Session/Radio 的 AP rate 拒绝条件；仍需
AP 配置验证、helper 准备、精确生命周期交接以及关闭后的恢复协调。

## 生产实现

`esp32_mquickjs_wifi_tx_rate.c` 新增共用 borrow_admission/borrow/restore，
并由现有 Radio Raw TX acquire 与 STOP 后速率恢复实际调用。

- admission 只读，检查目标接口、有效配置、同代可信前值、非零 write identity、
  空临时记录及本次写入/恢复的 identity 容量；在 Radio owner 分配前完成。
- borrow 在 caller 的 Radio mutex 与已证明的 STOP/排他边界内重新验证，先
  发布精确 owner、接口和永久前值，再调用现有可注入 writer；STA/AP 使用相同
  事务，不复制两套 ledger。SoftAP-disabled gate 在 SDK 写入前拒绝 AP。
- 初次写入失败但前值回滚成功时移除临时记录，仍返回初次错误。回滚也失败时
  保留 lease、前值、最新 write identity、restorePending 和 rollback error。
- restore 要求调用方先完成真实 STOP/event fence；只接受同代精确 owner 和
  最新 write identity，允许恢复本事务导致的 uncertain 记录。失败只保留未完成
  的速率后缀；成功后清空临时记录，Radio 再释放真实 lease。旧 token 不写 SDK，
  不改变另一 owner 的记录。其他生命周期故障仍由原路径保留。
- 没有新静态池、动态分配、JS/runtime 指针、SDK getter 推断或隐式 AP 重启。
  AP runtime/helper 接入仍待实现；公共 capability 保持真实的 station-only。

## 验证边界

新增 `test_wifi_tx_rate_borrow.py`，使用三目标 inventory 真实类型和生产 rate
代码，准备覆盖 STA/AP 与 SoftAP-disabled、前值隔离、失败回滚、保留 owner、
重复恢复、失效 token/interface/write identity、unknown/uncertain 和 identity 耗尽。
扩展生产 Radio fixture，检查不可信前值在分配 owner 前拒绝。只做 AST 解析，
未 import、编译或执行；调度/SDK/GC 运行验证仍后置。

普通 C5 2,847,488 bytes，FTM-enabled C5 2,880,784 bytes，均使用既有 immutable
Build Context 构建成功。三个新 helper 在两个最终 ELF 中实际链接；已跟踪静态
账本尺寸不变，含 rate state 68 bytes、temporary lease 36 bytes。构建/hash、
manifest/schema/features/SDK map、严格 TypeScript 和 MQuickJS 检查记录在
`build/w04-rate-transaction-evidence.json`。

Host/Python/VM、C3/S3/全 gate matrix、实机/RF、GC/队列/runtime restart、堆及
largest block 均 not-run；Wi-Fi API 完成后统一阶段验证，长 soak 留至 BLE API
完成。没有刷写、串口操作、擦 workspace、提交、推送、根 gitlink 更新、SDK
工作区修改或前端构建。完整 Wi-Fi 剩余范围保持不变。

后续：[AP-only 公开 Session/runtime helper](2026-09-10-w04-ap-rate-session.md)已接入；上述本批状态与尺寸为历史证据，APSTA 和集中运行验收仍待完成。
