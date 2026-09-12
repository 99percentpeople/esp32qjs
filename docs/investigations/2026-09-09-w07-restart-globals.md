# W-07 restart 全局设置 checkpoint

基于 firmware `d7db8d1` 的工作区增量，固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接 [Station/AP checkpoint](2026-09-09-w07-restart-configs.md)，本批仅扩展内部恢复路径。
公开 `wifi.driver.restart()` 尚未注册，完整 Wi-Fi API、阶段测试和实机功能验收仍待完成。

## 已实现

单份精确 lifecycle token 所属的 checkpoint 增加 country、Station/AP MAC、power-save
和 event mask。捕获 SDK 当前值并验证枚举、国家结构和 MAC；未知或控制事件 mask 位拒绝
捕获。任何捕获失败仍将整个含凭据的分配 secure-zero/free，不保留部分结果。

恢复在 RAM storage、driver stopped 下先将 mode 设为 NULL 并读回，随后写入/读回国家
信息、event mask 和 MAC，再启用配置接口、恢复 Station/AP 配置、省电模式和最终 mode/storage。

保存的 Station/AP MAC 可能与重新初始化后的默认地址对调。AP 编译启用时，先选择一个与
当前地址及两个目标地址均不冲突的本地单播 Station 地址并读回，再恢复 AP 和 Station。
临时地址使用期间接口保持禁用，成功覆盖并读回后才允许后续 start。NAN 编译启用时还检查
其当前地址冲突，不修改 NAN；本批没有 NAN-enabled 构建或运行证据。

成功的 SDK 步骤才推进恢复游标，失败保留原始 checkpoint 和 lifecycle token。新物理
Radio generation 重试从头重放，绝不捕获失败后的部分值覆盖原始意图。start 后再次验证
全部全局值及接口配置，验证成功才发布 owner；不一致保留故障和存储供显式清理。
SDK 调用均在 Radio mutation mutex 内、IRQ critical section 外执行。

国家结构中的 max_tx_power 是 SDK 只读观察值，本批不将它作为 TX-power 恢复。
AUTO 策略下已关联 AP 学得的 country 与应用原始 fallback 仍需在公开协调器中区分；
不能把当前 getter 快照称作完整监管意图恢复。完整 PHY/band、TX power、速率、interval、
其他控制和 runtime helper 协调仍待接入。

## 实际证据

C5 immutable Build Context `build/wireless-contexts/c5`（8 MB/no PSRAM）生产构建完成，
日志 `build/w07-restart-globals-c5-build.txt`。binary 2,760,592 bytes，比前批增加 880 bytes。
ELF getter 反汇编显示 checkpoint 为 596 bytes（前批 560），固定控制结构仍为 28 bytes；
其余记录的 Radio/ESP-NOW/Raw TX/policy/interval 静态对象大小不变。这是编译大小，
没有测量预热静止态 heap、largest block 或运行时回收。

capture/replay、临时 MAC helper、内部 restart 在生产对象中编译，但没有公开入口，
当前 ELF 未链接它们；最终 globals/config verification、discard、字节诊断已链接。

待执行 fixture 使用实际生产 capture/replay/readback/semantic comparison/secure-zero，
只注入 SDK、heap 和锁边界。新增全局 getter 失败、非法 country/PS/mask/MAC、交换 MAC
恢复、每个恢复 SDK 调用失败、最终各全局值不一致和含凭据分配清除用例；同时更新
country getter fixture 对生产 validator 的依赖。本批仅 AST 检查，没有导入/编译/执行 fixture。

API manifest（49 classes/464 functions）、feature 文档（27）、config schema（STA 35/AP 21，
live SDK header）、MQuickJS syntax（61 sources/53 snippets）、strict TypeScript 和 SDK map
一致性检查通过。证据汇总 `build/w07-restart-globals-evidence.json`。

Host/Python/VM/GC/OOM/竞争、完整 restart 执行、C3/S3/feature-disabled、NAN/coex-enabled、
实机、RF/共存与 soak 均 **not-run**。未刷写、串口操作、擦除 workspace、构建前端、提交、
推送或更新根 gitlink。全部 Wi-Fi API 完成后集中阶段测试和实机功能测试；长 soak 留到 BLE API 完成后。
