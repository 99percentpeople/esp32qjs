# W-02：AP/STA 中央 runtime 清理与 owner 交接

基线 firmware `d7db8d1`，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批接入实际 runtime teardown，补齐配置/APSTA 的中央清理基础。公开 configure
和 APSTA 参数仍未注册；完整 Wi-Fi 目标保持不变。

## 现有顺序与需要改变的原因

原顺序先调用 AP 独立清理，再取消/排空 Station 操作和释放其 owner。AP 的独立
lifecycle 准入只传 AP owner；因此未来 APSTA 场景中存活的 Station/application
owner 会阻止它开始。之后单独清理 Station 不能代替 AP 的后续重试。这是生产
调用链对多 owner 场景的缺口，不是已经执行过的 APSTA 实机故障复现。

现有 Radio 已支持精确三 owner 准入。此次复用该入口，未放宽 ESP-NOW/CSI/
promiscuous/wake/operation 排他，也不把其他 feature 的 owner 算作 Wi-Fi 自有。

## 生产实现

- AP 增加 begin_configuration：检查本地独立清理/永久错误，再调用 Radio 精确
  三 owner begin；成功后保存 borrowed token 的 identity/generation，释放 AP
  lease。该步没有 driver stop/configure 副作用。
- borrowed token 存活期间，普通 AP cleanup/prepare/slot 不能抢走事务。AP
  retire_for_configuration 要求精确 token；netif 成功退休才清除 borrowed identity。
  超时或 detach 错误保留 identity 和存储。
- runtime teardown 仍先断开 JS/runtime consumer，并保留已有 native scan/connect
  drain。健康 AP owner 此时暂不独立释放，待原生操作和 Future 注册退出后统一
  接管 application、STA、AP；独立失败的 AP setup 仍走其自己的 token/suffix。
- 中央清理依次：释放 application/STA lease → quiesce → AP retire → Station
  helper cleanup(false) → finish_lifecycle(shutdown=true)。成功前保留同一 token。
- 清理旗标位于 resettable Station helper 之外。即使两个 helper 都已销毁，最后
  shutdown 失败也能保留事务；重试只让底层继续未完成的 native suffix。
- 新 runtime 先重试中央清理，再初始化独立 AP helper；不会因 borrowed token
  存活而先在 AP init 处提前失败，从而永远跳过中央重试。

该路径已用于现有健康 AP 的 runtime teardown；多 owner 的正式入口仍需后续
公开配置协调器。没有通过模拟 APSTA public API 把这批标为功能完成。

## 诊断与失败边界

WiFiStatus.cleanupStage 新增 configuration-admission、configuration-stop、
configuration-ap-retire、configuration-shutdown，helper 内部阶段继续沿用其已有
字段。新增 accessPointNetifCleanupError/accessPointNetifRestartRequired，用于
报告 AP 永久 detach 失败；SOFTAP-disabled 构建返回 null/false。
Common WiFiError.details.restartRequired 合并 Radio、Station 和 AP 的要求。
队列满/等待超时仍不设置永久 detach error，也不冒充设备重启要求。

AP/Station 的 native storage 必须先安全退休，不能为了让 runtime restart 返回
成功而清空 token。外部 feature 仍有 owner 时，中央准入失败并保留 Wi-Fi 资源；
后续重试依赖原 owner 的实际释放，不会替它强制关闭。

仍需完成公开 configure capture/提交协调、完整 driver 字段、stop/start 事件身份
隔离和 APSTA 对外行为。此处中央清理不证明这些功能完成，也不替代默认事件循环
与固定 SDK timer 修补的运行验收。

## 验证账本

C5 immutable Build Context 编译通过：app `0x27a3e0` bytes、分区空余 17%。
MQuickJS、manifest/feature 文档、recorded SDK map、Python 用例语法与 whitespace
结果及 hash 在 `build/w02-central-cleanup-evidence.json`。

新 `test_wifi_configuration_cleanup.py` 调用生产中央 begin/finish、AP
begin/retire/cleanup 与 idle predicate，替换 Radio/资源边界。覆盖 Future 忙、外部
owner 拒绝、精确 token、borrowed AP 阻止独立清理、stop/AP/STA/shutdown 逐步
失败，以及 helper reset 后仍保留 token、不重复释放成功前缀。
原 Station-only scan/runtime 用例同步隔离新的 coordinator 分支；AP cleanup
用例同步 borrowed token。所有这些运行与故障注入仍为 not-run。

全 Host C/Python、三 target/disabled 构建、GC/异步并发、APSTA 与 runtime restart
实机功能和等静止态内存账本，均留到 Wi-Fi API 完成后的阶段测试。长时间 soak
放到 BLE API 完成后。未刷写、串口操作、提交、推送或更新父仓库 gitlink。
