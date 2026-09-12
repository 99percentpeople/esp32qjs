# W-02 二进制 SSID 捕获与交付

firmware `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接 [Station driver 输入](2026-09-09-w02-connect-driver.md)及 [AP driver 输入](2026-09-09-w02-ap-driver.md)。
完整 Wi-Fi 目标继续按[剩余清单](2026-09-08-wifi-api-remaining.md)推进。

## 原生可表示范围

connect 的位置 SSID 与 startAP 的顶层 SSID 现在接受 string 或 ByteSource。
复用已有 raw-config 生产捕获：1..32 字节、整数 byte、长度上限、ByteView read lease
成对释放，输入在任何 driver 操作前复制。文本仍拒绝 NUL；AP 字节输入允许 NUL。
Station 的 wifi_sta_config_t 没有独立 ssid length，字节输入也拒绝 NUL。不会把
截断、转义或文本编码假装成能连接任意 NUL SSID。

完整 32 字节不需要尾 NUL；更短 native 数组的尾部清零。共享 text parser 只用
临时非业务 SSID 验证其余配置，随后覆盖已捕获的字节，原字节不经 UTF-8 往返。
临时 SSID 缓冲区、失败 native config 均 secure-zero。捕获不保留调用者的 ByteView
指针；捕获后关闭/修改源不会改动 native 配置。connect 单参数路径也使用该捕获。

## sole v1 交付契约

共用生产 helper 输出 ssid 与 ssidBytes。ssid 仅在整个有界 span 为合法 UTF-8 时
创建 JS 字符串，否则为 null；ssidBytes 是独立数字数组，精确保留该 native span。
helper 接受 0..32 字节，拒绝无效 native 指针/长度；数组和目标对象均按既有
movable-GC root 契约创建和交付。

以下路径已经接入：connect 结果、AP 启动结果、Station status、扫描记录、Station
关联/断开 watch data。AP 当前 status 已有字节数组，本批改用同一文本校验 helper。
错误详情中的非空 Station SSID 也使用有界 UTF-8 判定，避免构造损坏字符串。
相关源类型的 ssid 改为 string|null，新增 ssidBytes，没有兼容别名或新版本。
能力发现新增 stationOptions.binarySsid/accessPointOptions.binarySsid。

每条路径保持原有数据来源：connect/关联事件是带长度的 native event；AP config
使用显式 length；Station status 是缓存的 requested SSID。扫描 SDK 仅提供 NUL
结尾数组，所以扫描 ssidBytes 是最多 32 字节的 SDK 前缀，不能重建 SDK 未暴露的
后续数据。AP status 采样失败仍返回两者 null；Station 未请求时是空文本/空数组。
文本 null 不意味着断连或隐藏 SSID。没有新增凭据输出。

## 验证与未运行项

C5 immutable Context `build/wireless-contexts/c5`（8 MB/no PSRAM）生产构建 exit 0，
日志 `build/w02-binary-ssid-c5-build.txt`。binary 2,773,504 bytes，比上一批增加 592。
九项静态 Radio/owner/policy/restart 账本不变。链接、源码 hash 和仓库边界见
`build/w02-binary-ssid-evidence.json`。JS 数组的临时堆成本留待集中内存验收，静态
账本不变不能代替 heap/GC/RF 证明。

六份 deferred fixtures 仅 AST 解析：完整 Station/AP parser 的数组、array-like、
ByteView/closed view、32-byte 和长度/byte/NUL 边界；生产 AP 结果的精确字节；
新增实际 connect result/shared helper 的非法 UTF-8、嵌入 NUL、32-byte、结果与原生
snapshot 独立、第 N 次分配失败及 movable-GC roots；既有 AP status/text、status/scan
和 architecture 提取依赖同步。未导入、编译或执行这些 fixture，不报告运行通过。

manifest 49 classes/469 functions、features 27、live SDK schema 35 STA/21 AP、
MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map、whitespace 检查通过。
Host/Python/VM/GC/OOM/fault、C3/S3/feature-disabled、完整 Wi-Fi 阶段与实机功能均
not-run，按约定集中进行；长 soak 放在 BLE API 完成后。没有刷写、串口操作、
擦除 workspace、前端构建、提交、推送或更新根 gitlink。
