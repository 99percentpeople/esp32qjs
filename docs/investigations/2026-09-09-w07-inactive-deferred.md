# W-07 停用接口的延迟阈值恢复

firmware `d7db8d1` 工作区增量，SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`。承接
[同代历史记录](2026-09-09-w07-inactive-history.md)。本批解除“恢复目标仍停用
已保存接口”这一内部拒绝条件；公开 restart、未知来源、停机写入和 RSSI 语义
仍未完成，未新增公开 callable。

## 从冻结快照转交原生意图

动态 checkpoint 仍保存全部已知阈值和 saved mask。START 后仅写入当前目标
模式启用的部分，并进行原有 RAM write/readback 与最终核对。没有启用的部分
在最终核对成功后、释放凭据快照之前，转交到固定原生记录的 pending_values 和
pending mask；不把它标成新 driver 已观察值。原 storage 仍在最终阶段提交。
FLASH 提交失败时保留冻结快照和 pending 意图，阻止 owner 交接。

这样 APSTA→STA 重建可以保留 AP 阈值，而无需额外广播 AP。冻结分配释放后，
pending 意图仍存活。随后再次 restart 时，隐藏项优先冻结 pending_values，
不是从可能失败或默认的 SDK 观察中猜测；普通观察失败只影响 observed 部分，
不能覆盖独立的 pending 意图。

普通全局 STOP 保留同代 pending；成功物理 shutdown/deinit 仍结束当前 Radio
配置并清除记录。本批没有承诺跨任意失败清理、runtime teardown 或冷重启保存
这些 RAM 值，完整故障恢复协调及其公开诊断仍需后续收尾。

## 下一次真正启用时恢复

新增 wifi_radio_restore_inactive_locked，接入三个生产位置：全局 START 的
event barrier 之后、ensure_started 的运行模式扩展/幂等路径、共享 AP reopen
的 mode/channel 核对之后。均在对应 owner 交接前执行，SDK 调用在 Radio
mutation mutex 下，不进入临界区。只有目标启用的 pending 项参与，其他项保持
待恢复；generation、driver、storage、fault/cleanup 准入不满足则拒绝。

每项先读 SDK 值：相同则只核对并完成，不写；不同则在 SDK 调用前记录 attempted，
写一次后读回。成功才清除 pending/attempted 和保存的请求值，更新实际观察。
SDK 写入失败即使已经修改值，也保留原意图和 attempt 标记；之后的恢复后缀
只允许读回，不能盲目再次写入。读回不一致或失败保留 fault 与 pending；故障
准入拒绝不会覆盖原始 configuration 诊断，也不发布成功 owner。

恢复使用本次激活时的 storage，不临时切换共享全局策略。在 RAM 下不尝试 NVS；
FLASH 下需要写入时，SDK 可能持久化阈值，已有 persistentMutationPossible
会记录。独立 persistent 位保留该次写入的存储事实，不能因后续 storage 变化
而将已尝试的 FLASH 写入改报为 RAM-only。只读即可匹配时不报写入。此机制不
宣称 NVS 恢复或 RF 计时器连续性，START 到阈值写入之间仍可能使用 SDK 初值。

新的显式 setInactiveTime 成功读回会替换同一接口的 pending 意图；普通 getter
或失败观察没有这种取消权限。没有新增队列、JS root、heap owner 或无界账本。

## 检查与边界

C5 immutable Context `build/wireless-contexts/c5` 构建 exit 0。binary
2,783,568 bytes，比前批增加 1,376 bytes。原生历史/待恢复记录 16→24 bytes；
STOP 历史仍 24 bytes、动态 checkpoint 696 bytes、control 40 bytes，其余静态
账本不变。新恢复 helper 链接到最终 ELF；内部 restart 捕获尚无公开绑定。
没有实机 heap/碎片或长期内存测量。

新增 deferred test_wifi_inactive_deferred.py，连接生产冻结转交、post-start、
commit、restore helper 与真实 START helper。编写快照释放后保留、连续 restart、
已匹配值不写、逐 SDK 步骤失败、写入后错误、禁止重复提交、保留 FLASH 尝试事实、
旧 generation 拒绝等用例。显式清除 fault 的单元探针只隔离后缀语义，不代表
已有公开 recovery。SDK、物理状态、锁和 native storage 仍为注入边界，不能
替代 AP reopen/netif/事件并发与 RF 证明。

共享 config fixture 接入真实恢复 helper，并让 SDK inactive-time mock 根据
原生 mode 判断接口是否启用，避免把尚未发布的框架 mode 当作 SDK mode。旧
历史/活跃用例改为保留禁用项；四份 fixture 仅 AST，未导入、编译或执行。

manifest 49 classes/469 functions、features 27、live SDK schema 35 STA/21 AP、
MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map 与 whitespace 通过。
构建日志、源码与产物 hash、静态/DWARF 账本在
`build/w07-inactive-deferred-evidence.json`。

Host/Python/VM/竞争、C3/S3/disabled、完整 restart、故障清理、NVS、实机/RF 均
**not-run**。按安排 Wi-Fi API 完成后统一阶段和实机功能验证；长 soak 留到 BLE
API 完成。未刷写、串口操作、擦 workspace、构建前端、提交、推送或更新根 gitlink。
