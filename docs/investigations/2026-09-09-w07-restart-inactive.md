# W-07 活跃接口 inactive time 的 STOP 捕获与 restart 重放

firmware `d7db8d1` 工作区增量，SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`。承接
[SDK 持久化与恢复缺口审计](2026-09-09-w07-inactive-persistence.md)。
本批将原始启用接口的 inactive time 接入内部 restart；隐藏接口历史、RSSI 一次性
通知和公开 restart 仍未完成，全部 Wi-Fi 范围继续按[总清单](2026-09-08-wifi-api-remaining.md)。

后续 [同代隐藏阈值历史与目标重新启用时恢复](2026-09-09-w07-inactive-history.md)
已接入；下文保存本批活跃接口增量的历史证据。

## 捕获与不可用的 getter

固定 C5 SDK 证据见上一批：inactive time 的 getter/setter 要求 START，且对应接口
必须在当前 mode 中启用。新捕获不尝试读取 disabled 接口，也不把错误换成 6/300
默认值。Station/AP/APSTA 的原始启用 mask 与秒数一起冻结。

运行来源在原 mode 下读取启用接口，并在可见 PHY 捕获后再次核对值；任何 SDK
错误、非法范围或不一致，均在本次 driver mutation 前拒绝并擦除/释放局部凭据
快照。源 STA/AP 模式不会为读取另一接口而启动它。临时单频到 AUTO 准备发生在
原阈值已冻结之后，不用临时 Station 的输出替换原 AP 值。

真实全局 STOP 路径在第一次 SDK stop 前追加启用接口阈值观察。所有读取成功才
发布完整 payload；部分失败只留下 generation、event identity、错误和失败步骤，
不阻止 STOP，也不分配 heap。STOP 后的捕获复用同一物理 generation、同 STOP
identity 且未被跟踪写入失效的历史，包括原启用 mask 和阈值。停机 getter 不参与
替换旧值。现有 mutation header 已覆盖 inactive setter，失败写入/回滚也会失效。

当前内部请求若要关闭某个已有阈值观察的接口，提前以
`restart-inactive-mode-admission` 拒绝，不在物理 deinit 后丢掉它。该限制是尚缺
隐藏接口恢复的明确边界，不是完整 mode-changing restart 已实现。没有历史的
从未启动来源、停机后修改来源仍沿用此前拒绝条件。

## START 后恢复与交接

新物理 driver 的配置/PHY 重放仍在 stopped 阶段完成。inactive time 放在真正
START 和信道/功率恢复之后、lease 向 runtime 交接之前：按 STA write/readback、
AP write/readback 推进，跳过未观察接口。SDK 每步成功才推进后缀。storage 此时
必须为 RAM；不能因为 SDK header 写着“不持久化”而提前恢复 FLASH。

SDK 写入失败、读回失败或不匹配保留原冻结快照、当前步骤和 Radio fault。直接
再次调用不能越过故障准入；中央清理与显式新物理尝试继续使用此前冻结值。新
generation 的原有 replay 初始化会重置 start phase，不把旧 driver 的成功阶段
当作新 driver 已完成。最终 acceptance 再读所有已观察阈值，成功后才能提交原
storage 并交接 owner。读回失败时仍为 RAM，不发布成功。

这只证明代码的值恢复边界。SDK 要求启动后才能写入，因此 START 到恢复之间可能
短暂使用 SDK 初始化值；本批不承诺该窗口内的 RF、客户端或计时器连续性。
曾启用后停用的接口值尚未纳入跨 mode 的历史账本，不能将该增量称作完整控制
恢复。RSSI 已消费通知不能通过盲目重写阈值伪装恢复，也仍待处理。

## 验证记录

C5 immutable Context `build/wireless-contexts/c5` 构建 exit 0，日志
`build/w07-restart-inactive-c5-build.txt`。binary 2,781,824 bytes，比前批增加
1,008 bytes。生产 ELF DWARF：STOP 历史 20→24 bytes，动态 checkpoint
688→692 bytes；control 仍 40 bytes，其余九项静态账本不变。没有实机 heap 比较。
公开 restart 尚未绑定；新增内部函数在生产 object 编译，捕获入口尚未链接到最终
ELF。post_start/verify 及 inactive read helper 通过共有 resume 路径链接；真实
STOP 观察代码也已链接。这些链接事实不等于完整 restart 已执行。

新增 `test_wifi_restart_inactive.py` 使用生产 STOP/capture/replay/acceptance，
准备覆盖三目标 AP on/off、运行与 stopped 来源、独立 STA/AP 值、每个捕获/恢复
SDK 失败、写后读回异常、最后读回漂移、RAM-only 恢复和 mode 丢失的提前拒绝。
SDK、物理 rebuild/START、锁和 allocation 为注入边界，不能替代原生生命周期、
NVS/RF/并发证明。共享 config fixture 加入 START/接口约束；同步 STOP 观察用例。
同时消除共享 connection-control state 新增 storage 后，config/storage/band
fixture 中的重复成员定义。五份修改/新增 fixture 仅 AST，未导入、编译或执行。

manifest 49 classes/469 functions、features 27、live SDK schema 35 STA/21 AP、
MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map 与 whitespace 通过。
源/产物 hash、object/ELF 边界与静态账本见
`build/w07-restart-inactive-evidence.json`。

Host/Python/VM/故障注入、C3/S3/disabled 构建、完整 restart、RF/实机、NVS 重读
均 **not-run**。Wi-Fi API 完成后集中阶段与实机功能验证，长 soak 留到 BLE API
完成。未刷写、串口操作、擦 workspace、构建前端、提交、推送或更新根 gitlink。
