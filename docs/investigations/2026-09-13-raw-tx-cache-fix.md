# Raw TX：TX cache descriptor 身份转移修复

## 结论与状态

已确认并修复固件的 descriptor 生命周期缺陷：SDK 开启 TX cache 时，
`ieee80211_output_process()` 分配新 TX descriptor，复制旧 descriptor 的
发送信息，再回收旧 descriptor。初版 Raw TX broker 只在 API 分配点绑定
旧 descriptor，没有跟随替换，导致完成回调无法关联到原发送记录。

真实 C5 SDK 缓存路径已在修复前复现 descriptor 不一致，修复后通过。
同一用户 bootId 的本地日志确认设备启用 static TX / TX cache，符合这一
触发条件。设备旧映像没有 descriptor 现场诊断，因此主机复现与设备复测
分别记录。后续获用户授权后已刷入目标 C5；原样 probe-request 的两轮
共 20 次发送取得完整通过记录，包含驱动停止后由 scan 拉起的路径。

基于 firmware HEAD `a57bafb`，改动未提交，未更新父仓库 gitlink。
早期源码排查没有操作设备；后续刷写与验收见本文末节，workspace 保留。
前两轮过程见
[completion-correlation 排查](2026-09-13-raw-tx-completion-correlation.md)。

## 复现和目标证据

- 用户目标：XIAO ESP32-C5 / `hw-10bda3c854e8`，Station MAC
  `10:bd:a3:c8:54:e8`，8 MB Flash / 8 MB Quad PSRAM，IDF v6.1，bootId
  `dc910fd436e3bdf6`。后续刷写前重新验证了 ROM MCU/MAC，证据见末节。
- `wifi.scan()` 启动未关联 Station，然后 one-shot send；默认 interface
  `station` / sequenceControl `driver`，显式 channel 4、timeoutMs 3000。
- A：26-byte deauth；B：32-byte broadcast probe request。精确输入保存在
  `tests/c/fixtures/wifi/tx/test_wifi_raw_tx_identity_sdk/reported_frames.inc`。
- 两组均报告 259 / `completion-correlation`；validationCode 0，
  submitError / registrationError 为 null，lane/client/activeOperations
  保留，显式 recover 有效。
- 只读检查本地缓存
  `/home/zach/.local/state/esp32qjs-hub/logs/devices/hw-10bda3c854e8/chunks.jsonl`，
  按同一 bootId 拼接日志块。`boot-log-excerpt.json` 保存所需摘录：
  `Init static tx buffer num: 2`、`Init tx cache buffer num: 32`、Wi-Fi
  firmware `e12a754` 及相同 Station MAC。没有读取或操作其他目标。

## 因果链与修复

1. `esp_wifi_80211_tx` 分配 descriptor A；broker 将 A 绑定到 native token。
2. SDK output_process 的 cache 分支分配 B，执行
   `ieee80211_copy_eb_header(B, A)`，随后回收 A。raw 标志、interface、
   rate、sequence 策略及完成 callback mask 随发送信息进入 B。
3. 完成回调收到 B；旧 broker 只认识 A，将 B 计作 orphan callback，
   设置 registration uncertainty，并隔离尚未完成的 operation。
4. one-shot poll 主动生成 259 / `completion-correlation`；retire 拒绝
   未确认完成的 token，cleanup 停在 `native-completion`，继续持有 lease
   和 lane。显式 recover 的 deinit fence 才允许回收。

修复增加精确的 output_process → copy_eb_header relocation hook：先执行
原 SDK copy，再在锁内将已有 Raw TX 身份从 A 转移到 B，在调用方回收 A
前完成。无关 SDK 流量不创建 Raw TX 身份；目标 descriptor 已被其他 token
占用时保留隔离并记录 `descriptor-transfer`。不通过帧内容猜测 identity，
也不删除 MAC/interface 校验。

