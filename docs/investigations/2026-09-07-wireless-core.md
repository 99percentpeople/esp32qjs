# 01 无线核心实施证据

实施范围仅为 01 的 F-CORE；02/03 只修订依赖与边界。本轮代码及 Host/构建验证已执行；F-HARDWARE 未通过，
后续功能没有实施。没有提交、推送、更新父仓库 gitlink 或擦除 workspace。

> 取样更正（同日后续调查）：旧 `lifecycle-existing-api` 把 `sys.status.memory`
> 惰性树当作快照保存，before/after 会在事后重新取值。因此下文旧脚本的
> “每组内存相等”不能证明循环内无回退，相关验收结论撤回；正式固件的真实快照复测已通过，见后续调查。关闭/重开结果、
> bootId/generation 变化和 Radio owner 归零仍有效。现已补失败用例并使用
> `test.memorySnapshot()` 实时读取各 region。后续原生堆调查另见
> [runtime heap 调查](2026-09-07-runtime-heap.md)。

## 固定输入

- Firmware：`e1b861c`，保留 RPC/log buffer 的 PSRAM SRAM 优化。
- ESP-IDF：`fff9895c82d744c7237be8847347bdd1b07c6643`。
- ESP-NimBLE：`139cada0ae932957fa06ba37d17e3c9c2c95c773`。
- MQuickJS：`47deb40fe9c9f548b2eb7ccf56ce2270694f667c`。
- 基线 52 Host C / 331 Python 仅为先前记录。本次新增测试单独执行。
- 本机没有 `/dev/serial/by-id`；授权 C5 通过 browser Serial executor 连接。
  重新读取 identity 为 `hw-10bda3c854e8`，ESP32-C5，8 MiB Flash、8 MiB quad PSRAM，
  bootId `fa2bebe3c27bf19a`，workspace 4,653,056 bytes，startup healthy、safe mode false。
  这次 Agent identity 读取不替代烧录前 backend 的 ROM MAC/MCU 校验。

## F-00 至 F-12 证据表

生产路径均相对 `components/esp32_mquickjs/src/`；测试名位于 `tests/c` 或 `tests/python`。

