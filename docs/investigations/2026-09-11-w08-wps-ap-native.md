# AP WPS 托管命令、原生退休与 worker

基线 firmware `d7db8d1`、SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批完成内部 native/worker 接线，公开 `apRegistrar` 仍为 false。

AP SDK enable/start 在执行时检查传入的精确 identity；普通 eloop 命令携带 0，
不能越过托管 reservation。普通 disable 在 prepare_close 之前同样拒绝托管
状态，避免先关闭新操作再返回错误。Station 的旧 start/disable、析构和无 cookie
callback 也在执行时拒绝 AP owner，不能修改共用的 type/status/SM。

内部 begin/start/stop/retire/checkpoint/release 已接入真实 SDK。初始化失败仍
返回已保留的 identity 和首个原始错误。stop 撤销输入、timer 和 IE；retire
只在关闭前缀完成、callback depth 为零时临时授权同步析构。忙客户端、EAP
children/producer 或清理错误保留父对象，后续只继续未完成后缀。析构回收客户端
WPS IE，SAE semaphore 使用零等待；不在 STA_LIST 锁内递归取得表锁。

SDK heap 完成析构后仍保留 metadata/result owner；factory allocation 在设备
构造前失败时也有独立清理后缀。checkpoint 检查真实 SDK owner/type/status、
EAP 账本和 callback revision；release 核对同一 identity/revision，旧身份不复用。

新 AP worker 按需持有 config、PIN、原生快照、IPC 参数与返回收据。关闭顺序
为 native stop → ESP_TIMER_TASK marker 完成并删除 → 后续 Wi-Fi task
retire/checkpoint → 单独 release。timer 错误只重试后缀；未知 IPC 交接保留
整个对象，不读取可能仍被写入的快照，也不因迟到收据自行重发/释放。
PIN copy/commit 和终态 metadata 的存活独立于公开 Future。

本批只执行 **C3 registrar 增量构建**与受影响源码/编译对象核对；没有重复
十配置矩阵。生产源码完成编译；Python patcher、新增 native/worker 回归 fixture
及受影响的既有 fixture 完成 AST 解析，未导入、编译或执行测试。具体记录见
`build/w08-wps-ap-native-evidence.json`。
SDK 未改动，未刷写或串口操作。

仍需 Radio/AP lease、AP 配置与 runtime 关闭交接、公开 Session/Future/watch。
worker 当前没有上层 caller。队列顺序、跨关联输入、GC/OOM、共享 AP 连续性及
RF/实机仍未获得运行证据。多目标/关闭配置在模块节点补构建，Wi-Fi API 完成
后集中运行与实机测试；长时间 soak 留到 BLE API 完成后。
