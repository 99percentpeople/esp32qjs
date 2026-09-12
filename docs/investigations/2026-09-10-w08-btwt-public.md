# W-08 广播 TWT Radio / Agreement / Future 公开接入

本批将此前已有的广播提交、原生结果、teardown 和联合回收接入真正的 Radio
owner、Future 与 `WiFiTwtAgreement`。`wifi.twt.setupBroadcast(options)` 已注册
为 C5 HE Candidate native-driver Future；C3/S3/Wi-Fi-disabled 不注册该模块。
完整故障恢复、all-flow 策略、其余 Wi-Fi API 和阶段运行/实机验收仍未完成。

## 公共生命周期

复用同一个 Agreement 类，状态以 `kind: individual | broadcast` 区分。
广播支持 setup/status/close；共享 prototype 的 suspend/resume 在广播 handle
上于分配/原生操作前拒绝。setup 捕获只接受已实现的 command、broadcastId、
responseTimeoutMs 和 timeoutMs，复用生产预验证器；不新增占位接口或 v2。

Radio 提交要求已经启动并关联的 Station，沿用当前信道、精确 lease 和 mutation
排他。SDK 仍负责 explicit modem-sleep preflight。目录按 ID 1..31 定位；只在
完整回收且 lease 释放后删除 owner。同 ID 已存在时明确返回 INVALID_STATE，
避免原未完成接线分支沿用前一步 ESP_OK、却没有发布 token 的缺陷。该问题由
源码核对发现；已写生产路径回归，尚未运行，不能声称已取得动态复现结果。

Future 等 worker 发布后才读取结果；广播要求原生 complete、已观察 ACCEPT、
对应 ID、有效 timing 且无错误/含糊/原连接关闭，才构造 Agreement。观察队列
错误不替代控制结果。超时路径不读取正在被 worker 写入的 token/结果。
构造失败、取消、GC 和 runtime 销毁都请求同一 boot cleanup 后缀。公共 Future
结束不释放原生 storage，不提前归还 Radio，也不允许同 ID 复用。

关闭 pending setup 使用取消/联合回收；已建立 setup 使用一次 teardown，再
等待 TX/recycler/PM、timer/native/event 证明。清理 worker 修改退休记录时，
非阻塞 status reader 拒绝该快照；复制后再次验证精确 token/状态，避免读取被
释放或替换的 owner。一个失败 owner 不阻止 worker 尝试其他 owner 的清理。
清理失败保留错误和 stage，不自动重发不确定的 RF 关闭；尚无完整显式物理
恢复 API。无法确认 driver handoff 时保留故障，可能需要设备重启。

## 预算与契约

每个广播 ID 的 wire dialog 在一次物理 boot 内最多 255 个非零值；失败 output
可能消耗一次。close、重连、runtime restart 和 driver restart 不重置。耗尽
在申请新 lease 前返回 NO_MEM。能力发现公开上限，广播 Agreement 状态公开
本 ID 剩余次数；关闭对象只保留最后观察快照。

实际 C5 DWARF：Radio owner 240 B INTERNAL，按 owner 分配并在完整退休后释放；
32 指针目录 128 B INTERNAL，首次使用后 boot 保留。原 3328 B timer/result
延迟池及 1568 B 可选 TX/dialog 存储不变。广播值快照 184 B，JS handle 208 B
（原 176 B，+32），Future state 296 B（原 272 B，+24），后两者沿用普通
8-bit heap 的分配政策。没有把这些编译布局当作实机 free/largest-block 数据。

广播 timing/trigger 字段仅在有效接受后提供；SDK 不给 duration unit，故仍为
null。错误、原生连接撤销、teardown 状态/观察错误和 cleanup stage 分别暴露。
源注册、源类型、manifest、API 文档和本阶段状态同步保持 sole v1。

## 验证范围

五种生产 Build Context（C5、C5-no-SoftAP、C5-Wi-Fi-disabled、C3、S3）构建
通过。manifest 53 classes / 521 functions、27 项 feature docs、Wi-Fi schema、
1267 项 SDK coverage header 检查、严格 TypeScript、MQuickJS 61 sources /
61 doc snippets 通过。生产 ELF 核对公开 submit/Future/Radio/retire 调用链，
此前 archive-only 的广播 submit 与退休 worker 现在由真实 caller 链接。
构建局部 SDK patch 不变，共享 ESP-IDF 保持干净。

证据：`build/w08-btwt-public-evidence.json`。具体二进制尺寸、调用链、对象与
archive 一致性、IRAM recycler 闭包和输入哈希见该文件。

新增 `test_wifi_twt_broadcast_agreement.py` 直接组合生产 Radio lease、广播
准入/关闭和共享 Future worker/poll/取消/清理 helper；只注入 SDK 结果、退休
证明及可控 worker 调度。覆盖重复 ID、旧 token、分配失败、计数耗尽、未确认
handoff、失败后缀、一次 teardown、隐藏清理中快照、观察失败不挡完成、取消
与 runtime 清理。同步旧 individual/probe fixture 的类型与 shared-helper 依赖。
这些 fixture 仅 AST，没有导入、编译或执行；并不证明完整 Future core、移动
GC、实际 RTOS 调度、RF dialog 互操作或 AP 接受行为。

无刷写、串口操作、提交、推送、前端构建或父仓库 gitlink 更新。集中运行及实机
功能验收继续安排在 Wi-Fi API 完成后；长 soak 留到 BLE API 完成后。
