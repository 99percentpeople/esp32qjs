# W-07 重建 driver 的 band preparation

firmware `d7db8d1` 工作区增量，固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接 [PHY checkpoint](2026-09-09-w07-restart-phy.md)，完成重建端的必要启动/停机准备。
旧 driver 单频隐藏配置捕获和原 band/channel 恢复仍待实现，完整 Wi-Fi 目标未完成。

## 本批实现

SDK public header 要求 setBandMode 在 START 后调用。C5 固定 libnet80211.a 的入口
反汇编也显示初始化/启动状态检查后才进入 ioctl；这只是当前二进制入口证据，不是
完整 SDK task、隐藏状态保持、RF 行为或其他 target 的证明。

内部 checkpoint replay 在 country/MAC/接口配置完成之后、PHY/速率/正式 start 之前
检查 band。已满足完整读写前提时无额外 start；C5 单频则执行临时 Station 准备：
设置 STA mode 并读回，等待 START 事件屏障，设置 AUTO 并读回，完成 STOP 事件屏障，
再次确认 AUTO 保留，再恢复需要配置的接口 mode 并读回，随后继续 PHY 写入。

此 helper 只接受同一精确 lifecycle token、实际新 generation、已初始化的 stopped
状态、零 Radio owners、无 wake/promiscuous/operation 和 fault/cleanup。准备过程中
不分配或发布应用 lease，不启动 saved AP，不调用 connect/scan，不创建另一套计数器。
普通 lease start 与该准备共用抽出的 `wifi_radio_start_stopped_locked`，保留原来的
START 提交、stop_required、事件等待和状态转换。STOP 直接复用原有中央 stop helper，
已有 stop_submitted 和事件后缀逻辑继续负责迟到完成，未新增直接 SDK stop 捷径。

SDK 或事件失败保留原始 snapshot/token，外层记录具体 restart-band-prepare 阶段。
不在已故障状态继续写 PHY；后续显式物理重建从原快照重放。没有承诺临时 START 至
最终 TX-power setter 之间的 RF 上限，也不把没有公开 owner 等同于无线电静默。
公开协调器还必须退休 runtime helpers 与活跃操作，原有 zero-owner 检查不能代替它。

## 仍需完成

旧 driver 的 C5 单频捕获仍明确拒绝，不能把缺少的另一个频段补成默认值。停机前
捕获、带副作用准备的部分快照寿命、原 bandMode/band/home channel、AUTO country
fallback 与公开 restart/helper 协调仍在原目标中。没有新增 callable restart API。

## 证据

C5 immutable Context `build/wireless-contexts/c5`（8 MB/no PSRAM）构建 exit 0：
`build/w07-band-prepare-c5-build.txt`。binary 2,762,336 bytes，比前批增加 32 bytes。
checkpoint getter 仍为 664 bytes，固定 control 40 bytes；已记录其他静态对象大小不变。
未测 live heap、largest block 或回收。

新的 band preparation、capture/replay 与内部 restart 在生产对象中编译，当前 ELF
仍因公开入口未接线而删除；共用 stopped-start helper 和最终 verification 已链接。

待执行 fixture 调用真实 band preparation 和抽出的 START helper；SDK、event/fence、
stop 边界及 heap/锁注入。覆盖重建后的单频准备、临时阶段只启用 STA、零 owner、各
SDK/事件边界失败后原快照保留、新代重放、最后停机再写 PHY，以及原捕获侧隐藏值拒绝。
这里的 injected STOP 不证明原生 STOP 事件竞争；集中测试仍须执行中央 Radio 相关用例。
仅 AST 检查，没有导入、编译或执行 fixture。

manifest 49 classes/464 functions、feature 27、STA/AP schema 35/21（live SDK header）、
MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map 和 whitespace 检查通过。
汇总 `build/w07-band-prepare-evidence.json`。

Host/Python/VM/GC/OOM/竞争、完整 restart、C3/S3/feature-disabled、NAN/coex-enabled、实机、
RF/共存、soak 均 not-run。未刷写、串口操作、擦除 workspace、前端构建、提交、推送或
更新根 gitlink。全部 Wi-Fi API 完成后集中阶段测试和实机功能测试；长 soak 留到 BLE API 完成后。
