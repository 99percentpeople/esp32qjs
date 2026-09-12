# W-08 TWT probe 原生 owner 与联合回收

基线 firmware `d7db8d1`、固定 SDK `fff9895c82`。接续精确取消，本项增加托管
提交、原生占用和回收执行器；Radio lease/Future/公开 TWT 仍待接入。

## 原生 owner 与提交

内部 submit/quiescent/release 复用 Action 私有 ioctl 通道，要求专用 sentinel、
精确 callback 和 Station interface。submit 接受 1–60000 ms，原生 task 内调用
已有受控 probe wrapper。固定 SDK handler 仅从同步消息的 word 12 读取时间，
未在 Wi-Fi task 递归调用会获取 `wifi_api_lock` 的公开 SDK API。

提交在同一原生任务中取得不可复用 result identity，并设置 `owned` 后返回。
一旦接受原生操作，即使 submit 返回错误，调用者也必须接收非零 identity 并
完成清理。原生准入在进入 TX scope 前检查 `owned`；其他托管提交及普通 SDK
probe 调用都不能覆盖该记录，也不能通过被拒绝的请求不断改变其 TX revision。
未接受的请求不产生 identity。此 pin 不替代 Radio lease 或公共 Future storage。

## 联合回收顺序

`wifi_twt_probe_retire_poll` 使用 owner 内嵌的稳定原生记录，所有 owner 调用需
串行，且在 Wi-Fi/default-event/esp_timer task 外执行。公共超时或 GC 不能释放
仍由 marker callback 引用的记录。

1. 用精确 identity 执行已实现的取消。失败保留后缀，成功后不重复取消。
2. 在原生队列检查 identity/owned/cancel_complete，以及 probe active、timer
   handle/数字权限/cleanup pending、独立 PM 引用和 TX probe slot/call。TX
   ledger fault 不作为空闲。取得 TX revision 与最后 timer identity 的值快照。
3. 执行已有 TASK timer marker，确认 callback 退出并接纳删除，再经过 native
   ioctl marker。marker 启动/停止/删除/native fence 失败均保留存储、继续后缀。
4. 经现有 Radio boot listener 投递零等待 event marker。payload 只有不可复用
   operation identity 和递增 sequence，不含 owner、JS 或 runtime 指针。
   handler 只确认当前 pending 的精确 pair；迟到、重复及旧 sequence 不授权释放。
5. 原生 release 再次检查全部 probe 条件、相同 TX/timer 快照以及精确事件确认，
   然后解除 `owned`。协调器清理自身 marker 记录后才返回 ESP_OK。Radio lease
   仍由后续适配器单独释放。

任何相关快照变化都会重建顺序点及事件序号。当前也保守地把其他管理帧引起的
TX revision 变化纳入重建，持续管理流量可能延迟回收；不通过忽略变化强行释放。
事件队列满时保留原生 owner 并重试 event post。取消之前已捕获的控制结果不变。

旧 timer fault/late queue-post error 可以在其 handle、权限和 pending 均清除后
完成本 probe 回收；原始诊断仍保留，不自动清除 fault 或声称下一次操作可用。
归属不明、TX ledger 故障或耗尽仍需后续实际物理恢复边界。

## 当前验证与边界

C5 roaming、C3、S3、C5 Wi-Fi disabled 的 immutable Context 构建通过。
C5 镜像 2,963,152 字节，比上一批增加 2,832 字节；其他三种尺寸不变。
probe result 44→48 字节，其余已跟踪静态对象不变。协调器在 C5 为 96 字节
owner 内嵌 storage，没有新增全局实例或 wrapper heap；marker 使用既有 helper
的 SDK timer 分配。关闭 SoftAP 的 C5 配置也构建通过，镜像 2,839,568 字节。

本轮私有 dispatch 使原生 probe submit 与 PM 获取保护首次链接进入当前 C5
ELF；native claim/quiescence/release 和 Radio boot event 确认路径也已链接。
联合 poll 执行器已编译入生产 archive，尚无 Radio/Future caller，最终 ELF 会
删除未使用的协调器函数。不能把该对象的编译通过写成公开操作已可调用。
证据：`build/w08-twt-probe-retire-evidence.json`。

manifest 52 classes/510 functions、feature 文档、SDK schema、严格 TS、MQuickJS
61 sources/59 snippets 及 whitespace 检查通过。生产 result/SDK/TX fixture 已
同步，新增 fixture 组合真实 result、timer marker 和回收执行器；覆盖身份占用、
失败 submit 的 identity 交付、TX pin、队列满、迟到事件、callback 退出窗口、
后缀失败与变化快照的重建。仅 AST，未导入、编译或执行 fixture。

待完成：Radio lease/公共 probe Future 接入、TWT Agreement/setup/teardown/
suspend 的完整退休与物理恢复，以及其余 Wi-Fi 范围。SDK probe 时间估计中的
1024/1000 单位问题仍待单独核对，未在本项改写成严格 deadline。
运行调度、RF 和实机证据均 not-run。所有 Wi-Fi API 完成后集中运行与实机测试，
长 soak 留到 BLE API 完成后；本轮没有设备写入、提交、推送或根 gitlink 更新。