实际目标的 ROM linker assignment 会覆盖同名 `ieee80211_output_process`
库函数。只修改库内 copy 调用不足以生效：第一版候选构建中 wrapper 被
GC 掉，已由最终 ELF 检查发现，未刷入设备。最终为已审查函数增加 hidden
internal symbol，令 output_init 的注册入口和 net80211_funcs_init 的
ROM dispatch table 均指向它；保留原 ROM ABI。

三个相关 SDK member 使用精确 SHA-256 和 relocation offset 校验。
API member 同时接受原始对象与前序 TX-rate 修复后的精确 hash；原有 pp.o
完成先于 recycle 的 hash guard 保留。共享 SDK 文件、指令和 RF 字节不变。

此前新增的首次失败原因/identity/generation 和四类 callback counters
继续保留，类型与公共文档同步；不增加格式版本。记录仅使用静态字符串
和整数，没有 callback 内分配或 SDK 临时指针保留。

## 排除和证据限制

- 未关联时 driver/application 均被框架和真实 SDK 校验接受，
  [官方 C5 契约](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c5/api-reference/network/esp_wifi.html)
  也允许这两种选择。默认 driver 不是参数误用。
- Probe request 属于 SDK 原生支持范围，故障不限于扩展 deauth subtype。
- A 的 reason 字节 `00 07` 按 little-endian 表示 0x0700，而非 7；这是
  独立帧内容问题，不能解释 B 的相同故障，未作为 firmware 修复依据。
- Getter 在返回后才被 snapshot 读取，没有发现“metadata 尚未填完”的
  顺序错误；同-descriptor 模型执行真实 getter 也通过。
- 原 identity 测试在 HMAC stub 中直接调用 completion，并把 getter stub
  为清零 metadata，绕过了 descriptor 替换，不能证明 cache 路径完整。
- 新 SDK 用例执行真实 submit、post、output_init、output_process、copy、
  cache recycle 和 getter；pool allocator、OS、关联搜索和 RF dispatch
  是显式测试边界。没有模拟 scan 生命周期、Wi-Fi task 时序或真实空口。

## 验证

本地证据目录：`build/raw-tx-correlation-investigation/`。

| 文件 | 证据 |
| --- | --- |
| `reported-sdk-frames.json` | 两组精确帧 × 两种 sequence × 两种 prefix × 成功/失败 metadata 的 16 组合通过；此阶段不含 cache 替换 |
| `static-cache-before.log` / `.json` | 真实 C5 output_process 完成 descriptor 替换后，旧绑定断言返回 71；为运行期失败，不是编译失败 |
| `static-cache-sdk-after.json` | Copy hook 修复后，真实缓存路径 16 组合通过 |
| `static-cache-rom-after.json` | 将原 output_process 名称强制绑定到 trap，真实 SDK output_init 仍注册并执行修复后的入口；3 个 SDK 用例通过 |
| `cache-fix-native.json` | Raw TX 专项 42/42 通过，0 skipped，覆盖生产 broker 转移、旧 descriptor 提前重用、乱序完成、无关流量和目标冲突 |

最终 ROM 修正后，受影响的 3 个 SDK 用例单独复跑，不重复计入 42 个用例。
API manifest 检查通过，62 classes / 663 functions；git diff --check 通过。

目标构建复用现有合法不可变 Build Context，在 IDF export 环境运行
`ninja -C <build-directory> -j 4`。最终各目标日志为
`<target>-cache-final-build.log`；退出状态、映像 hash 和最终链接入口检查
汇总到 `cache-final-verification.json`。此前不含 hidden origin 的候选构建
不算最终修复证据。

| 最终目标构建 | app bytes | 现有 app 分区余量 | ELF 入口检查 |
| --- | ---: | ---: | --- |
| C5 XIAO / Quad PSRAM | 3,141,440 | 4,288 | 注册入口、ROM dispatch table、copy hook 均指向修复实现 |
| C3 representative | 3,169,328 | 按现有 Build Context 大小检查通过 | 三处检查通过 |
| S3 Sense / Octal PSRAM | 2,959,024 | 186,704 | 三处检查通过 |

