# W-02：公开 stop 的共享等待预算

## 已编码的 v1 契约

`wifi.stop({ timeoutMs? })` 已注册，返回仍为 WiFiStatus。参数省略、空对象或
字段 undefined 使用 1000 ms；显式值须为整数 1–60000。未知字段、NUL 后缀字段、
null/数组、字符串、布尔、非有限数、小数和越界值在原生操作前拒绝。捕获期间
options/property 使用 MQuickJS roots，失败清空 native timeout 输出。

`timeoutMs` 限定本次调用的**协作等待预算**，不是硬性 wall-clock 返回 deadline。
Timer stop、配置失败后的 Station disconnect drain、Radio STOP/event fence、
helper/Radio 已进入 callback、AP/STA netif retirement 使用同一份剩余时间。
先前步骤、SDK 调用和 mutex 等待消耗的时间计入预算；后续等待不会重新获得完整
1000 ms。显式较大预算可超过原来的每步骤上限。毫秒向上取整到 tick，使用 64 位
乘法及无符号 tick 差值，覆盖正常 tick 回绕。

SDK 同步调用、handler unregister、内部 mutex 获取和结果对象构造无法被这个
参数抢占，所以整体耗时仍可能超过指定值。预算到期只停止后续等待；已经就绪的
完成状态可以被收取，非阻塞清理仍可推进。参数不授权取消 Future、抢占 CSI/
ESP-NOW owner、断开已建立的 Station 或降低安全要求。忙碌准入仍直接失败。

## 生产实现与资源保留

- `wifi_config.c` 捕获 stop options；`wifi.c` 在原生 stop 前建立 scope，并在
  所有原生返回（成功或失败）后、错误或状态 JS 分配前结束 scope。
- `wifi_radio/esp32_mquickjs_wifi_wait.c` 是 boot-owned、短临界区保护的单一
  scope，仅保存调用 task 和 tick 标量。重复/并行 begin 失败，不覆盖已有 scope；
  不同 task 的普通 Radio 操作沿用其原有默认等待；不同 task 无法结束该 scope。
  没有 JS/native-operation 指针，也没有新 pool 或堆分配。
- Radio 事件等待、netif 退休和 disconnect 读取剩余预算；Timer 使用实际剩余
  ticks。helper callback 超时诊断为 `callbacks-drain`；Radio shutdown callback
  超时诊断为 `channel-drain`。两者都先保留 callback 可见资源。
- 超时沿用现有 cleanup/lifecycle token：已接受的 STOP 不重新提交，handler
  成功注销后清空句柄，已进入的 netif job/迟到事件保留原 identity。重新调用 stop
  获得新预算，但只重试未完成后缀。公共错误仍为 WIFI_STOP_FAILED + 原始 SDK
  错误，原生等待到期为 ESP_ERR_TIMEOUT；不伪造成功或主动取消清理。

这次修补的代码依据是多个独立 `pdMS_TO_TICKS(1000)` 与 callback 无限等待，
并非已运行测试复现的硬件根因。默认 stop 的等待从各步骤独立上限变为共享上限，
属于当前唯一 v1 的直接契约变更。正常完成行为仍须集中阶段测试验证。

## 新增或扩展的生产路径测试源码（not-run）

- `test_wifi_stop_timeout.py`：真实 MQuickJS capture、移动 GC/第 N 次分配失败；
  完整生产 budget + netif retirement，连续退休两个 netif 耗尽共享预算，迟到
  detach/fence 后重试且只 detach 一次；任务隔离、嵌套 begin、到期、tick 回绕和
  100 Hz 下不足一个 tick 的向上取整。
- `test_wifi_radio.c` / `stop-wait-budget`：完整生产 Radio 与 budget，前置步骤
  消耗 7/10 ticks，STOP 等待只剩 3 ticks；迟到 STOP 后再次调用不重发 stop。
- `test_wifi_cleanup_regression.py`：实际 helper cleanup，在 callback drain
  超时后保留 storage/lease/token，callback 退出后继续且不重复 timer/handler 前缀。
- `test_wifi_public_lifecycle.py`：实际公开 stop/准入调用顺序，捕获失败无准入，
  scope 准入失败不结束别人的 scope，原生失败后 scope 确实结束。
- 旧独立 helper fixture 对新增预算调用显式注入默认边界；新预算本身使用上述
  生产实现测试，不以独立测试状态机代替。

## 本批验证与边界

- C5 immutable Build Context `build/wireless-contexts/c5` 编译通过：app `0x281cc0`，
  app 分区剩余 16%。日志 `build/w02-stop-timeout-c5-build.txt`。
- MQuickJS syntax 59 sources / 48 snippets，manifest 44 classes / 398 functions
  （stop arity 1），feature docs 27，live SDK schema 35 STA / 21 AP 检查通过。
- recorded SDK map、Python 测试源码 AST 和 whitespace 检查另记于
  `build/w02-stop-timeout-evidence.json`。AST 不是测试运行。
- Host C/Python/故障注入均未编译或运行，C3/S3/feature-disabled 矩阵未运行，
  无实机、GC/队列/竞争、RF、heap/owner/pool 对照或 soak 通过证据。
- 固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643` 未改动；新增少量 boot
  deadline 标量，无新增缓冲池，不撤回既有 SRAM 优化。实际内存比较待实机阶段。
- 未刷写、串口操作、擦除 workspace、构建前端、提交、推送或更新根 gitlink。

Wi-Fi 其余 W-02 参数、Monitor、Raw TX、CSI/driver/高级能力仍见
[剩余清单](2026-09-08-wifi-api-remaining.md)。全部 Wi-Fi API 实现后统一阶段测试及
实机功能测试；BLE 在相关测试之后；长时间 soak 放到 BLE API 也完成后。
