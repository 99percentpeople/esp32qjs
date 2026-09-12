# W-07 connectionless interval 的 restart 恢复

firmware `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接 [TX power 恢复](2026-09-09-w07-restart-tx-power.md)，完整 Wi-Fi 与实机功能测试继续待完成。

## 实现

interval core 新增无分配的 capture/replay。capture 只接受当前物理 generation 的 known、
非 uncertain 值，拒绝 ESP-NOW 活跃借用及 restore-pending；输出保存原 generation、revision
和 uint16 毫秒值。显式默认模式 0 与非零值一样来自真实成功写入，不推断 SDK 默认。
失败输出清零，且必须为下一次 init 的基准写入和一次 replay 保留两个 revision。

该快照纳入已有含凭据 checkpoint，在任何捕获 SDK getter 之前验证；拒绝时安全清除
整个分配。后续重试保留原始快照，不因失败写入产生的 uncertain 状态重新捕获。
每次内部 restart 重试在 shutdown 前再次检查 revision 容量，耗尽时保留故障证据和快照。

成功物理 deinit 仍通过原 invalidate 清除本代知识并保留 revision；init 仍执行真实的
默认基准写入。停止状态配置 replay 随后通过同一个 interval writer 恢复冻结值；必须
先有新 generation 的成功基准，不能把单纯修改 generation 当作恢复。成功才推进配置
游标并保存新 revision；SDK 错误保留 snapshot/token 并使 Radio fault。

owner 发布前核对当前 ledger 的 generation、成功 revision、值、known/uncertain 和
借用状态。即使值相同，未经该次 replay 接受的另一次 revision 也不能通过验证。
SDK 没有 interval getter，这不是硬件读回或 RF/共存时序证明。

ESP-NOW 原有精确 token、首次 previous 值、逐次 update 与 release/close 后缀保持同一套
interval core。restart 不夺取 ESP-NOW 的归还义务；必须先完成其关闭和 interval restore。
完整 PHY/band、TX rate、停机前 TX-power 捕获/public restart 协调和高级 API 继续待完成。

## 验证范围

C5 immutable Context `build/wireless-contexts/c5`（8 MB/no PSRAM）构建 exit 0；日志
`build/w07-restart-interval-c5-build.txt`。binary 2,761,472 bytes，比前批增加 144 bytes。
checkpoint getter 反汇编为 608 bytes（前批 596），固定控制结构 32 bytes（前批 28）。
其余记录的 Radio/ESP-NOW/Raw TX/policy/interval 静态对象大小不变，未测 live heap/回收。

capture/replay 和 interval core 新方法均在生产对象中编译，当前 ELF 仍因公开 restart
尚未接线而删除；最终 checkpoint 验证与诊断已链接。没有新增 callable JS 方法。

deferred fixture 使用真实 interval core 和真实 Radio checkpoint 实现：零/非零值、未知/
借用/错误 generation 拒绝、SDK 失败后保留原意图、成功新代基准前禁止重放、revision
边界、最终 revision 不一致、重试前容量检查和安全清除。仅 AST 检查，没有导入、编译或执行。

manifest 49 classes/464 functions、feature 27、STA/AP schema 35/21（live SDK header）、
MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map 和 whitespace 检查通过。
汇总 `build/w07-restart-interval-evidence.json`。

Host/Python/VM/GC/OOM/竞争、完整 restart、C3/S3/feature-disabled、NAN/coex-enabled、实机、
RF/共存、soak 均 not-run。未刷写、串口操作、擦除 workspace、前端构建、提交、推送或
更新根 gitlink。全部 Wi-Fi API 完成后集中阶段测试和实机功能测试；长 soak 留到 BLE API 完成后。
