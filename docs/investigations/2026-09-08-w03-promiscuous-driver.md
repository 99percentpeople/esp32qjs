# W-03：promiscuous driver 事务、共享 enable 与 CSI 清理

## 已接入的生产路径

新增 `wifi_common/esp32_mquickjs_wifi_promiscuous_driver.c`，集中持有 promiscuous
enable/filter/control-filter/callback 的所有 SDK 写入。现有 Radio acquire/release
已调用它，因此 CSI 的现有 promiscuous source 使用这一生产事务路径。

Radio 的 enable claim 从单 owner 改成每个精确 Radio lease 最多一个 claim；
总量由现有 16 个存活 Radio lease 上限约束。第二个 enable-only owner 不重复调用
SDK enable，释放其中一个也不关闭其他 owner；最后一个释放才关闭和恢复设置。
CSI 自己仍只有既有一个 session/pool，固定信道政策也尚未扩展为多 owner。

driver 的 raw receive 分支和 boot-owned callback 已实现；该 callback 调用真实
registry/target/parser/filter，使用 esp_timer 的单调 callback-time。当前 CSI
enable-only 需求不安装这个 RX callback，不改其原有 filter。registry 需求并集
转换与绑定 Radio token、Monitor 的 reserve/activate/close 复合事务仍待接入。
没有注册 `wifi.monitor` 或提升稳定等级。

## driver 事务与失败后缀

