# 第一阶段收尾与 W-00 覆盖清单

用户决定：长时间验证在全部无线功能完成后集中执行。本轮继续短路径正确性检查，
并启动 W-00 的输入采集；没有把长时间测试延期解释为短竞争测试已经通过。

## 第一阶段新增修复

`future_expire_deadlines()` 先于 ready queue/poll 执行，因此 BLE 扫描/广播即使已
同步启动并置 completed，仍可能在 JS 结果转换前走 timeout。原来的
`ble_future_on_timeout()` 忽略 `ble_gap_disc_cancel()` / `ble_gap_adv_stop()` 的失败，
无条件清除 active；随后 destroy 会跳过取消保护并释放原生可见存储。

新增 `test_timeout_failed_stop_retains_native_storage` 编译实际 timeout 分支和
实际 destructor。扫描、广播两种失败注入均先复现提前释放；修复后仅在成功或
`BLE_HS_EALREADY` 时清除 active。失败保持原生 owner，destroy 交给已有 orphan
cleanup；成功重试和 already-stopped 路径也通过。公开 Future 仍按原契约超时，
不新增 API 或新的关闭机制。

验证：Host C 65/65、Python 340/340、MQuickJS 59 个源文件/47 个文档片段、
manifest 43 classes/384 functions，以及 C3/S3/C5/C5-disabled 四个原有合法
Build Context 增量构建全部通过。命令与基线见
[核心调查](2026-09-07-wireless-core.md)。本次日志为
`build/wireless-core-evidence/closeout-*.txt`，新增源码及四目标镜像 hash 记录在
`closeout-summary.json`。没有刷写设备；此前设备结果不覆盖本次额外 timeout 修复。

F-03 全 SDK callback-entry/stop 调度和 F-08 全 JS allocator/移动 GC 覆盖仍未完成。
F-CORE 保留收尾状态；这些短测试缺口与延期的长时间测试分别记录。

## 第二阶段实际进度

第一批输入采集已完成。
[`inspect_idf_wifi_inputs.py`](../../scripts/inspect_idf_wifi_inputs.py)，读取已构建的
`project_description.json`、`compile_commands.json` 和 sdkconfig，使用各目标
原有交叉编译器及参数预处理 16 份公共头文件。保存完整 translation unit、逐头文件
条件声明（含结构字段/宏）及 source/config/manifest hash；缺少输入会失败。
不从函数名字猜测 JS 映射，也不修改 actual manifest。

```sh
.venv/bin/python scripts/inspect_idf_wifi_inputs.py \
  --build-dir build/wireless-c3 \
  --build-dir build/wireless-s3 \
  --build-dir build/wireless-c5 \
  --output build/wifi-refactor-inputs
```

三目标提取通过，IDF 都为 `fff9895c82d744c7237be8847347bdd1b07c6643`。
现有 Wi-Fi/CSI/ESP-NOW actual manifest 合计 45 个 callable 条目；这是源码注册
清单，不是所有目标都启用的能力列表。输出 `inputs.json` 标记
`coverageReviewed: false`，生成文件保留在 ignored build 目录。

已发现五份头文件的条件文本在三目标间有差异：`esp_wifi.h`、`esp_wifi_he.h`、
`esp_wifi_he_types.h`、`esp_wifi_types_generic.h`、`local/esp_wifi_types_native.h`。
必须逐目标审查参数/字段；仅统计同名函数不能证明覆盖完整。

### 修复提交之后的 W-00 实施

无线修复已提交到 firmware `4026f7f`；串口断开状态修复在根仓库 `d5d5fb1`。
根仓库 firmware gitlink 没有更新，无推送。下面是修复提交之后新增的第二阶段工作。

- [`idf-wifi-api-inventory.json`](../idf-wifi-api-inventory.json) 保存三目标声明、宏、
  enum 值、结构/union 字段、数组和 bitfield。C3/S3 各 1,240 条，C5 为 1,244 条；
  并集 1,244 条。每目标都有 259 个函数；字段记录 C3/S3 各 768 条、C5 737 条。
- [`idf-wifi-api-map.json`](../idf-wifi-api-map.json) 按符号记录工单及状态覆盖项，
  与 `tasks` 中的 disposition/implementation/contract/owner/completion/validation
  默认值合并读取。40 个有现存原生调用点的函数标为 in-progress，表示完整目标契约
  尚未完成；没有条目被推断为 implemented。字段覆盖仍 review-required，高级契约
  保持 contract-pending。目标映射不会生成正式 JS 声明或能力。
