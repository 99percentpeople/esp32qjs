# W-07 connectionless interval 与 ESP-NOW 接入

firmware HEAD `d7db8d1` 上的工作区增量；SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
前置核心见 [interval ledger](2026-09-09-w07-interval-core.md)，关闭后缀见
[close suffix](2026-09-09-w07-espnow-close-suffix.md)。本批接入生产路径和 sole v1 API。

## 代码事实与修订

旧 ESP-NOW open/setPowerSave/recovery/close 四处直接写 Wi-Fi 全局 interval；
关闭或禁用写 0，未捕获前值。现只有 Radio 的 SDK writer 调用该函数，受 mutation
mutex 保护，SDK 调用不在 IRQ critical section。初始化在 init/storage 成功后显式
写默认模式，接受后才登记 baseline；失败记录 `interval-baseline`，不重复初始化。
固定 C5 SDK 的 ioctl/PM 实现反汇编没有额外 started 检查；这是本地二进制观察，
不是 C3/S3 证据或实机验证。公开 SDK 没有 interval getter。

新增 `wifi.driver.setConnectionlessWakeInterval(milliseconds)`，严格整数 0..65535，
0 是显式默认模式。已初始化、稳定、停机零 owner，或运行时仅精确框架 owner 可写；
helper/native operation、其他 RF owner、wake lock 与临时 rate 阻止修改。没有隐式
init/stop。每次实际写入推进非回绕 revision。失败后的无 owner 状态可由显式写入修复。

ESP-NOW 持有 generation/Radio identity/interval identity 三项 token，捕获 baseline，
更新不覆盖最初前值。修改 interval 先于 window；任何部分失败保留清理责任。
仅准入拒绝不将健康 Session 置为 failed。SDK 设置不确定时需要 close，不允许
`recover()` 猜测恢复省电策略。普通 Radio release 不能丢弃 interval token；借用或
不确定期间禁止新 Radio/wake owner。现有框架 owner 的观察不冒充 interval readback。

关闭/禁用先恢复本模块 window，再恢复捕获的 interval。window 已完成但 interval
失败时，重试只执行后缀。ESP-NOW 成功 deinit 只能证明它自己的 window 已终止，
不能释放 Wi-Fi interval token。队列超时、跟踪发送超时和恢复失败清理路径的
callback/deinit 标记改为成功后才清除；native restore 禁止重复初始化尚存的实例。
注销失败后不等待仍可能进入的 callback 排空，不进入 deinit 后缀，交给后续清理。

`wifi.status().radio.connectionlessInterval` 提供 accepted/unknown/uncertain、revision、
精确 owner/token、捕获前值与独立 write/restore error。字段是单独 mutex 快照，
不保证与 status 其他部分同一瞬间。ESP-NOW powerSave status 增加 faulted/restorePending；
错误增加 `ESPNOW_POWER_SAVE_FAILED`。公开注册、类型、manifest、API 文档和 SDK map 同步。

## 验证边界

C5 immutable Context `build/wireless-contexts/c5`，8 MB/no PSRAM，生产编译已通过；
核心已经链接并被调用，不再只是被 section GC 丢弃的对象。日志：
`build/w07-interval-integration-final-c5-build.txt`。最终一致性检查和 hash 见
`build/w07-interval-integration-evidence.json`。

新增 `test_wifi_interval_integration.py` 提取真实 ledger/Radio/ESP-NOW helper；
覆盖非零 baseline、精确 token、旧 token、helper/operation 准入、部分失败、
恢复后缀、ESP-NOW deinit 后的共享 interval 归还和显式无 owner 修复。
关闭故障与 allocation/GC fixtures 同步生产依赖；Radio baseline 失败增加 stage 20。
这些用例本批仅 AST 检查，没有编译、导入或执行，不作为缺陷已复现或竞争通过证据。

Host C/Python/VM/OOM/竞争、C3/S3/feature-disabled、实机关闭重开/GC/队列/运行时重启、
RF 省电与共存全部 **not-run**。按用户安排，Wi-Fi API 完成后集中测试；BLE 后再做
长时间 soak。本批没有刷写、串口操作、workspace 修改、提交、推送或父仓库 gitlink 更新。
其余 W-07 setter、公开 restart/config restoration 和完整 Wi-Fi 高级模块仍未完成。
