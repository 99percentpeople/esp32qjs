# W-08 SmartConfig 专属观察队列

`WiFiSmartConfigSession.watch({capacity?})` 返回现有 EventQueue，交付
`{sequence, status}`。唯一 v1 的类型、ROM、manifest、capabilities 和 API 文档
同步，保持 Candidate。没有新增 Future driver 或 async poller 槽位。

## 公开行为与原生边界

- 首次轮询发布初始状态，随后只发布 metadata 变化；可合并中间变化，不承诺
  原始 SDK/RF 事件逐条历史。workerBusy 单独变化不触发事件，结构体 padding
  不参与比较。队列不含 SSID/password/AES/custom data/phone 字段。
- 同一 Session 同时一个 observer，容量 1–16、默认 8，全局最多四个 retained
  queue context；关闭后缓冲区尚存时仍计入预算。drop-newest 不重发旧快照，
  dropped/序号空洞可诊断丢失，丢掉末态后通过 Session.status 查询。序号耗尽
  请求关闭，不回绕。
- 观察接入既有 Wi-Fi poller；Future core 在 registered poller 之前运行。
  Session 的 capture/destroy 计数同时挡住“worker 在 Future poll 后完成”的
  交错：receive 可见结果、close 已退休状态在相应 Future 仍持有时不发布。
  计数释放发生在 driver destroy；观察只在后续 runtime poller 执行，此时核心
  已结算 Future。不能用观察队列决定任何连接/ACK/关闭的控制完成。
- open watcher 保留 Session。queue close/GC 先摘除 registry、释放 Session
  引用；只有最后外部引用退出才触发原来的 Session 自动关闭。queue 原生
  destruction 才释放 context/预算，pending receiver/reaper 不能提前归还。
- 轮询临时 retain queue 和 Session，close/reaper 并发不会释放正在读取的对象。
  runtime teardown 先摘除 watcher；已 disposed、无法 retain 的队列也摘除，
  它的迟到 close 回调看到空 Session，不重复 release。
- 私有 queue 创建或 context binding 失败，正确保留其 close callback 的 opaque
  直到关闭；未把 deferred dispose 后的 opaque 提前 free。成功创建的 observer
  则把 context 回收交给 EventQueue 的原生 destruction 回调。

## 验证边界

五种合法 Build Context（C3、S3、C5 roaming、C5 no-SoftAP、C5 Wi-Fi-disabled）
生产构建通过；manifest 54 classes / 530 functions，MQuickJS 61 sources /
62 snippets，类型、文档、schema、SDK coverage 检查通过。当前镜像、ROM/ELF、
静态账本、源码和日志哈希见 `build/w08-smartconfig-watch-evidence.json`。

新增 `test_wifi_smartconfig_watch.py` 使用完整生产 Session 和实际 watch helpers，
注入 SDK/worker/EventQueue 边界，登记 ready-before-observer、worker busy 交错、
饱和不阻止退休、关闭预算保留、最后引用关闭、disposed detach 及序号耗尽。
实际 MQuickJS converter fixture 增加观察对象的 Nth allocation/移动 GC 和无秘密
字段检查。它们只做 AST 解析，未导入、编译或执行；不是完整 EventQueue/Future
core/reaper 调度或 watch JS constructor 的运行证明，这些仍需集中验收。

未刷写、使用串口、提交、推送或变更父仓库 gitlink；共享 SDK 未变。SDK 内部
秘密副本清理仍待完成，Wi-Fi 剩余功能和阶段实机测试继续推进；长 soak 留到
BLE API 完成后。
