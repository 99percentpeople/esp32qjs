# W-04A Raw TX 原生 completion broker

当前为内部基础实现，尚未接入 Radio 和公开 `wifi.rawTx`。firmware HEAD
`d7db8d1`，本地 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本记录不能作为 one-shot 完成、Future 关闭正确或 RF 验收通过的证据。

## 原生状态与数据寿命

生产实现为 `src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx_broker.c`
（位于 `components/esp32_mquickjs`）。一个 boot-lived broker 只保留一份
24–1500 byte 发送副本；使用 generation、单调 identity 和 Radio lease identity
三元组验证 abandon/retire。identity 到 UINT32_MAX 后拒绝新提交，不因 deinit 回绕。
这只是下层 identity 校验，Radio 精确存活 lease 检查仍待接入。

调用者需持有 Radio mutation mutex 执行 register/submit/retire/unregister/reset；
status、abandon 和 SDK callback 可由其他任务调用。broker 自身的短临界区只保护
状态和快照；SDK、时钟与 allocator 均在其外。当前没有调用者实施这个外层锁约束，
不得把它描述成 Radio 集成竞争已验证。

提交前复用 MAC validator 并复制输入。driver 调用前发布 token，分别保存
submit_returned、driver_accepted、driver_completed；支持 SDK send 尚未返回时
同步进入 completion。只有 send 已返回、completion 已复制、callback 排空且无
关联故障才允许 retire/free。callback 只复制无指针快照，不读取 SDK data 指针。

abandon 表示上层已不再等待，不取消 RF，也不释放仍在途的存储。没有 completion
时保持隔离，拒绝复用；迟到 completion 可完成原操作的原生退休。SDK send 错误
无法证明 buffer 已不被引用，因此也保留副本和原始错误；不能仅凭返回错误 free。
已有 completion 又收到重复 completion，或 SDK error 同时出现 completion，进入
correlation_fault。没有任务、Session、JSValue 或 Future 指针保存在 driver callback。

## 无 cookie 的限制和注销边界

interface/MAC 不匹配只计数并拒绝，不结束当前操作；地址匹配不是请求身份。
无活动操作时进入的 orphan callback 会阻止新提交。正常完成后复用 lane 仍依赖
框架独占该 SDK 发送路径及 SDK 每次提交仅一个完成回调的契约；无法区分前一操作
退休后、下一操作开始时才到达的同地址重复回调。该限制须在 Radio 接入及集中阶段
测试中继续审查，不能凭生成的 token 声称 SDK 具备 cookie。

源码审查发现原始草稿在 unregister 成功且活动计数为零时会清空 registration
generation。活动计数为零不能证明 SDK 没有已派发但尚未进入的 callback，因此
改为保留 generation/unregister_written，禁止重新注册，直到实际物理 deinit。
新增用例先写入；按用户后置测试要求，未运行旧草稿取得失败结果，本项仅记为源码
审查修正，不能写成“已复现并回归通过”。

注销 SDK 失败保留 ownership/原始错误。注销已写入但 callback 尚未排空时返回
timeout，后续只重试排空后缀，不重复注销。注销成功仍封存该 registration。
普通逐包完成使用 retire；unregister 用于 driver 关闭，不用于每次发送的收尾。

`reset_after_deinit()` 只是接受 Radio 已完成物理 deinit/排空证明的内部释放入口，
不主动重启或 deinit。超时、JS GC、runtime restart、注销成功都不是这一证明。
Radio 尚需实现共享 owner 准入和失败关闭路径，不能为了 Raw TX 恢复断开无关
STA/ESP-NOW owner；当前不宣称 runtime restart 可恢复隔离状态。

## 待执行的生产实现用例

`tests/c/integration/wifi/tx/test_wifi_raw_tx_broker.py` 直接编译生产 broker、validator、RX parser
及 callback snapshot，使用 inventory 中 C3/S3/C5 的 SDK TX 类型；只替换 SDK、
allocator、时钟和 pthread 锁边界。包含：

- register 失败、重复注册、注销失败和仅排空后缀重试；
- 输入非法不分配/发送、分配失败、精确/旧 token 与重复退休；
- SDK 返回前同步 completion、超时后的原生副本保留、迟到完成；
- invalid/interface/MAC 不匹配、duplicate 和 submission error 隔离；
- 在 callback 时钟边界暂停，验证未排空时不能退休/重置；
- 物理 deinit 证明入口和 identity 耗尽不复用；SDK/allocator 不在临界区。

这些用例尚未编译或执行。没有 Future/EventQueue 的饱和测试或 runtime teardown
集成证明；该部分依赖下一步实际 Radio、ByteSource、Future 和 reaper 接入。

## 当前检查

C5 immutable Build Context `firmware-ci-esp32c5-representative` 构建通过，binary
`0x28c7d0` bytes，app 空余 15%。broker 的七个入口存在于目标对象，但 final ELF
没有这些符号：尚无生产调用者，不能据此宣称代码已在设备工作。

MQuickJS 语法 61 sources/48 snippets、manifest 47 classes/417 functions、feature
文档 27 项、config schema 35 STA/21 AP（含 live SDK）、strict TypeScript、recorded
SDK map 与三份 Raw TX Python AST 检查通过。SDK 工作区干净。

证据：`build/w04-raw-tx-broker-evidence.json`、`build/w04-raw-tx-broker-c5-build.txt`、
`build/w04-raw-tx-broker-check-js.txt`。Host C/Python/VM、C3/S3/disabled 矩阵、实机
及双机 RF/heap 均 not-run；长时间 soak 继续安排在 BLE API 完成后。没有刷写、串口
操作、擦除 workspace、前端构建、提交、推送或父仓库 gitlink 更新。
