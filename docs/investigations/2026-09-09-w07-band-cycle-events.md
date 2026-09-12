# W-07 band setter 内部 STOP/START 的事件屏障

firmware `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接 [band preparation](2026-09-09-w07-band-prepare.md)。原定继续单频捕获时，通过 SDK
实现检查发现需要先补内部重启事件隔离；捕获端及完整 Wi-Fi 目标仍未完成。

## 证据与修正原因

固定 C5 `libnet80211.a/ieee80211_ioctl.o` 的 `wifi_set_band_mode_process` 在未关联
Station 分支调用 `_do_wifi_stop`、scan detach、`chm_init`、scan attach 和
`_do_wifi_start`；后两层分别调用 station stop/start。该 setter 包含原生重启及信道
管理重建，不是单个 scalar 赋值。仅凭 AUTO getter 成功，不能认定这些事件已排空。

这是固定 SDK 的静态调用路径证据。尚未实机复现先前 preparation 与内部事件重叠，
也未运行 Host 注入用例；不能把本批写成已复现的硬件根因或 RF 验收通过。

## 已实现

Radio 增加内部 RESTART 事件阶段，复用现有 identity、revision、live mask 和队列
marker。每个预期接口必须先出现 STOP，后出现 START；单独 START 不计完成，重复 STOP
清除该接口此前的 START 证据。全部接口恢复 live 后，还必须通过匹配 identity/revision
的 marker。没有新增常驻队列、JS root、轮询状态机或回绕 identity。

新 driver 的 Station/AUTO preparation 在调用 band setter 之前建立此阶段，并将
Radio 标为 starting；读回 AUTO 后等待内部 STOP/START/marker，成功才恢复 started
并发起外层 STOP。旧 marker、单独/迟到 START、第二次 STOP、队列满、取消或超时都
不能使该步骤虚报完成。失败继续由外层保留原 snapshot/token/fault，重放没有绕过
原来的显式物理恢复边界。

`wifi.status().radio.eventPhase` 增加 `restart`，新增 `eventStoppedMask` 表示当前
stop/restart 阶段的 STOP 证据。`eventSeenMask` 仍只暴露 Station/AP 低位 mask，内部
打包的 STOP 位不泄露到原 mask 契约。源类型、API 文档和 GC fixture 同步更新。

本阶段专用于重建后无关联 Station 的 band mode 变更；不把它泛化成已关联/AP 的
普通 band setter 完成保证。捕获端单频隐藏 PHY、部分快照寿命、原 band/home channel
及公开 restart/helper 协调继续待完成。SDK 内部 `chm_init` 也要求这些原值在准备前冻结。

## 验证范围

C5 immutable Context `build/wireless-contexts/c5`（8 MB/no PSRAM）最终构建 exit 0：
`build/w07-band-cycle-c5-build.txt`。binary 2,762,816 bytes，比前批增加 480；记录的
Radio/ESP-NOW/Raw TX/policy/interval/control 静态大小不变。checkpoint 仍为 664 bytes。
未测 live heap、largest block 或运行回收。

事件 observer/wait 和 status 已链接；band preparation、capture/replay/内部 restart
仍只在生产对象中编译，未被当前 ELF 引用。没有新增 callable restart API。

新增 deferred `test_wifi_band_cycle_events.py` 提取实际 begin/observe/wait/fence 实现，
仅注入 SDK event queue、调度、时钟和 storage。准备覆盖 STOP→START 顺序、APSTA 两
接口、迟到 STOP 撤销旧 marker、队列饱和后只重发 marker、旧 identity、取消、时钟
回绕和 identity 耗尽。checkpoint fixture 额外要求内部 cycle 完成才允许外层 STOP；
实际 status converter 的移动 GC/OOM fixture 增加新状态字段断言。三个文件仅 AST
检查，没有导入、编译或执行。

manifest 49 classes/464 functions、feature 27、STA/AP schema 35/21（live SDK header）、
MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map 和 whitespace 检查通过。
汇总 `build/w07-band-cycle-evidence.json`，附固定 SDK 调用路径反汇编及库 hash。

Host/Python/VM/GC/OOM/竞争、完整 restart、C3/S3/feature-disabled、NAN/coex-enabled、实机、
RF/共存、soak 均 not-run。未刷写、串口操作、擦除 workspace、前端构建、提交、推送或
更新根 gitlink。全部 Wi-Fi API 完成后集中阶段测试和实机功能测试；长 soak 留到 BLE API 完成后。