- [`generate_idf_wifi_api_map.py`](../../scripts/generate_idf_wifi_api_map.py) 使用锁定
  的 pycparser 3.0 解析实际交叉编译器输出。保留类型引用和声明，不计算 ABI offset/
  sizeof。解析前仅归一化 GCC 注解、C23 依赖语法和 inline 函数体；原始头文件 hash
  单独保护这些内容，未知声明语法失败，不跳过未知 public declaration。
- Host CI 检查映射完整性及 planned/implemented 状态约束。各 firmware CI build
  开始前检查固定 IDF revision 与全部 16 个头文件 hash；即使新符号藏在未启用的
  `#if` 内，也要求重新采集和审查。远端 CI 尚未运行，本地等价检查通过。

```sh
uv sync --locked
.venv/bin/python scripts/generate_idf_wifi_api_map.py --check
.venv/bin/python scripts/generate_idf_wifi_api_map.py --check-headers /home/zach/esp/esp-idf
.venv/bin/python scripts/generate_idf_wifi_api_map.py --check \
  --build-dir representative=build/wireless-c3 \
  --build-dir representative-psram=build/wireless-s3 \
  --build-dir representative=build/wireless-c5 \
  --build-dir disabled=build/wireless-c5-disabled \
  --build-dir wireless-inventory=build/wireless-c5-inventory
.venv/bin/python -m unittest discover -s tests/python
```

需要更新基线时，先审查并编辑 mapping，再用上述五个目录执行 `--write`；未分类
的新符号会失败，不自动给新增能力填已完成状态。正式 callable 清单继续独立使用
`generate_api_manifest.py --check`（43 classes/384 functions）。

新增 9 项生产工具回归覆盖数组/bitfield/union、callback typedef、enum、宏、
依赖 inline asm、新增/删除符号、同名 API 字段变化、隐藏头文件变化、错误提前宣传
planned API，以及显式新增 mapping 后的再生成。完整 Python 349/349 通过；
三目标现场重新提取并对比、固定 SDK 头文件校验、正式 manifest 检查均通过。
截至本段首次记录仅有工具/文档改动；下节记录 2026-09-08 新增的原生扫描修复。

### 已知边界与下一步

W-00 仍为 **in-progress / coverage review**，不是完整字段契约冻结或 W-01 已完成。

1. 已记录 C3/C5 representative、S3 representative-psram、C5 disabled 和
   C5 wireless-inventory 五个 variant；其他 CI profile 目前只校验固定 SDK/header，
   输出明确的“未记录语义基线”，不宣称全配置覆盖。
2. NAN-Sync 已展开；实验性 NAN-USD 四个条件函数仍未启用。逐字段 secret/单位/
   默认值和高级子契约继续待审查；不把“未展开”改写成 target-unsupported。
3. F-03 的扫描调度缺陷已修复，广播/连接等完整 SDK 调度以及 F-08 全 allocator/
   移动 GC 覆盖仍未完成。W-01 的可重复生命周期、多 owner 和恢复状态机尚未实施。

### 2026-09-08：配置变体与扫描竞争修复

原工具按 MCU 唯一索引，C5 disabled 与 representative 的 `WIFI_CSI_ENABLED`
分别为 0/1，正确配置被报告为字段变化。现改为 sole-v1 的
`variants[target/profile].symbols`，`--build-dir` 必须显式提供 `PROFILE=PATH`；
同目标不同配置共存，重复 variant 拒绝，再生成必须携带全部已记录配置，避免丢基线。
没有为旧开发中格式保留兼容读取。

新增 C5 `wireless-inventory` 合法 Build Context，只启用 IDF 的
`CONFIG_ESP_WIFI_NAN_SYNC_ENABLE=y`；没有新增 framework NAN feature 或 JS API。
固定 SDK 的 `SOC_WIFI_NAN_SUPPORT` 在本次三个 MCU 中仅 C5 具备，生成器拒绝
C3/S3 使用这个配置。真实 C5 构建通过后新增 23 个声明（13 函数、9 宏和
`struct nan_peer_record` 六字段），并集变为 1,267 条。NAN-Sync 配置每项仍是
planned / contract-pending；NAN-USD 是另一实验性开关，保留明确未展开记录。

