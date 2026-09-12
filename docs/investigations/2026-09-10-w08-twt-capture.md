# W-08：TWT SDK 核对与生产输入捕获

基线 firmware `d7db8d1`、SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本轮新增 iTWT/bTWT 原生参数校验与 JS 捕获，受 Wi-Fi + HE gate 控制，尚未
调用 SDK、建立 Agreement 或注册公开 `wifi.twt`。公开能力继续 contract-pending。

## 真实 SDK 边界

核对本地 `esp_wifi_he.h`、`esp_wifi_he_types.h`、SDK iTWT example，以及 C5
`libnet80211.a` 中 api/ioctl/itwt/btwt 对象。提取对象和反汇编保存在
`build/w08-twt-ieee80211_*.o` / `.disassembly.txt`；hash 见本轮 evidence。

- setup 是 in/out 参数。iTWT 的 flow ID、bTWT 的 broadcast ID 可由 AP 改写，
  不能把请求 ID 直接作为最终 Agreement ID。C5 iTWT 内部在请求 flow 已占用时
  还会先选择空闲 flow，写回配置；框架身份与 SDK flow 必须分开。
- iTWT setup event 有 config.twt_id，取值 0..32767；但 teardown 只有 flow ID，
  bTWT setup/teardown 没有独立 request cookie。probe/suspend 也没有 cookie。
  新的框架 token 不会自动解决迟到 native callback 或 RF dialog-token 复用。
- C5 `esp_wifi_sta_itwt_setup` 除头文件范围外，还要求 timeout >=100 ms、
  `interval >= duration + 10000 us`，包括 REQUEST command。command 仅 0..2。
  反汇编使用 64-bit shift 计算 interval；框架必须避免先在 uint32 中溢出。
- public header 将最大 sleep 记为 `1<<35 us`。校验应用到
  `interval - duration`，而不是用窄整数或擅自把全部间隔限制为 uint32。
- C5 bTWT API 包装函数没有在入队前验证 ID；下层使用配置 ID 索引已公告表。
  因而输入范围必须在调用 SDK 前限制为 1..31。这里没有按未验证的 AP 表项
  内容声称协商可成功，native/AP 准入仍须单独完成。
- public `esp_wifi_sta_itwt_suspend` 拒绝负 suspend time。0 是无限期暂停，
  并非 resume；不能把 0/-1 伪装成任务书中的 resume 实现。resume 边界仍待展开。
- bTWT info header 的 `btwt_wake_duration` 注释称单位 flag，但 C5 getter 的
  拷贝位置对应广播参数的 nominal duration byte。其输出单位/布局还须完成
  核对；本轮不注册一个可能误导的广播参数转换 API。

这些是固定 SDK 源码/头文件/二进制检查，不是 native 运行或 RF 证明。

## 已实现的内部输入契约

`esp32_mquickjs_wifi_twt_options.c` 使用已有 plain-options、bounded-number 和
严格 enum helper。options 和当前 property 始终有 GC roots；每个字段读取一次，
getter exception 原样保留。先清零输出，只在完整捕获/校验成功后写入；没有
native allocation、Radio mutation 或 SDK 调用。保留位始终为零。

| iTWT 字段 | 范围/默认 |
| --- | --- |
| command | request（默认）/suggest/demand；其余响应侧 command 拒绝 |
| flowId | 0..7，默认 0；请求偏好，不保证最终分配值 |
| connectionId | 可选 0..32767；单独记录是否提供。后续 owner 必须先预留且防止旧操作复用 |
| trigger | boolean，默认 true |
| announced | boolean，默认 true；映射 flow_type=0 |
| wakeDurationUnit | 256us（默认）/1024us |
| minimumWakeDuration | 1..255，默认 255；按所选单位换算 |
| wakeIntervalMantissa | 1..65535，默认 512 |
| wakeIntervalExponent | 0..31，默认 12 |
| responseTimeoutMs | 100..65535，默认 5000；SDK 响应预算 |
| timeoutMs | 1..60000，默认 6000；后续公共 Future deadline |

`intervalUs = uint64(mantissa) << exponent`；`durationUs = duration * unitUs`。
要求 sleep 至少 10000 us 且至多 `1<<35 us`，减法仅在比较通过后执行。
数值布尔、字符串数字、NaN/Infinity、分数、位域截断、未知键、NUL enum 后缀
都不能变成 SDK 参数。SDK struct 没有独立 implicit 字段，未虚构此输入。

bTWT 只接受 command、必填 broadcastId（1..31）、responseTimeoutMs
（1..65535，默认 5000）和 timeoutMs（1..60000，默认 6000）。不添加 SDK
setup struct 中不存在的 wake 参数。两种 timeout 独立；小 Future deadline
不能被解释为 native 操作已结束，其生命周期接线仍待实现。

这些内部字段尚不构成可调用 API。connectionId 预留/耗尽、setup 变更和 flow
映射、Radio/省电/owner、事件控制终态、Future/cancel/close、退休与 runtime
cleanup、watch/agreements/状态输出及公开类型/注册仍需完成。

## 验证与交接

C3、S3、C5 启用及 C5 Wi-Fi-disabled immutable Context 构建和生产对象/archive
检查通过。C3/S3 的非 HE gate 和 Wi-Fi 关闭 gate 均不编入这些函数。
新 capture 尚无调用者，最终 ELF 均未链接这些函数；不能把编译对象当作公开
API 已可用。C5 启用镜像 2,953,408 bytes、关闭镜像 459,024 bytes，均与上轮
相同；C3 为 2,665,696 bytes，S3 为 2,570,768 bytes。新模块没有静态可写存储，
原跟踪 30 个框架静态对象大小不变，已有 EAP stop 记录仍为 32 bytes。
Manifest 保持 52 classes / 510 functions，不出现 `wifi.twt`；Feature 27、
SDK schema STA35/AP21、strict TypeScript、MQuickJS 61/59、SDK map 与 whitespace
检查同步记录在 `build/w08-twt-capture-evidence.json`。

新增 `test_wifi_twt_options.py` 组合真实 MQuickJS、生产 options 与本轮完整 capture/
validator；property lookup 注入 GC/失败边界并按 VM 语义 root 参数。包含 getter
GC/重读检测/原异常、逐 lookup 故障、输出不部分提交、字段边界、10 ms 临界值、
64-bit 高位换算、最大睡眠与保留位等。仅 AST，未 import、编译或执行。

TWT 请求/Agreement/SDK/Radio/Future/恢复仍待实现，RF/实机/GC/队列/长期测试
未运行；Enterprise full restart/共存和其余 Wi-Fi 清单保持不变。Wi-Fi API 全部
完成后集中阶段测试，长 soak 留到 BLE API 完成后。未刷机、串口操作、擦除
workspace、前端构建、提交/推送或更新根 gitlink。
