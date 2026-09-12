# W-02：停机配置事务核心

本批 firmware HEAD `d7db8d1`，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`；
代码是独立 firmware 工作区增量。上轮连接结果已完成必要检查，本轮推进
configure/APSTA 所需的原生事务，并已替换 startAP 的配置提交生产路径。

## 固定 SDK 约束与实现

`esp_wifi_set_config` 要求目标接口启用，因此提交顺序为 storage、mode、
接口配置，而不是向禁用接口写入后再打开 mode。getter 原型不要求接口启用；
在所有写操作之前先读取当前 mode 与待改接口配置，getter 失败则不写。
原生仍处于 STOPPED；此核心不隐式断连、停止或重新启动 driver。

- 内部 `wifi_radio_configure_locked` 在 task mutex 内完成有界事务。所有配置
  暂存由一次 calloc 分配，发生在首次配置 mutation 前；所有出口 secure-zero
  后释放，包括 allocation 之后的 snapshot、write、readback、rollback 错误。
  输入只在全部提交/读回成功后更新为 SDK 接受的配置。
- `esp32_mquickjs_wifi_radio_configure_lifecycle` 要求精确 lifecycle identity/
  generation、零 lease、零 operation/wake/promiscuous、已初始化健康停机 driver；
  输入支持 mode/storage、Station/AP config 与原生纯 readback validator。完整
  字段预验证由调用方 capture 完成；validator 不执行 JS/分配/driver 操作。
  此入口在任何结果下均不释放 caller 的 lifecycle token，便于后续协调 stop/start。
- 现有 startAP 冷启动排他准入已持有精确 AP lease，故在同一 mutex 下直接使用
  locked 核心。沿用 AP auth/password/PMF/SAE/GTK/cipher 的安全 readback 校验，
  校验成功才 start。后续[生命周期交接](2026-09-08-w02-lifecycle-handoff.md)已将原生提交
  改为持有 lifecycle token，成功后才发布新 AP lease；失败保留 token/netif 供 stopAP 清理。
- native config failure 尝试将 storage 切到 RAM、启用回滚所需 mode、恢复所有
  已尝试写入的接口、读回比对，再恢复原 mode/storage。比较固定 SDK 的全部
  非 reserved 配置字段，逐字段/数组比较，不用包含 compiler padding 的 struct
  memcmp。PMF 等嵌套字段也明确比较。驱动若不能精确恢复字段即保留失败证据。
- 未确定完成的 rollback 不自动使用已销毁凭据重试。记录原始 stage/error 与
  rollbackStage/error；Radio cleanupStage 为 configuration-rollback，故障及 lifecycle reservation 保留，需要显式 shutdown
  清理。AP 路径已有 stopAP shutdown 清理入口。
- storage 选择由所有框架 set_storage 成功路径记账；固定 SDK 没有 storage getter，
  所以恢复该选择的依据为成功 setter。外部代码绕过 Radio 写配置不在本契约内。
- Flash 提交即使失败也可能已有 NVS 写入；成功 RAM rollback 不能证明 NVS 原图
  已恢复。persistentMutationPossible 单独标记，失败保留故障，不声称 runtime
  restart 能恢复持久化历史。此项仍需要后续公开 storage 契约与实机故障验收。

## 公共诊断与当前边界

`wifi.status().radio.configuration` 为 null 或最近一次原生配置事务的纯元数据：
stage/error、mutationAttempted、rollbackAttempted、rollbackComplete、
rollbackStage/rollbackError、persistentMutationPossible。无 SSID/password/H2E
等配置内容。stop/shutdown 不抹除这份诊断；下一次事务替换它。JS converter 使用
GC root。配置 complete 后的 start 失败通过已有 Radio faultStage 表示，不能把
configuration.complete 当成 AP 正常广播。

公开 configure、start 的 mode/storage options、APSTA/shared AP 尚未注册。
还需完成 capture、helper/netif/lease 交接、allowDisconnect 的 operation 隔离、
stop/start 失败语义，以及 country/protocol/bandwidth/txPower/powerSave 的复合
恢复。内部事务字段支持不能替代这些功能，完整剩余范围未缩减。

## 本批验证与阶段验收

必要 C5 immutable Build Context 编译、MQuickJS syntax、manifest/feature docs/
recorded SDK map、whitespace 检查；结果/hash 见
`build/w02-config-transaction-evidence.json`。
`build/w02-config-transaction-field-audit.json` 将 rollback 比较字段与本地固定 SDK
struct 字段逐项比对；它只证明字段覆盖，不证明运行时回滚成功。递归检查首次发现遗漏
threshold.rssi_5g_adjustment（before.json 保留失败记录），补齐后 STA 35/AP 21
个非 reserved 叶字段全部覆盖。

下列测试按用户要求集中在 Wi-Fi API 完成后，不在本批执行：

| 事项 | 状态 |
| --- | --- |
| AP 启动正常、旧配置 snapshot 不可读、逐次 allocation failure | not-run |
| storage/mode/config/readback 每步失败与失败后 stopAP 清理 | not-run |
| 每个 rollback 步骤失败，原始错误不被覆盖，禁止新 owner | not-run |
| 原生输入缓冲全出口 secure-zero、JS converter OOM/movable GC | not-run |
| stale lifecycle、owner 并发、callback barrier 与停机状态 | not-run |
| STA/AP 双接口事务与 Flash 部分写入的真实副作用 | not-run |
| Host C/Python、三目标与 feature-disabled 全矩阵 | not-run |
| 实机功能测试 | not-run，Wi-Fi API 完成后 |
| 长时间 soak | BLE API 完成后 |

本批无串口/实机/刷写、提交、推送或父仓库 gitlink 更新。
