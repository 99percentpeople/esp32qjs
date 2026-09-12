# W-02：配置后的生命周期与 owner 交接

firmware HEAD `d7db8d1`，固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批是工作区增量，继续原生配置事务之后的启动/停机交接；未缩减 Wi-Fi API 总目标。

## 已实现的生产路径

- begin_lifecycle 的两个 owner 槽改为三个明确角色：Application、Station、AP。
  每个 acquired token 都需 generation/identity/client 精确匹配存活 registry；
  角色互换、遗漏任一存活 owner 或存在其他 feature owner 均拒绝，且不调用 driver。
  caller 提供的 helper 必须已经结束操作，原生 operation/wake/promiscuous 也会阻止 begin。
- 新 resume_lifecycle 要求健康已初始化 STOPPED driver、精确 lifecycle token、
  零其他 lease/operation/wake/promiscuous；mode 必须与实际配置相同。它不执行
  init/deinit 或修改 storage，避免将已提交的配置覆盖回默认 RAM/Station。
- start=true 时，输出槽需为空且互不别名，角色组合需覆盖目标 mode。预检整个组合
  所需的单调 identity 余量，再将 lease 暂存在栈中；仅在 SDK start 与 mode readback
  成功后交还输出并解除 lifecycle 排他。它没有新增队列/长期堆池或 identity 复用。
- start/readback 失败会释放内部暂存 lease，caller 得不到可用 owner；token 继续
  保留排他。start 已产生副作用时 stop_required/fault 保留供显式 shutdown。
  `resume-mode-readback` 是原生启动后 mode 无法确认的诊断阶段。
- start=false 需不提供任何 owner 输出；确认停机 mode 后仅结束排他，保留已初始化
  配置。此入口不等于公开 configure({start:false})，后者仍需 JS 协调器。
- startAP 已真实接入：netif 准备期间的 AP lease 转为 pending lifecycle token，
  随后 init/configure/resume 都在该排他内完成。成功发布新 AP lease；失败保留
  token/netif。stopAP 可清理准备阶段的旧 lease，也可直接使用原生失败留下的 token，
  不再尝试重复 begin。runtime retirement 沿用同一清理路径。
- 内部 restart 也使用 resume 的发布边界，先在原 lifecycle 排他内 shutdown/init/
  设置目标 mode，再启动并发布 Application owner。init 的不确定所有权故障仍按
  既有 reboot-required 政策保留；没有把 restart 当成恢复所有失败的保证。

`status().radio.lifecycleActive` 反映 pending token。配置 transaction complete
只证明配置前缀成功；后面的 native start/mode 读回失败仍独立保留 fault 信息。
本批没有开放共享 AP 外部准入，已有公开 startAP 仍冷启动独占。

源码复核还确认 wifi_ap_error 原先只根据 netif detach 错误设置 restartRequired，
会漏掉 Radio 初始化不确定所有权要求设备重启的情况。已合并 Radio 标志，原始
错误码/阶段保持；新增真实 MQuickJS converter 的四种标志组合及 GC/OOM 用例。
这些用例同样未运行，当前根因依据为生产源码，不宣称已取得运行复现。

## 仍需完成的协调层

公开 configure、APSTA、完整 start/stop/options 尚未完成。下一步需要在统一协调层
准备/接管 Station 与 AP helper/netif，处理 allowDisconnect、停止与事件 drain、
整体配置 capture/readback/恢复，并决定成功或失败后各资源 owner 的归属。
country/protocol/bandwidth/txPower/powerSave 复合事务仍需接入。新原生三 owner
入口不是公开 APSTA 能力证明，不提升 feature 稳定等级。

## 验证与未执行项

必要 C5 immutable Build Context 编译、MQuickJS syntax、manifest/feature docs/
recorded SDK map、whitespace 检查结果及源文件/产物 hash 见
`build/w02-lifecycle-handoff-evidence.json`。

新增四个 Host C case，均调用生产 Radio：lifecycle-handoff、handoff-capacity、
handoff-readback、lifecycle-owner-roles。它们覆盖 STA/Application 发布、空输出
停机交接、别名/stale token、identity 临界值、启动后读回失败不发布 owner、三个
角色精确准入；同时调整已有 AP native/adapter 清理用例到 pending token 契约。
按用户要求本批只编写，尚未运行。owner-roles 通过生产 registry helper 建立入参，
不伪装 Host SDK 已运行 APSTA；也不以该用例代替实际 APSTA start 验证。

| 待执行验证 | 状态 |
| --- | --- |
| 新增 Host C case、AP 错误标志 VM 用例与已有 AP/Python 回归 | not-run |
| start 暂停时并发 acquire/release、输出发布与 callback 顺序 | not-run |
| 三 owner APSTA 真正启动及逐步失败/netif 交接 | not-run |
| AP config/start/readback 失败后的 stopAP、runtime retirement | not-run |
| 全量三目标与 feature-disabled 构建 | not-run |
| 实机功能回归与等静止态内存账本 | not-run，Wi-Fi API 完成后 |
| 长时间 soak | BLE API 完成后 |

无实机/串口/刷写、提交、推送或父仓库 gitlink 更新。
