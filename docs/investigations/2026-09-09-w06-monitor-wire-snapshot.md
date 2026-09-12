# W-06：Monitor 快照到统一 wire 的转换

已编码 Monitor 原生 info → 共用 frame/metadata snapshot，并将公开 JS info 与 wire
的 PHY 解释收敛到同一个 helper。snapshot 尚无公开 Source 调用方；CSI snapshot
adapter、Source retain/stream 和 Host parser/导出仍待实现。本批不改变 Monitor
wireSource=false，也不切换现有 CSI writer/parser。

firmware HEAD `d7db8d1e40ee9f6a522c22806252b2ef712843a7`，增量未提交；SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`，继续使用既有不可变 C5 Build Context
`firmware-ci-esp32c5-representative`。

## 实际字段来源

`wifi_monitor_wire.h/.c` 接收资源池复制出的 pointer-free info，以及原事件 sequence、
Session generation 和 capture Radio generation。拒绝零 identity、无 driver facts、
非法长度和截断矛盾；共用 metadata 校验成功后一次性提交整个输出。没有 driver/JS
调用、heap 分配或 payload 读取，也不自动取得新的 payload/Session owner。

- timestamp 使用原 callback_time_us，accuracy 为 callback-time；不由 32-bit
  driver timestamp 推算 epoch。RX sequence 仍 unknown，保留原 capture 代次。
- RSSI/noise、channel、antenna 和 legacy HT availability 来自现有 target snapshot。
  PHY helper 用固定 SDK 的 RX_BB_FORMAT 枚举映射 HE target，legacy 使用已实现的
  raw format 解释。公开 JS info 现在调用该 helper，已有字符串命名不变。
- HE SIG 位没有在本批解码，不能因为 PHY 已知就虚构 MCS/STBC/带宽。legacy rate
  和 signal mode 保留 unknown；FCS 未被证明，仍 unknown。
- addresses 仅在完整 header 解析且 type 与 driver 匹配时可用。已读到的 FC/
  duration/sequence/QoS 可在 type mismatch 时保留；parse-valid 和地址不可用。
- snapLength 小于完整 header 时保留原 header 所需长度和实际复制前缀；short
  original header 只保留 bounded parser 已读字段，不读未捕获 bytes。
- metadata-only callback 不携带 packet section/header/addresses；即使原输入有
  无关 header 存储也不复制。保留 driver report 和可用 PHY facts。

此 adapter 不改变资源池持有方式。后续 Source 必须分别 retain 原 payload 与
Session；snapshot 本身不能延长 payload 寿命。输出可覆盖输入快照，但输入必须
在本调用期间稳定，不能用它验证悬空 native pointer。

## Driver 长度 availability

本地 SDK Monitor 入口提供 `sig_len`（C5 另有已证明 span 使用的 `dump_len`），
没有 CSI 风格独立 `payload_len`。因此不能把 `readableLength-headerLength`
冒充 driver-reported payload length；该值与公开 info 中的已解析剩余长度不同。

在唯一开发 v1 中补 packet flags bit 17/18：分别表示 driver payload/packet
report 可用。不可用则字段为零；已知零则 availability=true。Monitor 的 bit 17
为 false、payload report 为 0；bit 18 为 true，packet report 保留 driver 原值，
与 directory 的已证明 span 分开。目录 payload report 与 metadata offset 104
共用 bit 17。reserved 改为 bits 19–31；不是第二套格式或兼容 reader。

共用 metadata 校验拆出无分配 `metadata_valid`，snapshot 与 writer 使用同一入口；
没有先编码一次丢弃、再编码一次的临时缓冲。encoder 根据两个 availability 写入
标志，并拒绝不可用字段携带非零报告。

## 测试源码与检查

新增 `test_wifi_monitor_wire.py`，组合真实 bounded parser、snapshot adapter、
envelope 和 metadata encoder。HE enum 从 recorded SDK inventory 读取，未手写
替代 driver 类型/PHY encoder；SDK ABI/RF 仍不能由 Host fixture 证明。

覆盖 legacy/HE PHY 映射、unknown 和可选字段、原 callback 时间高 32 位、identity、
原始 report 与可读/捕获长度分离、snapLength=1、完整 packet、原始短 header、
type mismatch、metadata-only、输出覆盖输入和失败输出保持。

既有 `test_wifi_monitor_metadata.py` 复用相同 production builder，在真实 VM
转换前检查同一快照的 wire 时间/PHY；继续保留逐次 JS allocation/property failure
与 moving GC 场景。修正 fixture 中 metadata-only/单字节完整捕获留下的旧 truncated
标志，并让 type mismatch 使用实际不同的 driver type。

共用 metadata 测试追加长度 availability、已知零与未知的差别，所有 fixture 更新
到同一个 v1，不保留旧 bit 布局分支。以上仅源码与 AST 检查，未编译或执行测试。
完整 JS/wire 字段一致性、Source 所有权/取消/GC 和 Host malformed input 验收仍待
后续接入并集中运行，不能以当前 PHY/time 对照代替整个端到端验收。

C5 build passed，bin `0x28af30`，app 剩余 15%。nm 确认 shared PHY helper 实际
链接到公开 info；snapshot adapter 在目标 object 中，但因暂无 Source 调用方未
链接进最终 ELF。bin 大小相同不证明新的运行内存/性能。

MQuickJS 61 sources / 48 snippets、manifest 47 classes / 415 functions、feature
文档 27、config schema 35 STA / 21 AP 与 live SDK、strict TS declaration、recorded
map、四份 Python AST 与 whitespace 检查通过。SDK source clean。
日志/hash：`build/w06-monitor-wire-snapshot-evidence.json`。

Host/GC/OOM/竞争、C3/S3/disabled 构建、实机功能/RF/共存、预热内存与 soak 均
not-run。未刷写、串口操作、擦 workspace、构建前端、提交、推送或更新根 gitlink。
Wi-Fi API 完成后统一阶段测试并做实机功能验收；长时间 soak 等 BLE API 完成后。
