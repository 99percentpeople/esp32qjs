# W-08 SmartConfig 自动连接与 ACK 交接

显式 `autoConnect: true` 已接入生产 Session；默认仍只领取配置。唯一 v1 的类型、
状态和 API 文档同步，保持 Candidate。没有新增普通 connect 的排他例外，也不
创建伪造的 Future token。

## 连接和关闭边界

- decoder 完成捕获、恢复原信道后，复制完整 Radio operation 身份投递到常驻
  event handler。旧 identity/generation/lease/kind 不能完成当前屏障；队列满
  保留原 owner 和错误，后续重试。handler 不访问可释放的 Session/binding。
- 原 Radio operation 只允许一次 Station 借用。既有 helper 的连接 generation、
  IP/断连处理、定时器和事件屏障处理真实连接；原生终态先保存，独立于观察队列
  和 Public Future。普通 connect/scan/configuration 仍受 owner 排他约束。
- Session 在 IPv4 成功后提交一次 ACK，等待原生 ACK worker 完成。ACK 成功仅
  表示本地 UDP 发送结束，不代表手机已经收到，也没有以观察事件作为控制完成。
- 交接前 close/GC/timeout 只断开本次 generation，等待 IP/timer/native 清理。
  失败保留原注册和借用关系。交接后保留应用连接，继续退休 Radio/decoder owner；
  receive 仅在完整退休后交付凭据，转换失败保留 copy/commit 重试行为。
- 默认 WPA2 起、PMF capable，开放网络须显式允许；WPA3 必须 required PMF。
  连接配置只从验证后的有界字节构造，拒绝空 SSID、非法 BSSID、过短 WPA2
  密码和非十六进制 64-byte PSK。临时 wifi_config_t 始终 secure-zero。
  配置存储遵循既有 Radio 的 RAM/FLASH 策略，未承诺从 NVS 删除已保存配置。

## 审查发现与待执行回归

发现自动连接在获得 generation 前因无效凭据失败时，连接轮询仍反复进入并
推迟 next_retry，导致关闭 worker 无法调度。先登记对应生产 Session 用例，
再将连接轮询限制为“未关闭且凭据就绪”或“关闭且仍有连接 generation”。
这是代码审查确认的调度路径；按阶段安排没有执行修复前后测试，不记录为运行
复现或回归通过。

Session fixture 补充完整连接/IP/ACK/交接、无 generation 失败、部分提交失败、
获取期限和 worker 队列满；Radio fixture 补充精确屏障、队列满重试、借用期间
拒绝 close 和一次借用；新增 connection fixture 调用真实 helper、事件终态及
断连屏障，注入 Radio/SDK/定时器边界。MQuickJS fixture 补安全选项及冲突输入。
这些 fixture 只解析 Python AST，不导入、编译或执行。

## 编译和交接证据

`build/w08-smartconfig-connect-evidence.json` 记录当前源码、构建、生成物及 ELF
链接证据。C3、S3、C5 roaming、C5 no-SoftAP、C5 Wi-Fi-disabled 五种生产 Build
Context 均构建成功；manifest 54 classes / 529 functions；MQuickJS 61 sources /
62 doc snippets；类型、feature 文档、配置 schema 和 SDK coverage 检查通过。
本批增加 28 B 原生连接记录和两个 4 B event fence 数字，没有新增静态凭据池；
Session/binding 仍按需分配。以上静态账本不替代预热后的实机 heap 比较。

C5 roaming 的 3 MiB 应用分区接近容量上限；具体镜像大小和余量随本批 evidence
记录。后续功能需要在合法 Build Context 中处理容量，不修改已解析上下文或
以关闭要求中的功能掩盖容量限制。

未刷写、使用串口、运行 fixture、提交或推送；共享 SDK 和父仓库 gitlink 保持
原样。尚缺 SmartConfig custom data、专属观察、SDK 内部秘密副本清理及所有
阶段运行/手机协议/RF 验收；其他 Wi-Fi 功能继续以当前剩余清单为准。长 soak
仍安排在 BLE API 完成之后。
