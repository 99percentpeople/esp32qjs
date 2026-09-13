# W-06：共用 256-byte RX metadata 编码

共用 metadata encoder 已编码，接收规范化的原生字段、原 envelope frame descriptor
和可选 CSI layout，生成第 12 节定义的 256-byte record。尚无公开 Source 调用方；
target snapshot adapter、Source retain/stream、Host parser/导出仍未实现，不能据此
标记 W-06 完成或提升 Monitor/CSI 稳定等级。

firmware HEAD `d7db8d1e40ee9f6a522c22806252b2ef712843a7`，本批为未提交增量；SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`。使用既有不可变 C5 Build Context
`firmware-ci-esp32c5-representative`。

## 类型与编码边界

既有 CSI PHY、encoding、segment、layout 类型原样移入
`internal/esp32_mquickjs_wifi_csi_layout.h`，resources 头文件继续包含该定义。静态
文本比较确认字段、顺序、枚举和常量与原 HEAD 一致，避免 Monitor 依赖 CSI pool
结构或维护另一份 layout 类型；未改变 CSI 实际资源池和公开格式。

`esp32_mquickjs_wifi_rx_wire_metadata.h/.c` 提供 init 与 write_metadata。init
设定未知 PHY/secondary/可选 scalar、未知 RX sequence 和 callback-time；不生成
Session/Radio identity，也不把未知 driver 时间声称为已映射的 RX 时钟。

编码器输入是调用期间稳定的内部快照；CSI layout 指针只在调用期间借用。它不读
packet bytes、不触碰 driver/JS、不分配 heap、不创建 payload owner。规范化 PHY
unknown 为 255，不能直接强转 native CSI UNKNOWN=7；target adapter 必须映射。

先预检 envelope 和 metadata，再在栈上生成固定 256 字节记录，最后一次 memcpy
提交输出。容量/输入失败不写输出，输出可与 descriptor、metadata 或 layout 重叠，
因为读完快照后才提交；这不验证任意悬空指针。reserved、未用 segment/range/null
slot、不可用地址和 header word 统一写零；其他不可用 scalar 按 v1 sentinel 写入。

sequence、CSI/captured/payload 长度和五个 packet 状态标志直接从同一个 envelope
frame descriptor 派生，避免两套副本漂移。subtype 和 ToDS/FromDS 等八个位从可用
frame control 派生。driver 原始 packet report 仍独立于已证明 span。

## 校验范围

- 拒绝未知规范化 enum、reserved RX flag、value 缺少 availability、available
  antenna/MCS 使用 unknown sentinel、非法带宽；未提供的 scalar 不泄漏原存储值。
- 捕获 header prefix 必须等于 min(known full header length,captured length)；
  full/header/none 与实际捕获标志一致。parsed 要求 FC/version/type、duration 和
  完整 header 可读；可用 FC/duration/sequence/QoS 至少分别有 2/4/24/26 字节 span。
- header-only 必须复制完整 header；snapLength=1 的 full capture 可保留原 header
  解析事实。无 packet bytes 时可保留原始报告/解析信息，不制造不存在的 section。
- CSI layout 复用同一类型；检查 count/enum、连续 offset、非空 segment、总字节/
  IQ 数量、sample bits、每 IQ pair 字节宽度和 0–3 字节 trailing padding。
- range 有序端点、不相互重叠，保留原 range 排列；known layout 的 range 数量等于
  IQ count，必须有已知 schema/encoding/segment type。null index 不重复；不据 null
  列表修改 payload。未知 layout 仍可保存 opaque CSI bytes，不推断采样宽度。
- Monitor 没有 CSI section/layout/flags/channel-estimate validity。无 CSI section
  不允许附加 CSI first-word/data-valid flag。

这些是结构与字段关系检查，不替代 bounded MAC parser、target 可读 span 证明、
RF layout 验收或时钟映射。band/legacy rate/signal mode 等完整解码仍需 adapter；
现有 metadata 结构的部分字段尚没有生产来源，未宣称已支持完整 PHY。

## 测试与检查

新增 `tests/c/integration/wifi/monitor/test_wifi_rx_wire_metadata.py`，组合实际生产 layout、envelope、
metadata encoder，未复制测试编码器。覆盖完整默认 256 字节、带字段的 Monitor
record、合法零 MAC 的 availability、不可用存储清零、时间戳高 32 位、snap/完整
header、目录/metadata 关联、三种 CSI sample encoding、未知 layout、错误 flag/enum/
长度/range/IQ、失效输出保持、输入与输出重叠和容量/指针区间边界。

测试源码只做 AST 检查，未编译或执行。Host C/Python、moving GC/OOM、关闭/队列
竞争和设备测试留到 Wi-Fi API 完成后；本批没有以测试通过为依据声称已验证运行。

C5 最终 build passed，bin `0x28af30`、app 剩余 15%。nm 确认两个新入口存在于
目标 object，但最终 ELF 未链接它们（尚无调用方）；bin 大小不变不证明新增运行
内存消耗或性能。

MQuickJS 61 sources / 48 snippets、manifest 47 classes / 415 functions、feature
文档 27、config schema 35 STA / 21 AP 与 live SDK、strict TS declaration、
recorded map、两份 Python AST 和 whitespace 检查通过。SDK source clean。
日志及 hash：`build/w06-rx-wire-metadata-evidence.json`。

C3/S3/disabled 构建、全部 Host/运行竞争/实机功能/RF/共存、预热后内存比较与 soak
均 not-run。未刷写、串口操作、擦 workspace、构建前端、提交、推送或更新根 gitlink。
下一步补 target snapshot adapter，然后与 Source/Host parser/fixture 一起切换
唯一 v1；长时间 soak 仍待 BLE API 完成后。
