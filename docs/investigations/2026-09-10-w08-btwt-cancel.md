# W-08 广播 pending setup 精确取消与静止检查

本批补上托管广播 pending setup 的取消、失败后缀重试及原生静止值检查。
不把静止快照当作 timer/native/event 顺序证明；结果 held 尚不释放。已建立
Agreement 的 teardown、联合退休、公开 Future/Agreement 继续待完成。

## SDK 边界与生产实现

固定 SDK 的 bTWT setup builder 写超时并提交管理帧；ACCEPT RX 替换为 dwell
定时器。dwell process 在 +0x84 写入建立位图，然后继续事件/PM/interval
更新。因此 pending cancel 必须与原生处理串行并排除 busy，不能只看一个
瞬时 bitmap。当前 task 路径和 SDK object 证据随本批保存。

取消通过既有私有 Action ioctl 路由加入广播控制命令，在原生 Wi-Fi 任务
执行。先校验广播 ID、稳定 TX request identity 和 held；旧数字/未持有者
不触碰 TX/timer。若对应位已建立则拒绝 pending cancel，保留 Agreement
交由 teardown，不清 bitmap、不调整共享省电策略、不影响其他广播 ID。

准入后先记录 cancel_requested，撤销 timer active/dialog 和精确 TX callback
权限，再运行同一 timer 的 stop/delete。旧 TX/RX、timer callback 及已排队
的数字 timeout 不能借此进入后继请求。stop 失败不跳过到 delete，delete
失败不重新执行成功的 stop；错误保留 handle 和 held，后续只重试未完成步骤。
正常取消不清除原 TXFAIL/输出错误等结果。

取消成功仅将 result 标记为 cancelled/complete，**不会**释放结果或原生 EB。
TX 静止检查仍等待同一请求的 output、callback 和 recycler 全部退出；观察
队列/应用 timeout 不提供这些证据。两个广播 ID 的发送和撤销分别核对。

原生静止检查要求精确 held/cancelled、没有 active/busy/publishing、没有
保留 timer handle 或 cleanup_error、没有已建立位，并核对精确 TX 不再存活。
返回 TX 与 timer revision 的值副本；失败保持输出不变。历史 stop/delete
fault 继续可诊断，已完成的清理可取得静止快照；revision 耗尽拒绝证明。

## 后续联合退休所需工作

静止检查本身没有执行 timer-task/native/event fence。后续协调器必须在
取得上述快照后完成顺序屏障，再核对同一身份/revision，才能释放 held。
当前没有以空 timer、断连、Future 完成或一张 bitmap 直接释放的 API。
取消也不能撤回已经发到 AP 的 RF 请求，不把本地 cancelled 写成对端 teardown。

原生取消/静止函数已接入生产 private ioctl handler；worker 包装入口编译入
archive，尚待 Radio/Agreement 调用。公开广播 setup/close 没有新增占位注册。

## 预算和验证

cancel_requested/cancelled 使用原 result 的空余 bit，result 仍 56 B、timer
entry 仍 96 B、32 项延迟池仍 3072 B。dispatch 仍 36 B，TX 主账本 768 B
与可选 storage 1568 B 不变，无新增 heap pool。私有控制请求是同步队列调用
期间的栈上值，沿用已有 SDK 调度边界。

C5 HE、C5 no-SoftAP、C5 Wi-Fi-disabled、C3、S3 五个生产构建通过，最终
日志无 warning/error。C5 镜像 3030080 B（+1424）、no-SoftAP
2907056 B（+1424）；其他三个镜像不变。生产 object 与 archive 一致，
既有 timer/recycler IRAM 闭包、共享 SDK 和局部 archive patch hash 保持。
manifest 53 classes / 520 functions、27 feature docs、SDK schema/map、严格
TypeScript、MQuickJS 61 sources / 61 snippets、空白检查均通过。
ELF/archive/IRAM、生成物检查与 hash 记录在 `build/w08-btwt-cancel-evidence.json`。
新增/扩展用例调用生产 timer/TX helper
和私有队列 wrapper，覆盖旧身份、未持有、busy、已建立拒绝、不同 ID 隔离、
TX 未归还、stop/delete 后缀、取消后旧 timeout、输出不变及 held 持续保留。
fixture 仅 AST，未导入/编译/执行，动态竞争与 RF 仍未验证。

Wi-Fi API 全部完成后集中运行和实机验收，长 soak 留到 BLE API 完成后。
本批未刷写、串口操作、提交、推送或更新根 gitlink。