| ID | 当前代码 / 已有机制 | 本次测试或修复 | 未覆盖边界 |
| --- | --- | --- | --- |
| F-00 | `modules/wifi_radio`、固定 IDF/NimBLE/MQuickJS；合法 CI Build Context generator | 本文、构建上下文和日志 hash；区分历史和本轮结果 | 硬件结果单列 |
| F-01 | Radio init/start once，不在线重试 | 生产 Radio + SDK/RTOS stubs，NVS/init/storage/get-mode/mode/start 六处失败；记录 driverOwned、faultStage、faultError、restartRequired | 设备重启才重置 once；没有新增在线 cleanup/restart |
| F-02 | 单固定信道 owner、失败回滚 | 精确 16 项存活 lease registry；旧 token、重复 release、容量和 identity 耗尽、并发 ensure、初始化期间 release；driver 调用断言不在 critical section | 同信道多 owner 仍拒绝，属于 W-01 |
| F-03 | BLE callback refs、native Host stop/deinit；ESP-NOW 两阶段清理 | BLE 独立 boot-scoped operation cookie；Future detach 删除 roots，native owner 保留 storage；迟到 callback 不读新 slot；Host barrier 回收；scanner cancel 失败保留 storage；已有 `test_ble_runtime_resources` 检验 stop/deinit 失败准确后缀 | 完整 NimBLE callback-entry/scan-stop 并发调度尚无 Host SDK shim；不得把零 active callback 当未来 callback 屏障 |
| F-04 | ESP-NOW TX native completion、BLE connection snapshot、Wi-Fi Future 结果队列 | 生产 Wi-Fi publisher 满队列测试；Wi-Fi 控制状态直接在 event-loop task 记录，timer mutation 仍由 runtime 执行；生产 BLE indication callback 区分提交与确认，并按连接消除重复终态 | BLE/GATT 对端断连、真实 RF 队列压力仍需设备与对端 |
| F-05 | `ble_future_on_timeout` 逐操作副作用 | GATT/native lane 隔离；timeout 只针对匹配 generation 的连接；indication 只影响仍在等待确认的原提交连接；`docs/api/ble.md` 超时表 | GATT timeout 后无损保连接不作保证；没有自动重连或降低 MITM/SC |
| F-06 | `wifi_csi_maybe_destroy_resources`、`js_wifi_csi_open` 单 pool guard；Frame/Batch/Event owner 转移 | 现有 pool/lease/ownership/batch 测试；新增 parent close 后禁止销毁且 payload 仍可读；最后 retain 释放后归还 | 无 RF 帧时，真实 JS Frame/Batch/View/Source conversion/GC 为 not-run；不新增多代 pool |
| F-07 | `esp32_mquickjs_espnow_tx_queue`、无线 TX core、native queue retain、显式 recovery | 保留现有 TX queue、wireless core、architecture 用例；只调整共享 Radio 的 identity/清理语义 | ESP-NOW 双机加密、timeout/recovery RF 为 not-run |
| F-08 | Wi-Fi/BLE runtime resource helpers；CSI allocator、layout、batch checked sizes | 既有第 N 次资源分配失败、长度、offset、资源回滚测试；GATT discovery capture 在旧 lane 活跃时不清空旧 cache | 尚无覆盖所有 MQuickJS allocator 调用的故障注入器；JS conversion 的每个 N、每次移动 GC 不宣称全覆盖 |
| F-09 | 现有 wireless_secure_zero、ESP-NOW PMK/LMK 清零；pairing 需显式 boolean | 生产 Wi-Fi destroy 测试先失败再修复，parse 失败也清零 connect_config；BLE bond 临时副本和 pairing response 用完清零 | JS 不可变字符串/SDK 内部副本不声称可擦除；对端交互配对 not-run |
| F-10 | 唯一 v1；C registration、源类型、生成 manifest、docs selector | WiFiRadioStatus 在已有 radio 对象增加诊断；生成器和 check-js 验证；无后续能力占位 API | 稳定等级不变 |
| F-11 | CI Context generator 的 C3/S3/C5/disabled profiles | 三目标和 feature-disabled 构建见下表；设备执行另列 | 500 次完整 Adapter/Connection/RF/共存为 not-run；CSI 500 次 Host pool 循环不等价 |
| F-12 | W-01/W-04/W-05/W-09、B-01/B-07/B-10 | 02/03 明确启动前提、contract-pending 和本阶段交接 | 后续新能力未实施 |

## 已复现的失败

这些测试执行生产函数/分支，SDK/RTOS/JS 边界用 fixture 替换；没有独立测试状态机。

- `radio-before.txt`：11 个用例中 6 个失败：旧 token 计数、release 与 driver 初始化竞争、
  identity 回绕、get-mode/mode/start 失败后再次调用 driver。其余 5 个保留机制通过。
- `promiscuous-before.txt`：关闭 promiscuous 失败却提前清除 owner。现在保留精确 owner，
  retry 只执行未完成的关闭后缀，故障诊断要求设备重启。
- `ble-before.txt`：indication 的提交 `status=0` 被误作对端确认，Future 提前完成。
  固定 NimBLE `ble_gattc.c` 中 `ble_gatts_indicate_custom` 发提交事件，
  `ble_gatts_indicate_rx_rsp` 才发 `BLE_HS_EDONE`。错误终态和确认只消耗原连接的一次 pending。
- `ble-cancel-before.txt`：扫描 Future 转换/销毁时 cancel 失败仍释放 callback-visible storage。
  现在保留 scanner 并交给现有 orphan adapter cleanup；advertiser 同类路径采用同一修复。
- `wifi-before.txt`：driver 队列满会丢控制事件；owned connect password 在 free 前未清零。

GATT 测试编译生产 bind/acquire/detach/drop/Host-barrier 和 MTU/三种 discovery callback，
覆盖 callback 已进入时 detach、无活动 callback 但 native 仍未结束、旧 cookie 到达新操作、
另一连接独立进行、native registry 有界与永久 identity 耗尽。

## SDK 屏障与执行上下文

固定 NimBLE 的 `ble_gap_conn_broken` 先通知 GATT 模块，再通知 GAP disconnect；GATT callback
使用独立 identity，成功或错误终态后撤销该 identity。无 cookie 的 pair/connection-close 和
indication 在各自 GAP 终态前保留 lane；Host stop/deinit 成功是最终强制回收屏障，失败保留
registry。Runtime destroy 在 BLE deinit 成功之前不会销毁 Future runtime 或 JS heap。

