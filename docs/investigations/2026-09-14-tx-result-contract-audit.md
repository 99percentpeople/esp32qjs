# 发送结果统一设计审查

状态：接口改造已实施；源码审查、host 回归与目标构建结果见文末。
下列“当前行为”表记录改造前的审查基线。最初增加的 rawStatusName 已被共享
completion 契约替换，没有保留旧字段别名。公开 API 以
[发送完成契约](../api/tx-completion.md) 为准。

## 结论

采用 `completion` 聚合发送完成信息是合适的，但仅改 Raw TX 的三个字段还不能
形成统一契约。需要同时统一完成状态的语义、原生数值的命名空间、异常边界、
队列统计以及平台诊断消费方式。

共享的是“原生状态的表示”和“发送完成的语义”。排队回执、帧发送完成、操作
结束、原生资源退休、应用确认仍是不同事实，不能合成一个 `ok`。

## 已确认的不一致

| 范围 | 当前行为 | 需要解决的问题 |
| --- | --- | --- |
| Raw TX one-shot / Session.send | `driverStatus`、`rawStatus`、新增 `rawStatusName`；两条路径共用结果转换器 | 将状态、数值、符号名放入一个共享结构；已知完成结果的接受/完成布尔值重复 |
| ESP-NOW send / broadcast | 回调枚举立即转换为 `macDelivered` 布尔值 | 原始状态丢失，未来未知枚举也会落入失败；广播返回值容易被理解为对端收到了 |
| Action TX | `driverStatus` 与 `terminalStatus` 分开；未观察到 TX 状态时内部是 `-1` | 必须区分没有发送完成观察与未知枚举；驻留结束/取消不能作为帧发送成功 |
| NAN follow-up send | 完成失败被转换为 `ESP_FAIL` 并抛异常；成功结果才返回 TX 标志 | 与 Raw TX/ESP-NOW 的 MAC 失败正常返回不同；合成的 `ESP_FAIL` 不是原始完成码 |
| Raw TX Session / periodic | Session 的 `failed` 仅是 MAC 失败；periodic 的 `failed` 还包括 rejected/dropped | 同名统计不是同一口径，不能直接合并 |
| ESP-NOW queue | `completedPackets` 包括被结算的提交失败；MAC 失败折成 `lastError=ESP_FAIL` | 与 Raw TX periodic 的“有 MAC 完成”口径不同，且原始失败域丢失 |
| 平台诊断 | ESP-NOW 广播/单播完成布尔值都累计为 `delivered` | 调用方已经把完成观察写成送达统计，必须联动更名及修正判断 |

源码依据：

- [Raw TX 结果转换与 poll](../../components/esp32_mquickjs/src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx.c)：
  `raw_tx_poll`、`esp32_mquickjs_wifi_raw_tx_result_to_js`；
  [Session 转换](../../components/esp32_mquickjs/src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx_public_session.c)。
- [ESP-NOW](../../components/esp32_mquickjs/src/modules/espnow/esp32_mquickjs_espnow.c)：
  `espnow_send_callback`、`espnow_send_finish`、`espnow_complete_queued_send`。
- [Action lane](../../components/esp32_mquickjs/src/modules/wifi_action/esp32_mquickjs_wifi_action_lane.c)
  与 [Action finish](../../components/esp32_mquickjs/src/modules/wifi_action/esp32_mquickjs_wifi_action.c)。
- [NAN finish](../../components/esp32_mquickjs/src/modules/wifi_nan/esp32_mquickjs_wifi_nan.c)、
  [message 状态](../../components/esp32_mquickjs/src/modules/wifi_nan/esp32_mquickjs_wifi_nan_message.inc)、
  [公开转换](../../components/esp32_mquickjs/src/modules/wifi_nan/esp32_mquickjs_wifi_nan_message_public.inc)。
- [Session 结算分类](../../components/esp32_mquickjs/src/modules/wifi_raw_tx/esp32_mquickjs_wifi_raw_tx_queue.c)
  与 [periodic 统计契约](../api/wifi-raw-tx.md)。
- 工作区 `web/backend/src/devices/diagnostics.ts` 中的 peer traffic、unicast sender、
  reconnect traffic 生成代码。

另有一个已确认的类型缺陷：[WiFiTxRateError 声明](../../types/esp32qjs-c-api.d.ts)
将 `espCode`、`stage`、`driverAccepted` 等声明在错误顶层，而
[tx_rate_error 实现](../../components/esp32_mquickjs/src/modules/wifi_driver/esp32_mquickjs_wifi_driver.c)
实际将它们放在 `details`。统一工作要修正声明，沿用
[NativeError 契约](../api/native-errors.md)，不能照着错误类型改坏运行时。