CI 新增 C5 disabled/wireless-inventory 两项，安装与 host 相同的 pycparser 3.0。
已记录的五个 variant 在实际 build 成功后重新预处理并比较声明/字段；其他配置仍
只执行全公共头文件保护。新增回归覆盖不同配置共存、重复 variant、显式 CLI identity、
构建后字段变化拒绝，以及 NAN 配置适用范围。远端 CI 未运行。

固定 NimBLE `ble_gap.c::ble_gap_disc_report/ble_gap_disc_complete` 从 Host 状态复制
callback/arg 后，在锁外调用。因此取消成功与 callback-entry 之间存在窗口。
`test_ble_scan_callback_regression.py` 编译生产 `ble_scan_start`/release 和新 callback
helper，SDK 提交/事件处理边界受控；修复前两个断言实际失败：

- 旧 report 进入重开的扫描，`delivered == 0` 失败。
- SDK 在 start 返回前发 complete，生产 start 随后重设 active，`!active` 失败。

修复使用独立、设备启动期间不复用的扫描 cookie；先发布 active 再调用 SDK。
关闭先撤销 callback admission，等待已进入 callback 引用归零，再释放 queue/pool。
成功 cancel 不被当作“未来绝无 callback”的证明；失败 cancel 保留原有 orphan
cleanup 保护。identity 耗尽在调用 driver 前返回 `BLE_BUSY`，runtime restart 不重置。
静态账本在 ESP32 上增加 12 字节，不引入新 pool 或任务。

5 项生产路径回归通过：旧 callback、提交中同步完成、pthread 控制的已进入 callback
与 close 交错、identity 耗尽、提交失败后正确释放 cookie。锁/等待边界有断言；
它们没有运行完整 NimBLE Host，因此其他 GAP 路径不能据此提升为全覆盖。
证据日志 `build/ble-scan-before.txt`、`build/ble-scan-after.txt`；这次扫描修复尚未刷写。

验证：完整 Python 359/359，Host C 65/65，MQuickJS 59 源文件/47 文档片段，
正式 manifest 43 classes/384 functions，27 feature 文档一致性，五配置语义与固定
SDK header 比较通过。C3/S3/C5、C5 disabled 与 C5 NAN-Sync 五项构建均通过。
本轮源码/镜像 hash 见 `build/wifi-phase02-evidence.json`，各项日志为 `build/wifi-phase02-*`。
未构建前端、擦除 workspace、提交本轮新增工作或更新根仓库 gitlink。

## 集中验收账本

| 类别 | 当前安排与状态 |
| --- | --- |
| 短竞争、错误注入、输入、生成物、target build | 随每次实现执行；现存 F-03/F-08 缺口继续收尾 |
| 有界硬件关闭/重开、GC、restart | 保留此前真实结果；新增 timeout/callback 修复设备验证 not-run |
| 500 次完整生命周期、长时间泄漏/吞吐、共存 soak、时间回绕 | deferred / not-run；全部功能完成后集中验证 |
| BLE/GATT 对端、ESP-NOW 双机、CSI RF | not-run；缺少对端/采集条件的项目逐项保留，不能由 Host 或本机 driver success 替代 |

集中内存比较继续使用预热后、等待原生任务回收的相同静止状态，立即读取
internal/PSRAM free、largest block 和 owner/pool 账本；不保存惰性 getter 根对象
作为 before。feature-stability 不提升。

## 2026-09-08 后续：广播关闭与真实 MQuickJS 故障注入

本次继续 F-03/F-08 收尾，W-00 的五配置清单保持有效，W-01 尚未开始。
未刷写设备、执行长时间测试、提交或推送本轮修改，也没有更新根仓库 gitlink。

### 广播 callback 的确认缺陷和修复

固定 NimBLE 的 `ble_gap_slave_extract_cb()` 在 Host 锁内复制 callback/arg 后重置
广播状态，`ble_gap_adv_finished()` 在锁外调用 callback；新连接也继承广播 callback。
因此停止广播不能单独证明旧完成回调不会再进入，也不能把所有旧 cookie 事件一概丢弃，
否则会丢已建立连接的断连/MTU 等事件。

`test_ble_advertise_callback_regression.py` 在修复前取得三项实际失败：

1. SDK 在 start 返回前完成，生产 start 又设置 active。
2. 旧广播完成回调把重开的广播设为 inactive。
3. 排队期间 adapter 关闭或 generation 改变，生产 start 仍调用 driver。

