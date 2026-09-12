# W-06：Monitor PCAPNG 与显式时钟锚点

已接入 Host `scripts/esp32qjs_pcapng.py` 和 Monitor CLI 的 PCAPNG 路径。
当前以一个完整 wire batch 为输入，严格 parser 完成后再编码；不会合并不同设备或
boot 的文件。JSONL 保留原默认调用，额外支持原子文件输出。

本批 firmware HEAD `d7db8d1e40ee9f6a522c22806252b2ef712843a7`，SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`。未提交。

## 时钟、身份和数据边界

PCAPNG 要求外部提供 boot-id，并显式选择 relative 或同时提供 UTC/monotonic anchor。
wire 没有 physical boot identity；用户/传输 metadata 必须保证该标识、输入文件和
anchor 属于同一设备的同一次启动。Session/Radio generation 不充当 boot-id。

UTC 映射使用整数微秒相减/相加，拒绝负数、uint64 溢出、浮点/布尔 anchor 和不完整
或互斥的选项。不使用 Host 当前时钟、32-bit wrap 猜测或 driver timestamp 推算 UTC。
relative 使用包含 metadata-only 回调在内的最早 timestamp 作为零点，保留输入顺序和
原始 callback-time。PCAPNG 标准时间字段按 epoch 解读，所以 relative 是用户明确接受
的合成时间线；section/interface comments 标记 NOT UTC，API 文档说明普通 viewer
可能显示 1970，必须使用 relative-time 显示，不能用于跨设备绝对时间关联。

同 boot 内不同 session/radio generation 分配不同 Interface ID。metadata-only
回调写成 section comments，不伪造零字节 packet。每个 EPB 的 comment 保留完整
normalized metadata、外部 boot-id、原始/映射时间、capture/header/proven span 长度
与原 driver reports。仅 metadata-only 的 batch 可生成无 IDB/EPB 的有效 section。

输入先转为不可变 snapshot，输出全量完成后才交给文件层；保留调用中不变的 payload。
PCAPNG captured length 为 Radiotap 加真实 captured bytes；original length 为
Radiotap 加 snap 前 adapter-proven readable span，单独检查 uint32 加法。raw driver
length 从不进入读取、截取或 padding 长度。payload 不增补 RF/FCS 字节。

## Radiotap 与文件发布

实现 PCAPNG 1.0 SHB/IDB/EPB、显式微秒分辨率、Radiotap link type 127、块长前后
重复与零 padding。Radiotap 当前只填写可表达的 RSSI、已知 noise/antenna，以及 HT
已知 bandwidth/MCS/GI/FEC。STBC=true 不说明 stream count，不能伪造 1 stream；
STBC=false 才能标记已知 0。未知 FCS 不写 Flags；已知完整 FCS 也不从 Protected 或
rx_state 推算，短于 CRC 的完整 capture 与 present-FCS 矛盾会拒绝。

没有从 channel number 推测频率/频段，未把 driver legacy rate code、未解码 HE/VHT
SIG 或 callback-time 当作 Radiotap Rate/HE/VHT/TSFT。所有现有元数据保留在 comments。

CLI 的 PCAPNG 要求 --output，防止二进制意外写到终端；JSONL 仍默认 stdout。
拒绝输入/输出同路径（含 resolve 后同路径）。完成解析/编码之后，在目标目录写临时
文件、flush/fsync，再 os.replace；失败清理临时文件，保留旧目标内容。

capabilities.hostPcapngConverter=true 表示项目已包含 Host 工具，不表示设备原生
输出 PCAPNG，也不探测当前 Host 安装环境。candidate 稳定等级保持不变。

## 测试与证据

新 `test_wifi_monitor_pcapng.py` 在未来阶段运行时将编译真实 C Monitor
snapshot/envelope/metadata writer，使用其输出调用生产 parser/exporter/CLI。
测试源码涵盖：UTC 高低字/大时间戳、relative 原点与长空窗、metadata-only 不伪造
packet、session/radio interfaces、保留逆序时间、截断与独立 driver report、uint32
original-length 溢出、Radiotap HT availability/STBC/HE/FCS、boot/anchor 输入拒绝、
逐位置截断、无部分输出、原子替换失败保留旧文件和清理临时文件。另有实际 tshark
读取测试，缺少工具时标记 skip，不把内部块结构检查当成外部 reader 资格。

按照用户要求，这些测试没有编译或运行；没有执行 Host decoder/exporter 或 tshark。
本批只运行允许的 Python AST（3 文件）、MQuickJS 语法（61 sources / 48 snippets）、
manifest（47 classes / 417 functions）、feature docs（27）、config schema（35 STA /
21 AP，live SDK 匹配）、strict TypeScript declarations、recorded SDK map 和 whitespace。

使用既有不可变 `firmware-ci-esp32c5-representative` Build Context，C5 构建通过。
镜像仍为 `0x28c7d0`，app 分区余量 15%，SDK 保持干净。Host exporter 增量不增加
设备 payload/control 分配，capability 布尔值变化没有增加该镜像大小；这不是新 heap
或 SRAM 实机测量。

证据索引 `build/w06-monitor-pcapng-evidence.json`。三目标/feature-disabled、Host
全量、Wireshark、RF/实机和 heap 验收仍待 Wi-Fi API 完成后的集中测试；长 soak 等
BLE API 也完成。未 flash、访问串口、擦除 workspace、构建前端、提交/推送或更新
root gitlink。

## 格式依据

- [PCAPNG draft-ietf-opsawg-pcapng-05，2026-03](https://www.ietf.org/archive/id/draft-ietf-opsawg-pcapng-05.html)：1.0 blocks/options、微秒时间和 capture mechanism original length；它仍是 Internet-Draft。
- [Radiotap defined fields](https://www.radiotap.org/fields/defined)、[MCS](https://raw.githubusercontent.com/radiotap/radiotap.github.io/master/fields/MCS.md)、[Flags](https://raw.githubusercontent.com/radiotap/radiotap.github.io/master/fields/Flags.md)：字段位、known flags 和 FCS 语义。
- [PCAP link-layer types](https://www.ietf.org/archive/id/draft-ietf-opsawg-pcaplinktype-15.html)：Radiotap link type 127。
