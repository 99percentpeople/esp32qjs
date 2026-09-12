# W-02：公开 wifi.configure 与当前执行结果

基线 firmware `d7db8d1`，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批接通[缺省/授权与捕获执行交接](2026-09-08-w02-config-selection.md)，保持原
W-01～W-12 范围，尚未完成全部 Wi-Fi API 或阶段验收。

## 已注册的行为

新增 `wifi.configure(options): WiFiStatus`，恰好一个参数；同步执行现有 capture、
原子 selection admission、三 helper owner 交接、配置事务与启动屏障。支持显式
Station/AP/APSTA、RAM/Flash、start、allowDisconnect、Station/AP raw config、
国家、各接口协议/带宽、省电与启动后 TX power。Wi-Fi feature 关闭时不注册，
SoftAP 关闭时拒绝 AP/APSTA。`capabilities.features.configure` 为 true；apsta 随
SoftAP gate，modes 增加 station+softAP。这是 API 发现，不提升 RF/稳定等级。

新 `WiFiConfigureOptions`、两个 raw config 与 control object 类型进入正式唯一
v1，取值严格对应实际 parser。Station minimumAuthMode 仅为现有
WiFiConnectAuthMode；完整 SDK inventory 的 Enterprise/DPP 等额外认证值仍属
contract-pending，没有借本注册进入正式类型。Schema 生成文档明确区分已注册
子集和后续认证提案；实际 generated key 清单继续供 raw capture 使用。

每个给出的接口对象按构造默认值完整替换，不是 patch。Station 仍不自动连接；
AP start 要求完整 accessPoint 输入，不借读秘密补全。支持 bytes SSID、AP TU
beacon、PMF required 与既有安全/PHY gate。缺省与运行 AP 必须显式授权的规则
沿用上一批。raw Station 的 5 GHz/channel 参数仍是后续扫描 hint，不强制共用信道。

`WiFiStatus.radio.storage` 新增 ram/flash/null；模式继续用既有 status 表示
station/softAP/station+softAP/off。整个 configure 返回 `WiFiStatus`，不导出原始
配置或密码。调用后的 status 是状态观察；configuration/activation 是已有最近
原生操作记录，不隐含全部输入的可公开读回。

## 分配、错误和副作用

公开 binding 用一个有界 native capture 分配，先完成全部 JS 输入复制和静态
验证，再进入执行器。所有出口擦除并释放 capture，然后才创建执行错误或成功
返回值。新类型 execution 只记录本次 stage/admission/stop/config/resume 是否
到达，不包含 JS/native secret 指针，也不驻留为新 boot-global 状态。

- 参数/JS 分配失败：保持原 TypeError/RangeError/内存异常，没有 owner/driver 变更。
- 原生失败：WIFI_CONFIG_FAILED 或 WIFI_CONFIG_UNSUPPORTED，operation 始终为
  wifi.configure。details 含本次 stage、可用时的 option、原生错误、admission/
  stop/cleanup/restart 状态，以及本次确实到达的配置或 TX-power 失败记录。
- 配置成功后的结果 OOM：保留已生效的 driver/owner 状态。调用者先检查 status，
  不自动重复 stop/configure/start，不因为返回值转换失败回滚已接受配置。

错误的 configuration 仅在本次调用了原生配置事务后包含；activation 仅在本次
resume 到达并发生 tx-power-* 故障时包含。START 提前失败不得把以前的 activation
历史误写成本次失败。stopAttempted 表示调用了 quiesce，不断言先前 driver 正在
运行。持久副作用仍由 native configuration.persistentMutationPossible 表达，
包括 RAM storage 下的 country setter；runtime 回滚不声称重建旧连接或 NVS。

将 Station/AP 共享 parser 增加 operation-aware 的内部入口，原 connect/startAP
继续使用原入口。configure 的 build-gate 错误不再误标成 wifi.connect/startAP。
这些捕获错误使用无 Radio 查询的 error converter；避免已有通用错误构造器在
capture 拒绝时查询运行 driver。静态复核也补齐 Station parser 的 SSID/password
C-string 临时缓冲擦除，成功和异常路径都执行；无动态复现/运行通过声明。

## 测试与验证边界

新增 `test_wifi_public_configure.py`：实际公开 binding、配置错误/metadata converter、
共享 Station unsupported helper 与原生 Error 构造器，在实际 MQuickJS 中注入
第 N 次分配/属性写入失败和移动 GC。capture/apply/status 明确为替换边界，其完整
生产实现继续由既有各 fixture 覆盖。检查 native capture 清零后释放、原始捕获
异常、每次至多一次 apply、成功后结果 OOM、不带旧 transaction 记录、秘密字段
不进入配置错误。这个 fixture 不冒充完整 native/RF 状态机。

同步改动旧 raw/Station/AP/executor/status fixture 的生产入口签名；仅 AST 检查，
**全部新增和更新测试 not-run**。本阶段不执行 Host C/Python、运行时竞争或实机
测试，遵守“Wi-Fi API 全部完成后集中测试”。必要 immutable C5 编译、MQuickJS
语法、manifest/features、raw schema/live SDK、recorded map、whitespace 的日志
和 hash 记录于 `build/w02-public-configure-evidence.json`。最终 C5 app 为 0x27f880，
分区余量 17%；map 中 capture/apply/js_wifi_configure 均有实际 text 地址，已进入
固件链接，不再仅是可被裁剪的内部 helper。语法 59 sources / 48 snippets、manifest
44 classes / 398 functions、feature 文档 27 项通过；这些不替代运行或 RF 证据。

还需要完整 start/stop options、共享 AP、保留 Station 的 stopAP、AP 状态及客户端
IP、deauthClient 竞争边界、高级认证与后续 Monitor/Raw TX/CSI/driver/W-08/W-09
等工作。APSTA 可启动不等于上述共享生命周期已完成。C3/S3/disabled 矩阵和实机
功能尚未运行；长时间 soak 留到 BLE API 完成。未刷写、串口操作、擦除 workspace、
提交、推送、构建前端或更新父仓库 gitlink。