Wi-Fi event handler 位于 ESP event-loop task；它只更新原生状态、EventGroup 和 Future 结果，
不创建 JS、不执行 Wi-Fi mutation。timer callback 只发布原生 timeout obligation；runtime
poller 执行 disconnect。观察/唤醒失败不撤销已记录的完成状态。ESP-NOW 和 CSI 的 native
queue retain、EventQueue drop/transfer 机制保持原实现。

## 命令与日志

原始证据目录：`build/wireless-core-evidence/`（构建输出，不提交二进制）。
完整上下文由以下命令产生，使用 firmware fixture，未用 Board 名猜硬件：

```sh
python scripts/prepare_ci_build_context.py --target esp32c3 --profile representative --output build/wireless-contexts/c3
python scripts/prepare_ci_build_context.py --target esp32s3 --profile representative-psram --output build/wireless-contexts/s3
python scripts/prepare_ci_build_context.py --target esp32c5 --profile representative --output build/wireless-contexts/c5
python scripts/prepare_ci_build_context.py --target esp32c5 --profile disabled --output build/wireless-contexts/c5-disabled
source /home/zach/esp/esp-idf/export.sh
python scripts/remote.py --build-context build/wireless-contexts/c3 --build-dir wireless-c3 --assume y build
python scripts/remote.py --build-context build/wireless-contexts/s3 --build-dir wireless-s3 --assume y build
python scripts/remote.py --build-context build/wireless-contexts/c5 --build-dir wireless-c5 --assume y build
python scripts/remote.py --build-context build/wireless-contexts/c5-disabled --build-dir wireless-c5-disabled --assume y build
python scripts/remote.py test --scope c
python -m unittest discover -s tests/python
python scripts/remote.py check-js
python scripts/generate_api_manifest.py --check
```

第一次 C5 命令没有激活 IDF Python，报 `No module named rich_click`，没有进入编译。
随后用 `export.sh` 环境重跑；不将这个环境失败归入代码缺陷。

| 验证 | 当前记录 |
| --- | --- |
| Host C | pass：65/65，`host-c-final.txt`，退出码 0 |
| Python | pass：336/336，`python-final.txt`，退出码 0；其中 5 项为编译生产 C 函数/分支执行的回归 |
| MQuickJS/manifest | pass：59 sources / 46 doc snippets；43 classes / 384 functions，退出码 0 |
| C3/S3/C5/disabled | pass：四个合法 Context，最终构建退出码均为 0；`build-matrix.json` 记录完整 Context、sdkconfig、镜像 SHA-256 |
| C5 preserve flash、设备回归和内存 | 镜像 hash verified；自动重连失败后用户重新连接。新固件四组预热 + 5 次回归、两次 pending 无线 Future restart 和一次空载对照完成；旧取样结论已由后续内存调查更正，正式固件静止状态复测通过，见下文 |
| BLE/GATT 对端、ESP-NOW 双机、CSI RF、500 次完整生命周期、共存 | not-run：缺少本轮对端/完整 RF 场景 |

F-CORE Gate 判定见本文末尾；F-HARDWARE 未通过。完整 SDK callback-entry 调度、
所有 JS conversion 分配失败/移动 GC 的覆盖缺口保留为明确风险，不因 Host mock 通过消失。


## C5 烧录与硬件边界

最终生产 Build：`359722be1d7ef3d831464bf1`，Artifact
`5221015bddd134798f401f600834e5442db13ecca3efe9d04163898642fdbf9c`。
MCU/Flash/PSRAM 与 live identity 一致，application image 为 2,468,096 bytes。
Build Context 固定 firmware commit `e1b861c2264718b99e77b4ab31c4efe511d393d5` 加本轮
未提交改动；包含 `ble-lifecycle` 文档。先前测试 Artifact `c3e7e40a...` 没有烧录。

一次 preserve flash：`cf007e4ed203d76fd79dba78`。backend 的
`serial-helper/.../esptool_adapter.py` 在调用 `write_flash` 前比较 ROM MCU 与
`BASE_MAC`，目标为 `hw-10bda3c854e8`。四个镜像（bootloader、partition、app、storage）
全部写入并通过原生 hash 校验；workspace 起点 `0x390000`、4,653,056 bytes 未擦除。

