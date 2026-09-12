# W-08 Action/ROC 缺失终态的独立退休证明

本增量接续 [ROC Session](2026-09-09-w08-roc-public.md)。基线为 firmware
`d7db8d1e40ee9f6a522c22806252b2ef712843a7`、SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`；实现仍为 Candidate。
这是缺失/歧义事件的回收路径，不是完整物理故障恢复协调器。

## 原因与 SDK 证据

固定 SDK 的 `offchan_txop_end` 在 reason 为 0 或 4 时发送终态，其他 reason
也能释放 native 请求和 channel-manager lock、清空记录而不发终态。
`offchan_roc_req` 的部分失败路径同样会释放已分配请求和 lock、结束 PM 责任、
清空记录并返回错误，没有终态事件。只等待事件会永久保留框架 token/Radio owner。
这些是原生代码/反汇编审计结果；未执行故障复现，运行证据仍为 not-run。

C3/S3/C5 原始 SDK 对象的反汇编位于 `build/w08-action-sdk/<target>/`：

- `offchan_txop_end.txt`、`roc_op_end.txt`：请求释放、channel/PM/RX policy
  恢复及事件发布在完整 28-byte off-channel record 清零之前；Action 的其他
  pending pointer 在同一函数返回前清零。
- `offchan_action_tx_req.txt`、`offchan_roc_req.txt`：新提交期间也会先清零再填充，
  因此不能从 JS task 或其他 task 直接读全局记录作为证明。
- `chm_end_op_timeout.txt`、`ieee80211_timer_process.txt`、
  `chm_end_op_timeout_process.txt`、`chm_end_op.txt`：timer 通过 native queue
  转交结束处理；channel-manager 先拆除自身记录，再调用 end callback。
- `offchan-record-references.txt`：扫描各 target 的 Wi-Fi archives，记录定义于
  `wl_offchan.o`，另有 `ieee80211_supplicant.o` 引用。后者的
  `wifi_set_rx_policy` **会写 byte 26** 以保存旧 RX policy，不能称为只读引用。
  本检查要求所有 28 bytes 为零，残留该字段也会阻止回收。

构建沿用完整 archive SHA-256 gate，既约束既有 Vendor IE 修补，也约束此私有
record/layout/清理顺序假设。未知 archive 拒绝构建，必须重新审查；未修改本地 SDK。
这里只证明固定 SDK 的静态控制流，不能替代真实 SDK task、并发回调或 RF 验收。

## 实现与公共行为

`esp32_mquickjs_wifi_action_sdk_quiescent()` 将带有精确 callback identity 的
私有 sentinel 经现有 `esp_wifi_action_tx_req` ioctl queue 提交。原生执行处检查
完整记录，仅全零返回 ESP_OK；任何非零（包括其他模块状态）返回 ESP_ERR_TIMEOUT。
它不清空记录、不取消其他 owner、不修改用户配置或断连。SDK API 仍会临时分配
消息并使用成对 PM wake/done，不承诺无分配或能打断卡死的 native task。

Radio 在 operation mutex 内调用 SDK、在 critical section 外执行 driver I/O。
成功证明必须匹配原 token、generation、revision；随后仍须同 token/revision 的
默认事件队列 marker。匹配的迟到事件，包括未知 status，递增 revision 并废弃旧证明。
队列满仅重试 marker 后缀；原生证明成功后不再发送 cancel。revision 耗尽、查询失败、
非零记录或 marker 未完成时保留 owner，不能凭超时或 JS runtime 重启清除。

正常 terminal + SDK fence 的原路径保持。独立证明不写 terminal、TX status 或成功
结果；原生 Action Future 若缺少终态仍按其 deadline 结束，随后退休槽继续清理，
已结束 Future 不改写为成功。ROC 原生 worker 在普通驻留期间也检查退休状态，
正常非零记录不记作 cleanup fault；缺事件但已经全清空的 ROC 可自动关闭并缓存
`nativeQuiescent: true`、`terminalStatus: null`。`wait` 完成只表示退休/释放，
不能把缺失结果补成驻留完成或 RF 发送成功。

`wifi.action.status()`、`wifi.status().radio.action`、ROC Session status 和源类型
同步 `nativeQuiescent`。未增加 callable 或版本别名。

## 检查与限制

- immutable C5 context `build/wireless-contexts/c5` 编译通过，最终日志
  `build/w08-quiescent-c5-build-final.txt`；本增量没有中间编译失败。
- 最终 ELF 的 SDK wrapper 和 quiescence helper 已链接；反汇编证据
  `build/w08-quiescent-linked-sdk.txt`。C5 binary 2,833,552 → 2,834,032 bytes
  （+480）；此前记录的 28 个无线 mutable static 对象尺寸不变。
  这是构建尺寸比较，不是运行时 heap、largest block、stack 或 SRAM 空闲量证明。
- Manifest 50 classes/481 functions、feature docs 27 项、固定 SDK STA/AP schema
  35/21、strict TypeScript、MQuickJS syntax 61 sources/55 snippets、SDK map 和
  whitespace 一致性检查；具体快照见 `build/w08-quiescent-evidence.json`。
- 仅补写生产路径 fixtures 并 AST 解析：SDK wrapper 的逐 byte 阻止/全零接受；
  native ledger 的 revision/marker/未知事件/耗尽；Radio 的提交失败、查询期间事件、
  队列满和证明后取消；真实 ROC Session 的缺终态自然退休。
  六份相关 fixtures **未导入、编译或执行**，不计作回归通过。

仍待：无法取得原生退休证明的完整物理恢复、其他 Wi-Fi API、集中 Host/Python/VM、
C3/S3/feature-disabled 全固件构建、真实并发/队列饱和/runtime teardown 和实机/RF。
完整 Future factory OOM/GC 集成 fixture 仍须补齐。长时间 soak 依用户安排留到 BLE
API 完成后。本批未刷写、访问串口、擦 workspace、构建前端、提交、推送或更新 root gitlink。
