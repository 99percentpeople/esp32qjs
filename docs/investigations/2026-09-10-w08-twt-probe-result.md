# W-08 TWT probe 原生结果与观察队列

基线 firmware `d7db8d1`、SDK `fff9895c82`。继续 TWT 原生控制边界；本项没有
公开 TWT API，也没有把事件到达当作 TX/timer/队列退休证明。

## RX 语义核对

固定 SDK 的 `wifi_itwt_probe_status_t` 已明确：success 可由关联 AP 的 Beacon
或 Probe Response 触发。C5 `ieee80211_sta.o:sta_recv_mgmt` 的实际控制流对应：

- subtype `0x80` 走 Beacon，`0x50` 走 Probe Response。
- `0x1b8–0x1de` 先检查当前 node，并比较帧 transmitter address（header + 10）
  与 node + 4 的 AP 地址。不相等跳过 probe 完成块。
- 匹配且 node + 1056 active 时，`0x292` 清 active，`0x2a4` 发布 event 30，
  随后 disarm/done probe timer，再 `pm_wake_done`。
- 后面的 header + 16 比较不是进入 probe 完成块的唯一地址检查；不能据此
  推断任意外国 AP 的包都能结束 probe。

这是一项 AP 存活观察，没有携带本次 request 的 RF cookie；既有 Beacon 也可
满足条件。不能将 success 宣称为本次 Probe Request 的精确响应，或已完成 TSF
读回/更新验证。本项保留 SDK 的语义，不修改 RX 接收策略或安全过滤。

## 确认的队列问题

`ieee80211_api.o:wifi_event_post` 调 OSI + 180，传入 `ticks_to_wait = -1`；
C5 `esp_adapter.c:esp_event_post_wrapper` 将其映射为 `portMAX_DELAY`。
RX success、timeout handler、TX callback 失败、submit 失败都在 post 之后
还有清理。队列满且消费者不能继续时，timer/active/PM 唤醒引用的清理可被无限
等待挡住。以上是固定 SDK 源码和对象控制流证据；动态饱和复现按用户要求后置。

## 已接入的生产修复

在 C5 Wi-Fi/HE 构建包装 `wifi_event_post`，仅对 `WIFI_EVENT_ITWT_PROBE`：

- 原生 submit 通过已有关联/pending/timer/TX 准入后，分配 boot 单调 identity。
  输入和 native call 仍由原 wrapper 执行，scope 包括提前 TX callback 的重放。
  identity 耗尽拒绝新提交，不回绕，也没有 runtime reset 接口。
- 第一条原生完成先保存到独立值记录，再 `esp_event_post(..., 0)`。复制定义
  字段并清零 padding；success 的 reason 归零，失败保留 SDK 原始 reason。
- submit error、原生 event/status/reason、首次完成的 observation error 分开。
  重复事件不能覆盖首次完成；投递失败不清除已保存的完成结果。
- post 期间保留调用计数，不允许新 submit 替换记录。所有 event/SDK 调用在
  临界区外；queue full/OOM 返回后，原生调用者可以继续其清理后缀。
- 其他 event 原样调用原 SDK 函数。未知 probe payload 布局明确 fault 并拒绝
  读取；新提交保留该诊断故障，不默默忽略 ABI 变化。

SDK snapshot 增加 `probe_result`。这是最近一次原生提交的值快照，不是新的
owner registry：未来 Radio/公开请求必须先占用排他 lane，并按 exact identity
消费结果。下一次已准入 submit 会替换快照；观察队列里的旧 event 不回写此记录。
没有 Future 已接入，因此本项只完成原生结果在观察发布前的保存；Future 完成和
统一关闭/退休仍待下一层实现。

## 验证与未完成项

新增静态记录 32 字节，无新 heap/JS roots；原有 probe timer 28 字节、TX
ledger 静态 32 字节和懒分配 512 字节保持不变。C5 镜像 2,957,968 字节，
比上一批增加 736 字节。C5、C3、S3、C5 Wi-Fi 关闭构建和生成/类型/语法
检查记录在 `build/w08-twt-probe-result-evidence.json`；构建不等于运行验收。

最终 C5 ELF 的 RX、timeout、TX callback 均调用新 event wrapper；wrapper
对 probe 调 `esp_event_post`，对其他 event 调原 `wifi_event_post`。原生 submit
handler 的 event 引用和 framework submit/result 接入另核对 archive/object；
它们尚未因公开 TWT 调用而全部链接进最终镜像。

新增 fixture 调用生产结果记录/事件 wrapper，覆盖提前事件、queue full/OOM、
重复、错误身份、post 内重入、padding、畸形 payload、identity 耗尽；既有 TX
fixture 组合生产结果模块验证提前 TX callback 与失败提交收尾。SDK snapshot
fixture 同步扩展。均只做 AST 检查，未导入、编译或执行。

仍待完成：timer create/arm 的 abort/OOM 策略、完整联合退休/取消/断连和故障
恢复、Radio owner、Agreement/Future/公开 TWT，以及剩余 Wi-Fi 范围。其他
TWT event 的控制/观察路径也需独立接入，不能把本次 probe 修复推广成已完成。
全部 Wi-Fi API 完成后集中运行与实机测试，长 soak 留到 BLE API 完成后。
本轮未刷写、串口、擦除、前端构建、提交、推送或更新父仓库 gitlink。
