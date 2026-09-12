# W-08 SmartConfig SDK ACK 生命周期与凭据日志

固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`，firmware 基线
`d7db8d1`。本批是 SmartConfig 公开 Session 的原生前置，不新增占位 API。

## 已确认的源码问题

`components/esp_wifi/src/smartconfig.c` 默认 GOT_SSID_PSWD handler 复制 SSID/
password 到栈并调用 `ESP_LOGD("PASSWORD:%s", password)`，也未清零栈副本。
框架不应依靠当前日志等级屏蔽秘密。该 handler 只需 type/token/phone IP 发 ACK。

`src/smartconfig_ack.c` 每次 start 都分配参数并创建任务，所有任务循环读取同一
`s_sc_ack_send`。stop 仅写 false；前一个任务尚未醒来时，第二次 start 写 true，
前一个任务可继续发旧 ACK。没有运行中任务准入、退出确认或精确身份。
创建任务失败也未清这个全局开关。

同一任务用 `portMAX_DELAY` 发布 ACK_DONE，队列饱和会阻塞 socket/参数释放。
socket/send 错误缺少稳定结果；发送失败仍累计次数，可能最终发布成功。

这些是固定源码可直接确认的路径；本批没有执行动态复现。用户要求 Wi-Fi API
齐备后集中测试，因此新生产路径 fixture 仅写入并做 AST 检查，不声称已通过。

## 实现与限制

- 新增 SHA-256 固定的 build-local SDK 源替换，只在 Wi-Fi feature 与 SDK
  BSD/IPv4 ACK 后端启用时生效；共享 IDF 文件不改动，依赖漂移构建失败。
- ACK 使用 boot-scoped uint64 identity，耗尽拒绝，保留单个忙任务。
  STOP 只撤销原任务权限；socket 与参数释放完才清 busy，重复 STOP 幂等。
  创建任务同步完成时，调用端不再读其已释放参数，也不覆盖完成结果。
- 完成状态先记录，再零等待发布 ACK_DONE，保存观察错误。
  完成只表示本地 UDP 发送，不证明手机收到或整个配网完成。
- socket/MAC/发送失败保存原始 ESP error 或 errno，不发布成功；发送使用
  MSG_DONTWAIT，睡眠/接收返回后重新检查停止。AirKiss 接收仍受原 1.5 s timeout
  约束；STOP 请求本身不承诺即时任务退出。
- 参数零初始化，最后释放复用 wireless secure-zero；默认 SDK handler 删除
  SSID/password 栈副本和日志。此项不证明解码器内部和事件队列的秘密已清零。
- 内部 snapshot 无秘密，返回 identity、busy/stopping/completed、原生与观察错误。
  `busy=false` 只证明 ACK 资源退休，不证明 decoder、SC_EVENT 队列或 Radio 已静止。

公开 start/receive/status/cancel/close、AES key、自定义数据、可选自动连接、
Radio 共享边界、decoder start/stop 与旧 SC_EVENT 排空仍待实现。
没有把 ACK 的数字身份当成原生 SC_EVENT 自带的 cookie。

## 验证

生产构建和静态结果写入 `build/w08-smartconfig-native-evidence.json`。
C3、S3、C5、C5-no-SoftAP、C5-Wi-Fi-disabled 五种 Build Context 构建通过；
manifest、feature docs、schema、SDK coverage、TypeScript、MQuickJS 语法与
whitespace 检查通过。四种启用构建的生成源与修补器输出一致，实际对象与
`libesp_wifi.a` 内成员一致；禁用构建未应用该修补。新增 ACK 状态记录在对象中
为 24 B，尚无公开 caller，相关 SDK 对象未链接进最终 ELF，五种镜像尺寸均未变。
这证明源码已编译归档，不是新增 SmartConfig API 的链接或运行证明。

新增 `test_idf_smartconfig.py` 使用完整生产 SDK ACK translation unit，仅替换
allocator/task/socket/event 边界，覆盖 STOP 后重开、迟到旧 identity、创建失败/
同步完成、观察队列满、socket/MAC/send 失败、无 netif、预算耗尽及释放清零。
本批不导入、编译或运行该 fixture。完整原生调度、配网 RF、GC/关闭/重开和
runtime restart 均 not-run；长 soak 继续留到 BLE API 完成后。

## WAPI 草案核对

C3/S3/C5 的 soc_caps.h 都声明 WAPI；`ESP_WIFI_WAPI_PSK` 默认关闭。
`esp_wpa_main.c` 在 supplicant init/deinit 调用私有 WAPI init/deinit，libwapi.a
包含对应实现。公开 Wi-Fi 头文件没有独立 WAPI enable/disable/status API。
现有 Station 认证下限及 capability 已认识真实 WAPI build gate，不能将它
解释成 WAPI-only 策略，也不能另调私有 init 制造第二个 owner。
已回写 02 的草案限制，独立控制契约与 WAPI-enabled 构建/RF 仍待完成。
