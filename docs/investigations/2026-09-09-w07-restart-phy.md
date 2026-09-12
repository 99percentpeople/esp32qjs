# W-07 可完整读取的 PHY checkpoint 与恢复

firmware `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接 [TX rate 恢复](2026-09-09-w07-restart-rate.md)。本批完成 PHY 恢复主体，完整 band
生命周期协调和公开 restart 仍待实现，不将下述临时准入限制当作最终功能范围。

## 固定 SDK 限制

`esp_wifi_get_protocols/get_bandwidths` 在单频段模式下不返回另一频段的配置；0 不是
那个频段的默认配置。`esp_wifi_set_band_mode` 公开契约要求 driver 已启动，因此不能
在 stopped replay 中直接调用它并声称完整恢复。关联/扫描/AP 广播及 home channel
也不能由普通 getter 或一个框架 mutex 假定已经隔离。

后续需要在独占生命周期中完成停机前捕获、必要的 band preparation、对应 START/STOP
事件屏障和原 band/channel 的恢复。该工作仍属于原目标，不能把单频模式标成永久
unsupported，或将默认新 driver band 与旧 band 相等当作保证。

## 本批原生实现

已有 checkpoint 增加 Station/AP 的 protocols/bandwidths，使用当前生产 PHY getter、
validator、语义比较与 writer。2.4 GHz target 捕获全部本地频段；C5 当前要求 AUTO，
使 2.4/5 GHz 都能被读取。C5 单频模式在 deinit 前明确拒绝，不保存不完整快照。

捕获验证协议位、带宽枚举及 HT40 与 AC/AX 组合；SDK 错误和非法返回值安全清除部分
含凭据分配。恢复在接口配置之后、TX rate 和 start 之前进行；先检查新 driver 的 band
是否仍满足完整读写前提，失败保留 checkpoint/token/Radio fault，不静默恢复半份配置。

每个接口复用现有 writer：先收窄至 20 MHz，再写 protocols，最后写目标 bandwidth，
随后读回并比较。setter 使用独立副本，不能更改冻结值。接口恢复成功才推进阶段；
故障后的物理重建从头重放全部步骤。最后 START/其他控制之后再次读回全部 PHY 配置，
完全一致才进入 owner 发布。比较不涉及 SDK reserved/padding。

C5 单频隐藏配置、bandMode/band/home channel 恢复、AUTO country fallback、停机前
TX-power 捕获与公开协调器仍未完成。当前没有 callable `wifi.driver.restart()`。

## 证据

C5 immutable Context `build/wireless-contexts/c5`（8 MB/no PSRAM）生产构建 exit 0，
日志 `build/w07-restart-phy-c5-build.txt`。binary 2,762,304 bytes，比前批增加 464 bytes。
checkpoint getter 反汇编为 664 bytes（前批 640），固定控制结构仍为 40 bytes；其余
记录的 Radio/ESP-NOW/Raw TX/policy/interval 静态对象大小不变。未测 live heap 或回收。

PHY read/compare helper 已链接；capture/replay/内部 restart 在生产对象中编译，当前
仍因公开入口未接线而被 ELF 删除。构建不证明 C5 当前 driver 默认 band 为 AUTO。

待执行 checkpoint fixture 调用实际 production getter/validator/writer/恢复函数，注入
SDK/heap/锁，新增完整字段捕获/读回、逐 SDK 失败、setter 修改输入、HT40 恢复顺序、
非法返回、最终 PHY 变化、C5 隐藏频段和重建后 band 不匹配拒绝。捕获调用数量由一次
生产路径执行确定，避免增加 getter 后遗漏故障注入点。本批仅 AST 检查，未执行 fixture。

manifest 49 classes/464 functions、feature 27、STA/AP schema 35/21（live SDK header）、
MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map 和 whitespace 检查通过。
汇总 `build/w07-restart-phy-evidence.json`。

Host/Python/VM/GC/OOM/竞争、完整 restart、C3/S3/feature-disabled、NAN/coex-enabled、实机、
RF/共存、soak 均 not-run。未刷写、串口操作、擦除 workspace、前端构建、提交、推送或
更新根 gitlink。全部 Wi-Fi API 完成后集中阶段测试和实机功能测试；长 soak 留到 BLE API 完成后。
