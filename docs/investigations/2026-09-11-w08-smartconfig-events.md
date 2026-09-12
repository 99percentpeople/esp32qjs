# W-08 SmartConfig 原生事件与凭据所有权

本批继续公开 Session 的前置实现。新增内部单 owner 记录和真实 SDK event-post
边界，未注册 SmartConfig JS API。固定 SDK 与 firmware 基线同
[ACK 生命周期修复](2026-09-10-w08-smartconfig-native.md)。

## 原生路径证据

C5 `libsmartconfig.a/sc_sniffer.o` 的 `sc_wifi_scan_done` 通过 OSI `_event_post`
以无限等待投递 SC_EVENT_SCAN_DONE。OSI adapter 最后调用 `esp_event_post`。
因此只修 ACK_DONE 不足以防止默认事件队列满阻塞 decoder/stop。
`SC_EVENT_GOT_SSID_PSWD` 的 SDK 结构包含 SSID/password；直接放入默认观察
队列会形成另一份未由 Session 控制的凭据副本。

本批编译时核对该事件的大小与字段偏移，接入 `__wrap_esp_event_post`：
只有 base 为 SC_EVENT 且已有保留 owner 时走原生捕获；其他 base、无 owner 的
SDK 调用保持原始参数、等待时间和返回值。现阶段没有 public caller 创建 owner，
故不能把链接到拦截器等同配网 Session 已可调用。

## 记录与交付

开始前保留单个 INTERNAL 记录，分配前先占用名额；并发调用不会额外分配第二份
凭据缓冲。boot identity 不回绕，radio generation 一并校验；分配失败不消费
identity。原生 callback 不分配、不调用 SDK、不开 ACK、不等待事件接收者。

scan/channel/credential/ACK-observation 先记录在原生状态。第一份合法凭据保留
至显式成功交付或关闭；后续重复事件只计数，不覆盖正在转换的数据。校验长度、
bool 与 enum 的原始表示，不假定满长 SSID/password 末尾有 NUL，不复制 padding。
metadata status 不含 SSID/password、phone token/address 或 custom data。

copy 不等于交付：调用端构造结果失败时可以重读；commit 后清零原始凭据。
调用端自己的副本也必须在每条退出路径 secure-zero。关闭原子撤销交付并清零，
但继续保留记录吸收迟到事件；不能仅凭 public Future 结束释放该记录。

release 要求同一 identity/generation 已 closing，并由外层先完成 decoder、
timer/native、ACK 和必要事件排空。这里的 token 校验保护的是记录本身，不是
外部 native quiescence 的证明。外层协调器尚未接入，禁止据此提前放行重开。
ACK-observed 只是观察，不替代上一批 exact ACK snapshot 的完成/退休事实。

托管凭据不再投递到默认 SC_EVENT 队列，因此 SDK 默认 handler 不会自动启动 ACK；
后续 Session coordinator 必须精确启动/持有 ACK，并发布自己的有界观察事件。
此职责不能通过重新广播凭据事件转交给无身份的默认 handler。

## 仍待接入

Radio 排他准入与 helper pin、decoder start/stop、timer/native 排空、AES key
与自定义数据、明确的 ACK 提交身份、公开 start/receive/status/cancel/close、
可选自动连接、GC/runtime 退出与完整失败清理仍未完成。

另在固定 C5 decoder 中确认 `esp_smartconfig_get_rvd_data` 按 C 字符串式
`len-1` 拷贝并补 NUL；不能以 strlen 推断任意二进制 custom data 的长度，
也不能传零长度。自定义数据契约仍须在接入时解决，未注册占位字段。

## 验证边界

结果与文件哈希记录在 `build/w08-smartconfig-events-evidence.json`。生产编译、
SDK adapter 到拦截器的 ELF 路由、生成物/类型/语法检查分别留证。
C3、S3、C5、C5-no-SoftAP、C5-Wi-Fi-disabled 五种构建及静态检查通过。
启用目标实际 ELF 的 `esp_event_post_wrapper` 调用框架拦截器，拦截器同时链接
捕获与原始 `esp_event_post` 转发路径；reserve/copy/commit/close/release 已编译
归档，因尚无公开 caller 而未链接。DWARF 确认按需记录为 168 B；当前捕获常驻
状态 C3/C5 为 4 B，S3 含锁为 12 B。禁用构建未链接这些符号，镜像尺寸不变。
这些数据不代替运行时 heap/largest-block 或实际 Session owner 预算验收。

新增 fixture 调用完整生产记录与 post wrapper，控制 allocator/event 边界，
覆盖单 owner、OOM、identity 耗尽、stale token、凭据 copy/commit、重复/坏布局、
关闭时清零、迟到事件和非 SC_EVENT 转发。fixture 仅 AST，不导入/编译/执行。
copy 后不 commit 的用例只检验内部重试原语，不冒充实际 JS GC/OOM 验收。
动态调度、decoder 终止证明、实机配网与 RF 均 not-run；长 soak 留到 BLE 后。
