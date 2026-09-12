# W-07 restart 的启动后 TX power 恢复

firmware `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接[全局 checkpoint](2026-09-09-w07-restart-globals.md)。完整 Wi-Fi 和最终实机功能测试仍未完成。

## 原生行为

已有 driver 的 checkpoint 在 deinit 前通过 `esp_wifi_get_max_tx_power` 捕获精确的
quarter-dBm SDK 观察值，验证 8–80 范围，不从 country.max_tx_power 推算。该 getter
在固定 SDK 中明确要求 START；没有提前 checkpoint 的 stopped driver 会在 deinit 前
返回原始错误，stage 为 `restart-tx-power-snapshot`，并安全清除部分含凭据分配。
公开协调器仍须在停机前捕获，这是当前内部路径的明确限制，不是 stopped restart 已完成。
冷初始化没有旧 driver，仍不制造功率快照。

恢复在 START/mode 验证和无 getter 策略重放之后执行，在 owner 发布之前设置旧功率并
读回。与显式设置新功率允许 SDK 向下量化不同，恢复必须与原 SDK 观察值完全一致；
更低的值同样不能被称作精确恢复。最终配置验证还会再次读取功率，以发现后续变化。

新字段和两步游标保存在已有 checkpoint/control 中。SDK 写成功才推进到读回阶段，
读回成功才完成；任一步失败保留原始快照、token 和 Radio fault，staged lease 不发布。
在故障状态直接重复 post-start helper 被拒绝。下一次物理 generation 重建会重放全部配置
并重置功率游标。清理成功仍 secure-zero/free，失败不释放原始凭据。

同一次 resume 不接受 restart checkpoint 加显式新 TX-power controls；在 start/owner
分配之前拒绝，避免悄悄覆盖调用方请求。普通非 restart 的启动控制保留原有契约。

SDK setter 要求 start 后调用，因此这一实现不承诺 start 到 setter 之间射频发射已受旧
功率上限约束；没有用 staged owner 隔离冒充 RF 静默。完整 PHY/band、速率、interval、
AUTO country fallback 与公开 restart/helper 协调继续待完成。

## 证据

C5 immutable Context `build/wireless-contexts/c5`（8 MB/no PSRAM）构建 exit 0，日志为
`build/w07-restart-tx-power-c5-build.txt`。binary 为 2,761,328 bytes，比前批增加 736 bytes。
checkpoint getter 反汇编仍为 596 bytes，静态 control 仍为 28 bytes（复用原有 padding）；
记录的 Radio/ESP-NOW/Raw TX/policy/interval 对象大小不变。这不是 live heap/回收证明。

post-start 恢复和 final verification 已链接；capture/replay/内部 restart 在生产对象中
编译，当前 ELF 仍因无公开调用而删除。不存在 callable `wifi.driver.restart()`。

已扩展 deferred production checkpoint fixture：真实 getter/恢复/最终校验路径，SDK/锁/
heap 注入，覆盖 stopped snapshot 拒绝、非法功率、set/get 错误、量化不一致、原快照保留、
同代故障禁止重入、新代重放、最终值变化。实际 resume fixture 增加功率恢复错误时 owner
不发布的注入边界。仅 AST 检查，未导入、编译或执行这些 fixtures。

manifest 49 classes/464 functions、feature 27、STA/AP schema 35/21（live SDK header）、
MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map 和 whitespace 检查通过。
汇总为 `build/w07-restart-tx-power-evidence.json`。

Host/Python/VM/GC/OOM/竞争、完整 restart、C3/S3/feature-disabled、NAN/coex-enabled、实机、
RF/共存和 soak 均 not-run。未刷写、串口操作、擦除 workspace、前端构建、提交、推送或
更新根 gitlink。全部 Wi-Fi API 完成后集中阶段测试和实机功能测试；长 soak 留到 BLE API 完成后。