生产实现现在单独登记 boot-scoped 广播 identity，耗尽不回绕，排队请求先重验 generation。
先发布 active 再调用 start，失败撤销 cookie；释放 queue 前撤销 admission 并等待已进入
回调退出。连接自己的 GAP 事件继续按 handle 处理，不依赖 advertiser 是否仍开着。
迟到 CONNECT 不进入新 advertiser 的队列，只拒绝尚未登记为 live connection owner 的目标连接；
不会终止已登记的 live connection。断开失败时保持原生 owner，由显式 adapter close 的
Host stop/deinit 屏障清理；新增 `rejectedConnectionError` 保留原始错误，下一次 open 重置。
没有为清理该连接而自动关闭其他连接或降低安全设置。

7 项生产函数回归包含上述三项、pthread 控制的 callback/close 交错、迟到连接与继承事件、
拒绝失败/无可用 slot/已消失连接、set-data/scan-response/start 失败及 identity 耗尽。
日志为 `build/ble-advertise-before.txt`、`ble-advertise-stale-before.txt` 和
`ble-advertise-after.txt`。测试替换了 SDK 边界，不冒充完整 NimBLE Host 调度或 RF 验证。

核对连接 close/pair 的源代码时确认：它们在开始原生操作前通过 `ble_gap_set_event_cb()`
切换到带 connection slot 的 callback，因此“继承广播 callback 导致 pair/close 一直没有
active state”不是已证实缺陷，本次没有据此改写连接状态机。未展开的 callback/队列 owner
转移路径继续保留为 F-03 风险项。

### F-08：实际移动 GC 与第 N 次失败

新增 `test_ble_status_gc_regression.py`，编译生产 `ble_adapter_status_to_js()` 和属性
helper，并链接固定 vendored MQuickJS 引擎。仅 SDK 状态来源与可分配 API 的注入边界
由 fixture 提供；对象、GC roots、压缩移动、属性写入及异常均由实际引擎执行。

- 原代码在第 2/25 次可分配 API 调用失败时仍返回普通对象，吞掉 roles 字符串失败；
  强制 GC 时正常转换也失败。`JS_SetPropertyUint32(ctx, *roles, ..., JS_NewString(...))`
  允许 C 在创建字符串前读取 array value，移动后留下旧值。
- roles 改为创建后读取 rooted array，并检查每次数组/字符串/元素写入的异常。
- 仅修 roles 后，第 9/25 次失败仍被吞掉。实际 MQuickJS setter 不自动拒绝作为 value 的
  `JS_EXCEPTION`，原公共 helper 把异常标记存进属性后返回成功。
- 两个公共 `esp32_mquickjs_set_property[_ref]()` helper 现在在调用 setter 前拒绝
  exception target/value，保留原异常。这是状态转换失败的必要依赖修复，没有修改 vendored VM。

3 项回归通过：逐个 API 失败、强制 GC 下逐个失败，以及两个公共 helper 的 exception
输入保护。GC 模式还断言生产 root 的实际 value 发生了移动，成功路径检查两项 roles
和诊断值；所有失败路径核对 roots 按 LIFO 归零。测试覆盖该 converter 的 25 个可分配
API 边界，不声称注入了引擎内部每一次 malloc，也不代表所有无线 constructor 已覆盖。
证据为 `build/ble-status-before.txt`、`ble-status-roles-fixed.txt`、`ble-status-after.txt`。

### 本次验证与剩余 gate

完整 Python 369/369、Host C 65/65、正式 manifest 43 classes/384 functions、27 feature
文档和 1,267 条覆盖清单检查通过。C3/S3/C5、C5 disabled、C5 NAN-Sync 五项构建和 MQuickJS
59 源文件/47 文档片段检查通过。源码/镜像 hash 见 `build/ble-closeout-evidence.json`，
各项日志为 `build/ble-closeout-*`。本轮增加的是安全修复和必要诊断字段，没有新无线能力注册。

F-CORE 继续区分“已确认缺陷已修复”和“所有风险已验证”：其余 callback/队列转移、
无线输入与转换失败路径尚未全部形成生产调度/GC 证据。F-HARDWARE 和集中长时间验证
仍为 not-run，不能用这次 Host 测试提升 feature 稳定等级。

