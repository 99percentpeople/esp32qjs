# W-08 SmartConfig Radio 绑定与 runtime Session

在[原生解码器协调](2026-09-11-w08-smartconfig-decoder.md)之上，本批接入
Radio 排他操作和后台 Session 管理。SDK 启停、凭据转移、关闭重试已由实际
runtime poller/teardown 路由引用；JS Session create/activate、正式类型与注册
尚未接入，不能据此宣布公开配网 API 可用。

## Radio 准入与恢复

runtime 先复制 Application/STA/AP 的精确 lease 值。worker 在 Radio mutex
内重新核对全部身份、已启动状态、无其他 owner/操作/固定信道/唤醒锁/混杂
接收，拒绝尚有 Enterprise 借用。原生 Station 必须未连接，当前 home channel
在 2.4 GHz；不隐式断开已有网络，不隐式改变 band policy。

APSTA 需要调用者显式同意 AP 信道中断，且必须有对应 AP helper lease。
当前内部布尔授权还不是已发布的 JS option。开始前保存主/副信道。SDK 原生
解码可能改信道，捕获结束后按保存值恢复并读回；恢复失败保留 operation 与
全部 lease，禁止当作关闭成功。启动前拒绝了外部 decoder 时不恢复信道，
因为该操作根本没有获得 RF 修改权限。

复用已有不可回绕 operation identity/generation。通用 end_operation 不能
解除 SmartConfig reservation。Application/STA/AP 三个 lease 均被固定，
新增 Radio owner、混杂接收和 wake lock 被拒绝；失败启动也保留清理 token。
只有解码器完整 close/release 和信道恢复都成功，才解除 operation。
所有 SDK 调用均在普通操作 mutex 内，未放入临界区。

## Session、凭据与取消

内部最多四个存活 Session handle、一个活动 Session；create 预留有界存储、
复制 key/owner，不改 driver。activate 才进入后台任务，在未来 JS 对象构造
成功之前不开始 RF。输入和 deadline 整数边界在提交前检查。

后台 worker 独立持有引用；public timeout、close、最后一个外部引用释放或
runtime teardown 只请求关闭，不释放仍被原生操作使用的 storage。worker
队列满会保留 pending owner、记录错误并重试；不把排队失败写成 native close。
运行 deadline 覆盖启动、获取凭据、停止捕获及稳定的凭据转移；不是单纯的
RF 收包 timestamp deadline。凭据转移完成后停止这一 deadline。

凭据先在 worker 中停止捕获、恢复信道，再从 event owner copy 到预分配的
Session 存储并 commit。Session 与 worker 引用保证 copy/commit 期间的存活；
取消竞争不会把已关闭 Session 的结果交给 JS。SDK event 副本 commit 后清零，
Session 的 copy/commit 支持将来的 JS 转换失败重试。JS 任务只访问快照和保留
字节，不为结果查询等待 SDK/Radio 操作锁。

ACK 的私有 protocol/token/phone 元数据保留在 decoder owner，不因凭据交付
而丢失。SDK 凭据缓冲与 Session 凭据缓冲都可清零后再显式请求 ACK；完整关闭
或 release 清零这份私有元数据。ACK 不自动连接 Station，提交/完成仍按精确
receipt 记录；显式自动连接的 helper 操作交接尚未完成。

## Runtime 边界

Wi-Fi init 打开新的 Session admission；现有 Wi-Fi poller 调度清理 worker。
core destroy 在检查所有 Future 是否排空之前也推进 SmartConfig cleanup，
因此 Future 正在等待不妨碍 native 关闭。活动 Session 和 worker 未退出时，
禁止拆除 helper、释放 VM 或允许下一代 runtime 接管。

未激活对象可立即销毁；已激活对象的最后一个外部引用退出后由 registry
继续负责关闭。新 runtime 的 open 同时检查旧 handle/worker，不能覆盖旧状态。
未知 SDK handoff 仍保持可诊断保留；这里没有新增冒充成功的物理故障恢复。

## 验证与后续

五种生产构建（C3/S3/C5/C5-no-SoftAP/C5-Wi-Fi-disabled）以及生成物、类型、
SDK map、MQuickJS 语法检查结果记录于 `build/w08-smartconfig-session-evidence.json`。
证据另核对 runtime poller/destroy 到实际 Session 函数的 ELF 调用、SDK native
start/stop 的链接和归档一致性，并记录按需结构大小与常驻状态。

新增 Radio 和完整 Session source 的调度 fixture，扩展既有真实 core/Future
销毁 fixture：覆盖旧 token、全 lease 固定、AP 授权、恢复失败保留、队列满、
已排队取消、copy/commit 取消竞争、GC/最后引用退出、超时和 runtime 重试。
仅 AST 检查，未导入/编译/执行，不将构建与静态路由当作运行/实机证明。

仍待完成：公开 JS/Future/观察事件及凭据类型，custom data 的精确二进制长度
与 SDK 内部秘密副本清零，显式自动连接/ACK 的 helper 交接及完整失败收尾。
Wi-Fi API 完成后统一阶段测试和实机测试；长 soak 留到 BLE API 完成后。
本批未刷写、使用串口、提交、推送或修改父仓库 gitlink。
