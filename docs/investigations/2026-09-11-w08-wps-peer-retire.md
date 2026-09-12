# W-08 AP WPS 延迟客户端身份与 EAP 释放交接

基线 firmware `d7db8d1`、固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批继续 AP registrar 内部生命周期，公开 `apRegistrar` 仍为 false。

## 代码路径与修复

SDK 原延迟删除保存 MAC，回调时重新查询，无法区分同 MAC 重连后的客户端。
延迟 EAP deauth 保存 `sta_info` 指针；取消 timer 不代表已派发回调消失。
另外，WPA3 任务原先从 STA 表移除客户端后才析构 EAP，父对象关闭仅扫描
STA 表会漏掉这段仍存活的引用。以上是源码路径证据，竞争复现尚未执行。

- `sta_info` 分配保留原结构作为首成员，追加两个 64-bit ticket 和 AP 身份。
  ticket 在 boot 内不回绕，回调只携带数字，在真实 STA 表中精确查找并取得
  SAE semaphore 后操作；不会按 MAC 删除替代客户端。
- driver 删除在调度前保存删除义务。timer 分配失败或 SAE 忙不会丢失义务；
  关闭撤销 timer 后重试未完成删除，忙时沿 SDK `remove_pending` 交接。
- EAP deauth 失败保留原始错误；不再将个别客户端认证失败写为全局 WPS 成功。
- 在持有 STA 表锁、尚未移出客户端时，将 EAP 从客户端转移到待释放链表。
  链表节点嵌入原 EAP allocation，不额外分配退休节点。外部任务不运行 EAP/
  WPS 析构；实际析构由 Wi-Fi task 执行，且不在 STA 表锁或临界区内执行。
- 子对象计数包含存活、待释放和正在析构的记录；step 和整个 timer handler
  持有独立记录引用。父对象不能因为客户端离开列表就提前释放。
- 唤醒携带 AP 数字身份，合并待执行请求。producer 计数覆盖注册尚未返回的
  区间；唤醒分配失败保留队列，由显式关闭或下一次 allocation 排空。
- 普通 SDK deinit 使用可返回错误的 hostap release；释放 EAP、authenticator、
  registrar 后才释放 SDK heap。排空未完成时保留父对象，重试继续使用 SDK
  owner 保存的 `wps_ctx`。AP result detach 也检查 EAP 子对象已排空。
- 关闭前缀新增 peer delays 步骤。托管 AP result 仍禁止普通 SDK disable 释放，
  本批不把 EAP 子对象排空当成整个 native RX/driver 路径静止。

## 验证边界

九种生产配置构建通过：C3/S3/C5 普通配置、C5 no-SoftAP、C5 feature-disabled、
C3/S3/C5 registrar-enabled、C3 registrar-enabled no-SAE。生成源码、实际
编译对象、archive/ELF 与静态契约核对记录在
`build/w08-wps-peer-retire-evidence.json`，未修改共享 SDK。

三目标 EAP state 原结构仍为 296 B，完整 record 从上批 320 B 增至 328 B，
本批每 EAP 增加 8 B。全局 child/owner/producer/queue/wake 账本增加 20 B；
S3 临界区锁另为 8 B，peer ticket counter 为 8 B。启用 SAE 时 STA 从
72 B 增至 104 B，no-SAE 时从 56 B 增至 88 B，均为每客户端增加 32 B。
堆峰值和碎片仍需实机账本核对，不能用镜像大小推断运行 SRAM 收益。
普通五配置镜像大小未变；C3/S3/C5 registrar 镜像相对上批分别增加
2,080 / 1,984 / 2,112 B，C3 registrar no-SAE 增加 1,872 B。

新增 `test_idf_wps_peer_delays.py` 和 `test_idf_wps_eapol_retire.py` 调用生产
helper，登记 ticket 复用、OOM、SAE 忙、回调持有、唤醒提交交错、关闭与迟到
回调。现有父对象 teardown 和 AP result fixture 同步依赖。依用户安排仅
AST 解析，不导入、编译或执行运行用例；硬件/RF/GC/队列竞争均 `not-run`。

## 下一步

继续 AP PIN 辅助 timer 的身份、部分初始化失败保留、完整 queued RX/driver
排空和托管 heap/result release，再接 worker/IPC、Radio/AP lease 与公开
Session。DPP、WAPI、受支持 NAN/Mesh、其余 Driver/CSI/恢复缺项及全部 Wi-Fi
集中运行/实机验收仍在总目标内；长时间 soak 留到 BLE API 完成后。