watchdog reset 导致 browser Serial endpoint 丢失。任务最终失败：
`BROWSER_SERIAL_RECONNECT_TIMEOUT`。**字节已校验不等于应用已成功启动**。
当时只进行只读连接查询，没有重试 flash/reset，没有操作其他设备。
随后用户明确告知“已连接”，连接阻塞已解除，补充结果见下节；原 flash job 的失败记录保留。

### 相同预热后状态的旧固件基线

注册的 `wireless_core/lifecycle-existing-api` 通过有界 operator exec 在旧固件执行：
1 次预热 + 5 次 BLE open/scan/close、ESP-NOW open/close、CSI open/close，每轮 GC，
无 workspace 写入。旧固件的扫描 `scanDrops=0`，因此不将它称为真实队列饱和证据。

| 旧固件静止状态 | 预热后 | 5 次循环后 |
| --- | ---: | ---: |
| Internal free | 95,535 | 95,535 |
| Internal largest | 59,392 | 59,392 |
| PSRAM free | 3,970,228 | 3,970,228 |
| PSRAM largest | 3,932,160 | 3,932,160 |
| Managed PSRAM | 152,397 | 152,397 |

新固件对应测试随后实际执行，结果如下。测试模块已纳入 registry，
`scanDrops=0`，仍不将本轮扫描测试当作真实 RF 队列饱和证明。

最终生产 ELF 的 `s_rpc_decoders=800`、`s_runtime_logs=556` bytes，与 SRAM 优化基线相同；
二者相对优化前仍减少 64,936 bytes 内部静态存储。本轮增加的有界控制状态为
`s_ble_operations=132`、Radio task mutex storage=92 bytes；`s_radio` 总计 288 bytes。
这些是链接测量；新固件相同预热脚本首次 internal free 比旧固件少 384 bytes，
PSRAM free 相同，internal largest 从旧固件的 59,392 变为 63,488 bytes。
SRAM 静态优化保留，但这不等于跨 runtime restart 的碎片验收通过。

### 用户重连后的新固件回归

2026-09-07 用户确认连接后，先只读核对 `hw-10bda3c854e8`、ESP32-C5 rev 1.0、
8 MiB Flash / 8 MiB quad PSRAM、workspace mounted / 4,653,056 bytes。
新 bootId 为 `571805a12aea8d87`，runtime generation 1，startup healthy，safe mode false，
failureCount 0。`wifi.status().radio` 已有本轮 `driverOwned`、`faultStage` 等诊断字段；
结合先前完整镜像 hash 校验，确认新代码已在运行。本轮没有再次烧录或硬件 reset。

执行同一份 `wireless-lifecycle.js`（每组 1 次预热 + 5 次循环），共四组。
BLE passive scan/close、ESP-NOW open/close、CSI open/close、每轮 GC 均成功；
每组内部的 before/after free、largest、managed owner 账本相等。四组扫描 dropped 均为 0。

两次 runtime restart 前各保留 BLE adapter/scanner、ESP-NOW session、CSI session，
并确认 BLE scanner.receive 和 CSI receive 的两个 Future 均为 pending；执行 GC 后
安排 200 ms 延迟的 `sys.restartRuntime`，不写 workspace。两次均完成：

- bootId 不变，runtime generation `1 → 2 → 3`，Radio generation 保持 1，符合 once 政策。
- pending Future 从 4 回到 Agent 基线 2，eventQueues 从 8 回到 4；orphans 为 0。
- Radio clients 从 2（ESP-NOW/CSI 各 1）归零，无 fault/restartRequired；无线资源可再次打开。
- 每次重启后均重跑一组预热 + 5 次循环；随后 startup healthy、failureCount 0。

因发现小幅跨 generation 内存差值，又执行一次**无线资源已全部关闭的空载对照** restart，
generation `3 → 4`，随后同样重跑。最终 generation 4 已健康运行，bootId 不变，
workspace 仍挂载，failureCount 0，pending Futures 2 / eventQueues 4 / orphans 0。
这里的“空载”仅表示没有本次无线资源，Agent 正常运行，不是整个设备没有分配活动。

