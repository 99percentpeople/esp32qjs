# W-04A Raw TX Radio 接入

firmware `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
已接入原生 Radio acquire/submit/retire；公开 `wifi.rawTx`、ByteSource/Future、
runtime reaper 和故障恢复仍待实现。没有 one-shot/RF 完成声明。

## 精确 lease 与共享信道

新增 RAW_TX client，复用现有有界 live lease registry。`raw_tx_acquire()` 在 Radio
mutation mutex 下完成 acquire/start/channel；AP 要求实际 AP 已在运行，不创建
隐式 AP 配置。Raw TX 只可加入已启用的 AP/APSTA 接口，不放开其他模块的 AP
生命周期策略。部分 acquire/start 失败时，非零 lease 仍交给原生 caller 清理。

数字 channel 持有共享固定信道；channel=0 使用当前信道，空闲时不固定。冷 driver
尚无可读国家配置时先做目标信道编号检查，初始化/启动后再用现有 channel setter
做法规读回；因此冷启动后的法规失败可能留下已取得的 lease，需要 caller 清理。
当前 helper 不是输入失败后 driver 永无初始化副作用的证明。

submit、retire、通用 lease/channel 释放由同一个 Radio mutation mutex 串行化。
submit 核对精确 generation/identity/client、live required interface、启动状态、
lifecycle/scan/connect 预留和 broker 状态。使用真实 SDK mode/association/MAC
读回生成 policy；SDK getter 错误保留原值返回，不从 JS 接收 associated_path。

任一 STA 连接或 AP client 都要求 driver sequence；根据所选接口 Addr1/Addr2 与
实际 peer/self 匹配判定 connected path，执行已有 DS 和 PM/Retry/MoreData 校验。
Radio mutex 不会暂停无线关联状态变化；这些是发送前快照，SDK 仍负责最终接收或
拒绝，不能宣称消除了关联表 TOCTOU。

在途提交临时固定已观察到的 channel，数字信道 owner 保留原固定声明。callback
或 SDK 返回之前不能释放 lease/channel，timeout/abandon 也不能解除。只有精确
broker token 成功退休后，才撤销临时固定声明；数字信道声明继续保留。框架 mode
变更和 channel setter 不得绕过在途保留；scan/connect 复用现有 fixed owner 准入。
无线侧自行换信道仍由既有 observation/conflict 机制记录，不冒充 RF 锁。

## driver 关闭与未完成的恢复

正常物理 shutdown 在零 owner/stop 后执行 broker unregister，成功后仅重试 callback
排空后缀。SDK deinit 成功记录 driver_owned=false 后，才允许 broker reset；排空
失败不重复已完成 deinit，也不提前推进 generation。driver stop/start 不注销 boot
callback，避免每个发送操作封存 registration。

无法确认原生完成的发送仍保留精确 lease，因此普通 stop/shutdown 会拒绝。这一轮
没有绕过 lease 执行强制 deinit，也没有为恢复 Raw TX 断开无关 Station/ESP-NOW。
后续 native reaper 必须在 JS Future timeout/GC/runtime teardown 后继续持有 lease，
迟到 callback 到达后调用 Radio retire/release。无 completion 或 correlation fault
的显式恢复尚未实现，不能声称 runtime restart 已能恢复。

`status().radio.clients.wifiRawTx` 计数并纳入 total；activeOperations 包含保留中的
Raw TX 提交。类型与 API 文档同步，这些诊断不代表公开 Raw TX namespace 已注册。

## 用例与当前检查

新增 `test_wifi_raw_tx_radio.py`：生产 Radio registry/lease/channel observation/
submit/retire/release，加生产 broker/validator/snapshot；使用固定 SDK 类型与
可控 pthread/SDK 边界。覆盖旧 lease、scan 冲突、连接路径校验、SDK query/OOM
失败、send 中另一个任务释放 lease、超时后原生保留、迟到完成、数字固定信道保留、
信道冲突与运行 AP 加入。fixture 不覆盖 AP startup 或物理 shutdown；不能把这些
分支写成已测试。扩展既有 status GC fixture 的 client 数组，保留集中 GC/OOM 用例。

按用户安排，以上用例未编译或执行。当前 C5 immutable Build Context
`firmware-ci-esp32c5-representative` 构建通过：binary `0x28cdb0` / 2,674,096 bytes，
app 空余 15%。三项 Radio Raw TX 入口存在于目标对象；final ELF 目前只链接 broker
status/unregister/reset 的 shutdown 路径，尚未链接发送入口。不能称设备已发送成功。

目标符号账本：`s_raw_tx` 128 bytes，Radio registry/state `s_radio` 824 bytes。
新增 lease 在途字段和 broker 有固定原生内存成本；没有还原先前 RPC/runtime-log 的
SRAM 优化；Raw TX client/在途字段由 FEATURE_WIFI gate 排除，不向仅共享 Radio 的
feature-disabled 构建追加这份账本。动态 buffer 仅在提交时分配且最多一份 1500 bytes；实机预热后同等静止
状态的 free/largest-block/pool/owner 比较仍待集中阶段执行。

MQuickJS 61 sources/48 snippets、manifest 47 classes/417 functions、feature 27、
schema 35 STA/21 AP（live SDK）、strict TypeScript、recorded SDK map、五份 Python
AST、whitespace 均通过。SDK 干净。证据为 `build/w04-raw-tx-radio-evidence.json`。
Host/VM/故障注入、C3/S3/disabled、设备/RF/heap 均 not-run；soak 留在 BLE API 后。
未刷写、串口操作、擦除 workspace、前端构建、提交、推送或更新父仓库 gitlink。
