# W-02 Neighbor report watch snapshot

firmware `d7db8d1` 工作区增量；固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接 [TWT snapshot](2026-09-09-w02-watch-twt.md)。本批实现观察事件的变长捕获与
交付，未实现主动 neighbor report request 或 roaming owner/Future；完整范围仍见
[剩余清单](2026-09-08-wifi-api-remaining.md)。

## 输入证据与正式字段

固定 SDK `components/wpa_supplicant/esp_supplicant/src/esp_common.c` 的
`neighbor_report_recvd_cb` 分配 `sizeof(wifi_event_neighbor_report_t)+report_len`，
复制 report 后交由 `esp_event_post` 再复制，随后释放自己的 allocation。
`components/wpa_supplicant/src/common/rrm.c` 验证并保留首字节 dialog token，
timeout 使用 NULL report。框架仅在事件 callback 有效期间读取 SDK span，未保存
SDK pointer，也不负责释放 SDK allocation。event API 不提供独立 allocation 长度；
结构头部 report_len 的可信边界是上述固定 SDK producer，不能用于任意不可信指针。

只复制经过完整 TLV 长度检查的白名单字段。Neighbor Report IE ID 52、candidate
preference subelement ID 3 来自固定 SDK `ieee802_11_defs.h`，固定邻居 body 至少
13 bytes。BSSID information 按 little endian 解码为完整 unsigned 32-bit。
外层/子元素长度不完整、preference 长度不是 1 或重复 preference 均拒绝整条 data。
未知外层 IE/子元素只计数，不复制内容；没有原始结构、padding、vendor byte 导出。

正式 data 为 `{received, reportLength, dialogToken, neighbors, skippedElements}`。
每项 neighbor 是 `{bssid, bssidInformation, operatingClass, channel, phyTypeId,
candidatePreference, skippedSubelements}`，缺失 preference 为 null。
reportLength 包含 token。NULL 事件为 received false、length 0、token null、空数组；
token-only 报告为 received true、空数组。两者不混同。非 NULL 零长度为非法布局。

报告超过 4096 bytes 或 64 项返回 data null / too-large，不截断；非法 TLV 返回
unsupported-layout；两槽占满返回 capacity。限制为框架公开上限，不声称是 SDK/RF
最大长度。未申请 neighbor 的订阅不触发其 payload 读取或 pool allocation。
rawEventData 仍不可用，WPS/DPP/NAN 仍按 sensitive 处理。

## 有界生命周期和所有权

首个关注 neighbor 的 watch 在 runtime task 分配一个两槽 heap pool。每槽包含最多
64 条已规范化邻居；callback 不分配 heap，不操作 JS。所有 subscriber 共享 immutable
slot，通过独立引用持有，普通 watch ingress/subscriber 条目尺寸保持不变。

引用路径已接入生产实现：capture 准入持有一份；ingress 拒绝立即释放；poll 给每个
接收者先 retain，send 拒绝释放该份，fanout 完成释放 ingress；队列 drain/cancel
调用 drop；converter 成功和 OOM 都释放自身引用。EventQueue 在调用 converter
之前标记 event_finished，故不能依赖后续 Future destroy 再释放已转换记录。
直接 drop 会清空该 owned record 的 pointer，重复清理同一记录不会再减计数。

最后一个 watch 关闭先 drain ingress，再将 pool 标为 retired。已经进入订阅队列或
receiver 的记录仍可转换/丢弃，最后一个 native owner 释放后才回收 pool。此期间
新建关注 neighbor 的 watch 返回 WIFI_WATCH_FAILED / ESP_ERR_INVALID_STATE；
过滤掉 neighbor 的 watch 可打开。释放旧队列和 receiver 后可重开，不建立第二代池。
已交付 JS 结果独立拥有所有字段，保留 JS 结果不会保留 native pool。

pool 另用 portMUX，仅保护状态和引用；drop 不反向取得 watch mutex。分配/释放不在
critical section 内。回收期间保持 REAPING，free 返回后才公布 NONE，防止短暂
双池占用。订阅关闭释放 source 后，drop/converter 均不访问 source。

`wifi.status().watch` 新增 neighborSlotsUsed、neighborPoolBytes、neighborPoolRetired；
`wifi.capabilities().limits` 新增 watchNeighborSlots、watchMaxNeighbors、watchMaxReportBytes。
资源容量失败由该事件 reason 可见，不混入 ingressDropped；队列饱和仍各自计数。
本实现是单个模块的有界池，不等同于 W-09 全局 active/retired/control-reserve 完成。

## 构建与延期验证

C5 immutable Build Context `build/wireless-contexts/c5` 生产构建 exit 0，日志
`build/w02-watch-neighbor-c5-build.txt`，binary 2,778,480 bytes（较前批 +2,608）。
ELF DWARF：event 88 bytes，neighbor 20 bytes，slot 1,292 bytes；按需 pool 共 2,584
bytes。C5 新增 pool pointer/state/active 三项静态账本共 12 bytes，九项既有 Radio/
owner/policy/restart 静态大小不变。该比较不是实机静止态 heap/碎片或生命周期证据。

manifest 49 classes/469 functions、features 27、live SDK config schema 35 STA/21 AP、
MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map 与 whitespace 检查通过。
源码/SDK hash、仓库边界及 ELF 记录见 `build/w02-watch-neighbor-evidence.json`。

新 fixture 直接提取生产 parser、pool、capture、poll、close、converter，注入 SDK/
锁/分配/FIFO/VM 边界：17 种输入和第 N 次 JS 分配失败/移动 GC、NULL 与空报告、
4096 bytes/64 项上限、非法 TLV、未知字段过滤、两槽容量、订阅 fanout/reject/filter、
关闭 drain 与退休池重开拒绝。旧 values/TWT fixture 同步新依赖，旧非法指针 case
改为 sensitive DPP 事件。三份文件仅 AST 解析，未 import、编译或运行；没有测试
通过声明。完整 EventQueue/Future 调度、runtime teardown、并发释放仍待阶段执行。

Host/Python/VM、C3/S3/feature-disabled、实机/RF/共存均 not-run，Wi-Fi 全部 API
完成后统一测试；长 soak 留到 BLE API 完成后。未刷写、串口操作、擦除 workspace、
前端构建、提交、推送或更新父仓库 gitlink，不提升稳定等级。
