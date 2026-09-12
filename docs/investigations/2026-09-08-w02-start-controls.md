# W-02：启动后 TX power 提交与 owner 发布

基线 firmware `d7db8d1`，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
上一批[停机控制事务](2026-09-08-w02-config-controls.md)已加入国家/PHY/省电；
本批补 configure 所需的启动后 TX power。仍是独立 firmware 工作区增量。

## 实施与状态边界

固定 SDK `esp_wifi.h` 要求 TX power setter 和 getter 都在 Wi-Fi started 后
调用，因此不能获取停机前值，也不能用 country.max_tx_power 代替 setter。

- 共用配置执行器接受独立 start_controls 原生输入，纯检查要求 start=true、
  TX power 为整数 8–80 个 0.25 dBm 单位，保持当前公共 setTxPower 的 2–20 dBm
  范围。start=false 携带 TX power 在任何 helper 停机/退休前拒绝，不隐式启动。
- 精确 lifecycle resume 在检查 token、零既存 owner/operation/wake/promiscuous、
  输出槽和 identity 容量后，暂存 owner，等 START/events/fence 与 mode 读回
  成功，再读取启动后的 power 快照、设置和读回。其他 mutation 始终受同一
  mutex/token 排除；driver 调用不在临界区中，输入不保留到返回之后。
- SDK 可量化或降低功率，读回只接受合法范围且不高于请求值。实际值通过已有
  status.radio.maxTxPowerDbm 查询。本批不拷贝 SDK 的量化表作为假想硬件结果。
- 只有 TX power 成功后才发布新 owner 并退休 token。setter 即使返回错误也按
  可能部分修改处理；尝试恢复启动后的功率快照，读回必须精确一致。
- 任何 TX power 失败均保留原生故障、started/stop-required 和 lifecycle，清除
  所有暂存 owner；回滚失败额外保留 cleanupStage=activation-rollback。中央
  显式清理执行 stop/events/helper retirement/shutdown。不会因为功率恢复成功
  就把 failed start/configure 作为可用连接交回，也不通过释放 token 放行新 owner。

这是“启动后功率配置”，不保证原生启动阶段按新功率广播。START 发生后已有的
RF/广播副作用无法撤销；scalar rollback 不恢复原来整个配置或生命周期。该
约束须由未来公开 configure 的完整结果/错误契约继续说明。

## 诊断与公共契约

新增实际 status converter 字段 `wifi.status().radio.activation`，复用
WiFiConfigurationStatus 的纯元数据，无凭据。记录最近一次执行到 TX-power
步骤的尝试，未执行时 null；不携带 start_controls 的启动及后续 stop/shutdown
保留已有记录。停机配置记录 `configuration` 独立保存，不被 activation 覆盖。

stage 为 tx-power-snapshot、tx-power-config、tx-power-readback 或 complete；
error 保留原始错误。rollbackStage 为 rollback-tx-power 或
rollback-tx-power-readback；rollbackComplete 仅证明启动后的功率前值恢复。
此次操作不直接写 NVS，persistentMutationPossible 为 false；停机阶段的国家/
Flash 副作用仍在 configuration 中。faultStage/cleanupStage 源类型同步增加，
并补齐上一批国家/PHY/省电已存在的 faultStage 字面量。

现有 startAP 与内部 restart 传空 start_controls；尚无公共 JS caller 设置该
输入。公开 configure、其捕获/状态返回、APSTA/allowDisconnect 仍待完成。
band mode 的 started-state 控制属于后续 driver API，不因本批 TX power 而完成。

## 检查与尚未运行的验收

C5 immutable Build Context 编译通过，镜像 `0x27d7d0`，应用分区剩余约 17%。
MQuickJS/manifest/features/schema/recorded map/whitespace 的最终证据及 source
hash 见 `build/w02-start-controls-evidence.json`。没有实机内存比较结果。

- 新增 Host C activation-controls case，直接编译完整生产 Radio，以 SDK 边界
  注入成功、snapshot/write/readback 失败、超请求读回、rollback write/readback
  失败/不一致、无效 snapshot 共九种场景。覆盖参数在 start/identity 消耗前
  拒绝、失败后不发布 owner、阻止外部 owner、功率恢复与显式 shutdown。
- 共用配置执行器测试同步新参数，补 start_controls 纯验证早于停机。
- 新增 Python/MQuickJS 用例直接调用生产 metadata converter，注入每次分配/
  setter 失败及移动 GC，核对异常、roots、原始错误与无凭据字段。源码 AST 已检查。

上述 Host C/Python 用例均 **not-run**；等待 Wi-Fi API 完成后集中阶段测试。
C3/S3 与 disabled 编译矩阵、真实 SDK START/功率规范化、队列饱和/关闭竞争、
实机功能回归也尚未执行；长时间 soak 等 BLE API 完成。未操作串口、刷写、
修改设备 workspace、提交、推送或更新父仓库 gitlink。
