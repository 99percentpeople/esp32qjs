# W-08 TWT 共享信息定时器关闭授权与 closeAll

本批完成多 Agreement 关闭协调及公开 `wifi.twt.closeAll(options?)`。仍未
实现完整 TWT STOP/deinit/replay 物理故障恢复，也未新增 all-flow suspend/
resume；后续 Wi-Fi 功能与集中运行/实机验收继续保留。

## 源代码确认的依赖阻塞

individual teardown 共用单发送 scope，直到本 owner 联合退休完成才释放。
旧信息 timer cleanup 拒绝删除还包含其他 retained flow 的共享 timer。
所有 Agreement 同时请求 close 时，第一条 teardown 即使已完成，其联合
退休仍等待共享 timer；下一条 teardown 又需要第一条释放 scope，形成依赖环。
此前只有真实连接关闭或其他原生变化可能打破它。这是代码路径确认，尚未
执行动态复现，不能报告竞争测试已经通过。

## 实现及公共契约

结果表复用 uint16 flags 增加永久 CLOSE_REQUESTED，按不复用的 request
身份记录 owner 关闭意愿；不伪造 NATIVE_CLOSED、CANCELLED 或 teardown
成功，不覆盖原始错误。标记变化撤销旧 event fence；重复请求不增加 revision。
Radio 单 owner close、GC/runtime close 及 worker 在提交 identity 交接后的
重试均发布该意愿。未知 request 不获得关闭授权。

信息 timer 清理先检查全部相关 entry。其余包含的 flow 若仍被保留，必须
逐一有精确关闭意愿才可删除共享 timer；任何未授权 flow 或 busy native
处理阻止整轮清理。查询结果账本在 timer 锁外完成，不引入嵌套临界区。
调用仍限 native task，timer callback 只投递数字 identity；正常 done 先撤销
权限，stop/delete 失败保留 handle/payload 并重试后缀。其后的 TX/timer/
native/event 屏障与精确释放均保留。

closeAll 在 Future start 用一个 Radio 锁选择全部当前 individual/broadcast
owner 并标记 closing。最多保存 8+31 个精确 token，包含 pending、失败或
原 Future 已结束的 owner；不包含 probe，也不包含选择后新准入的 owner。
空集合无需启动 Wi-Fi。只接受 timeoutMs（整数 1..60000，默认 6000）；
全部所选 owner 真正退休后返回 undefined。超时/取消结束 public wait，后台
原生清理继续，Future 存储不被 worker 借用。错误 WIFI_TWT_CLOSE_TIMEOUT
包含 started/selected/pending，原始每 owner 错误仍通过 agreements() 查看。
不隐式断开连接、重启 Radio、清 tracking fault 或自动重发 teardown。

注册、源类型、manifest、API 文档及 capability 同步；唯一 wifi-twt/1，
Candidate 等级不变。既有 Agreement/Future/结果表/原生池尺寸保持原样；
新增 closeAll Future 的 C5 布局为 640 B（其中 token group 为 632 B），
仅按调用分配普通 8-bit heap，不增加
常驻原生池或 callback storage。实机 free/largest-block 仍待预热后测量。

## 验证安排

C5、C5-no-SoftAP、C5-Wi-Fi-disabled、C3、S3 五种生产 Build Context
构建通过，无 warning/error。manifest（53 类/522 函数）、feature docs、
Wi-Fi schema、SDK coverage header、严格 TypeScript、MQuickJS 语法及
whitespace 检查通过；ELF 确认公开 Future → Radio 精确选择/等待、关闭授权
→ shared timer cleanup 的实际调用链，既有 IRAM recycler 闭包保持。
共享 SDK 和 build-local archive patch 哈希不变。生产构建与静态契约检查
结果记录于 `build/w08-twt-close-all-evidence.json`。
新增/扩展 deferred fixtures 调用生产结果表、信息 timer、Radio 和 closeAll
Future helper，覆盖部分/全部关闭授权、迟到观察、queued callback、delete
失败后缀、混合 owner、旧 slot 的后继 owner、空集合、取消与保留失败。
SDK/调度是可注入边界；不冒充完整 Future core、VM、GC、RTOS 或 RF 验证。
本阶段只做 fixture AST，不导入、编译或运行。

未刷写、串口操作、提交、推送、前端构建或更新父仓库 gitlink。
长 soak 仍留到 BLE API 完成之后。
