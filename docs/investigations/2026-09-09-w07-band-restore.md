# W-07 原频段模式与 Station 信道恢复

firmware `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接 [单频捕获](2026-09-09-w07-band-capture.md)，公开 restart 仍未注册。

## 恢复顺序与证据

固定 SDK `esp_wifi.h` 规定 band mode 与 channel setter 均要求 START；set_channel
的值不保证跨 STOP 保留，5 GHz secondary 由 SDK 自动决定。因此不能在最终 START
前调用 set_channel，也不能用准备阶段的信道读回代替最终接口的实际状态。

原 AUTO 准备逻辑改为共用的 band select helper。完整 Station/AP PHY 在 AUTO 下写入
并读回后，重放流程选择原 band mode：若已经一致则不额外启动；否则暂时设置为
Station、START/events、写入原 band mode、读回、等待 SDK 内部 STOP/START/marker、
完成外层 STOP，再恢复停机接口 mode。准备期间仅用 RAM storage，不启动 AP、不
发布 owner。不同物理重试继续使用最初冻结的 checkpoint；故障不会把临时设置重新
捕获成原值，也不绕过已有 shutdown/deinit 边界重放不确定的原生写入。

该步骤位于完整 PHY 读回之后、省电/最终接口模式/发送速率/interval 重放之前。
单频最终 PHY getter 只能核对可见频段；两频段的完整读回发生在切回单频之前。
没有声称单频最终读回能重新观察隐藏频段，切换后隐藏值与 RF 行为仍需阶段测试。

最终 Station START 后、owner 交接前，依次 set_channel → band mode/band/current/
home channel 精确读回 → TX power 写入及精确读回。最终整体校验再次检查信道与
可见 PHY。沿用一个有界 post-start cursor，不新增分配或常驻状态。AUTO 原本位于
5 GHz 时同样按原信道恢复实际 band，不默认为 2.4 GHz。

SDK 写入失败或读回不匹配时保留原 checkpoint/token/阶段错误，owner 不交接；已
接受的写入与未完成读回分别记账。故障后的新物理代际重新执行恢复，未对结果不确定
的同代写入作自动重试。5 GHz secondary 仍要求最终值匹配冻结观察，没有接受近似值。

## 尚未完成

AP/APSTA 的最终启动信道需独立的 activation 协调：原 AP 配置中的信道可能不同于
此前跟随 Station 的实际 home channel，而且 AP 开始广播后可发生原生关联。当前
post-start 对 AP/APSTA 只读并要求原值匹配，不调用 set_channel 发起 CSA，也不以
尚未发布 JS owner 作为无原生客户端的证明。该启动协调、公开 restart/helper/
scan/connect 生命周期、其他配置项及高级 Wi-Fi 模块仍在完整剩余清单内。

## 验证范围

C5 immutable Context `build/wireless-contexts/c5`（8 MB/no PSRAM）生产构建 exit 0：
`build/w07-band-restore-c5-build.txt`。binary 2,763,504 bytes，比前批增加 256；
checkpoint 仍 688 bytes，常驻 restart control 仍 40 bytes，记录的 Radio/ESP-NOW/
Raw TX/policy/interval/rate 静态大小不变。没有 live heap 或运行回收证据。

band select/replay/内部 restart 在生产对象中编译，当前 ELF 未引用这些入口；
post-start channel/power helper 与最终检查已链接。没有新增 callable API。

deferred checkpoint fixture 扩展为：原 2.4/5 GHz 单频成功恢复、AUTO 原 5 GHz 当前
信道恢复、频段恢复每次 SDK 调用失败后保留原快照/新代际重试、信道每次调用失败、
成功返回但读回不匹配、最终 home 漂移拒绝、AP/APSTA 信道不匹配时拒绝并且不写
信道。继续提取实际生产函数，仅注入 SDK/锁/heap/事件等待边界。本批仅 AST 解析，
未导入、编译或执行 fixture。

manifest 49 classes/464 functions、feature 27、STA/AP schema 35/21（live SDK header）、
MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map、whitespace 检查通过。
记录见 `build/w07-band-restore-evidence.json`。

Host/Python/VM/GC/OOM/故障注入、C3/S3/feature-disabled、完整 restart、RF/共存与实机
均 not-run。所有 Wi-Fi API 完成后统一阶段及实机功能测试；长 soak 留到 BLE API
完成后。未刷写、串口操作、擦除 workspace、构建前端、提交、推送或更新根 gitlink。
