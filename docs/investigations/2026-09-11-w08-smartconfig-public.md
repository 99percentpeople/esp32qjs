# W-08 SmartConfig 公开凭据 Session

本页记录凭据获取批次。后续[自动连接与 ACK 交接](2026-09-11-w08-smartconfig-connect.md)
已接入显式 autoConnect；下方 capability=false/自动连接待实现是本批历史状态，
不能作为当前 API 状态。当前契约以 [API 文档](../api/wifi-smartconfig.md) 为准。

本批把既有 Radio/runtime Session 接入公开 Candidate API，而不是增加另一套
原生生命周期。`wifi.smartConfig` 提供 start/status/capabilities；
`WiFiSmartConfigSession` 提供 status/receive/close，后两者注册真实 Future driver。
类型、ROM 表、manifest、API 文档和 docs.json 同步到唯一 v1。

## 已接入行为

- start 同步预验证、保留 native handle 并构造 JS 对象，成功后才 activate；
  没有 JS 对象的构造失败不会开始 RF。原生 Radio/SDK 准入仍在后台 worker，
  因此返回 Session 不等于 driver 已接受，错误由 status/receive 报告。
- SDK BSD TCP/IP + IPv4 条件同时用于 target 和 host ROM 生成。新 class id 为
  USER+55，未移动既有 id；没有启用 Wi-Fi 的构建不注册类、对象或绑定。
- receive 的等待超时返回 null，取消不关闭解码；Session 获取超时则请求关闭，
  以 WIFI_SMARTCONFIG_TIMEOUT 拒绝。close 的等待超时/取消保持 registry 清理。
- 生产 Session 引用跨 Future/GC 保存；JS finalizer 仅释放本身引用，最后外部
  引用（含 Future）退出后才自动请求关闭。runtime 的原生清理门槛保持不变。
- 凭据用 ssidBytes/passwordBytes 交付，避免假设 SDK 字节是 UTF-8。SDK 没有
  明确长度，按固定 C-string 容量取 first-NUL/full-capacity，不声称 embedded-NUL
  SSID 支持。成功构造整个 JS 对象后 commit，失败可重试，临时 native 副本清零。
- status/error/global status 均只输出 metadata；64-bit decoder identity 用两个
  32-bit word 精确表达。global 快照在同锁内复制，不把 active 指针交给 JS。

## 编译与静态证据

`build/w08-smartconfig-public-evidence.json` 记录 C3/S3/C5、C5-no-SoftAP、
C5-Wi-Fi-disabled 五种合法 Build Context 的生产构建结果，以及生成物、类型、
SDK header map、MQuickJS 语法检查。manifest 为 54 classes / 529 functions；
MQuickJS 检查 61 sources / 62 doc snippets。生成 ROM 和 ELF 另核对公开函数、
Future 驱动及实际 Session create/activate/credentials/close 的链接。

新增 `test_wifi_smartconfig_future.py` 调用实际 Future start/poll/cancel/destroy
与完整生产 Session，覆盖 wait deadline、Session deadline、close timeout/cancel
后的存活、未开始取消无副作用和 public finalizer 后 Future 引用。
`test_wifi_smartconfig_capture_gc.py` 使用真实 MQuickJS 与生产 options、Session
copy/commit、status/error/credential converters，登记参数 getter、Nth allocation、
移动 GC、完整 32/64-byte 数据、转换失败重读和单次消费测试。

两组测试只做 AST 解析，未导入、编译或执行，运行/GC 正确性尚未获得测试证明。
未刷写、使用串口、提交或推送；未更改父仓库 gitlink 或共享 SDK。

## 仍未完成的完整目标

本批公开的是凭据获取流程。自动连接、手机 ACK、精确二进制 custom data、
专属观察队列和 SDK 内部秘密副本清理继续列为待完成，不把默认“不自动连接”
替代原任务的显式自动连接目标。相应 capability 为 false，无占位 method/option。

当前 operation 固定全部 helper lease，领取凭据后仍阻止 wifi.connect；调用方
须先成功 close 才能另行连接，而关闭会放弃 ACK reservation。因此完整配网的
下一步是捕获退出后的精确 Radio/helper 交接，随后连接和 ACK，不能通过放宽
普通 connect 的排他检查绕过。所有阶段运行、实际手机协议/RF、关闭重开及资源
账本验收仍待 Wi-Fi API 完成后集中执行；长 soak 留至 BLE API 也完成后。
