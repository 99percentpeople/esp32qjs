# W-02：配置事务授权断连与原生屏障

基线 firmware `d7db8d1`，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
前批[统一 stop](2026-09-08-w02-unified-stop.md)保留显式 Station disconnect；
本批继续公开 configure 的依赖，增加其内部 allow_disconnect 路径。

## 已接入的内部行为

- 共用配置执行器在参数/target/controls/start-controls 纯检查之后，才请求
  中央 owner admission。allow_disconnect=true 只放宽已建立 Station 连接的
  检查，不放宽 scan/connect/disconnect 的 native pending/Future/result/drain。
- Radio 首先核对 application/Station/AP 精确 identity，排除 foreign owner、
  wake lock、promiscuous 和原生 operation，并取得 lifecycle token。之后才
  允许配置事务断开 Station，避免先破坏连接再发现共享 Radio 冲突。
- token 内复用实际 wifi_request_disconnect、disconnect epoch 与默认 netif
  handler/IP fence。请求允许的连接退出，不销毁其他 Future，不保留 JS 输入，
  不执行 JS poller；等待使用 native_wait/cooperate，至多 1000 ms。
- 原生 DISCONNECT、netif down 和正确 epoch 的事件 marker 都完成后才继续
  stop、helper retirement、停机提交和启动。观察队列/Future 完成不能代替
  原生断连屏障；旧 GOT_IP/marker 不产生成功。
- 接受过的 esp_wifi_disconnect 不因 timeout/队列满而重发。setter 失败可在
  下一次显式清理中重试；marker 投递失败保留未完成义务，重试只推进相应后缀。
- timeout、中断、原生错误保留 token 和 helper storage，stage 为
  configuration-disconnect。后续 wifi.stop/中央 runtime cleanup 先完成该
  断连阶段，再释放其余 owner、stop 和退休 helper，不能先销毁回调可见数据。
  状态 pending 标志位于 helper reset 之外，成功屏障清除它。

默认 startAP、普通 stop、通常 runtime admission 继续传 false。原生 GOT_IP
处理要求活动 connect Future/in-progress；本批未放宽该检查。常规 stop 仍不
授权主动断开 Station；上批 stop-only 与失败配置 shutdown 的选择保持。

## 与公共 configure 的距离

本批没有注册 configure 或外层 JS 参数。还要完成：外层 country/protocol/
bandwidth/模式/allowDisconnect 的 typed capture、完整返回与错误副作用说明、
高级认证入口的实际 gate、AP 客户端断开授权、APSTA 共享控制及保持 Station 的
stopAP。不能仅凭已建立 Station 的断连支持就声称所有连接授权边界完成。

一个 AP 客户端列表快照不能排除之后的新关联；未来公开 configure 默认禁止
断连的路径不能用“当时列表为空”作为可安全 stop AP 的证据。公共契约需要在
实际 admission/模式切换路径中解决或明确拒绝，不新增占位声明。

## 检查和阶段测试

C5 immutable Build Context 编译通过；MQuickJS、manifest、feature docs、raw
schema/live SDK、recorded map、whitespace 结果/hash 记录在
`build/w02-config-disconnect-evidence.json`。

新增 `test_wifi_configuration_disconnect.py` 使用生产 request/epoch/fence/
event processor/配置 wait，SDK 事件通过可控调度注入；覆盖正常晚到事件、旧 IP
与 marker、timeout 后显式重试、marker queue full、disconnect setter 失败和
cooperative interruption，不用独立测试状态机替代生产逻辑。中央 admission
测试补默认拒绝连接、allow=true 仍拒绝 Future/foreign owner，只有成功 token
后才开始交接；执行器覆盖断连失败阻止 stop/提交并保留清理。

相关 fixture 的 link snapshot/connection event 参数及公共生命周期继承边界
重复定义同步修正。七个 Python 文件仅做 AST 检查；全部上述 Host C/Python
用例 **not-run**，留在所有 Wi-Fi API 完成后的阶段测试。三目标/disabled 矩阵、
真实 SDK/网络/APSTA/队列饱和/GC/重开等实机功能也未执行；长时间 soak 等 BLE
API 完成。本批无刷写、串口、设备 workspace 写入、提交、推送或父 gitlink 更新。
