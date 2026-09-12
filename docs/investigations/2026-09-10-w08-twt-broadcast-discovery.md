# W-08 广播 TWT 发现

`wifi.twt.broadcasts({timeoutMs?})` 已连接原生快照、Radio 准入和 Future，保持
唯一 v1、C5 HE Candidate。它交付关联 AP 的缓存公告及捕获时的 native joined
位图，不创建 Agreement，不把公告条数当成已建立协议数。广播 setup/teardown
及其 owner/TX/timer 生命周期仍未完成。

## SDK 边界

固定 SDK `fff9895c82` 的 `esp_wifi_sta_get_btwt_num` 单独读取缓存计数；
`esp_wifi_sta_btwt_get_info` 通过 ioctl 119 的处理器调用
`ieee80211_get_btwt_info`。原生 getter 扫描 32 个 ID，按调用方容量压紧输出，
每条十字节，填入 `btwt_id_in_use=true`，但不回传实际填入条数。单独查询
count 后按该容量读取会有公告变化窗口。

本实现复用已审查的私有 Action/native ioctl 通路，在 Wi-Fi task 中核对
当前 Station state=5 及关联 node，再一次完成 count/get/加入位图复制。
原生 getter 固定传 32 条容量，缓冲区先清零，实际条数由已填入项计算；
空公告不调用要求已分配 AP 表的 getter。重复 ID、超界计数和 getter 错误
都保留错误，不交付部分输出。此处调用实际 native getter，避免在 native
task 内递归调用 public ioctl API。

归档 `ieee80211_get_btwt_info + 0xd0` 读取帧参数 byte 4，并在 `+0xd4`
写入 output byte 4。它是原始 minimum wake duration 数值；头文件将该字段
注释为单位，不能据此把 0/1 当单位解码。getter 未交付单位位，因此公开
`minimumWakeDuration` 并明确 `wakeDurationUnit:null`；interval 使用 uint64
左移后转换 JS number，最大值 140735340871680 小于 2^53。

## 生命周期和公共契约

Radio mutation mutex 覆盖健康 started/Station 准入、原生读取和 generation
复制，SDK 调用不进入临界区。查询不获取长期 lease、不发送 RF、不改 PS，
允许现有 owner 存活。关联状态由 native task 再检查；radioGeneration 不是
association identity，返回后 AP 公告和连接都可能改变。

Future 在后台 worker 读取 native 值。默认等待 6000 ms，合法 1..60000；
这是 public wait deadline，不是 RF 响应时间。超时/取消后可丢弃公开结果，
但 worker_done 发布前继续保留 storage。使用现有 Future core 的 poll 与
runtime teardown 等待，不增加清理 owner。JS 转换期间 result/array/item
分别 rooting；转换失败只释放本次值，不触及协议。

正式类型、注册、manifest、API 文档同步，SDK map 保持 in-progress，
global broadcastTwt 能力仍不能因为 discovery 存在就宣称 setup 已实现。

## 验证范围

C5 HE、C5 no-SoftAP、C5 Wi-Fi-disabled、C3、S3 五个生产配置构建通过，
构建日志无 warning/error。manifest 为全项目 53 classes / 520 functions；
27 feature docs、SDK schema/map、严格 TypeScript 与 MQuickJS 61 sources /
61 snippets 检查通过。最终 ELF 核对公开入口/Future 注册、worker→Radio→
私有 ioctl→实际 native getter 路由及原 recycler IRAM/ROM 闭包。
证据在 `build/w08-twt-broadcast-evidence.json`。

单次 discovery Future state 为 344 B（MALLOC_CAP_8BIT，可用 PSRAM），
没有新增 boot-retained INTERNAL 账本。C5 两配置镜像各增加 3488 B，分别为
3022192 B 和 2898576 B；disabled/C3/S3 镜像大小不变。既有 TWT 固定账本
尺寸及共享 SDK/本地 archive patch 均不变。这些是构建和静态大小证据，
不是实机 heap 基线或 RF 正确性证明。

生产 SDK/私有 dispatch fixture 已补：无公告、32 条/ID 31、压紧输出、错误
不覆盖输出、重复 ID、断连准入。新增生产 Radio/Future + 真 MQuickJS fixture
覆盖 worker 饱和、cancel/timeout 存储保留、Radio 生命周期拒绝、最大 interval
和第 N 次分配失败/GC。按用户安排仅检查 Python AST，不导入、编译、运行。
完整 Future core 调度、native task/RF、真实 heap/stack 与运行验收仍 not-run。
Wi-Fi 功能完成后集中验证；长 soak 留到 BLE API 完成后。

未刷写、串口操作、提交、推送或修改根 gitlink。
