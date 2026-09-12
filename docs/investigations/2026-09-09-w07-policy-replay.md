# W-07 内部 restart 的策略快照与阶段重放

firmware HEAD `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
前置为 [write-only 记录](2026-09-09-w07-policy-record.md)。完整 Wi-Fi API、最后实机功能验证
仍是目标；本批不代表公开 restart 完成。

## 核对结果与本批实现

现有配置事务只快照请求修改的字段，不能直接复用为完整 restart checkpoint。固定 SDK
`esp_wifi_get_protocols/get_bandwidths` 明确不返回当前关闭频段的数据；TX power、channel
等还需要明确 stopped/started 的可观察性。不能把部分 getter 输出当成所有配置已保存。
Station/AP config 的现有安全临时存储与语义比较可复用，完整捕获/重放仍需后续实现。

本批先将已经具有可信前值的四个布尔策略接入内部生命周期：

- 新增纯原生 capture，要求本代 known/accepted。uncertain、失效的历史接受值以及
  revision 不足一次完整重放均在 deinit 前拒绝，输出不留下部分快照。
- frozen intent 绑定精确 lifecycle generation/identity，同时保存原 policy revision、
  mode、选中 mask 和布尔值。显式 false 也必须重放；未配置值不猜 SDK 默认值。
- SDK 要求 11b 在 start 前、dynamic CS 在 start 后；coex power 是 init 后设置。
  前两项 SDK 约束来自固定 `esp_wifi.h:1475` 与 `:1646`，coex gate 来自 `:1739`。
- 内部 restart 在 shutdown 前捕获，init/mode 后重放 11b/coex；正常 resume 的
  staged lease 在 start 与动态 CS 重放都成功后才交还调用方。
- SDK 失败保留原始错误、lifecycle token 和原 frozen intent。不能从失败后的
  unknown 记录重新生成“成功”的快照。前值接口在目标 mode 中被禁用时拒绝。
- 原生 replay cursor 只在接受后前移，同代再次调用跳过已成功步骤。若 restart 重试
  实际重建了 driver，新的 generation 必须重新写入所有设置，不能沿用旧完成位。
- 显式清理成功或完整 owner handoff 后才清除 frozen intent；失败的清理保留它。
  非空重放要求物理新 generation，空策略的冷初始化无需制造一次 generation 变化。

准备失败记录 `radio.configuration.stage = restart-policy-snapshot`，不制造 SDK mutation；
重放失败保留 `restart-policy-pre-start` / `restart-policy-post-start` fault 和 SDK error。
这仅恢复布尔策略；不保证 runtime restart、普通 deinit 或完整 driver 配置恢复。

## 证据与缺口

C5 immutable Context `build/wireless-contexts/c5`（8 MB/no PSRAM）生产构建通过，日志
`build/w07-policy-replay-final-c5-build.txt`。链接检查显示 policy replay、post-start
协调器和 32-byte frozen state 已进入当前 ELF；policy capture 与内部 restart 入口
在生产对象中编译，但因尚无公开调用方而被 section GC 移除。不能称公开 API 已交付。
最终 hash/静态存储/链接边界见 `build/w07-policy-replay-evidence.json`。

`test_wifi_policy_replay.py` 调用真实 capture/replay、Radio exact-token helper、
finish_lifecycle 和 resume 实现；SDK/driver 物理边界注入，覆盖阶段顺序、失败后缀、
原 frozen 值、显式 false、generation 重建、revision 空间、清理失败和 post-start
失败时禁止 owner 发布。该 fixture 不替代完整真实 SDK restart/event/RF 测试。
本批只执行 Python AST 检查，没有导入、编译或执行该 fixture。

Host/Python/VM/GC/OOM/竞争、C3/S3/feature-disabled、coex-enabled SDK 构建、硬件功能、
RF 与共存均 **not-run**。本批没有刷写、串口操作、workspace 擦除、前端构建、提交、
推送或更新根 gitlink。长时间 soak 继续留到 BLE API 完成后。

剩余重点：完整 config/credential/PHY/band/MAC/channel/power/interval/rate checkpoint，
重放后读回与安全要求保持，runtime helper 准备与失败收尾，公开 restart/Future/timeout
绑定及其集中测试。其余 Wi-Fi 高级模块与最终实机验证也没有被缩减。
