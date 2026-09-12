# W-08 individual Agreement 公开接入

`wifi.twt.setupIndividual(options)`、`wifi.twt.agreements()` 及
`WiFiTwtAgreement.status()/close(options?)` 已接入唯一 v1，保持 Candidate。
本批将已有 setup submit 和联合退休执行器连接到实际 Radio/Future/JS 调用方。
完整 TWT 仍缺 broadcast、信息操作结果关联、suspend/resume、RF 晚到帧排除、
物理故障恢复和阶段运行验收。

## 所有权与关闭

八个延迟分配、boot 保留的 owner 共用 Radio lease registry。每个请求独立
持有当前关联信道的 lease；不占住公共 probe 的 operation record，也不静默
改变 mode、信道、连接或 power-save。现有固定信道和配置 owner 检查阻止
扫描、切换连接及共享省电配置覆盖。

请求 ID 与 owner token 消耗单调前缀，包括失败调用；读到原生第三方调用
已经消耗的 ID 时前移，绝不回绕。参数预验证和 owner 容量检查在 SDK 之前。
SDK 准入与真正的原生配置指针交接继续复用已有生产 helper，保持原始 SDK、
driver 与 handoff 错误。非零 token 在提交错误后仍归调用方清理。

关闭读取实际 native request ID 表，找到该请求当前拥有的唯一 flow 后才
提交 teardown。已经 attempted 的 teardown 不因 API 错误或超时再次发送；
pending setup 走精确取消。清理继续使用原有 TX/timer/native/event 顺序、
结果释放与 TX 释放后缀。结果已释放但 marker 后缀失败时，状态仍可读，
重试不再读取已释放结果。所有 native 清理成功后才允许通用 lease release。

Future 仅持有 native token 和值快照，worker 最后一次访问通过原子完成位
发布。超时不读取并发写入的结果；GC finalizer 只请求 close 并释放 JS holder，
不调用 SDK。runtime teardown 请求全部 individual owner 关闭并等待清理；
一个 owner 的关闭不释放其他协议或 Station 的 lease。关闭失败仍可通过
`agreements()` 观察，不能声称 runtime restart 可以恢复物理故障。

## 公共契约

完整请求参数、时间单位、单调 ID 与错误副作用见 [TWT API](../api/wifi-twt.md)。
setup Future 只在无歧义的 accepted setup 后返回对象。close 是等待本地退休
的 Future，取消等待不会撤回已经发出的 close 意图。已关闭对象保留最后读取
的 setup 快照；64-bit 目标唤醒时间以十进制字符串返回，避免 JS number 舍入。
尚未实现的方法未加入正式类型或注册表。

## 检查与未执行项

本批 C5 HE、C5 no-SoftAP、C5 Wi-Fi-disabled、C3、S3 生产构建通过。
链接与静态预算记录见 `build/w08-twt-individual-public-evidence.json`。

| Build Context | binary bytes | 相对 information-tx |
| --- | ---: | ---: |
| C5 roaming | 3,009,872 | +16,800 |
| C5 no-SoftAP | 2,886,272 | +16,800 |
| C5 Wi-Fi-disabled | 459,024 | 0 |
| C3 | 2,665,696 | 0 |
| S3 | 2,570,768 | 0 |

C5 最终 ELF 已包含公开 submit → 原始 SDK API → native setup hook，以及
close worker → 精确 teardown → 联合退休/释放执行器；不再是 archive-only。
GC 与 runtime 清理也调用同一 owner 关闭路径。已有 recycler IRAM 闭包、
共享 SDK archive 和前批已核对的静态对象大小保持不变。

C5 新 Radio owner 数组延迟分配 8 × 208 = 1664 B INTERNAL，并在 boot 内保留；
每个 Future state 为 216 B、JS holder 为 176 B（MALLOC_CAP_8BIT，可用 PSRAM）。
这些是 ELF/分配参数静态尺寸，不是实机 heap 基线。五个构建不等于完整 feature
矩阵；当前 C5 roaming 的 3 MiB app 分区只剩约 4%，后续能力接入仍需核对容量。

manifest 53 classes / 517 functions（全项目）、27 feature docs、配置 schema、
1,267 项 SDK map、严格类型与 MQuickJS 61 sources / 61 snippets 通过。

新增 fixture 调用生产 Radio 和 Future worker/poll/cancel/cleanup，覆盖八 owner、
旧 token、非托管 lease release 拒绝、分配失败、一次 teardown、失败后缀、
取消提交与 runtime 清理。这里只检查 AST，未导入、编译或运行 fixture。

JS object handoff、完整 Future core、GC/OOM、实际 SDK 调度、RF/对端互通、
runtime restart 和内存静止基线验收仍为 not-run；构建/静态检查不能替代这些。
未刷写、串口操作、提交、推送或更新父仓库 gitlink。长 soak 继续留到 BLE 后。