## 2026-09-08 后续：连接结果交接与入站队列清理

继续 F-03/F-04/F-08 的生产路径收尾；W-00 五配置清单与 sole-v1 API 保持不变，
W-01 尚未开始。本轮不刷写设备，不执行集中长时间验证，不提交或更新根 gitlink。

### 确认缺陷

`test_ble_connection_transfer_regression.py` 修复前 3 项中 2 项失败：
`ble_connect_finish()` 在创建 JS connection handle 前标记 transferred，构造失败后
Future destructor 不再承担 native connection 清理；`ble_connect_destroy()` 又把
`ble_gap_terminate()` 失败当作已经断连，提前清除 open/allocated 和 native operation。
日志为 `build/ble-transfer-before.txt`。

`test_ble_incoming_queue_regression.py` 另复现 advertiser queue 注册的 drop callback
为 NULL（`build/ble-incoming-before.txt`）。入站事件携带仍存活的连接索引，队列 discard
及已 dequeue Future 的销毁因而没有对应的原生清理。

进一步核对生产 EventQueue：`event_queue_future_finish()` 在调用 converter 前已设置
`event_finished`，失败后 destructor 不会再次 drop；必须由 converter 清理。
一个已经创建、但未成功交付的 connection handle 若保留 opaque，会在后续 finalizer
请求 adapter-wide orphan close。失败路径现在先撤销该部分句柄，再清理目标连接。

### 实现与回归边界

- 连接 handle 构造成功后才提交 Future 的 transferred 状态。终止请求失败保留 exact
  native slot、connect operation 和 `release_on_disconnect`，记录原始错误。
  成功提交终止仍等待 DISCONNECT；ENOTCONN 才证明目标已经不存在。
- 入站连接在 slot 中记录 advertiser generation 和 pending ownership。队列丢弃按
  connection generation + advertiser generation 认领一次，driver 调用在 critical
  section 外。generation 字段为原子类型；重复或过期事件不能清理新 owner。
- 原生 CONNECT 发布已提取为实际生产 helper，先记录 stop reason 和 incoming owner，
  队列满时结束该目标连接并统计 dropped。已交付的其他连接不受影响。
- advertiser close 在回调入口屏障后清理尚未交付的连接，包括已 dequeue、未 finish
  的 receive Future。后来的 finish 返回 stale；成功交付的连接保留。
- adapter close、orphan cleanup 及 runtime teardown 会重试尚未终止的连接；退出等待
  connect callback 前先重试，失败仍保留 runtime/native storage。Host 已停止后的
  pool 释放不再调用 terminate。
- `rejectedConnectionError` 的 sole-v1 类型和 API 文档说明扩展到所有未交付连接的
  终止失败，包括 outgoing result 构造失败；下一次 adapter open 重置。

新增 14 项测试（连接交接 5 项、入站队列 9 项）编译实际生产函数，并以 SDK/JS API
边界注入失败。覆盖成功交付、构造失败、终止失败/重试/ENOTCONN、runtime cleanup
后缀、队列满、重复 drop、旧 generation、断连后 drop、Host stop 后释放、关闭时
已 dequeue 的事件、9 个转换 API 故障点以及真实 EventQueue finish/destroy 的清理责任。
本轮入站 converter 测试的 JS API 是故障 fixture，不等价于真实移动 GC；此前实际
MQuickJS 状态 converter 的移动 GC 测试仍保留。

### 验证与下一项短测试

完整 Python 383/383、Host C 65/65、MQuickJS 59 源文件/47 文档片段通过。
正式 manifest 43 classes/384 functions、27 feature 文档、1,267 条覆盖 map 检查通过，
并重新比较五个实际构建配置的预处理声明。C3/S3/C5、C5 disabled、C5 NAN-Sync 构建
均通过。日志为 `build/ble-transfer-*`，源码和镜像 hash 保存于
`build/ble-transfer-evidence.json`；保留前轮 `ble-closeout-evidence.json`。

本轮没有新增 pool 或恢复先前被移出的 SRAM 缓冲区；新增的是每个连接的少量 owner
字段。未运行预热静止态 heap/PSRAM/owner 账本的设备对比，不据此宣称内存无增长。

