# W-08 TWT 共享 Radio 恢复

本批接入 `wifi.twt.recover` Candidate 公开入口，复用现有 Action/Raw TX/FTM
恢复协调器。支持健康可读 STARTED 来源上的托管 TWT operation 恢复；未知
submit handoff、sticky tracking fault、已故障/不可读配置来源和 all-flow
suspend/resume 仍未完成。集中运行、实机/RF 验收继续后置。

## 已核对的阶段边界

TWT cancel/quiescent/release 和 native fence 通过 esp_wifi_action_tx_req
进入 Wi-Fi 原生队列。当前 C5 ELF 的该 SDK 函数先检查 wifi_init_completed，
再投递同步 ioctl；没有要求 started。deinit 后无法继续这些原生清理步骤。
因此 TWT 与 Action/FTM 的退休位置不同：必须在 STOP 之后、deinit 之前排空。
此核对是固定 SDK/ELF 静态证据，不是运行或 RF 证明。

probe 的 wake reference 与当前原生 node/phase 相关。新增共享 prepare 阶段
在断连前执行精确 probe cancel，撤销其原生操作权限；取消成功不代替 TX/
timer/event drain。失败保留 driver 与 owner，重试该前缀，不绕过 PM 引用。
普通及取消后的中央清理均调用 prepare，防止 Future 超时后跳过这个顺序。

## 实现

公开参数为 radioGeneration、closeAll:true、allowDisconnect:true、可选
1..60000 ms timeoutMs（默认 10000）。不接收 sequence，因为各 TWT 原生
操作的 sequence 域独立。内部 TWT recovery 用 kind + generation、identity=0
表示明确的整代选择；未为它编造一个原操作身份。

准入在 mutation mutex 下核对实际 TWT owner lease 以及三个 Wi-Fi helper
lease。要求至少一个托管 TWT owner；拒绝其他 Radio owner、wake lock、借用
interval/rate、parked Vendor IE、Enterprise owner、冲突原生操作及非健康
配置来源。lifecycle 冻结新准入，同时标记所有现有 TWT owner closing；保留
原 lease/结果/回调存储，未把所有权移交给 coordinator。

STOP helper 增加显式多 TWT owner 准入参数；普通调用继续要求零 owner 或
既有精确单 owner 例外。checkpoint 同样验证所有 remaining lease，使用既有
不切频段的恢复读取路径；背景清理先释放了全部 TWT 时仍保持恢复捕获语义。
probe 预取消、断连、checkpoint、STOP、helper 退休之后，shutdown 在任意
TWT lease/operation 或信息操作存储仍存活时返回 pending，保留初始化 driver
和 callback routes。原 cleanup worker 完成 timer/native/event/TX 证明并
释放最后 owner 后，才进入既有 deinit、generation 更新及配置重放执行器。

取消、deadline 或失败只结束公开 Future，中央清理继续至安全 shutdown，
不承诺之后重放。配置与凭据仍走既有 checkpoint/secure-zero 机制。成功
返回 previousRadioGeneration/radioGeneration，不重建 Agreement、不重新
协商 TWT，也不宣称 Station 已重连或 AP 客户端保留连接。

probe cleanup 改为 Radio 中精确 token 的请求标记和 value-only handoff。
服务只填空的退休槽；worker 先核对当前 requested token。已经退休的旧 Future
迟到销毁不能覆盖后继 owner；回收中的旧 token 也不能触碰另一个新请求。
恢复关闭标记可使原 probe Future 失败，而不交付之前观察到的成功。

唯一 wifi-twt/1，注册、源类型、manifest、capabilities 与 API 文档同步；
不提升 Candidate。新增常驻 TWT recovery 记录 C5 12 B；复用现有共享 Future
和配置快照，无新原生池。原 Agreement/结果/cleanup 账本预算不变，实机
free/largest-block 与栈水位仍待集中测量。

## 验证与待执行项

C5、C5-no-SoftAP、C5-Wi-Fi-disabled、C3、S3 五种生产 Build Context 构建
通过；manifest、feature docs、schema、SDK coverage、严格 TypeScript、
MQuickJS 语法及 whitespace 检查通过。完整静态产物证据、ELF 路由与文件
哈希记录于 `build/w08-twt-recovery-evidence.json`。

新增/扩展 deferred fixtures 使用生产准入/owner/prepare/phase/finish helper，
覆盖多 owner、其他 lease、stale generation、冻结新准入、失败前缀、probe
精确取消与迟到 Future 销毁。公共 capture/converter fixture 增加双 true 授权、
无 sequence、参数边界和第 N 次分配/GC case。物理 SDK stop/shutdown、
checkpoint 结果和调度是注入边界，不把它们当成生产原生驱动证明。
本阶段仅 fixture AST，未导入、编译或运行。完整 STOP/event 排空、跨模块
竞态、运行恢复与射频测试仍为 not-run。

未刷写、串口操作、提交、推送、前端构建或更新父仓库 gitlink。长 soak
留到 BLE API 完成之后；本批不代表剩余 Wi-Fi 功能或最终目标已完成。
