# W-08 FTM 原生 Session、报告存储与运行时清理

本批承接 [FTM Radio 控制](2026-09-10-w08-ftm-radio.md)，接入原生 Session 与
Wi-Fi poller/core runtime 销毁检查。尚未注册公开 `wifi.ftm`/Session/Future；
responder offset 与缺失/歧义终态的物理恢复仍未实现。firmware HEAD `d7db8d1`，
SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。

## 所有权与数据

`esp32_mquickjs_wifi_ftm_session.c` 使用独立 Session lock 管理引用和状态，所有
Radio/SDK、分配/释放、worker 提交都在锁外执行。create 复用生产 Radio 参数
validator，在任何 driver I/O 前完成 Session 与报告两次分配；第 N 次分配失败
返还预留预算。配置按值保存，不保留调用者输入指针。

- 最多 8 个原生句柄（包括创建后未 start、已退休和已关闭但仍保留的句柄）。
- 每份报告最多 64 项；所有句柄最多预留 256 项。capacity 0 表示只接收摘要。
- 报告是独立 native allocation；worker 使用期间由 worker reference 保护。
  close 在空闲时立即回收报告，在 worker 运行时先禁止读取、由 worker 完成后回收。
  分配额度在实际 free 后归还，不能以指针脱离 Session 代替释放。
- 公开调用者、active registry、queued/running worker 各有自己的引用。最后一份
  引用释放时才回收 Session；不能因 Future/GC 释放公开引用而释放正在使用的存储。
- 正常报告只有在精确 Radio 退休后才标记 ready。report_entry 按索引复制一个
  已保存的 native entry，不再次读取 SDK，不借出原始指针。64 位时间戳无损保留。
  退休后的旧报告不依赖新 FTM 会话的 SDK 全局报告 buffer。

以上是当前 FTM 私有上限，不能替代尚未完成的 W-09 跨模块共享预算。timer fence
的单份 boot heap、SDK 内部分配和 allocator overhead 仍须统一记账。

## 推进、终止与关闭

start 只登记 active owner，实际 Radio start/collect/retire 由后台 worker 推进，
每次仅一个 worker。队列满保留 active reference 并延迟重试，不重复提交测距。
已关闭 Session 在队列满时也能释放不再被 worker 使用的报告存储。

end 请求终止测距并保留最终报告；close 同时请求终止并放弃报告；两者都不把
SDK end 成功当作退休证据。提交失败仍保留已经准入的 Radio token，后续只做
end/报告/屏障清理，不重试 initiation。

worker 在同步 SDK 操作后和最终发布前重新合并 end/close intent，避免覆盖
并发关闭。collect 成功后的 worker 重试只做剩余退休步骤；控制 marker 队列满
不会导致再次领取报告。原始 submit 错误与当前 cleanup 错误/阶段分开保存。

Wi-Fi driver event poller 调用原生 service；core runtime destroy 独立请求 FTM
清理，与 Action/ROC、Raw TX 的请求都执行后再检查汇总结果，不通过布尔短路
跳过另一个模块。FTM active owner 未退休时销毁返回未完成；队列项、timer marker
和 Session 不包含 JS/runtime 指针。没有 native 终态的会话继续隔离，仍需后续
物理恢复协调器；不把 runtime restart 宣称为恢复手段。

## 构建与证据

两个既有不可变 Build Context 均编译通过：

- FTM enabled：`build/wireless-contexts/c5-ftm` → `build/wireless-c5-ftm`，
  日志 `build/w08-ftm-session-c5-build.txt`，exit 0。
- FTM disabled：`build/wireless-contexts/c5` → `build/wireless-c5`，
  日志 `build/w08-ftm-session-disabled-c5-build.txt`，exit 0。

启用 FTM 的 binary 为 2,858,576 bytes；同 context 上一批为 2,852,016 bytes。
关闭 FTM 的 binary 仍为 2,842,144 bytes，既有跟踪静态对象尺寸保持不变。
实际 C5 DWARF 中 Session 为 136 bytes；256 个详细 entry 的 payload 上限为
12,288 bytes（每项 48 bytes），不含 allocator overhead。8 份 Session payload
上限为 1,088 bytes。当前未注册创建入口，这些运行分配尚未发生或测量。

这次 native worker、Radio start/end/collect/retire 和 SDK report getter 已链接。
最终 ELF 的 getter 反汇编仍为 `li a0,258` 后 `beqz a0`，确认使用的是修复后的
NULL-discard 分支；输出 `build/w08-ftm-session-get-report-linked.txt`。
Session create/start/read 等公开调用方尚未接入的入口在 object 中编译、最终 ELF
裁剪，不能把现有 binary 当作完整 FTM API 的大小或端到端证明。

新增静态项为 20-byte boot timer fence（前批仅 object 存在，本批链接）和三个
4-byte Session 指针/计数。既有 Radio/FTM static 尺寸不变。SDK heap、真实峰值、
internal/PSRAM free/largest block、调度时延尚未测量。

`test_wifi_ftm_session.py` 组合生产 Session 与生产 Radio，准备：第 1/2 次分配
失败、句柄/报告预算、关闭后保留句柄、worker queue full、提交/报告读取期间
close 与公开 release、SDK 报告一次领取、旧 Session 报告独立、summary-only、
end 保留报告、事件 marker 饱和以及 runtime 清理。`test_wifi_ftm_radio.py` 同步
抽取共享 validator。只 AST 解析，未导入、编译或运行 fixtures。

生成物、strict TypeScript、MQuickJS 语法、SDK map 与 whitespace 检查记录及 hash
见 `build/w08-ftm-session-evidence.json`。Host/Python/VM、C3/S3/完整 feature matrix、
硬件/RF 与长期 soak 仍为 not-run。未刷写、串口操作、提交/推送、修改共享 SDK、
更新根仓库 gitlink 或构建前端。

## 下一步

接入公开逐字段 options/status/report、Session/Future 与 GC/OOM 安全转换，定义
等待 timeout 和 end/close timeout 的不同副作用；完成 responder offset 的
准入/记录/重放以及物理恢复。Wi-Fi 全部 API 编码完成后再统一阶段测试与实机
验证，长时间 soak 留到 BLE API 完成后。
