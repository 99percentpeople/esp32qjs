# W-08：Neighbor Report 公开 Request/Future

firmware `d7db8d1` 未提交工作区，固定 SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`。承接
[原生 Request/Radio](2026-09-10-w08-rrm-request.md)，本批把公开入口、报告转换、
Future waiter 和观察发布接入生产路径。等级保留 Candidate。

## 公开契约

RRM gate 注册 `wifi.roaming.requestNeighborReport(options?)`、`status()` 及
`WiFiNeighborReportRequest.status/receive/cancel/close`。open 和 receive 注册为
native-driver Future，直接调用使用现有 cooperative wait。类型、manifest、
ROM global/prototype、runtime registration 和 API 文档同步，仍为唯一 v1。

open 的 timeoutMs 为 1..60000，默认 1500；maxReportBytes 为 1..4096，默认
4096。参数捕获不做 SDK I/O。原生分配完成后才 start；返回 handle 只确认 SDK
提交。JS handle 构造失败时不交接引用，原生请求由清理 registry 继续关闭。
receive 同样有 1..60000/default 1500 的 deadline，正常报告只在精确退休后转换。
重复 receive 复制原报告，不再发请求；转换失败保持报告供再次读取或 close。

receive 的本地 deadline 请求原生取消并拒绝 Future；started Future 的 cancel
也取消该请求。固定 Future core 对 still-queued cancellation 直接 destroy driver，
因此 queued receive 只放弃 waiter，queued opening 则关闭未交接的原生请求。
已选 native terminal 不被迟到 callback 或后续 cancel 覆盖。
SDK null callback 仍称 native-no-report，不能把 reset 伪装为明确超时。

cancel/close 为同步意图操作，不等待 native retirement。close 立即撤销访问，
已返回的 JS report 不受影响；status 保留 cleanupPending/错误/序号及预算。
模块 status 提供资源计数和当前原生 request，供 opening Future 失败后继续诊断。
计数与 request 是相邻的观察快照，不是 admission token。

## 报告与 GC

从 `wifi.watch()` 抽出原有 Neighbor Report IE 字段 decoder，保留结构布局与
字段意义，两个公开路径共用它。解析固定 13-byte body、LE uint32 information、
candidate preference 及有界 subelement TLV；重复 preference/截断/错误长度拒绝，
未知 TLV 仅计数。decoder 失败不部分写入输出。

Request 转换逐段读取退休后的原生 span，单次最多 255 bytes，邻居上限 64。
所有 JS result/list/item 跨分配均有 GC root；不会把 callback/SDK pointer 留给 JS。
错误/关闭/OOM 只影响这次结果构造，不触发 resend。异常对象也使用独立 roots，
并保留原生 cleanup 状态。完整 VM class handoff/真实 Future 调度仍待阶段测试。

审查还确认 Future core 在 capture 后释放原始 receiver root。receive driver
因此独立保存 receiver GC ref，防止 JS handle 在等待期间被 finalizer 提前关闭；
driver destroy 释放该 root。JSContext/GC ref 只在 runtime 驱动状态内，原生
Request、worker 和 SDK callback 仍不持有 JS 指针。该 GC 场景的实机/完整 Future
回归仍 not-run，不能把已有纯 converter fixture 当作此场景的验证。

结果明确带 `correlation:"sdk-dialog-token"`。8-bit dialog token 在回绕或连接
reset 后仍有 RF 迟到报告歧义；boot operation identity 只保护 native callback
storage，不能声称已实现空中 freshness。专属 roaming watch 和 RF 隔离仍未完成。

## Future 与观察顺序

实际检查 Future core：正常 finish 后先 destroy driver，再 future_settle；queued
cancel 也先 destroy，再写公开 CANCELLED。因此在 driver destroy 直接 post 观察
会过早。本批没有改 Future core，而使用它已有的 runtime poll 顺序：

1. 每个 opening/receive capture 增加 native waiter，并独立 retain 请求。
2. driver destroy 只移除 waiter/引用、推进后台清理，不发布观察。
3. `esp32_mquickjs_poll` 先完成 Future polling，之后调用注册的 Wi-Fi poller。
4. 该 poller 才发布已退休且无 waiter 的请求观察，最多处理 8 个有界记录。

观察列表本身持有一个原生引用，publish 时以 busy 标记保护报告 span。
default event allocation/queue 满只丢观察；终态和 Radio 退休不依赖发布结果。
closed/cancelled/oversized 记录可跳过发布，runtime teardown 丢弃待观察记录。
原生 worker 与 SDK callback 不调用此发布路径。后来才创建的 receive 可以读取
已经被观察的保留报告；不宣称观察相对未来尚不存在的 Future 也延迟。

这组顺序目前有生产源码和链接证据，尚未运行完整 Future-core/GC/reentry/队列
调度验证，不能把源码审查替代该测试。

## 构建与准备的验证

| 实际构建 | binary | 范围 |
| --- | ---: | --- |
| C5 roaming enabled | 2,895,904 bytes | 新类、六个 callable、两个 Future drivers、SDK/observer/decoder 链接 |
| 普通 C5，RRM disabled | 2,856,656 bytes | 无 Request/roaming RRM 新注册，共用 watch decoder 仍链接 |

ROM 与 ELF 分别检查 gate；两套 context 不混用。已跟踪 30 个框架静态对象尺寸
保持不变，RRM registry/计数/SDK trace/观察列表指针在 C5 合计 20 bytes，比前批
多 4 bytes。没有添加全局报告数组；动态 request、JS roots、callback stack 和
internal/PSRAM/largest block 尚未实机测量。

manifest 的 write 命令生成成功后，严格 check 实际发现遗漏全局构造器类型声明。
已按现有 factory-only 类补上只读 prototype 声明，不允许 TypeScript 直接 new。
失败日志保留为 `build/w08-neighbor-public-manifest-initial-failure.txt`；最终必须
以 check 成功为准，不能把 write 输出当作完整一致性验证。

manifest 52 classes / 503 functions，open/receive 的 execution 均为 nativeFuture。
features 27、SDK schema STA35/AP21、严格 TypeScript、MQuickJS 61 sources /
58 snippets、SDK map、whitespace 和相关 Python AST 检查收入
`build/w08-neighbor-public-evidence.json`。

新增 `test_wifi_neighbor_capture_gc.py` 组合生产 options、原生分配/读取/释放、
共享 decoder 和 JS converter，在真实 VM fixture 中准备逐次 OOM/GC、异常 getter、
截断、重复 preference、65 项容量和 uint32 高位用例。只注入原生报告 producer。
原生请求 fixture 增加 waiter 未退出时不发布、队列满后继续回收、只发布一次；
已有 watch fixture 的组合代码纳入共用 decoder。四份相关 fixture 均仅 AST，
没有 import、编译或执行；既有 watch 用例也未重跑。

Host/Python/VM、C3/S3/部分 SDK gates/full matrix、实机生命周期/GC/队列/重启/
RRM RF 均 not-run。全部 Wi-Fi API 完成后统一阶段验收，长 soak 等 BLE API 完成。
没有提交、推送、刷写、串口、擦 workspace、修改 SDK/根 gitlink 或构建前端。