F-CORE 尚未全部通过。下一项 F-03 短测试应具体覆盖 **slot identity 校验与 native
terminate 之间，Host DISCONNECT/handle reuse 的交错**；本轮 SDK 边界 fixture 不证明
该复合操作已串行化。固定 NimBLE `ble_gap_terminate()` 自行获取 Host lock，而
`ble_hs_lock()` 明确禁止嵌套，不能简单在外层再包同一锁；需核对实际 Host 调度边界。
F-08 仍需扩展其余无线 constructor/converter 的实际分配与 GC 证据。
这些是待完成开发测试；双机 RF、CSI RF、500 次完整生命周期及共存/长时间内存测试
保持 deferred / not-run，不提升 feature 稳定等级。

## 2026-09-08 后续：原生连接竞争、控制终态与 GATT GC

本轮完成前条记录中指定的连接 identity/native terminate 竞争复现和修复，
扩展 F-04 控制完成核对与 F-08 实际 VM 故障注入。F-CORE 尚未全部完成，
因此未触发用户授权的“F-CORE 完成后提交”；W-01 尚未启用新生命周期。

### F-03：串行化连接 identity 与原生调用

`test_ble_connection_mutation_regression.py` 修复前实际执行生产
`ble_terminate_unclaimed_connection()`，在 driver 返回前安排旧连接断连、slot/数字
handle 被新连接复用。旧 helper 在 ENOTCONN 返回后清除新连接 open/allocated，
取得失败结果 `build/ble-mutation-before.txt`。回归后的调度使用 pthread trylock
和明确的 attempted 通知协调交错，不依靠某段 sleep 充当隔离屏障。

固定 NimBLE 的 `ble_gap_call_event_cb()` 断言应用 GAP callback 不持有 Host lock；
`ble_gap_conn_broken()` 删除原生连接后在同一 Host 处理流程中派发 DISCONNECT。
本实现采用 boot-lifetime 的独立可重入 FreeRTOS mutex：GAP callback 的 slot 更新、
连接 reservation、generation 校验、terminate 与其返回值处理在同一同步域。
允许 GAP callback 内再次执行拒绝连接逻辑，不嵌套获取 SDK 的不可重入 Host lock。
短 `s_ble.lock` 仍用于字段/registry 发布，driver 调用没有放进该 critical section。

该保护也覆盖 connect/pair/MTU/RSSI/discover/read/write/subscribe/server notify
的提交入口、连接 close、timeout、未交付结果销毁。实际九个生产 start wrapper
均有 pthread 调度回归，成功/失败返回后释放锁。测试用 SDK submission fixture，
不宣称链接了完整 NimBLE Host 或完成 RF 调度测试。

另外确认 `ble_connection_close_finish()` 对已经断开的旧数字 handle 仍调用
`ble_gap_set_event_cb()`；回归在修复前失败（`build/ble-close-rebind-before.txt`）。
关闭结果现在直接回收本代 handle，不再修改可能已复用的原生 handle。pair finish
只在连接仍存活且 generation 匹配时撤销其 callback。

配对响应也在原生注入前重验 open、generation、requestId、action 和 deadline；
参数转换期间断连、换代、新请求或过期均拒绝并清零临时响应。测试对 HEAD 原生产
函数取得“仍返回成功”的失败证据 `build/ble-pairing-before.txt`，然后验证修复。
Numeric Comparison 仍需显式 boolean，false 保持拒绝，不自动确认或降低安全要求。

### F-04：完成不依赖观察队列

`test_ble_disconnect_control_regression.py` 的实际生产 guard/分支复现：

- FAILED 生命周期直接丢弃 DISCONNECT，已保留的 native connect operation 无法结束。
- DISCONNECT 和 ENC_CHANGE 先调用观察 publisher，再标记 close/pair Future completed。

修复后 FAILED 仍消费原生清理终态，同时拒绝新的入站 connection owner；
断连和配对先更新 snapshot/native state、完成 Future，再发布观察事件。
满队列不阻止完成；security 观察事件丢弃也记入 droppedConnectionEvents。
失败证据为 `build/ble-disconnect-before.txt`、`build/ble-security-before.txt`。

控制完成核对：Wi-Fi publisher 的生产满队列回归保留；BLE indication 仍区分 submission
与确认、不重复扣减；MTU 先更新 slot，其请求结果由独立 GATT callback 完成。
ESP-NOW `espnow_send_callback()` 直接完成 active_send 或 queued_send 原生状态，
`espnow_close_worker()` 在清理后设置 close completed，两者均不通过接收观察队列。
没有为本次修复重写已正确的 ESP-NOW recovery/queue retain 或共享 Radio 机制。

