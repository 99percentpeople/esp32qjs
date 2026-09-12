# W-08 Action/ROC Radio 接入与两级排空

承接[原生记录与取消保护](2026-09-09-w08-action-native.md)。firmware `d7db8d1`
工作区增量、SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。本批接入生产 Radio
原生入口及 default event-loop 控制路径；公开 `wifi.action` 的输入/Future/ROC
Session、自动 runtime 关闭及故障恢复协调器仍未完成，不增加占位 namespace。

## Radio 准入与 SDK 提交

新增专属 ACTION client 和 ACTION/ROC operation kind。原生 native request 的
interface/channel/secondary/duration/type/callback/op_id、完整 byte span 先校验，
Action body 1..1476 bytes、duration 1..60000 ms；未知组合在 SDK 调用前拒绝。
SDK callback 使用框架专属函数，ROC done_cb 为 NULL，保留集中事件终态路径。
公开 frame/ByteSource 捕获、MAC header 与安全字段检查仍属于下一批适配层，
不能把此处接受 native request 说成公共帧校验已经完成。

admission 要求已初始化健康 STARTED、无 lifecycle/operation/cleanup/restart
隔离，实际 mode 与框架 mode 一致且包含所选接口，不隐式 init/start/set_mode/
set_channel。法规检查同时覆盖 primary 与 secondary 所在信道；SDK 仍负责最终
信道组合检查。APSTA 的 ROC allowBroadcast 在获取 owner 前拒绝。

默认不允许 running AP 或 associated Station 离开当前主副信道。disconnected
STA 的 native off-channel 还要求无其他 RF session 依赖当前信道；Application、
STA helper 和仅持有 IE 数据的 Vendor IE owner 可保留。已有 strict fixed-channel
owner 必须匹配请求信道；进行中的 Raw TX、channel-conflict owner 拒绝进入。
共享 registry 的 channel/冲突字段在 snapshot critical section 中检查，避免和
原生 channel callback 并发访问。实际 RF/coexistence、自动漫游时序仍未验收。

Radio mutation mutex 中先获取 exact ACTION lease，再创建 exclusive operation
与原生 lane token，SDK 调用在 critical section 外。SDK 返回 error 时仍把结果
和实际 op_id 写回 lane，保留 token/lease；不声称 error 或 timeout 证明没有
原生操作。通用 release 无法越过 operation 保护；通用 end_operation 拒绝专属
Action/ROC token，只允许 dedicated retire 解除保护。

wifi.status().radio.clients 增加 wifiAction 并计入 total，现有唯一 v1 源类型
同步。尚无 JS 发起入口，因此当前观察时通常为零；这是 client 账本字段，不是
Action capability 或完整公共 status 契约。

## 控制终态和排空

Radio 的 boot-owned Wi-Fi listener 在 lifecycle/channel 处理路径中读取
ACTION_TX_STATUS/ROC_DONE 的 native scalar fields，直接更新 bounded lane；
无 JS、无额外观察 queue、无 SDK 指针保留。该路径不依赖 wifi.watch 队列容量。
原生 lane 序号/终态由 Radio snapshot critical section 与 runtime 调用串行化。

retire 先确认 native terminal，再调用真正经过 SDK ioctl 执行队列的屏障，最后
向 default event loop 发布 exact token+revision marker。单纯 get_mode 等 getter
不能充当 SDK 执行队列屏障。新增 private sentinel 通过 esp_wifi_action_tx_req
的消息路径进入 __wrap_wifi_action_tx_process，仅在专属 callback 和私有 type
同时匹配时直接返回 ESP_OK，不读 owner、不委托 driver mutation。SDK wrapper
仍会分配其普通 24-byte ioctl 消息；分配失败保留 cleanup 状态供重试。进一步
审查三目标 ieee80211_ioctl/process：runtime task 路径经 pp_post 入 Wi-Fi task，
等待 semaphore，handler 返回值先写回消息再唤醒 caller；Wi-Fi task 自调用则
同步执行 handler。外围仍有 SDK 的成对 PM wake/done，屏障不等于完全没有临时
运行成本，也不承诺中断卡住的 SDK task。新增反汇编同存 w08-action-sdk 目录。

SDK barrier 通过后发布 control event id=2，原 lifecycle marker 仍为 id=1；
boot handler 注册到同一私有 base 的 ANY_ID 并按 id 解码。两个身份空间互不
借用，Action marker 还校验 physical generation。Event post 是非阻塞调用，
失败撤销 posted 标记，下次只重试未完成后缀；不会重新提交 Action/ROC。

迟到或重复终态会使旧 SDK/event fence 失效并重置 posted。若事件发生在 SDK
fence 成功后、准备 marker 之前，必须重新取得 SDK fence，不能发布一个无法
被确认的旧 marker 并一直等待。只有两级 fence 都满足才清除 operation、lane，
释放 exact ACTION lease 并清空 caller token。取消返回 OK 同样只记录 accepted，
等待 native terminal；失败保存 raw code，可显式重试 cancel 后缀。

## 仍需完成的接入

- 公开请求的 ByteSource/GC/参数捕获、结果与 errors、Future/ROC Session。
- Future timeout、Session close、runtime teardown 的中央 native cleanup 交接。
- SDK submit 失败且没有 terminal、丢失/矛盾 completion 的物理终止恢复；本批会
  保留 owner，不允许通过 runtime restart 丢弃责任。
- 实际 native/default-loop 并发、自动漫游/home-channel 变化及相关对端/RF 证明。
- 最终公共路径连接后核对 ELF 的 SDK handler 指向 __wrap 函数；当前未引用的
  submit/cancel/retire/SDK shim 可能由 linker GC 移除，不能仅凭编译或链接选项
  断言已经在设备调用链生效。

## 待执行生产路径用例

`test_wifi_action_radio.py` 复用真实 registry 与 lease/lifecycle helper，加入
实际 Action admission、submit/cancel、event decoder、private marker handler、
retire。编写无效 span/duration、未 STARTED、connected/AP off-channel 拒绝、
早到双阶段事件、不能通用释放、旧 token、queue post 失败、迟到终态旧 marker、
SDK error/取消失败后缀、ROC id=0 和最终零 owner。SDK getter/setter、执行队列
fence 与 event post 调度为注入边界，不代替实际 SDK/FreeRTOS 证明。

`test_wifi_action_sdk.py` 增加 private sentinel 零 native delegate 和非框架
sentinel 透传；status GC fixture 同步新增 client 槽。全部只 AST，未导入、编译
或执行。完整 Host/Python/VM、三目标/disabled matrix 和实机阶段测试仍待 Wi-Fi
API 完成后，长 soak 留到 BLE API 完成之后。

最终 C5 immutable Context build exit 0，binary 2,811,520 bytes（前批 2,810,576，
增加 944）。新增 s_action 静态记录 84 bytes；原有 Radio 832 bytes 及其他无线
静态账本大小不变。控制事件 ledger/marker 处理已链接；未引用的公开适配前置
submit/cancel/retire 与 SDK shim 仍被 linker GC 移除。manifest 49 classes/474
functions、strict TypeScript、SDK map、三份 fixture AST、whitespace 检查通过。
源/产物哈希、实际符号和 not-run 见 `build/w08-action-radio-evidence.json`。
未刷写、串口操作、擦 workspace、构建前端、提交、推送或更新根 gitlink。
