# W-08 广播 TWT wire dialog 不回绕分配

本批补上前一批明确保留的同一 boot 内 dialog 重用缺口。公开广播 Agreement
setup/close、结果 owner 与联合回收仍未完成，未新增正式占位 API。

## 实施边界

原 SDK builder 的 dialog 在 1..62 重用；前一批 RX 虽检查当前 timer/dialog，
同 ID 的旧 RF 响应仍可能在该字节重用时匹配新请求。内部 32-bit timer ID
没有进入 RF 帧，不能代替 wire token。

现在每个广播 ID 有独立的 boot-scoped uint8 最后分配值，在交给原始 output
之前使用 1..255，不回绕。同一个 ID 在同次设备启动期间最多分配 255 次。
这个上限是明确的资源政策；它不是允许 255 次后无条件重用，也不能靠 runtime
restart、Wi-Fi stop/restart、重连或关闭绕过。硬件重启之外不重置计数。

在 producer 仍拥有新建 EB 时，仅改写实际发送 body 的 dialog 字节，并同步
保存该值到 TX identity。保护布局、加密标志和其他帧字节保持原值。现有 TX
完成将同一值绑定到 response timer，RX 的 node/ID/dialog 校验因此能够拒绝
同 boot、同 ID 的旧 token。发送返回错误也不归还 token：返回错误不能证明
空口没有发送或 AP 不会回复。

耗尽位图保留在原生 TX snapshot。producer 准入在 SDK 写共享 timeout 前
返回 ESP_ERR_NO_MEM；直接 output 路径也重复检查并释放未提交 EB。某个 ID
耗尽不会置全局 TX fault，其他 ID 及 information 操作仍可使用。分配失败、
未知 EB、非法字段等其他故障仍遵循现有诊断和存储保留政策。

这个范围不覆盖设备断电重启后的任意历史帧或主动伪造；也不代替 PMF、关联
检查或 native callback-before-recycle 证明。当前仅依赖一字节 dialog 的
非零取值空间，不把扩展至 63..255 的对端互操作性写成 RF 已通过。

## 内存和公开契约交接

可选 TX storage 由 64×24 B identity union 加 32 个一字节计数组成，从
1536 B 增至 1568 B INTERNAL。information 与广播共享同一块 boot-retained
分配，清理只清 live entry，不释放或重新初始化计数。TX 主账本仍 768 B；
新增耗尽诊断令 TX 静态对象从 48 B 增至 52 B。广播 timer 预算不变。

后续 setupBroadcast 的正式契约必须同时公开每 ID/每 boot 的可分配上限、
耗尽诊断和错误语义，不能只注册建立方法而隐去这个限制。当前仅内部 native
snapshot 增量，没有将尚未实现的广播 setup 能力登记到正式类型/manifest。
500-cycle 验收不能通过默默清空计数伪造通过；相关 RF/生命周期用例须按这一
真实边界记录，并验证不同 ID、runtime restart 后计数保留及耗尽后无 RF。

## 验证

C5 HE、C5 no-SoftAP、C5 Wi-Fi-disabled、C3、S3 五个生产构建通过，最终日志
无 warning/error；原 TX/RX/关闭路径仍在最终 ELF，原 timer 与 recycler 的
IRAM/ROM 闭包保持有效。两个身份分配路径均核对为共享的 1568 B storage，
object 与实际 archive 成员一致。记录见 `build/w08-btwt-dialog-evidence.json`。

manifest 53 classes / 520 functions、27 feature docs、SDK schema/map、严格
TypeScript、MQuickJS 61 sources / 61 snippets 检查通过。C5 主镜像
3025936 B（+224），无 SoftAP 2902944 B（+240）；disabled/C3/S3 镜像不变。
共享 SDK 和原 build-local archive patch 保持原 hash。
新增用例调用生产 TX wrappers/ledger，顺序分配至 255、验证第 256 次拒绝且
不进入 output、不修改 SDK 原 dialog、不回绕、不置全局 fault，随后另一个
ID 仍可发送。还覆盖连接关闭后不退回 token、SDK output 失败后仍消耗 token、
producer 准入在耗尽时不调用 builder。原 15-byte/PMF、callback、回收与 OOM
用例随当前 storage 布局更新。只做 AST 检查，未导入/编译/运行 fixture。

集中运行/RTOS/GC/实机 RF 与内存验证仍在 Wi-Fi API 完成后；长 soak 留到
BLE API 完成后。未刷写、串口操作、提交、推送或更新根 gitlink。
