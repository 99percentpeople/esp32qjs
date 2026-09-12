# W-08 广播 teardown 原生提交、TX 与 PM 所有权

本批接入单个已建立广播协议的原生 teardown 提交、callback 18 完成关联和
控制结果保存。公开广播 setup/close、Radio/Agreement/Future 及 teardown
联合回收协调器仍未完成；不将内部入口注册为占位 API。

## SDK 证据与处理

固定 C5 SDK 的 `ieee80211_btwt_teardown` 在 `+0x5a` 取得 PM wake 引用，
随后通过 `ieee80211_send_action` 构建三字节 body。单 ID control 为
`0x60 | id`，32 表示全部 ID。发送器设置 EB byte 62；受保护帧使用 23/5，
普通帧为 22/7，callback mask 为 bit 18。

共用 `he_twt_teardown_txcb` 原本先从全局 Station 获取当前 node，再归还 PM
引用并分派广播回调。广播回调按固定 24-byte header 读取 control。这些行为
不能作为托管请求的精确身份验证；本批在真实 output/shared ledger/recycler
路径关联请求，在回调前验证原请求、广播 ID、原 node 和当前已建立状态。

广播请求复用既有 teardown 单例，不新增 owner 池；individual/broadcast
互斥提交，不覆盖未退休记录。请求必须来自 held 且成功完成的 setup。原生
连接关闭会永久设置该记录的 `native_closed`，即使新连接复用相同 node 地址
也不恢复旧请求权限。错误、含糊结果或仍在发布/处理的 setup 不准入 teardown。

三字节 body parser 分别使用 output/完成时的真实布局，处理保护头和 native
prefix，拒绝截短或 all-ID 帧。托管广播完成不再进入共用 callback 的全局 node
读取；经精确验证后，将 ID/status 复制成有界规范化 EB，调用 SDK 原广播回调。
SDK 回调不保留或回收该副本；位图、PM 协议及 inactive time 更新仍由 SDK 执行。

复用广播事件 scope 捕获 event 34，SDK 返回后先保存 completion/status/error，
再零等待发布观察。观察失败单独记账，重复或已回收 callback 不再消费控制。
跳过过期回调也仅归还本操作的 PM 引用；recycler 自身保持 IRAM，仅记账、不
调用 PM。输出失败、未收到 callback 或 queue 观察失败都不冒充协议已关闭。

构建局部 archive 仅新增上述 `+0x5a` 的 CALL relocation 重定向，指向现有
teardown wake helper。共享 ESP-IDF 不变，原始 archive SHA gate 保留。
既有共用 callback 的 wake-done 重定向继续供 individual 使用。

## 回收与资源边界

Pending cancel/quiescence 现在还检查广播 teardown 单例的精确身份。即使
teardown 已清除 bitmap，也不能在 TX/recycler/event 顺序未确认前释放 held。
调用方后续必须完成 teardown 的联合屏障，再释放单例并退休 setup。当前没有
公开 caller，不开放绕过该顺序的广播 close。

teardown 单例从 44 B 增至 56 B，增加原生/观察结果及广播类型标志；广播
result 使用已有标志位记录连接关闭，timer entry 仍 104 B，32 项延迟池仍
3328 B。TX 主账本/可选存储、timer 静态状态不增加。

## 验证边界

C5、C5-no-SoftAP、C5-Wi-Fi-disabled、C3、S3 生产 Build Context 构建通过。
生成 manifest/feature/schema/coverage、严格 TypeScript、MQuickJS 语法检查
通过。C5 镜像 3032176 B，无 SoftAP 为 2909168 B，均增加 1584 B；
C3/S3/Wi-Fi-disabled 镜像大小不变。241 项哈希及实际 ELF/archive/IRAM、
SDK relocation 证据保存在
`build/w08-btwt-teardown-evidence.json`；worker 提交包装仍待公开 owner 调用。

待执行 fixture 使用生产 teardown/shared TX/event scope、生产 timer owner
验证和 SDK/private ioctl 实现；覆盖 PMF、提前/重复 callback、队列饱和、
截短/all-ID、连接地址复用与未退休 teardown 阻止 pending 释放。旧 SDK archive
fixture 同步补齐此前 information/individual relocation 的完整期望列表。
fixture 仅 AST，未导入、编译或运行，不作为动态通过证据。

剩余：teardown 联合回收及失败策略、广播公开 owner/Future/Agreement、完整
故障恢复、all-flow 策略及 RF/阶段验证。其他 Wi-Fi 模块缺口仍以当前总表为准；
长 soak 留到 BLE API 完成后。本批未刷写、操作串口、提交或更新根 gitlink。
