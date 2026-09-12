# W-04：AP 临时速率的原生生命周期交接

基于 firmware `d7db8d1` 工作区，SDK 保持 `fff9895c82`。在共用速率事务之后，
本批编码 AP 启动/关闭的 Radio 边界。**尚未接入公开 Session 或 AP runtime
coordinator**，没有放开 AP rate capability，也不代表 AP 临时速率已可用。

## 已编码的生产阶段

内部头 `esp32_mquickjs_wifi_raw_tx_ap.h` 的三个入口由生产 Radio 实现：

1. begin 要求 initialized、configured、健康 fully-stopped AP-only、零 owner，
   没有其他 lifecycle/operation、借用策略或 restart checkpoint。复用生产
   borrow_admission 验证 AP 速率前值，再原子取得普通 lifecycle；不写 SDK，
   不初始化或隐式停止其他接口。APSTA 协调继续保留为后续范围。
2. 调用方随后用已有 copy_stopped_ap_configuration 取得安全临时配置，完成
   AP helper 准备。start 在同 token 下重新验证配置、法规信道、原生 mode 和
   完整语义字段；在分配 Raw TX owner 前完成所有检查及临时内存分配。
   然后发布精确 AP rate lease、写入速率、启动并等待现有 START/event 边界，
   核对 mode 和实际信道；没有改写 AP 配置或直接切换现存 Station/APSTA。
3. quiesce 要求原 SDK packet 已退休、Session worker 排他和精确 AP Raw lease。
   执行已有 STOP/event fence 与速率恢复；同一个 Radio mutex 覆盖恢复完成、
   原 lease 释放和新 lifecycle 取得。AP helper 因而可以在持续排他下退休，
   最后由调用方 finish_lifecycle(token,false)，不需要 deinit 或重新初始化。

没有新增静态 owner 表、第二份持久凭据、JS/runtime 指针或另一套 STOP 状态机。
所有速率错误/回滚/重试仍使用共用生产事务和原始诊断。

## 部分失败的所有权

- 参数、分配、AP 快照或 rate admission 失败保留原 lifecycle，未交出新 owner。
- 已分配 Raw lease 后，若借用写入失败但回滚成功，临时记录已移除，仍保留
  原 lifecycle 与 Raw lease。quiesce 在该 token 内释放 lease、确认停止，后续
  退休 helper；不能按失败返回值丢掉输出。
- 有有效临时记录时，无论 START 成功、失败或回读失败，均把排他责任从
  lifecycle 转给真实 rate owner，并清空 caller token。临时记录拒绝新 Radio/
  wake owner，普通 release 不能释放它。关闭恢复完毕后再原子交换回 lifecycle。
- 关闭开始前检查新 lifecycle identity 容量；耗尽不先 STOP/恢复/释放 owner。
  packet pin、外来 owner、旧 token、失败 STOP fence 或失败恢复均保留责任。
  成功后再次 quiesce 不重复已完成 STOP 或速率写入。
- 启动临时 AP 配置副本在所有返回路径 secure-zero 后释放；错误不包含凭据。

## 后续 runtime/Session 接入要求

这些入口必须从 AP helper 协调器调用，不能让普通 Raw TX 后台 worker 直接绕过
netif 准备。Session 的打开失败、取消、GC 和 runtime teardown 都必须保存上述
两种输出形态，保留原生 Session 引用直至 helper 退休和 lifecycle 完成。

成功打开后，发送仍走现有 broker/arbiter。正常 close 先退休 packet，再调用
专用 quiesce；不能提前走旧 release_and_stop_idle 丢失保护 AP helper 的 token。
若显式 Raw TX physical recovery 已准入并完成 deinit，中央恢复负责 AP helper
退休与配置重放；原 Session 应消费 nativeTerminated 并释放原 lease，不能再启动
第二个 AP rate 关闭事务。此分支尚待接入，不能声称运行时恢复已覆盖新增 AP 路径。

## 证据与限制

新增 deferred `test_wifi_raw_tx_ap_rate.py` 组合生产 registry、rate、STOP、普通
lifecycle、AP 快照语义比较和三个新阶段；SDK/heap/helper 前置验证结果为注入
边界。准备覆盖正常交接、其他 owner、packet pin、identity 耗尽、配置变化、OOM、
START 失败、写入/回滚失败、关闭后缀和旧 token。也补齐已有 rate fixture 的
connectionless owner 边界。仅 AST 解析，未 import、编译或执行 fixture。

普通 C5 2,847,488 bytes；FTM-enabled C5 2,880,784 bytes；既有 immutable Context
构建成功。三个原生入口编译在 Radio object 中，尚无 runtime 消费者，**未链接到
最终 ELF**。最终二进制尺寸和已跟踪静态账本尺寸不变。manifest 51/493、features
27、STA/AP schema 35/21、严格 TypeScript、SDK map、MQuickJS 61/56 与 whitespace
检查见 `build/w04-ap-rate-native-evidence.json`。

Host/Python/VM、C3/S3/full gate matrix、实机/RF/GC/队列/runtime restart、内存比较
均 not-run；Wi-Fi API 完成后集中验证，长 soak 延后到 BLE API 完成。不提升稳定
等级。没有刷写、串口、擦 workspace、提交、推送、父 gitlink/SDK 修改或前端构建。

后续：[AP-only 公开 Session/runtime helper](2026-09-10-w04-ap-rate-session.md)已接入；上述本批状态与尺寸为历史证据，APSTA 和集中运行验收仍待完成。
