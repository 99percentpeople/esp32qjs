# W-06：Monitor Batch wire Source 与 Host JSONL

已接入 `WiFiMonitorBatch.source({format: "esp32qjs-monitor/1"})`，复用现有
Monitor snapshot、共用 envelope/metadata writer 和严格 Host parser。最终 C5 ELF
已链接公开入口、Monitor snapshot 和共用 writer。Frame.source() 按任务书保留
原始 packet 字节；没有新增 rawSource 别名或另一版本。PCAPNG/时钟锚点、CSI
单帧统一 Source/correlated packet、完整 PHY/time 能力与集中运行验收仍待完成。

基线 firmware HEAD `d7db8d1e40ee9f6a522c22806252b2ef712843a7`，SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`，SDK 工作区保持干净。
本批修改未提交；使用既有不可变 `firmware-ci-esp32c5-representative` Build Context。

## 实现与所有权

- source 要求精确 format 和 plain options，只允许 format 字段。读取参数后才
  重新解析 Batch opaque，避免参数 getter 关闭 Batch 后使用已释放的适配器。
- 创建时取得独立 Session 引用和每帧线性 payload 引用；不保留公共 JS Batch，
  不借用其 event array。所有 retain 与 metadata 构造之间不执行 JS。
- 1–128 帧的临时 descriptor array 使用堆分配，按共用 checked layout 分配控制区。
  两次 metadata snapshot 使用仍存活的 PUBLIC roots；生成后释放临时 descriptors。
  控制区为 32-byte header / 24-byte directories / 256-byte metadata records。
- 迭代输出控制区和原 payload spans，每帧包含 0–3 字节零 padding，最后一帧也对齐。
  metadata-only 记录没有 packet span。每帧 retained length 必须与编码长度一致。
- Source 独立存活于 Batch、原 Session configure/close 和 GC；一次性 open，读取完成
  或取消释放 payload 后再 release Session，确保最后 owner 能退还关闭后的 pool。
  JS wrapper 在 native iterator 活跃时销毁，延迟到 iterator close 才释放数据/控制区。
- 第 N 次分配失败和部分 retain 失败释放本次所有引用，保持原 Batch 不变。
  不合法 token/snapshot 使用 WIFI_MONITOR_INVALID_DATA，实际分配失败为 OOM。
  known length 保存完整控制区/packet/padding 长度。

## Host 与公共契约

新增 `scripts/esp32qjs_monitor.py`，复用 `esp32qjs_rx.parse_rx_batch(kind="monitor")`。
CLI 完整校验输入 batch 后才输出 JSONL，每行包含一帧 normalized metadata、捕获长度、
原可读 span 和捕获 header 前缀。没有执行 decoder 的本批运行证据；公开代码已接入。
时间仍是 callback-time，不从设备单调时间推测 UTC；PCAPNG converter 尚未实现。

注册、头文件、源类型、生成 manifest、API 文档、02 任务书和剩余工作表同步。
wireSource=true，hostPcapngConverter=false，稳定等级仍为 candidate。

## 测试源码与当前证据

本批新增/扩展测试源码，按用户安排没有编译或运行 Host C/Python/设备测试：

- 生产 Session fixture 接入真实 Monitor snapshot、共用 writer 和公开 Source 使用的
  create/open/next/close/destroy helpers；覆盖三次分配失败、部分 token retain 失败、
  count 边界、双帧非对齐 packet、metadata-only、未打开即销毁、重复 open、原公共
  owners 先关闭、JS wrapper 先销毁，以及控制区读取后取消。SDK heap free(NULL)
  边界修正为实际允许行为，空指针不增加 free 计数。
- 新跨语言测试在阶段运行时编译实际 C snapshot/envelope/metadata writer，将双帧
  596-byte 输出交给实际 Host decoder/JSONL CLI；覆盖借用 packet view、精确时间/
  generations/长度、逐位置截断、字段/保留位/padding 破坏、校验失败无部分 JSONL。
- 既有实机 Monitor Batch 测试增加严格格式、wire 总长度、configure/Batch/Session
  close/GC 后写入文件。它仍需实际 AP、明确目标设备和完整运行；本批未执行。
- 公开 options 重入与 JS wrapper 分配失败的完整 VM 注入仍需在阶段 GC/OOM 验收中
  补足；当前 native fixture 和构建不构成 movable-GC 通过证据。

已执行允许的检查：C5 构建通过，镜像 `0x28c6b0`，app 分区余量 15%，相对前批
`0x28bc60` 增加 `0xa50`。MQuickJS 61 sources / 48 snippets；manifest 47 classes /
416 functions；features 27；config schema 35 STA / 21 AP，live SDK 匹配；严格
TypeScript declarations、recorded SDK map、4 个 Python 文件 AST 和 whitespace 检查通过。
AST 检查没有导入测试、执行 decoder 或编译测试程序。

证据索引：`build/w06-monitor-source-evidence.json`；构建日志：
`build/w06-monitor-source-c5-build.txt`。本批没有三目标/feature-disabled 矩阵、flash、
串口、RF、heap 对比或 soak 证据；没有触碰 root gitlink，也没有提交/推送。
