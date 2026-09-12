# W-04：公开 Raw TX 恢复与临时速率前值重放

继续基于 firmware `d7db8d1` 的未提交 Wi-Fi 工作区，固定 SDK 为
`fff9895c82d744c7237be8847347bdd1b07c6643`。本批将上一批原生阶段接入
实际共享协调器，新增 Candidate `wifi.rawTx.recover(options)`；完整故障来源
和运行验收仍未完成。正式参数、副作用与错误见 [Raw TX API](../api/wifi-raw-tx.md)。

## 实现边界

- `wifi.status().rawTx` 的 `operationIdentity` 与新增 `radioGeneration` 来自
  同一 broker snapshot。调用方分别传入 sequence/radioGeneration；不使用
  Session queue sequence、periodic ticket 或其他 lane token。
- 共享 Future 在准入前捕获严格 options。Radio 将所选 identity 与当前 broker
  完整 token 比对，再在 mutation mutex 下重新验证原 lease 与恢复条件。
  调用方不会获得原 mutable lease 的释放权限。
- 原生 checkpoint 只接纳精确 Raw TX lifecycle、可信健康 STARTED STA/AP/APSTA
  来源。使用已有 policy/config/PHY/home channel 快照路径，无临时切频或启动。
- 新生产 helper `esp32_mquickjs_wifi_tx_rate_recovery_capture` 验证同代可信速率
  记录、精确临时 owner/write identity、有效前值与剩余 identity 容量。在局部
  snapshot 中将借用接口的临时速率替换为 lease 保存的永久前值，其他接口保持
  原记录。捕获不写 SDK，不修改现存 ledger 或临时 lease。
- 共享 Action/Raw TX/FTM 路由沿用现有恢复 Future lane、runtime helper 和中央
  清理。STOP、callback/SDK task fence、deinit 后，原 Future/Session worker
  消费物理终止并退休 token/lease；随后才重建并重放冻结的配置和永久速率。
- 已完成的发送结果不改写，不伪造 TX success。受影响的排队/周期工作关闭，
  不重新发送、重启周期作业、自动连接 Station 或恢复应用会话。
- 超时/取消终止本次重放意图，中央清理保留精确 lifecycle 与原生 storage。
  runtime 销毁也推进已准入恢复和原 owner 服务；所有 drain gate 通过前不释放
  runtime。成功的清理后缀不重复执行。
- JS 注册、Future driver 注册、源类型和唯一 v1 manifest 已同步；FTM 关闭
  时 Raw TX recover 仍实际注册和链接。capabilities 表示目标绑定支持，不表示
  当前操作满足准入，也不表示硬件验收通过。

## 延后用例

新增 `test_wifi_raw_tx_recovery_checkpoint.py` 使用生产速率与配置捕获/重放；
注入 owner 准入和 SDK/storage 边界，覆盖临时前值、其他接口、重复捕获、重复
重放、失效 identity/generation/interface/前值、未知记录、容量耗尽与 OOM。
物理终止生产者由原生 recovery fixture 覆盖，未构造替代恢复状态机。

现有生产 Radio routing、runtime、Future、capture GC、teardown fixtures 扩展
为 Action/Raw TX/FTM 三类：精确错误方法/错误码、旧 token/错误 kind、阶段取消
与原 owner 排空。11 个相关 Python 文件仅 AST 解析，未 import、编译或执行。

## 构建与未验收项

- 既有 immutable C5 Context：FTM enabled 2,880,400 bytes，FTM disabled
  2,847,104 bytes；两次生产构建通过，日志无 warning/error。
- 最终 ELF 包含公开 JS 入口、注册、原生恢复和前值捕获；上一批跟踪的静态
  账本尺寸不变（含 `s_raw_tx_recovery` 24 bytes）。新增 const Future driver
  为 36 bytes。这些结果不证明实机堆/PSRAM/largest block 或 GC 稳定性。
- Manifest 51 classes / 493 functions、features 27、STA/AP schema 35/21、
  严格 TypeScript、SDK map、MQuickJS 61 sources / 56 snippets 与 whitespace
  检查通过；对象、源码 hash 和构建日志见
  `build/w04-raw-recovery-public-evidence.json`。
- 已故障/不可读来源、失败临时速率恢复来源、AP 临时 rate 仍是实现缺口；未将
  这些目标改为永久 unsupported。共享全局预算、其他 W-01～W-12 缺口继续保留。
- Host/Python/VM、C3/S3/full gate matrix、实机/RF、GC/队列/runtime restart 与
  内存比较均 `not-run`，Wi-Fi API 完成后集中执行；长 soak 延后至 BLE API 完成。
- 未 flash、serial、erase、提交、推送、更新父 gitlink、修改 SDK 或构建前端。
