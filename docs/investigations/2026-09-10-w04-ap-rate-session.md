# W-04：AP-only 临时速率 Session 与 runtime helper

基于 firmware `d7db8d1` 工作区和固定 SDK `fff9895c82`，本批将前两批原生阶段
接入公开 `wifi.rawTx.open({interface:"access-point", rate:...})`。这是已配置、
停止、零 owner 的 AP-only 来源；APSTA 临时速率与完整故障来源仍待实现。
正式前提、副作用和操作顺序见 [Raw TX API](../api/wifi-raw-tx.md)。

## 生产接入

- 参数捕获沿用实际严格 rate parser，SoftAP 构建开放 AP rate。native Session
  再次验证 SDK config，capabilities/types 同步；没有增加第二版本或占位方法。
- Session 取得共享 arbiter grant 后，AP 打开交由新的 runtime service。原
  sessions_service 也被周期任务的后台 worker 调用，因此两者必须分开；后台
  service 只处理 lane/packet，不操作 AP helper 全局状态。同步 Future wait
  调用实际 runtime poll，能够推进 AP 打开。
- AP coordinator 先分配安全配置工作区，确认其他 runtime helper 空闲，然后
  取得原生 AP rate lifecycle、读取并验证保存配置、退休旧 Station helper、
  创建 AP netif/handlers，再调用生产 start。非零 channel 必须与保存配置一致；
  不改写 AP 配置、不隐式停机或丢弃 Station/APSTA owner。
- Session 持有原生 AP context；AP helper 仅保存 generation/identity 观察记录，
  不复制可释放的 lease、Session 指针或 JS root。所有失败返回保留真实 context、
  lease/lifecycle 和 native cleanup ref，公共 Future 结束不丢弃它们。
- 普通 close/GC/打开失败先排空 packet，再由 runtime quiesce 完成 STOP/速率
  恢复和原子 lifecycle 交接；helper 退休、finish 后才能释放 lane 和 Session。
  queue/ticket/worker/periodic 子引用仍受原关闭门槛约束。
- 等待 helper 退休时后台不反复投递无事可做的 worker，也不覆盖 runtime 的
  cleanup error/stage。Session snapshot 引用与 worker_busy 覆盖每次 helper
  调用，SDK/helper 操作期间不持 Session mutex 或 IRQ critical section。
- 普通 stopAP/configure/helper prepare 不得接管活动 Raw AP。status.accessPoint
  使用 identity-only 只读引用，Radio 重新验证精确 AP temporary-rate owner 后
  才采样，不给观察记录释放权限。

## 显式恢复与销毁

AP helper 仅为匹配 generation 的 Raw TX recovery 接受原生准入；中央恢复在
physical deinit 前退休 AP helper 并清除观察记录。原 Session 消费 nativeTerminated
后释放原 lease，再由 runtime 清空自己的 AP context。该分支不重复关闭 helper，
即使中央恢复已经重建新 helper 也不触碰它。

runtime prepare 已通过同一个 Raw TX poller 服务所有 Session；Future 取消、
未返回的 Session、JS GC、worker queue 饱和及 runtime 停止都继续保留原生清理
责任。SDK 调用不能被 Future deadline 抢占；失败 detach/未知故障来源仍可能
阻止销毁，不能声称 runtime restart 总能恢复。

## 检查与尚未验证

新增 deferred AP helper fixture 组合生产 AP coordinator 与原生 Radio phases；
SDK/helper I/O 和 Station readiness 为注入边界。新增 Session fixture 调用实际
Session/runtime/后台 service、queue、arbiter、broker，覆盖 runtime-only 打开、
失败打开的清理、取消、helper 失败重试、grant 保留和物理终止交接。实际 helper
与 native Radio 则由独立生产 fixture 覆盖，未用替代 Session 状态机证明自身。
更新捕获、AP status、中央清理/recovery fixture 的新增边界，全部仅 AST 解析，
未 import、C 编译或执行。

普通 C5 2,851,904 bytes，FTM-enabled C5 2,885,200 bytes；最终两个生产构建通过。
AP helper、新 runtime service 与三个原生阶段均已链接最终 ELF。新增只读 owner
记录 `s_ap_raw_owner` 为 8 bytes；此前跟踪的静态账本尺寸不变。原生 Session
新增 context/物理终止标记，FTM-enabled 最终 ELF 的 DWARF 显示其当前大小为
880 bytes；动态内存增长须纳入 W-09 和后续实机比较，不能以
静态尺寸不变推断总内存不变。

Manifest 51/493、features 27、STA/AP schema 35/21、严格 TypeScript、SDK map、
MQuickJS 61/56、whitespace 与 hash 见 `build/w04-ap-rate-session-evidence.json`。
Host/Python/VM、C3/S3/full gate matrix、真实 AP/DHCP/发送/RF、GC/队列/runtime
restart、heap/largest block 仍 not-run，Wi-Fi API 完成后集中验证；长 soak 留到
BLE API 完成。Candidate 不提升。没有刷写、串口、擦 workspace、提交、推送、
父 gitlink/SDK 修改或前端构建。

后续：[停止态 APSTA 临时速率与 Station 清理保留](2026-09-10-w04-apsta-rate.md)已接入；上述 AP-only 边界和构建尺寸为本批历史证据，运行验收仍未执行。
