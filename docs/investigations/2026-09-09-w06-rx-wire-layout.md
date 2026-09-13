# W-06：统一 RX wire 的布局与目录编码基础

CSI/Monitor 共用的内部 envelope 已编码：32-byte header、40/24-byte directory、
256-byte metadata slot，以及按帧顺序排列、4-byte 对齐的数据区域。它尚无公开
Source 调用方；metadata encoder、保留 payload 的 stream adapter、严格 Host
parser 和导出仍待实现。当前公开 CSI writer/parser 未切换，Monitor 的
wireSource 仍为 false。本批不作为 W-06 完成证据。

firmware HEAD `d7db8d1e40ee9f6a522c22806252b2ef712843a7`，增量未提交；SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`，使用既有不可变 C5 Build Context
`firmware-ci-esp32c5-representative`。

## 原生实现

`internal/esp32_mquickjs_wifi_rx_wire.h` 与
`src/modules/wifi_common/esp32_mquickjs_wifi_rx_wire.c` 提供三个内部入口：

- layout：先校验全部 descriptor，再返回控制区和总长度；加法与补齐均检查
  uint32 overflow，frame count 限制 1–128，静态断言控制区乘法安全。
- offsets：完整预检后返回指定帧 metadata/CSI/packet offset 与补齐后的末端。
- write_control：预检、容量和输入/输出区间检查全部成功后才改写输出；写入
  little-endian header/directory 并清零 metadata slots，不接触 payload 区。

调用方必须提供稳定且有效的 descriptor snapshot。实现不分配内存、不操作
Radio/JS、不复制大块 payload。stream adapter 后续负责独立保留 payload owner，
填充所有 metadata、按顺序输出 payload span 与零 padding。清零 slot 本身不是
合法 metadata；不得据此直接发布 Source。

输入/容量失败保留原输出。write_control 拒绝输入 descriptor 与将写入的控制区
重叠，检查 uintptr 区间溢出；这不意味着可以验证任意悬空指针。最后数据段也补齐
到 4-byte，计入 total bytes。不存在的 section offset/length 同时为 0。

## 唯一 v1 契约补充

任务书第 9.4/12 节现在明确三种长度的区别：driver 报告、adapter 已证明可读、
实际捕获。只有实际捕获长度参与复制和 section offset；捕获不超过已证明 span，
driver 报告不作读取边界。无 packet section 仍可保留已知的原始报告和解析事实。

metadata headerLength 为已知完整 MAC header 所需长度；directory header length
是实际复制的前缀，等于 min(metadata.headerLength,capturedPacketLength)。这允许
snapLength=1，而不会把一个字节声称为完整 MAC header。parse-valid 描述原始
callback header；header-only 策略则要求完整 header，有意省略 body 不算截断。

增加 secondary=255 unknown、smoothing availability、frame-control/duration
availability 和 subtype=255 unknown，区分未知与已知零/false。明确 directory 与
metadata 的逐字段一致性规则，避免错误地要求 driver 报告等于可读 span。仍是唯一
开发 v1；后续 Source/Host/fixture 必须一起切换，不增加 reader fallback。

以上 metadata 规则目前只在目标契约中明确，尚无 metadata encoder/parser 实现，
不声称本次 envelope 已验证所有字段关联。legacy rate/signal mode 的完整规范化和
时间映射仍随 target adapter 接入处理。

## 测试源码与检查

新增 `tests/c/integration/wifi/monitor/test_wifi_rx_wire.py`，编译并调用实际生产 header/source，
不复制布局实现为测试状态机。源码覆盖：

- CSI/Monitor 的固定字节值与多帧 offset、空 section、reported/captured 分离；
- snapLength=1、完整 packet、header-only 与 truncated/pointer/parse flags；
- 128 帧边界、非法 kind/count/flags、容量不足、输出不变；
- uint32 加法/对齐溢出、输入与控制区重叠、uintptr 输出末端溢出；
- 控制区写入不改动 payload/padding 存储；metadata slots 清零。

测试仅完成 Python AST 检查，没有导入、编译或运行测试 fixture。按用户安排，
Host C/Python 和故障注入等 Wi-Fi API 完成后统一执行。

C5 build 通过，bin `0x28af30`，app 剩余 15%。目标 object 包含三个内部 wire
符号；最终 ELF 没有这些符号，因尚无调用方被链接器移除。bin 大小与前批相同
不能证明 wire 运行正确或新增内存预算可接受。

MQuickJS 61 sources / 48 snippets、manifest 47 classes / 415 functions、feature
文档 27、config schema 35 STA / 21 AP 与 live SDK、strict TS declaration、
recorded SDK map 和 whitespace 检查通过。SDK source clean。日志/hash 见
`build/w06-rx-wire-layout-evidence.json`。

C3/S3/disabled 构建、Host/GC/OOM/队列竞争、设备功能/RF/共存、预热后内存比较和
soak 均 not-run。未刷写、串口操作、擦 workspace、构建前端、提交、推送或更新根
gitlink。下一步实现共用 metadata，再同时接入 Source、Host parser 和 fixtures；
长时间 soak 仍放到 BLE API 完成后。