三个目标构建均退出 0；未调整分区。C5 新 output_process 占 676 bytes IRAM，
copy wrapper 占 192 bytes flash text；这是符号大小，不是运行期 heap/soak
证据。最终 C5 app SHA-256：
`4847c26417c2a0195918bcf73cb7d7964b7ee2c4385a43fa2013aa81ec38ce9a`。

## C5 刷写及 one-shot 实机验收

用户随后授权“帮我刷机检查”。仅操作 C5 `hw-10bda3c854e8`；USB by-id
对应 `10:BD:A3:C8:54:E8`，ROM probe 确认 ESP32-C5、8 MB Flash，Agent
确认 8 MB Quad PSRAM。刷写器在同一串口独占期内再次核对 ROM MCU/MAC。
证据目录：`build/raw-tx-correlation-investigation/hardware-c5/`。

- 后台 build job：`62987480048c88a8d7ebaba9`。
- Artifact：`84527253d094a035dccdce0a1f608e4f2b5cadd26a2f4c8e4c133585ab2500b3`。
- Flash job：`201b7e9cca0296af826d6d11`，`workspaceAction: preserve`。
- 本次后台新 Build Context 为 `e3d763e70a590f91dc1d14f46063855dbe0892a7804669411db032e1d4bee88a`；
  app 仍为 3,141,440 bytes，SHA-256 为
  `78874279276ce426e5be4611084054828f5a902995a616dce59acc5bc1ec1282`。
  已核对 Artifact app hash 与构建文件一致，并重查最终 ELF 三处入口。
- 四个映像写入均通过哈希校验，`flashVerified=true`、`agentConfirmed=true`。
  后台固定返回的 `artifactIdentityConfirmed=false` 表示没有运行期 Artifact
  身份证明；本次映像证据来自实际刷写校验和最终 ELF 检查。
- 新 bootId：`e28c2d80bf393ec3`；workspace 挂载，safe mode false，
  startupFailureCount 0。2,752-byte workspace `index.js` 刷写前后全文相同，
  SHA-256 为 `7ebfb188f0fd0c8e7d53e9774aaff2d08d14ea84613cf66d3a804854e1b0e1d4`。
  Flash plan 没有 workspace 映像，设备测试通过 exec 运行，没有写入临时文件。

使用用户原样 32-byte broadcast probe request，channel 4、timeoutMs 3000、
默认 Station，未关联。每轮先 `wifi.scan()`，再默认 driver sequence 5 次、
application sequence 5 次：

| 完整结果 | 起始 driverState | 完成 sequence | 结果 |
| --- | --- | --- | --- |
| `probe-compact-results.json` | started | 11–20 | 10/10 |
| `cold-scan-probe-results.json` | stopped，由 scan 拉起 | 21–30 | 10/10 |

每次 `driverAccepted/driverCompleted=true`；laneIdentity null、operationActive
false、clients.wifiRawTx 0、activeOperations 0，quarantined/correlationFault/
cleanupPending 均 false；四类 callback 异常计数均 0。整个测试 bootId 不变，
radio generation 始终为 1，没有调用 recover。最终保持未关联 Station、
driver started、channel 4，与用户原始复现环境一致。

首次详细返回尝试收到 HTTP 409，原脚本未保存错误 body，因此不计入通过数。
该返回结构包含每次完整 Raw TX 状态，与 Agent 的 16 KB exec 结果限制相符；
只读检查先确认 bootId 不变、资源已释放，再缩减返回字段执行上述两轮。
保留初始请求、事后状态及两轮完整结果，避免将结果回传失败算成 TX 故障。

本次确认 probe-request 的 native completion 与资源回收；没有发送 deauth，
没有接收端空口捕获，也未重新验收 Session 或长时间稳定性。