## 建议的共享结构

```ts
interface NativeCode {
  domain: string;
  code: number;
  name: string | null;
}

interface TxCompletion {
  status: "success" | "failed" | "unknown";
  native: NativeCode | null;
}
```

具体模块应收窄 `domain` 和已知 `name` 的类型。通用结构容纳不同原生接口，
不能因此允许模块输出任意拼写的域。`native` 对象也可复用于
`NativeError.details.native`；错误顶层的 `code` 仍是稳定的框架错误字符串。

Raw TX 结果示例（其余身份、帧信息、时间和速率字段保持各自含义）：

```js
{
  sequence: 5,
  radioGeneration: 2,
  completion: {
    status: "failed",
    native: {
      domain: "wifi_tx_status_t",
      code: 1,
      name: "WIFI_SEND_FAIL"
    }
  }
}
```

| 来源 | 原生数值域 | 映射规则 |
| --- | --- | --- |
| Raw TX 回调 | `wifi_tx_status_t` | SUCCESS / FAIL 映射为 success / failed；保留真实数字 |
| ESP-NOW 回调 | `esp_now_send_status_t` | 使用 ESP_NOW_SEND_SUCCESS / ESP_NOW_SEND_FAIL 名称，即使数字与 Raw TX 相同 |
| Action TX 发送事件 | `wifi_action_tx_status_type_t` | DONE / FAILED 形成 completion；DURATION_COMPLETED / OP_CANCELLED 形成操作终态 |
| SDK 调用返回错误 | `esp_err_t` | 放在异常详情；不能当作 TX 完成码 |
| NAN 当前同步回调桥接 | 当前仅提供 bool | completion.status 可确定，native 为 null；追溯上游后确有原始枚举才能填入 |
| NAN USD Action 事件 | `wifi_action_tx_status_type_t` | 在转换成 bool 前保留事件原始值 |

这些域已由本地 ESP-IDF 的 `esp_wifi_types_generic.h`、`esp_now.h` 以及 NAN
适配代码核对。域只标识数值命名空间，不自动证明故障发生在 SDK 内部；例如
框架生成的 `ESP_ERR_INVALID_STATE` 还需要结合错误 `stage` 理解。

空值规则必须明确：

- 已收到合法完成回调但枚举未识别：`status: "unknown"`，保留数字，`name: null`。
- 没有可归属的发送完成观察：`completion: null`，不能用默认数字 0 表示成功。
- 完成结果只有布尔观察：`completion` 非空、`native: null`；不伪造 SDK 枚举名。
- Raw TX / ESP-NOW 正常返回应保证 `completion` 非空。只有状态快照、错误详情、
  或 Action 先结束而未观察到发送完成的结果需要允许 `completion: null`。
- Action 当前会把不认识的事件判为 ambiguous。共享显示器支持 unknown 不等于
  可以放宽事件相关性和所有权校验；不能把无法归属的回调发布为普通 unknown。

`status` 用于业务分支，`native` 用于诊断。不要增加从 `WIFI_SEND_FAIL` 推断的
`reason: "no-ack"`、`address-rejected`、`delivered: false`。

## 完成、异常与所有权

| 事实 | 建议公开语义 |
| --- | --- |
| 参数不合法 | TypeError / RangeError；帧验证的稳定模块错误需与现有专用校验码一并明确 |
| 队列接纳 | 返回 admission，仅证明入队 |
| SDK 提交拒绝、Radio 冲突 | NativeError，保留 operation、stage 和对应原生错误域 |
| 可归属的 TX 完成失败 | 正常返回 `completion.status = "failed"`，四类发送 API 保持一致 |
| 等待超时、相关性失败 | NativeError；保留身份、已观察到的完成和原生所有权信息 |
| Action 操作驻留结束/取消 | 单独保留 terminalStatus；需要数字/名称时可复用 NativeCode |
| buffer 回收、native 退休 | 保留模块生命周期字段；不能由 completion 推断 |

这需要调整 NAN 的失败分类：正常 TX 失败不再伪装为操作异常，但提交错误、
超时、取消、关闭和退休错误仍走对应错误路径。不能简单把 `status.error` 全部忽略。

此前建议删去的 `driverAccepted/driverCompleted` 仅适用于保证这些事实成立的
正常 Raw TX 结果。先验证 one-shot 与 Session 的构造入口，再删冗余输出。
`status()` / 错误详情中的提交、清理、隔离、身份及 generation 信息仍然必要。
NAN 的 `bufferRetired` 在成功返回后也可能为 false，不能顺手删除。

各模块取消时机不同：Raw TX 提交后取消可能只结束等待，ESP-NOW 已开始后会
拒绝取消。统一结果结构不应改变这些行为，也不自动增加重试。

