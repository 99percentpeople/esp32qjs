# W-08 information 原生提交失败与观察队列

本批处理信息帧到达 TX output 之前的省电引用回滚，以及观察事件阻塞该回滚的
路径。完整 information TX 身份、原生结果/Future 关联、公开 Agreement 暂停/
恢复与 RF/阶段验收仍未完成。

## 固定 SDK 的证据

`ieee80211_itwt_information` 在 `+0x160` 获取 `pm_twt_wake_up` 引用，随后
调用管理帧构造/输出。`he_send_action_twt_information` 的分配失败分支先发布
information 错误事件，再返回 `ESP_ERR_NO_MEM`；该分支没有进入 callback，
也没有偿还该引用。正常 `he_twt_information_txcb` 才调用 `pm_twt_wake_done`。
这来自原始 C5 archive 的静态控制流，尚未作为设备或运行 fixture 故障复现。

`he_twt_information_event_post` 发布 event 31、40 B 数据。原生 `wifi_event_post`
等待观察队列，而调用方仍可能需要继续 PM/参数清理，因此不能沿用无限等待。

## 变更

- 为 native suspend → information 调用编码同步 scope，沿用现有 TX
  锁记录 task、是否获取 PM、是否已进入 information output。参数及原始错误
  原样交给 SDK；scope 地址不进入 callback 或队列，返回前撤销。
- 只重定向 `ieee80211_itwt_information + 0x160` 的一个 PM 调用点。若取得引用
  而没有 EB 到达 output，则在原生提交返回后归还一次；仅有 output error
  不能证明 TX 结束，不能提前归还。正常完成的 PM 路径保持原生行为。
- 同一 native task 的同步嵌套调用保留各自 scope；不同 task 的重叠调用在
  driver 前拒绝，不能留下指向已退出栈帧的链。
- event 31 按公开字段归一化，未选 flow 的时间与 padding 清零，零等待发布。
  这是观察事件，不以 flow bitmap 伪造 operation identity 或完成尚未实现的
  information Future。队列满返回观察错误，不能阻止本地回滚。

共用 TX ledger 仍为 64 × 12 B；新增的是同步 scope 指针，不新增 pool 或
常驻 native operation registry。scope 中的几个值留在当前原生调用栈。

## 验证边界

新增生产 TX/事件路由 fixture 覆盖无 output 回滚、无 wake 的 preflight 返回、
嵌套/外来 task、提前完成、output error 保留引用、观察队列失败与字段归一化。
本批仅 AST，未导入、编译或运行 fixture。构建及原始 archive/最终 ELF 路由
证据记录在 `build/w08-twt-information-submit-evidence.json`，构建/静态检查通过：

| Build Context | binary bytes | 相对 teardown-retire |
| --- | ---: | ---: |
| C5 roaming | 2,991,264 | +272 |
| C5 no-SoftAP | 2,867,664 | +272 |
| C5 Wi-Fi-disabled | 459,024 | 0 |
| C3 | 2,665,696 | 0 |
| S3 | 2,570,768 | 0 |

当前 final ELF 已链接 event 31 的零等待发布。information producer wrapper 与
其 PM hook 已存在生产 archive，精确 `+0x160` relocation 已核对，但这条调用
链因缺少公开 suspend caller 被 final ELF 的 GC 移除。**不能声称提交回滚已在
当前公开 API 中生效。** 后续接入 caller 时须重新核对实际链接和运行路径。

共用 TX 静态对象从 36 B 增为 40 B；768 B 延迟 TX ledger 不变，其他已有
静态对象尺寸保持。共享 SDK 未修改，两个 C5 context 的 build-local patch
hash 相同。全项目 manifest（52 classes / 513 functions）、27 feature docs、
配置 schema、1,267 项 SDK map、严格类型、MQuickJS 61 sources / 60 snippets
与 5 份 fixture AST 通过。所有阶段运行、实机/RF 仍为 `not-run`，不刷写、不提交。
