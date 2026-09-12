# W-08 individual TWT 原连接关闭与清理

本批补齐后续物理恢复所依赖的真实连接撤销证据。并未新增完整 `recover` API；
未知 driver handoff、sticky tracking fault、STOP/deinit 后的协调、all-flow 公共
策略与其余 Wi-Fi API 仍需继续实现。集中运行/实机验收仍按用户安排后置。

## 已确认的代码缺口

此前广播 TWT 在原生 `ieee80211_close_all_twt_sessions` 路径记录 native_closed，
individual result 却没有对应证据。其 cancel/quiescent 总要求已尝试的 teardown
观察到成功；即使原连接已经被 SDK 关闭，也会等待不会再来的成功事件。已接受
的 individual setup 状态也可能仍显示 active。

固定 SDK C5 原始 close-all 代码清除 individual/broadcast 位图和 ID，并调用
PM 清理，但不完成框架的 setup/information timer、TX、结果或事件退休。仅
以空位图推断回收会遗漏旧 timer 和 native 队列消息。原 node 地址可能被下一
连接复用，不能单独当作连接身份。

上述为源代码及固定 archive 核对，尚无动态复现结果；新增生产路径 fixture
已编写，按当前阶段安排未运行。不能把编译通过称为竞争测试通过。

## 改动

在既有 close-all wrapper 调用原始 SDK 前：

- individual 结果设置永久 `SETUP_NATIVE_CLOSED`，撤销旧 event fence；不清
  submitting，不伪造 cancelled，不改写原始 setup/teardown 错误或释放 owner。
- 撤销全部 setup 和 information timer 的数字回调权限，再逐个尝试已知 handle
  的 stop/delete；失败仍保留具体 handle/payload 和 cleanup 后缀，其他槽继续
  清理。单个 Agreement 的 close 仍无权取消包含其他有效 flow 的 all-flow timer。
- 原有广播撤销与原始 SDK close-all 继续执行。没有新增隐式 disconnect 或关闭
  其他无线模块的操作。

SDK setup 准入拒绝关闭边界之前已预留的 request；setup timer/TX、information
capture/匹配与 teardown 的实际 native 准入也拒绝该旧 request。晚到观察仍可
保存诊断并撤销旧 fence，但不能清掉永久关闭标记或构造新的 active Agreement。
请求 ID 仍不复用，后继请求不会继承该标记。

只在真实关闭标记存在时，cancel/quiescent 才不再等待 teardown 成功观察。
所有 submitting/observation、原生 ID/flow 冲突、TX/recycler/PM、timer/native/
event 证明继续保留；tracking fault、未退出 callback 或未清理 handle 仍阻止
释放。Radio close 不再为已撤销连接提交 teardown，沿用现有联合退休执行器。

公开 individual status 新增 `nativeClosed`、`teardownError` 和
`teardownObservationError`，Future 不把已撤销请求交付为有效 Agreement。
`nativeClosed` 表示原连接操作权限已撤销，不表示资源已经排空。源类型及 API
文档同步；类/方法数量不变，唯一 v1。

## 验证与内存

C5、C5-no-SoftAP、C5-Wi-Fi-disabled、C3、S3 五种生产 Build Context 构建通过。
manifest、feature docs、schema、SDK coverage header、严格 TypeScript、MQuickJS
语法及 whitespace 检查通过。生产 ELF 核对 close-all → result/timer 撤销、SDK
capture/准入 → 旧 request 检查、Radio/Future 和原联合退休链，保留原 IRAM
recycler 闭包及 build-local SDK patch。

标记复用现有 uint16 flags，原 setup result 72 B、八槽 576 B 延迟池不变。
新增定时器关闭使用有界栈副本，没有新静态池、动态 owner 或常驻 callback
存储；上批 Radio owner/JS handle/Future 的 C5 布局不变。实际 free/largest-block
仍待实机预热后测量。

证据：`build/w08-twt-native-close-evidence.json`，包含本批生产构建、ELF 调用链、
原始 SDK close-all 反汇编、布局及文件哈希。共享 ESP-IDF 保持干净。

扩展 deferred fixture 覆盖真实结果表关闭/迟到成功/预留但未提交/错误保留；
真实 setup/information timer 的已排队回调、全部撤销、stop/delete 失败后缀；
生产 SDK cancel/quiescent 在撤销后仍拒绝 TX 未回收、tracking fault 或信息
timer 错误；Future 拒绝交付已撤销的成功结果。只做 AST，无导入、编译或执行。

未刷写、串口操作、提交、推送、前端构建或更新父仓库 gitlink。长 soak 继续留到
BLE API 完成后。