ACK 不应成为新增的推测结果。Action 的 `noAck` 是请求策略，不是实际 ACK
观察。SDK 完成也不是应用确认；需要可靠消息时由已有的
`js-libraries/espnow-reliable` 等上层协议维护序号与应用 ACK。
该 Library 当前不依赖 macDelivered 判定成功，而是等待自己的 ACK，并透传
native send 结果。Espressif 也明确区分 ESP-NOW MAC 完成与应用接收。
参见 [ESP-NOW 发送说明](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c5/api-reference/network/esp_now.html#send-esp-now-data)。

## 统计与其他模块的边界

建议以 Raw TX Session 已有的互斥结算分类作为 packet ledger 基础：

```text
completed = succeeded + failed + unknown
settled = completed + rejected + aborted + dropped
pending = admitted - settled
```

`completed` 只计可信 MAC 完成，`failed` 只计失败完成，`rejected` 计已接纳但
提交失败的包；入队前拒绝另计。物理 teardown 结算的 accepted 包可能是 aborted，
不能计作 completed，也不证明未发射。统计从一致快照产生，不凭分散原子读保证恒等式。
periodic 的 scheduled / issued / skipped 保留为调度统计，不能拿 issued 代替 admitted。
ESP-NOW queue 需要拆出完成失败与提交错误，不能继续用 ESP_FAIL 覆盖回调原因。

以下 API 不套用 TxCompletion，但应遵守相同的原生码表示规则：

- Mesh `WiFiMeshCommandResult`：当前表示 SDK 命令返回，completedAtUs 也不是 MAC 回调时间。
- ROC：驻留操作，没有帧发送结果；FTM：保留测距报告自己的终态和数据。
- TCP/UDP send、BLE write：保留字节数及各自协议语义。BLE notify 的
  submittedConnections 也不能改叫 deliveredConnections。
- NimBLE host / ATT、TLS 等各自有错误域；不要统一调用 esp_err_to_name。

## 联动修改与验收

实施时一次替换所选范围的 sole v1 契约，不保留 rawStatus / macDelivered 等旧字段
别名，不新建 v2，也不在平台增加兼容读取分支。

1. 固件：共享原生码/完成转换助手；Raw TX one-shot、Session、ESP-NOW、Action、
   NAN 的回调快照及 JS 转换；相关 queue / periodic 统计；TX 相关错误详情与类型。
   字符串及 JS 对象只在 VM 线程构造，SDK 回调只复制有界原始数据。
2. 公共契约：同步更新 `types/esp32qjs-c-api.d.ts`、相关 `docs/api/*.md`，
   共用语义在一个 API 文档中定义，并通过 `docs/api/docs.json` 提供给平台 AI/MCP。
   本审查文档不加入已发布 API 索引，避免 AI 将草案当成已实现能力。
3. 工作区：更新 `web/backend/src/devices/diagnostics.ts`，将发送端统计改为
   txSucceeded / txFailed 等准确名称；有独立接收证据才报告 peerReceived。
   同步相关测试、`js-libraries/espnow-reliable/tests/reliable.test.js` 的 mock
   以及 `boards/seeed-xiao-esp32c5/docs/hardware.md` 的示例。
4. 平台 AI 与外部 MCP 消费同一固件结果和文档，不各做一次状态翻译；工具执行
   成功与其返回数据中的 TX failure 分层解释，不能由 transport 成功推断无线送达。
   新固件发布后刷新对应文档 Artifact，避免旧固件配新返回结构文档。

必须覆盖的验证：

- 各发送路径的 success、failed、已观察未知枚举；缺失回调不冒充成功。
- Action 的发送事件/终止事件不同顺序，以及终止时无发送观察；NAN 完成但 buffer 未退休。
- ESP-NOW 同步发送与队列都保留原始域；MAC 失败与提交失败分别结算。
- Raw TX Session/periodic 的 rejected、dropped、aborted 与 MAC failure 计数边界。
- 超时/取消/相关性失败后 native ownership 和 recovery 保持正确，不触发自动重发。
- 嵌套对象转换的分配失败和 moving GC；新增 native 对象有 GC root，OOM 不重提发送。
- 平台生成的实际设备诊断程序分别执行成功、失败、未知和广播场景，不能将 TX
  成功记成对端接收。单纯检查生成字符串包含新字段不足以验证语义。
- C5/S3 目标构建及正常自有设备通信验收分别记录，不能用 host mock 代替 RF 证据。

## 实施记录

已替换 Raw TX one-shot / Session、Action、ESP-NOW send/broadcast 及 NAN follow-up
的返回结构，新增共享 NativeCode / TxCompletion 转换器及类型。Raw TX 结果
构造要求已接受且已完成；Action 未观察到 TX 时 completion 为 null；NAN 完成
失败正常返回，但提交错误、取消、超时仍抛异常，bufferRetired 保持独立。
NAN USD 在转换为布尔值之前保存 Action 枚举，同步 NAN 保留 native:null。

ESP-NOW callback 和 queue 保留未知枚举，不再把 MAC 失败合成为 ESP_FAIL。
sendFailures / failedPackets 与 Raw TX failed 一样仅计 MAC 失败；提交拒绝与
等待超时单独计数。Session 新增 completed，periodic 新增 succeeded，并纠正
periodic 将 rejected/dropped 计入 failed 的旧行为；stopOnError 的停止范围保持。

统计细化：ESP-NOW 的 timeout 路径可能保留 native 责任，因此不能生造 Raw TX
的 aborted 证明。该队列使用明确的 timedOutPackets；settledPackets / settledBatches
只计完成 worker 结算的回调和提交拒绝，不计 timeout/discard 清理。共同统一的是
completed / failed 的完成语义，不强行对这些独立现场观察套用同一个恒等式。

发送相关错误的 native 数值采用 details.native；ROC、Raw TX/Action recovery
及其他非发送 API 继续使用各自文档规定的错误字段，未新增兼容别名。
WiFiTxRateError 已修正为与运行时相同的 details 层级。

平台诊断改为 txSucceeded / txFailed / txUnknown，广播结果不再包含 delivered。
ESP-NOW reliable Library 仍以自己的应用 ACK 判定成功，mock 已同步。
公共文档已加入 docs/api/docs.json，供平台 AI 和 MCP 共用。

验证记录：

- Raw TX 原生回归 43/43；Action 原生回归 8/8。
- NAN Session/USD、ESP-NOW 分配/关闭生命周期 13/13。
- 新增 ESP-NOW 完成与 NAN 完成 GC/Future finish 用例均通过；合计 66 个不同 native 用例。
- 首次 Raw TX 新契约测试在旧接口上出现预期失败，改造后通过。
- 平台诊断 6/6；ESP-NOW reliable Library 1/1；API manifest / NativeError 工具测试 6/6。
- 后端 typecheck、固件 d.ts 检查、API manifest（62 classes / 663 functions）、
  实际生成的诊断 JS 与 Library / 共用文档片段的 MQuickJS 语法检查通过。

目标构建使用独立测试 Build Context，未刷写设备。初次 C5 全特性测试镜像
超过旧测试 Context 的 3 MiB app 分区；没有为此改驱动功能或开发板分区。
复制到 build/tx-contract-contexts/c5 的测试 Context 使用 4 MiB app、8 MiB flash；
S3 Context 从当前 representative 生成器重新生成，补齐旧 Context 缺失的无线预算，
使用同样的测试分区布局。这些 Context 只用于构建验收，不能当成开发板刷机配置。

最终源码的双目标增量编译、链接、镜像及分区大小检查均通过：

| 目标 | app bytes | app SHA-256 | 打包 |
| --- | ---: | --- | --- |
| ESP32-C5 | 3732272 | `836f5f787658b57b354b3216855459f408b08a847c4dbed338186b811815932d` | 通过 |
| ESP32-S3 | 3026608 | `bf841a1703eeecda307b9e9c7e5883ed31527340077bd81b9581595978d8c437` | 通过 |

命令（在 firmware/，先加载本地 ESP-IDF export.sh）：

```sh
.venv/bin/python scripts/remote.py --python-exe /home/zach/.espressif/python_env/idf6.1_py3.14_env/bin/python --build-context "$PWD/build/tx-contract-contexts/c5" --build-dir wireless-c5-nan-usd --assume y build
.venv/bin/python scripts/remote.py --python-exe /home/zach/.espressif/python_env/idf6.1_py3.14_env/bin/python --build-context "$PWD/build/tx-contract-contexts/s3" --build-dir wireless-s3 --assume y build
```

上述统一契约实施阶段完成了源码、host 和目标打包验收，当时尚未刷写设备。
后续 Raw TX 状态细化已完成 C5 与 StickS3 刷写、各 24 项完成结果检查，以及
两个方向各 12 帧的对端收帧验证；设备文档 Artifact 也已同步。具体目标、镜像、
工作区处理和验收边界见
[Raw TX descriptor completion status](2026-09-15-raw-tx-descriptor-status.md)。
这些 Raw TX 实测不替代 ESP-NOW、Action、NAN 的 RF 验收，也不包含独立 ACK
抓包或长时间生命周期测试。平台诊断和产品 Library 的改动属于主仓库，提交边界
与本 firmware 仓库分开。
