# W-04：物理终止证据向 Raw TX owner 传递

firmware HEAD `d7db8d1`，固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批完成原生终止证据的消费路径，尚未实现或注册公开 recover API，也没有可主动
执行 stop/deinit 的恢复协调器。W-04 与完整 Wi-Fi 目标继续保持未完成。

## 证据与所有权

固定 SDK 的 esp_wifi.h 声明 deinit 释放初始化资源、停止 Wi-Fi task；Raw TX
callback 在该 task 执行，没有逐包 cookie。wifi_init.c 的 deinit 错误可能发生在
部分清理之后，因此只有成功物理 deinit 加已进入 callback 排空，才能作为这里的
终止证据。注销 callback、timeout、GC、runtime restart 均不能代替此证明。

broker 增加 exact token 的 quiesce：未来协调器须先持有独占物理清理权限；调用只
封闭注册入口并保留 payload/token。重复调用仅排空未完成后缀，不重复 SDK 注销。
当前无生产协调器调用它，C5 对象已编译，最终 ELF 将其未引用入口裁剪。

reset_after_deinit 释放 SDK packet copy，但保留原 operation identity、Radio lease
identity/generation、accepted/completed/error/correlation 快照，设置 native_terminated。
新注册和提交继续被阻止，直到原 owner 精确 retire；重复 reset 不重复释放，旧或
错误 token 不能消费证据。physical termination 不伪装为 TX completion。

实际 Session worker 可消费此证据，退休 broker/Radio packet 并把 queue 记为 aborted。
仍注册的 uncertain watcher 可更新为 TERMINATED；已移除 watcher 的 storage 不再访问，
调用者以前复制的错误结果不变。周期 job 经独立 periodic_terminated helper 清除精确
活动 ticket 的 uncertainty、增加 aborted，并完成 timer/template/Session child 的原有
清理后缀。已接受发送仍记 submitted，绝不增加 completed，也不声称未发生 RF。

one-shot Future 将终止作为 native-terminated 错误；全局 native status 增加
nativeTerminated、terminatedRadioGeneration，保留 operationIdentity 和 cleanupPending。
类型和 API 文档同步，正式注册数量不变。

## 验证与限制

6 份 Python fixture 仅完成 AST parse，未导入、编译或运行：broker、Session、results、
periodic ledger、periodic job、capture GC。用例直接使用生产 helper，通过注入已取得的
物理 deinit 边界覆盖 exact token、注销后缀、重复终止、错误/接受状态保留、watcher
storage、周期账本及最终释放。它们不证明尚未实现的恢复协调器已取得物理清理权限。

C5 immutable Build Context firmware-ci-esp32c5-representative 编译 exit 0；binary
`0x29ac40` / 2,731,072 bytes，比前批增加 768 bytes，app 空余 13%。ELF 已链接
broker reset/retire、periodic_terminated、Session result publish 和 job_observe。
s_raw_tx=136 bytes（增加 8），s_radio=824、s_jobs=32、s_lane=44、s_retired=56、
s_sessions=32、s_tx_rate_lease=36、s_tx_rates=68 bytes。动态 heap 与硬件内存未测量。

MQuickJS 61 sources/53 snippets、manifest 49 classes/433 functions、strict TypeScript、
feature docs 27、config schema 35 STA/21 AP、recorded SDK map 与 whitespace 通过。
日志/符号在 build/w04-termination-*，hash 记录在 build/w04-termination-evidence.json。
SDK 工作区干净。Host C/Python/VM/竞争/故障注入、C3/S3/disabled 矩阵、实机/RF/heap
均 not-run；长期 soak 留待 BLE API 完成。未刷写、串口操作、擦 workspace、构建前端、
提交、推送或更新根 gitlink。

后续恢复协调器必须解决独占准入、stop/deinit 失败后缀、旧代 token 消费以及配置和
临时速率恢复，不能复用当前零 owner shutdown 冒充活动 Raw TX 恢复，也不能重启
无关 owner。详见[剩余清单](2026-09-08-wifi-api-remaining.md)。
