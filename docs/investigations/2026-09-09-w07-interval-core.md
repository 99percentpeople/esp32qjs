# W-07：Connectionless interval 已知前值与恢复核心

firmware HEAD d7db8d1，SDK fff9895c82d744c7237be8847347bdd1b07c6643。
本批是原生核心准备，尚未接入 Radio/ESP-NOW 调用链，没有新公开 API。完整 interval
共享控制和 ESP-NOW 关闭修复尚未完成，不能将本批构建视为问题已解决。

## 本批核对时的代码缺口

固定 SDK 明确所有 connectionless modules 共用 wake interval。ESP-NOW 在 open、
setPowerSave、recovery、finish_close 四个地方直接写 SDK。finish_close 不保存前值，
写回 DEFAULT_MODE，忽略 wake-window/interval 失败；esp_now_deinit 失败时也清除
now_initialized 并继续释放 Radio/session/queue。close worker 忽略 finish_close
的 false 结果却完成 close state。这些是当前源码行为；未运行故障注入，未声称
获得实机复现。新增 Driver interval setter 前必须统一写入及关闭恢复义务。

## 新生产核心

wifi_interval.h/.c 接受生产 SDK writer 边界，无动态分配、JS、保留指针、定时任务
或锁。调用者必须持 Radio mutation mutex，证明 SDK state 和精确 Radio registry
owner。它本身不代替 Radio，也不编造一个独立模拟 Radio 状态机。

显式 write：无临时 owner 时写入指定 uint16 interval（包含显式 SDK default-mode
请求 0），记录同代 SDK 接受值；失败标记 unknown/uncertain。可以由下一次显式
write 建立新知识，不声称恢复未知前值。模块绝不初始化一个假设的默认 interval。

临时 acquire：必须已有同代已知前值；捕获前值并在 SDK 调用前发布 owner/token，
即使 setter 已修改后报错也返回需要保留的 token。token 包含 Radio generation、
Radio owner identity 和独立 interval identity；同一个 Radio owner 关闭后重新申请
也得到新 identity。update 必须精确匹配并且当前不 uncertain，保留最初前值。

release：只重写捕获的前值，失败保留 token/owner/restore_pending，后续只重试该
恢复步骤。原策略错误与 restore_error 分开保存。成功清空 token，恢复已知记录，
不宣称 SDK getter/readback。已消耗的全零 token 幂等且不影响后来 owner；旧 token
副本不能释放后来申请，即使 Radio owner 相同。无隐式释放或清理失败后忘记义务。

revision 在每次 SDK 尝试前增加，失败也消耗 revision，不回绕。临时 acquire/update
至少预留一次 restore 写入；若后续连续失败消耗最后 revision，保持可诊断未恢复
owner，需物理设备重启，不循环复用 identity。invalidate 仅供成功物理 deinit 后调用，
要求临时 owner 已退休并验证 generation；清空知识，保留 boot-scoped revision。

## 尚待接入，不能省略

- 明确建立第一条已知 baseline 的生产路径；不能把 DEFAULT_MODE 常量当成已验证前值。
- Radio 注册表/全局准入、实际 mutex、成功 deinit 失效和 interval 状态诊断。
- ESP-NOW window/interval 的顺序、部分成功、失败后恢复及 open/recovery/disable/close。
- finish_close/close worker 的结果、未完成后缀、Future/queue/Radio 保留与重试。
- Driver 公开 setter、严格参数和所需类型/manifest/API 文档；尚未注册占位方法。

## 延后测试准备

test_wifi_interval.py 使用完整生产 header/implementation，仅 writer 注入 SDK 接受值
与“写入后报错”。准备覆盖未知前值拒绝、无 owner 失败后显式修复、同代准入、发布
owner 先于 SDK、更新保留初始值、错误后仅恢复、两种错误分别保留、token/generation/
owner 不匹配、同 Radio owner 重新申请、全零释放幂等、物理失效与 revision 耗尽。
仅 AST parse；没有导入、编译或运行该 fixture。真实 Radio/ESP-NOW/Future 竞争测试
还需调用方接入后补齐，不能用这一核心 fixture 替代。

## 构建与契约检查

C5 immutable Build Context firmware-ci-esp32c5-representative 最终构建 exit 0；新
production object 含 write/acquire/update/release/invalidate 五个导出函数。当前没有
调用方，ELF section GC 将它们移除，不能宣称已运行或已接入 SDK。无状态实例，
因此静态账本和 binary 大小保持前批值：0x29fa30 / 2,751,024 bytes，app 空余 13%。
s_radio=824、s_raw_tx=136、s_tx_rates=68、s_tx_rate_lease=36、s_sessions=32、
s_jobs=32、s_retired=56、s_lane=44 bytes。后续接入会计入真实状态实例和调用链成本。

MQuickJS 61 sources/53 snippets、manifest 49 classes/463 functions、strict TypeScript、
feature docs 27、config schema 35 STA/21 AP（live SDK）、recorded SDK map、Python AST
与 whitespace 通过；SDK 工作区干净。日志/object 与 ELF 符号/source hashes 在
build/w07-interval-core-evidence.json。Host C/Python/故障注入、真实 SDK/队列/关闭
并发、C3/S3/disabled 构建、实机/RF/动态内存均 not-run，Wi-Fi API 完成后集中执行；
long soak 留待 BLE API 完成。未刷写、操作串口、擦 workspace、构建前端、提交、
推送或更新父仓库 gitlink。当前缺陷修复和完整 Wi-Fi 目标均继续保持未完成。

后续进展：[关闭失败后缀保留](2026-09-09-w07-espnow-close-suffix.md)已编码，修正失败后忘记 native/queue/Radio 义务和误报 Future 成功；interval core 仍未接入，不能据此认定全局旧值恢复完成。