固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643` 的 esp_wifi.h 提供 enable、
packet filter、control filter getter。首次 claim 先读取这些原值；若原本已处于
promiscuous 模式，拒绝接管。driver init 是框架所有，RX callback 初始值按唯一
写入边界为 NULL；SDK 没有 callback getter，不能对外部写入者声称自动恢复。

- 启用顺序为 control filter、packet filter、callback、enable；不变项跳过。
  最后关闭的顺序相反，先 disable，再 callback 注销与 filter 恢复。
- 每个 setter 调用前记录回滚步骤，错误返回也按可能已修改原生状态处理。
  enable/filter setter 后调用 getter 读回验证。callback 仅以 SDK 成功注册契约
  为依据，不把它当作未来所有旧 callback 的终止屏障。
- 前向失败立即逆序回滚到该事务之前的已知状态，而非总是恢复全局默认值。
  已有其他 owner 时，失败更新回到原先共享配置。
- 回滚失败保留 claim、原始错误、独立 cleanup stage/error 和尚未完成的步骤。
  recover 仅重试未完成后缀；已恢复的 prefix 不重复写入。pending 期间拒绝新 apply。
- 所有操作由 Radio mutation mutex 串行化；SDK 调用不在 port critical section
  中。handler context 是 boot registry，不保留某个 JS session 指针。

这些是已编码的 driver 事务，真实 SDK 错误/回调时序与共存尚待阶段测试。

## 本批检查发现的两个资源边界

1. 原 promiscuous token 只保存 Radio generation/client/lease identity。同一个
   Radio lease 释放后重新申请，旧 token 可满足新 claim 的匹配条件。现改为独立
   boot 单调 acquisition identity，存入存活 Radio lease；每次重新申请均不同，
   UINT32_MAX 之后明确拒绝，不能通过 runtime restart 或最后 owner 关闭回绕。
   完整 Radio release 自动释放的是表内当前 claim，旧复制 token 不影响新 claim。
2. 原 CSI finish_stop 在 promiscuous release 保留 acquired=true 时仍释放 channel
   并写 STOPPED；finish_close 先写 CLOSED/释放 queue，再尝试 Radio release。
   现保留未完成的 Radio/promiscuous/channel/queue/control，记录 FAULTED 和原生
   cleanup 原因，释放成功后才进入 STOPPED/CLOSED。初次 start 失败但 lifecycle
   仍为 STOPPED 的 retained claim 也进入清理，不被幂等判断跳过。

以上来自当前生产源条件核对。已新增回归源码；按用户将测试后置的要求，本批
没有执行失败复现或修复后运行测试，不把源代码判断写成设备复现结果。

acquire 返回错误时，如果 driver rollback 尚未完成，out token 仍可能 acquired。
CSI 调用方此时保留 channel；后续 stop/reaper 重试。Public Future/JS roots 与
native retained storage 的既有边界保持，失败不会转移其他 owner 的计数。

## 公共诊断与契约

`wifi.status().radio` 新增：

- `promiscuousOwners`：存活 claim 数，包含失败清理保留的 claim。
- `promiscuousIdentityExhausted`：boot acquisition identity 已耗尽；设备重启才能
  建立新的空间，runtime restart 不能清零。

原始前向失败进入 faultStage/faultError；回滚/关闭失败进入独立 cleanupStage/
cleanupError，不能将回滚错误冒充原始失败。正式 v1 源类型增加实际 driver stage
联合并同步 manifest/API 文档。CSI close 在清理未完成时返回 false 并保留 reaper
路径，不宣称 raw Monitor 或多 CSI session 已可用。

## 测试源码（not-run）

- Host C `test_wifi_radio.c` 直接包含生产 Radio 与 driver 事务。新增两个 owner
  交错释放、同 Radio 重新申请后旧 token、完整 Radio release、失败 acquire 保留
  token、identity 耗尽及原始/回滚阶段分离用例。
- `test_wifi_promiscuous_driver.py` 使用真实 driver/registry/target/parser/filter；
  SDK 边界逐第 N 次调用失败，setter 即使失败也会变更 fixture 状态。覆盖全部
  初始快照/写入/读回位置、共享更新回滚、双重失败后只重试 suffix、稳定 callback
  的实际分发、raw 关闭后 CSI enable 保留及原始 filters 恢复。
- `test_wifi_csi_radio_cleanup.py` 提取实际 begin_stop/finish_stop/finish_close 与
  错误记录 helper，注入 Radio release 未完成；验证 channel/queue/control 保留、
  重试不重复 CSI disable/unregister，以及初始 STOPPED 状态的 retained claim。
- 无线 status 移动 GC/第 N 次分配失败 fixture 同步新的字段，待集中执行。

没有独立测试状态机代替生产实现；但上述 Host C/Python 都未编译/运行。SDK
boundary fixture 不证明真实 driver/Host/RF 调度。

## 编译、静态检查与资源证据

最终 C5 immutable Context 构建通过：
`build/w03-promiscuous-driver-diagnostics-c5-build.txt`，app `0x283d00`、余量 16%。
nm 已确认实际 ELF 链入 driver apply/recover、dispatch、target adapter 和 filter。
这证明实际 firmware 链接，仍不是已刷写或运行证明。

与上一批 app `0x2824c0` 相比增加 0x1840 字节。nm 当前显示新链接的 boot registry
为 116 字节、driver 事务状态 52 字节；Radio 每个存活 lease 另有一个 u32 claim
identity 和一个 boot counter。没有新增常驻采集 pool。既有 SRAM 优化代码保留；
这不是 internal/PSRAM free/largest-block 的实机对比，后者仍待统一验收。

中间 `...final-c5-build.txt` 因新增 JS_NewBool 调用误传 ctx 编译失败；已按 vendored
MQuickJS 的单参数签名修正，之后的 verified/diagnostics 构建成功。保留失败日志，
不通过修改 SDK 或放宽告警修复。

MQuickJS 59 sources / 48 snippets、manifest 44 classes / 398 functions、feature
文档 27 项、config schema 35 STA / 21 AP（live SDK header）、recorded map、
TypeScript strict declaration、3 份 Python AST 与 whitespace 检查通过。详细 hash
见 `build/w03-promiscuous-driver-evidence.json`。SDK 工作区保持干净。

Host/Python、C3/S3/feature-disabled 矩阵、真实 RX/CSI 共存、实机功能与 heap 均
not-run；soak 仍待 BLE API 完成。没有刷写/串口操作/擦除 workspace/前端构建/
提交/推送/根 gitlink 更新。完整 Wi-Fi 目标继续保持未完成。
