# W-08 individual Agreement suspend

`WiFiTwtAgreement.suspend({ durationMs, timeoutMs? })` 已接入唯一 v1，保持
Candidate。定时和无限期暂停使用固定 C5 SDK 的真实 native operation 112；
不是用观察事件模拟状态机。显式 resume、广播协议、完整物理恢复仍未完成。

## SDK 边界与实现

固定 `fff9895c82` 的公开头明确 duration 0 表示无限期暂停。原生 producer
`ieee80211_itwt_information` 在 +0xcc 用 32 位乘法计算 `ms * 1000`，因此本次
捕获限制为 0..4294967 ms。正数由原生 service-period/TSF 逻辑计算恢复时间，
不承诺精确墙钟暂停时长；0 不能冒充 resume。当前没有注册显式恢复方法。

Future worker 先校验精确 Agreement token/generation/lease，使用既有私有
native queue 命令进入 Wi-Fi task，再复核已接受 setup、当前 flow/request ID、
关联 node、pending 重协商、原生 suspend/resume bitmap 以及 timer/TX。
不进行全 flow 操作、不改变共享 PS 策略或断开 Station。每次只保留一个信息
操作；已有原生存储未退休时，新操作失败而不隐式重试。

信息操作的 boot-stable 记录在发送前分配身份。TX 输出前将真实 TX revision
与该操作绑定，完成回调只消费匹配的 live EB/flow/request。事件 31 在精确
producer/callback scope 内复制；原生回调返回（包括后续本地 suspend 副作用）
后记录 complete，再零等待发布观察。原始提交错误、native 错误、观察错误与
退休错误分开保存，队列饱和不消除控制结果。成功不等于 RF 对端验收。

公共 timeout/cancel 只结束等待，不撤销已发送的暂停，也不取消已经安装的
自动恢复 timer。Future 只保存数字身份；原生操作、TX 和 timer 各有独立寿命。
close/GC/runtime teardown 先放弃信息结果并等待真实 TX callback/recycle
完成，再进入已有 Agreement teardown/timer 联合释放。没有 callback 的 TX
仍由既有 native recycler/PM 清理证明结束，不能因超时复用 lane。

`wifi.twt.status().information` 提供当前信息操作及保留错误；退休后为 null。
这是操作诊断，不是当前电源状态。Agreement 的 active 表示协议仍存在，
不会把暂停误报为协议关闭。公共参数和副作用见 [API](../api/wifi-twt.md)。

## 检查与待执行项目

C5 HE、C5 no-SoftAP、C5 Wi-Fi-disabled、C3、S3 五个生产 Build Context 均构建
通过，无新增编译警告。manifest 为全项目 53 classes / 518 functions；严格
类型、feature/schema/SDK map、MQuickJS 语法与生成一致性检查通过。

最终 ELF 的公开 suspend → Radio → native queue → SDK suspend process →
information producer/wake hook，以及 callback/result/关闭退休路径已链接。
原本被链接器剔除的 producer/wake hook 现已实际接入。已有 recycler IRAM/ROM
闭包未引入 allocator、事件发布或 PM 调用。共享 SDK 与本地 archive patch 未变。
具体 hash、调用边和构建记录保存于 `build/w08-twt-suspend-evidence.json`。

C5 信息操作账本首次使用延迟分配 112 B INTERNAL，boot 保留；全局指针与身份
计数共 8 B。原 Radio 八 owner 账本 1664 B、TX 身份数组和 timer 账本保持原预算。
Agreement Future state 从 216 B 增至 264 B（MALLOC_CAP_8BIT，可用 PSRAM）；
JS holder 仍 176 B。这些是静态尺寸，不是实机内存基线。

新增实际 production information 操作 fixture，并将既有 TX producer/output/
callback/recycler fixture 接到同一生产记录；覆盖早到完成、队列失败、错误
身份、重复 callback、回收前保留、分配失败和身份耗尽。其他受影响 fixture 的
依赖同步更新。仅 AST 检查，未导入、编译或运行测试 fixture。

完整 native SDK 调度、Future/GC/OOM、短定时/自动恢复、关闭并发、断连、实机
RF 与内存静止基线仍为 not-run，留到全部 Wi-Fi API 完成后的集中阶段。
本批未刷写、串口操作、提交、推送或更改父仓库 gitlink。长 soak 仍留到 BLE 后。
