# W-08：Neighbor Report 原生请求与 Radio 退休

firmware `d7db8d1` 未提交工作区，固定 SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`。本批承接
[SDK 前置修复](2026-09-10-w08-rrm-sdk.md)，实现原生请求记录、报告存储、
Radio reservation、后台清理与 runtime 排空。公开 Request/Future 和报告解析
仍未注册；不把本批 native implementation 或构建提升为完整 API/RF 验证。

## 身份与 Radio 占用

新增内部 RRM operation kind，使用 Radio 现有 boot-scoped identity 分配器。
UINT32_MAX 可作为最后一个 identity，随后零值哨兵明确拒绝，runtime/driver
restart 不重置此计数。不再引入另一套可复用 callback token。

reserve 在 Radio mutation mutex 内验证精确 application/Station/AP leases，
沿用健康 STARTED Station 和共享 owner 准入，读取并要求 Station `rm_enabled`。
配置在成功/失败出口均 secure-zero，不隐式修改配置或启动 driver。
reservation 绑定 Station lease；generic release/end-operation 不能释放它。
已有 driver mutation/lifecycle 准入看到 operation 后拒绝冲突动作。

SUBMIT/CANCEL/QUERY 都在 Radio mutex 下重新核验 generation、identity、
lease identity、kind 和 driver ownership，再进入 supplicant dispatcher。
SDK/driver 调用不放在临界区，SDK callback 不拿 Radio mutex。
专属 retire 再次执行 native QUERY，只有明确 ownership==0 才清空精确 token。
query dispatch 失败、外来 owner、不一致存储均保持 reservation；不能用本地
timeout、SDK 返回错误或 callback 内的终态记录代替退休证明。

## 原生请求与资源

`esp32_mquickjs_wifi_rrm_request.c` 提供内部 create/start/status/copy/cancel/
close/refcount/service；每次请求只提交一次，后续 worker 只重试清理。
create 完成两次原生分配和预算预留，之后 start 才进入唯一活动 registry。
最多 8 个存活 handle、每份报告 1..4096 bytes、所有 handle 合计 16384 bytes。
closed/completed handle 仍计入 handle 上限，报告预算在实际 free 后归还。
这些是当前 RRM 局部上限，不等于 W-09 全无线资源预算完成。

registry、排队/执行 worker 和 SDK callback 各自持有原生引用。SDK context
只有非零 operation identity，不是 request 地址或 JS pointer。callback 用 identity
查找当前活动记录，拒绝旧 identity，并在原生 buffer 中复制有界完整报告：

- 复制前标记 callback busy 并 retain；复制在临界区外执行。
- close 立即撤销读取权限；callback/worker 使用中的 span 继续保留。
- 超过容量的报告记录原长度及明确 size error，不把截断字节伪装成完整报告。
- 完整报告只有精确退休之后可读；每次 copy 至多 256 bytes，offset/length 使用
  减法检查，无 borrowed pointer，失败不会部分写入输出。
- 本地 deadline/cancel 首先写终态，迟到 callback 不覆盖已选终态。SDK 的 null
  report 只记作 native-no-report：固定 SDK 的 timeout 与 reset 均使用它，不能
  擅自归因为超时。显式本地 deadline 则记录独立 timed-out 原因。

worker 的 submit、cancel、最终 retirement 结果分别保存 dispatch/原始 SDK
code/ownership，保留 tx_attempted。清理错误不覆盖原请求终态，成功清理只清掉
当前 cleanup error；不会重发已经尝试的 request。

默认事件观察发布尚未接入。未来公开完成路径必须先完成 Future，再发布观察；
本批 callback 仅记录可靠 native control，不依赖 default event queue。
请求尚无公开入口，因此没有替换现有 `wifi.watch()` 的调用行为。

## Runtime 接入与边界

Wi-Fi poller 调用 service，以已有 background worker 队列推进提交/清理。
队列满记录 queue error 并保留 registry，后续重试；100 ms 只是工作调度间隔，
不是“等待足够久即可退休”的证明。runtime destroy 在 Station/helper 销毁前
请求 close 并推进 worker，registry 尚未排空时返回未完成；worker 不持有
JSContext/runtime pointer。后续公开 Future/finalizer 仍须接入这些原生引用规则。

原生 identity 解决 callback storage 的代际归属，不解决空中 8-bit dialog token
回绕/reset 后的 RF 报告歧义。该问题、报告 IE 解析、公开 timeout/结果契约、
观察排序和专属 watch 继续保留，不用猜测的隔离时间声称已解决。

## 构建与准备的验证

| 实际构建 | binary | 证据范围 |
| --- | ---: | --- |
| C5 roaming enabled | 2,888,528 bytes | runtime poller/cleanup → worker → Radio → SDK request/retire 全部链接 |
| 普通 C5，RRM disabled | 2,856,608 bytes | 新原生入口/状态不链接，体积与前批相同 |

原生 create/start/copy 等尚无公开调用者，其独立函数在 object 内编译、最终 ELF
回收；不能声称设备已有可调用 Request。启用构建相对前批增加 4080 binary bytes。
已跟踪 30 个框架静态对象尺寸不变，新增请求 registry/两个计数和 SDK trace
指针在 C5 ELF 合计 16 bytes；没有预分配全局报告 pool。动态 request/report、
heap/stack/PSRAM/largest block 尚未实机测量。

新增 `test_wifi_rrm_request.py` 组合真实 Radio registry/reserve/command/retire
与完整生产 request 实现，仅注入 SDK 存储/调度、worker queue、allocator、锁和时钟。
准备逐次分配失败、handle/byte 上限、旧 token、generic release 拒绝、提前回调、
队列满、callback 复制中 close、deadline 后迟到报告、越界 copy、native null
report、cancel/query 不确定性、runtime drain、SDK submit error 和 identity 耗尽。
此前 BTM fixture 的组合代码发现重复追加 reset helper，已删除重复拼接。
两份 fixture 仅 AST 解析，没有 import、编译或执行，不声称回归通过。

manifest 51 classes / 497 functions、features 27、SDK schema STA35/AP21、严格
TypeScript、MQuickJS 61 sources / 57 snippets、SDK map 和 whitespace 静态检查
另行记录到 `build/w08-rrm-request-evidence.json`。

Host/Python/VM、C3/S3/部分 RRM gate/full matrix、实机关闭/GC/队列/重启/RF
均 not-run，待全部 Wi-Fi API 完成后集中验证。长 soak 等 BLE API 完成。
未提交、推送、刷写、串口、擦 workspace、修改共享 SDK/根 gitlink 或构建前端。