| 同一脚本预热后的静止状态 | Internal free | Internal largest | PSRAM free | PSRAM largest | Managed PSRAM |
| --- | ---: | ---: | ---: | ---: | ---: |
| generation 1 | 95,151 | 63,488 | 3,970,228 | 3,932,160 | 152,397 |
| generation 2，pending 无线资源 restart 后 | 95,123 | 63,488 | 3,970,224 | 3,932,160 | 152,397 |
| generation 3，第二次 pending restart 后 | 95,103 | 63,488 | 3,970,224 | 3,932,160 | 152,397 |
| generation 4，空载对照 restart 后 | 95,095 | 36,864 | 3,970,224 | 3,932,160 | 152,397 |

**跨 restart 内存验收未通过**：internal free 累计少 56 bytes、PSRAM free 少 4 bytes，
空载对照后的 internal largest 下降 26,624 bytes。Managed internal 始终为 0，managed PSRAM
及其 owner 集合不变。现有数据不能把差值归因为无线泄漏，也不能证明它有界；
尤其不能把最大块下降写成“内存无回退”。未预热的独立 snapshot 与上述脚本不是同一状态，
不混用其 free 值计算泄漏。后续需固定原生分配跟踪，区分 runtime/Agent/SDK 的存活分配
与重建导致的碎片，再做有界重复对照。本轮没有为这个尚未定位的现象修改驱动。

生产 ELF 的 SRAM 优化仍保留。真实 BLE/GATT 对端、ESP-NOW 双机、CSI RF 帧和
Frame/Batch/View/Source 的真实转换 GC、500 次完整生命周期及共存继续为 `not-run`。
本轮设备结果不补足 F-03 完整 Host 调度或 F-08 第 N 次分配失败的缺口。

原始 JSON、执行脚本及 SHA-256 已保存于
`build/wireless-core-evidence/c5-reconnected/`，汇总为该目录的 `summary.json`。
所有执行脚本只使用现有 API、5 秒 operator exec 限额及明确的延迟重启，未修改 workspace。

### 记录位置

`build/wireless-core-evidence/build-matrix.json` 包含四个完整 Context 的文件 hash、
合成 hash、最终 sdkconfig 路径/hash、image bytes/hash。其合成 hash 前缀分别为：
C3 `6211f20950116f79`、S3 `a610080c97237e64`、C5 `65c3cbb31c168f79`、
C5 disabled `4de779e64e7ccbdb`。完整值以 JSON 为准。

本机原始设备记录：`/tmp/wireless-preflash.json`、`/tmp/wireless-baseline-lifecycle.json`、
`/tmp/wireless-artifact-final.json`、`/tmp/wireless-production-final-status.json`、
`/tmp/wireless-flash-status.json`、`/tmp/wireless-device-after-flash.json`。

## Gate 判定

已复现的 Radio identity/once/mutation/cleanup、BLE indication/scan cleanup、Wi-Fi
控制队列及秘密释放缺陷均完成修复，Host 检查、三目标/disabled 构建和公共生成物通过。

**F-CORE 暂不勾选为全部完成**：F-03 的完整 SDK callback-entry/stop 竞争调度，以及
F-08 所有 JS 构造/转换的第 N 次分配失败与移动 GC 覆盖尚不足。现有生产 helper 测试
和明确的固定 SDK 边界已记录，但不将其扩大成完整适配器证明。这两项保留为待完成短测试；W-00 输入采集已启动，W-01/B-01 仍以 F-CORE 通过作为启动前提。见[最新收尾与集中验收账本](2026-09-07-wifi-refactor-start.md)，包含后续复现并修复的 BLE timeout 取消失败存储释放问题及 Python 340/340、四目标构建结果。

**F-HARDWARE 未通过**：新固件的有界关闭/重开、GC、pending Future runtime restart
已取得设备结果；取样与任务回收时机问题已在后续内存调查修复并复测。真实 BLE/GATT 对端、
ESP-NOW 双机、CSI RF、500 次完整生命周期及共存未完成。此前首次 flash 依靠用户重新连接；后续去探针正式固件的自动重连已通过，见[内存调查](2026-09-07-runtime-heap.md)。新增 timeout 修复尚未刷写。长时间测试按用户安排在全部功能完成后集中执行，仍为 not-run；feature-stability 未提升。
