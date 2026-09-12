# W-08 iTWT setup 原生结果与观察队列

基线 firmware `d7db8d1`、SDK `fff9895c82`，接续
[setup timer 身份](2026-09-10-w08-twt-setup-timer.md)。本批接入 C5 setup 原生事件
的零等待发布，并编码有界结果记录与 SDK 提交前后的交接。Agreement 公开 API、
完整 TX/RF 身份及退休仍未完成；现有公开 probe 保持 Candidate。

## 确认的问题

固定 C5 `he_twt_setup_event_post` 生成 32 B 的 `WIFI_EVENT_ITWT_SETUP`（28），
再调用 `wifi_event_post`。后者通过 OSI `_event_post` 传入无限等待参数；默认
事件队列满时，原生 Wi-Fi task 会停在观察事件发布。

成功 dwell 的部分 HW 状态更新、timer done 和 pending mask 清理，以及 TX
完成路径的 temporary ID 清理位于发布之后，可能因此无法继续。response
timeout 则先清理 timer/pending，再 tail-call 发布；不能把所有分支写成相同
顺序，但无限等待仍会阻塞随后队列操作。这是固定 archive 的静态证据，没有
宣称运行竞争或设备故障已复现。

同一 event helper 在成功和失败路径均写入连接 ID、配置字段、status、reason
与 target wake time；SDK success 是 **1**，不是 `ESP_OK`。审查的 C5 ABI：
config 在 0、status 在 16、reason 在 20、target wake time 在 24，总计 32 B。
字段之外的 padding 不构成公共数据。

## 已编码的结果记录

- 八个有界记录，首次需要时分配；不依赖 JS roots、观察队列或 runtime 地址。
  boot-scoped uint32 identity 不回绕；0..32767 的 request ID 必须严格递增，
  包含 SDK 返回错误的已预留尝试。清空记录不会使旧 ID 可复用。
- SDK setup wrapper 在 driver mutation 和 flow 写回前预留记录；原生调用返回
  后记录实际 SDK/timer error 并结束 submitting。事件可能在调用返回前到达；
  提前结果不会被返回错误或重复 submitted 覆盖。
- 事件按已预留 request ID 定位记录，先保存首个结果，再零等待发布观察。
  同一身份的冲突事件标记 ambiguous，保留原结果；后继槽使用新 ID，旧事件
  不能仅因槽位复用命中它。未托管事件仍可发布，但不会分配或创造 owner。
- 只复制定义字段，清零 padding；成功 reason 归零，其他 status/reason 保留
  原始值。AP 配置与 64-bit target wake time 原样保存，未按请求范围重新过滤。
- 原生控制结果、SDK 返回错误、首个观察投递错误分别保留。队列满/OOM 返回
  原生调用方，不抹掉已保存结果。临界区内没有 heap、driver 或事件调用。

只读接口按精确 numeric identity 返回副本，错误不改写输出。release 接口是
退休证明的消费者：必须在调用方独立证明 TX/timer/native/event/HW 排空后，
再核对同一 revision、无提交和观察调用存活。结果到达、timeout 或空 bitmap
都不是该证明。每次相关事件和提交/观察结束都会改变 revision；耗尽明确故障，
不把饱和 revision 当作可释放状态。实际退休协调器仍待接入，不能据此报告
Agreement 已回收或 runtime restart 能恢复。

## 实际调用链和剩余边界

最终 C5 ELF 的 `he_twt_setup_event_post` 已调用 `__wrap_wifi_event_post`，再进入
`esp32_mquickjs_wifi_twt_setup_result_post` 和零等待 `esp_event_post`。既有 probe
分支保留，其他 Wi-Fi 事件继续原 SDK 路由。SDK snapshot 包含结果记录的当前
占用、最后身份、ID、预算和全局故障；这些都是内部值，没有新增公开占位类型。

预留、submitted、read/release 和 setup admission wrapper 当前只编译进入
archive，尚无公开 Agreement caller，所以本镜像没有实际建立托管 setup owner。
不能把事件发布已接入等同于公开 setup 已可调用或 Future 已完成。

仍待完成：SDK setup 调用方与身份返回、Agreement Radio/Future/owner、完整
取消/teardown/suspend、setup TX/RF 关联、广播/Information 路径、事件 fence
及物理恢复。原 SDK 的 TX 回调可能依据当前可复用 RF dialog 找到 pending；
本批按连接 ID 保存结果，不证明 SDK 生成该 ID 的 TX/RF 关联已正确。

## 验证与预算

immutable Build Context 生产构建通过，没有烧录：

| Context / build directory | 镜像字节 | 相对 setup timer 批次 |
| --- | ---: | ---: |
| c5-roaming / wireless-c5-roaming | 2,976,656 | +1,296 |
| c5-no-softap / wireless-c5-no-softap | 2,853,056 | +1,296 |
| c5-disabled / wireless-c5-disabled | 459,024 | 0 |
| c3 / wireless-c3 | 2,665,696 | 0 |
| s3 / wireless-s3 | 2,570,768 | 0 |

从 firmware 加载 `/home/zach/esp/esp-idf/export.sh` 后，使用
`.venv/bin/python scripts/remote.py --build-context build/wireless-contexts/<context> --build-dir <directory> --assume y build`。
五份构建日志无 compiler warning/error，生产对象与 archive member 一致；新增
路径限 C5 HE Wi-Fi gate。共享 SDK 干净，原始/已有 build-local patched archive
hash 不变，本批没有新二进制 SDK 补丁。

C5 新增静态 SRAM **24 B**，此前 39 个跟踪对象及 128 B probe Future capture
尺寸不变。结果记录按需分配预算为 **8 × 56 = 448 B INTERNAL/8BIT**，boot
保留，不宣称 close 后归还。该 allocator 目前只在 archive，未在这份公开
probe 镜像中链接；448 B 是已编译实现预算，不是实机分配测量。既有 setup
timer 192 B、TX ledger 512 B、SDK timer 分配继续分别记账。

manifest 52 classes/513 functions、feature 文档 27、SDK schema STA35/AP21、
strict TypeScript、MQuickJS 61 sources/60 snippets、Wi-Fi coverage map 1267
条和 whitespace 检查通过。最终 ELF 核对发布调用链、前批 timer/OSI 路由、
三个 probe ROM 方法及 Future driver 九个 callback 指针。证据与 hash 在
`build/w08-twt-setup-result-evidence.json`，摘要、构建日志和 disassembly 使用
同名前缀。

新增 `test_wifi_twt_setup_result.py` 调用生产 registry 和公共 native event
wrapper，注入 heap、锁和事件投递，描述提前完成、队列失败、重复/冲突、八槽
满、释放期间存活引用、旧 ID/slot 复用、padding、原始错误和计数耗尽。SDK
fixture 补预留失败前不 mutation、提交结果交接；probe/TX fixture 的 setup
边界使用独立 stub。release 用例只检验证明消费者，不冒充真实 SDK 排空。

仅 AST 检查，未 import、编译或执行 fixture。Host/VM、完整 SDK 调度、设备
close/reopen/GC/runtime restart、RF/对端/共存和实际 heap 比较均 not-run。
集中运行与实机测试在全部 Wi-Fi API 完成后；长 soak 在 BLE API 完成后。
没有串口、烧录、擦除 workspace、前端构建、提交、推送或根 gitlink 更新。
