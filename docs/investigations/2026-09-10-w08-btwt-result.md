# W-08 广播 TWT 稳定请求结果

本批将原生 setup 结果绑定到发送前的请求身份，覆盖 TX 失败、响应/dwell
切换、输出错误和连接关闭。公开广播 Agreement/Future、提交身份交接、
teardown 与联合回收仍待完成；没有新增正式占位 API。

## 已接入的生产路径

此前 timer result 以当前 timer identity 为键。ACCEPT 响应会替换响应定时器，
新 dwell identity 无法直接充当同一 setup 请求的身份；TXFAIL 又可能发生在
任何 timer 建立之前，其观察事件不能作为可靠的控制结果。

现在 `ht_action_output` 交给 SDK 前，使用现有不回绕 TX revision 预留原生
结果记录，保存 node/参数副本。分配失败不进入原始 output。这个数字与 RF
wire dialog 分开：TX request identity 跨响应和 dwell 保留，timer identity
随阶段更新，wire dialog 继续遵守每 ID/每 boot 255 次的不复用政策。

TX completion 先校验 ledger，再进入同任务事件 scope。原始 SDK callback
返回并处理 timer 错误后，记录 TXFAIL/本地错误，再零等待发布观察事件。
成功 TX 只绑定响应 timer，不伪造 setup 成功。RX ACCEPT 替换 timer 时保留
同一请求结果；最终 dwell 返回后的结果仍以原请求数字读取。不同请求即使
复用广播 ID，旧数字也不能读取后继结果。

原始 output 返回值另记为 `submit_error`：同步 callback 已保存的结果不会
被覆盖，输出报错也不会销毁它已经建立的 timer。原生处理中的 `tx_busy`/
busy 阻止读者提前把局部 fault 当作已返回的终态；callback post/arm 错误
在处理退出后可由精确结果读取发现，不依赖观察队列。

连接关闭先撤销 TX、停止/删除 timer，再执行原始关闭，最后为尚未结束的
请求保存关闭错误。已有 stop/delete 错误与 handle 保留机制继续有效。
`complete` 不等于 TX/定时器/事件/Agreement 已回收，失败后仍须完整退休。

## 所有权与内存边界

复用原广播 timer entry 中的结果，没有添加第二套结果池。结果结构由 48 B
增至 56 B（稳定身份、当前 timer identity、独立输出错误和处理标记），entry
由 88 B 增至 96 B，32 项延迟 INTERNAL 预算由 2816 B 增至 **3072 B**。
静态 timer 对象仍 52 B；TX 主账本 768 B 和可选 1568 B storage 均不变。

结果读取只做锁内值复制，错误不修改输出。它不提供公共 owner 预留/释放：
后续 Agreement owner 必须在生命周期内阻止同 ID 结果被重新预留，并独立
证明 TX/timer/native/event 退休。当前 native TX gate 保护正在发送的 ID，
timer gate 保护活跃/保留 handle，不能把这些当作完整 Future/GC owner。

新 TX 预留、输出返回和 callback scope 在最终生产 ELF 有调用链；worker
精确结果 reader 已编译入 framework archive，尚无公开 Future caller，不能
把 reader 的存在写成广播公开 setup 已完成。

## 验证范围

C5 HE、C5 no-SoftAP、C5 Wi-Fi-disabled、C3、S3 五个生产构建均通过，
最终日志无 warning/error。C5 镜像为 3027680 B，无 SoftAP 为 2904688 B，
相对前批各增加 1744 B；disabled/C3/S3 镜像不变。原 timer/recycler IRAM
闭包核对通过，生产 object 与 archive 成员一致，共享 SDK/已有局部 patch
hash 不变。证据为 `build/w08-btwt-result-evidence.json`。

manifest 53 classes / 520 functions、27 feature docs、SDK schema 35/21 与
map 1267 项、严格 TypeScript、MQuickJS 61 sources / 61 snippets、空白检查
均通过。这些是构建/静态证据，未运行测试 fixture 或实机。

待集中执行的真实生产 helper 用例覆盖：发送前分配失败；同一请求跨 response/
dwell 的精确读取；后继请求拒绝旧数字；TXFAIL 先保存再遇到观察队列失败；
重复 callback；SDK 回调仍在执行时不提前完成；缺失事件；create/arm/post
失败；提前回调后 output 报错仍保留 timer；连接关闭结束尚无 timer 的请求。
现有 RX/PMF、旧 timer 消息、停止/删除失败后缀用例继续保留。
只做 Python AST 检查，未导入、编译或执行这些 fixture。

Wi-Fi API 全部完成后集中运行/实机验证，长 soak 留到 BLE API 完成后。
本批未刷写、串口操作、提交、推送或更新根 gitlink，共享 SDK 不作修改。