### F-08：实际 VM 扩展与新确认缺陷

新增 `test_ble_value_gc_regression.py`，链接 vendored MQuickJS，调用生产地址、安全、
connection status、control event、GATT properties、service/characteristic/descriptor
record 和 discovery finish。11 个场景覆盖普通与强制 GC、每个可分配 API 的单点失败，
检查异常传播、根按 LIFO 归零、快照输出、断连 terminal 标志仅在成功转换后消费。
含多次分配的场景断言实际 rooted value 被压缩移动；单对象纯布尔安全状态不要求
该断言。这里仍是外部 API 边界注入，不声称遍历 VM 内部每次 malloc。

GATT properties 数组在第 2/15 次 API 失败时吞掉字符串异常；正常转换在强制移动
GC 下也失败。原因与前次 roles 相同：`JS_NewString()` 和未重新读取 root 的 array
在同一 setter 调用表达式中。先构造并检查 name、再读取 rooted array 写入后通过。
证据 `build/ble-values-before.txt`、`build/ble-values-after.txt`；正确的地址/状态/
GATT record 与 snapshot 机制只补测试，没有据此重写。

### 验证及仍需完成的短测试

本轮完整 Python **397/397**、Host C **65/65**、MQuickJS **59 源文件/47 文档片段**，
C3/S3/C5、C5 disabled、C5 NAN-Sync 五项构建通过。正式 manifest **43 classes / 384
functions**、27 feature 文档及五配置 **1,267** 条覆盖清单重新比较通过。
日志 `build/ble-core-next-*`；源码、日志和镜像 hash 为
`build/ble-core-next-evidence.json`。没有刷写、擦除 workspace、构建前端或改动根 gitlink。

C5 linker map 中新增 mutex storage 为 84 bytes、handle 为 4 bytes，共 88 bytes
静态 RAM；沿用先前 SRAM 缓冲区优化，没有新增 payload pool。未运行设备预热静止态
heap/PSRAM 对比，这个静态账本不替代设备内存证据。

F-03 指定竞争与 F-04 控制完成已取得对应证据。F-CORE 余下 F-08 短测试按以下具体
构造/转移链收尾，不把长时间 RF 测试重新纳入当前门槛：

| 路径 | 现有证据 | 剩余短测试 |
| --- | --- | --- |
| 共用 ByteView/ByteSpanSource | 原生 retain/pool 账本、资源失败测试 | 实际 VM 构造/GC + 每步失败，验证 owner 只释放一次 |
| BLE scan/notification/server payload | native pool、callback admission、入站 queue drop；普通状态/GATT snapshot GC | payload copy → owned view → event object 的整链失败注入 |
| Wi-Fi status / scan records | runtime resources 与控制 publisher | 状态嵌套对象、scan entry/array 的实际 VM 分配/GC |
| ESP-NOW receive / peer / status | native TX、queue retain、recovery 机制与 Host 回归 | ByteView 交接及对象构造失败的整链回归 |
| CSI Frame/Batch/View/Source | 单 pool guard、原生多 owner/last release | synthetic frame 构造与 retained view 的实际 VM/GC；真实 RF 另列 |
| 无线 Future capture / 注册路径 | 资源 helper 第 N 次失败及各操作已覆盖路径 | 逐个核对尚未覆盖的 state/queue/注册分配点与回滚责任 |

下一步先建立共用 ByteView/Source 的实际 VM 注入边界，再复用到 payload converter。
这些短测试完成、风险账本闭合后才标记 F-CORE 并提交；集中长时间/RF/完整生命周期
与共存验证保持 deferred / not-run，feature 稳定等级不提升。

## 2026-09-08 最终收尾与提交

上述“剩余短测试”已按路径补齐并核对回滚责任，详见
[F-CORE 最终证据](2026-09-08-fcore-closeout.md)。实际 VM 的 ByteView/Source、
无线 payload/status、CSI 合成 owner 转移和 capture 分配测试完成；新增修复 GATT
server 事件池槽泄漏、失败转换误触发 adapter orphan close，以及 ByteSource
小数/整数回绕接受问题。F-CORE 可以交接 W-01；本次提交修复与 W-00 工具，
没有实施 W-01 新生命周期。长时间/RF 验收继续 deferred / not-run。
