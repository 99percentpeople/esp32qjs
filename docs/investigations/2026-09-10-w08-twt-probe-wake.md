# W-08 TWT probe 唤醒引用与保留 timer 的关闭

基线 firmware `d7db8d1`、SDK `fff9895c82`。关闭审查发现上一批 timer 删除失败
保留与 SDK stop 的交互缺口；本项修复该前置问题，完整取消/联合退休仍待完成。

## 确认的交互缺口

`itwt_stop_process` 以 `itwt_probe_timer.timer_arg != NULL` 判断是否执行 probe
的 disarm/done/`pm_wake_done`。它没有独立检查该 probe 是否仍持有唤醒引用。

上一批删除失败会保留 handle，而 RX/TX/timeout/原生错误收尾仍可能已释放
PM 引用。之后 stop 再根据 handle 存在释放一次，会影响共享 PM 计数。
固定 C5 `pm_wake_done` 只检查全局 `g_pm + 292` 是否大于零再减一，并不识别
是哪项操作的引用；其零值保护不能阻止减少其他 owner 或 disconnect 自身的引用。
因此上一批的局部清理不能作为完整关闭已完成的证据。

这是原生对象/最终 ELF 的控制流证据，尚未执行运行竞争复现。新 fixture 将
共享计数初始化为其他 owner 的三份引用，覆盖一次 probe 释放后再次 stop。

## 已接入的生产处理

新增 boot 原生 probe wake 记录：held、acquiring、releasing、fault。正常
原生 submit 准入要求无 probe 引用/调用/故障；获取和释放时标记调用进行中，
实际 PM 调用在临界区外。引用已释放时再次释放直接返回，不调用共享 PM。
意外重复获取或重入保持诊断故障，禁止把不明引用当作已排空。

SDK build-local archive 仅改以下六处 CALL relocation 的目标：

| SDK member / function | offset | 操作 |
| --- | --- | --- |
| ieee80211_ioctl.o / wifi_sta_itwt_send_probe_req_process | 0x4a | 获取 probe wake |
| 同上 | 0x9c | submit 失败释放 |
| ieee80211_twt.o / itwt_probe_rc_tx_cb | 0xea | TX 完成失败/超时释放 |
| ieee80211_twt.o / itwt_probe_timeout_fn_process | 0x9a | timeout 释放 |
| ieee80211_twt.o / itwt_stop_process | 0x6e | stop 释放 |
| ieee80211_sta.o / sta_recv_mgmt | 0x2cc | RX 成功释放 |

原生错误 abort helper 也使用同一释放入口；wake fault 时不再进行 node/PM
清理。SDK snapshot 增加独立 `probe_wake`。不以 timer 是否存在推断引用归属，
也不在 `pm_wake_done` 全局加拦截；包括 Station disconnect 自身在内的其他
获取/释放仍调用原 SDK 函数。

## Archive 修补边界

`patch_idf_vendor_ie_context.py` 先核对原 SDK archive 的完整 SHA-256，再调用
新的 helper。三份对象仅追加 undefined symbol/string tables，保持旧 symbol
indices，改指定 `R_RISCV_CALL` 的 symbol index。指令、allocatable sections、
原有符号、其他 relocation 和 debug 引用保持不变。

对象变长后重新计算 GNU archive symbol index 的 member offsets；不是对
任意二进制模式进行替换。SDK 原件和 immutable Build Context 均不改写。
修补脚本也加入 CMake 配置依赖；C3/S3/Wi-Fi 关闭不启用这个修补。

独立静态核对使用 ar/nm/ELF relocation 读取：50 个 member 的顺序与定义符号
索引一致；仅三份对象的 linking metadata 改变，其他 member 字节一致；六处
改动与上表完全一致，所有其余 relocation 相同。证据为
`build/w08-twt-probe-wake-archive.json`。

## 验证与剩余工作

C5 最终 RX/timeout/TX/stop 与 framework abort 均调用新的释放入口；获取
入口和 native submit 尚在 archive/object 中，未因公开 probe 调用而全部
链接进入镜像。不能将其对象存在写成已公开可调用。

新增静态记录 8 字节，无 heap 分配。C5 镜像 2,959,504 字节，比上一批增加
384 字节；probe timer/result 各 36 字节、TX 静态 32 字节和懒分配 512 字节
ledger 及已跟踪框架静态对象保持不变。四目标/关闭构建、链接和语法证据见
`build/w08-twt-probe-wake-evidence.json`。

生产 wake fixture、timer/SDK 准入 fixture 和 archive relocation fixture
已补齐，仅 AST 检查，未导入、编译或执行。构建不替代 SDK 调度、RF 或实机
PM/省电验证，Candidate 等级不变。

剩余：按 exact operation identity 的取消、晚到 post 失败轮询、断连及
timer/TX/native/default-event-loop 联合退休、故障恢复、Radio owner、
Agreement/Future/公开 TWT，及其他 Wi-Fi 范围。本次没有提供 fault reset 或
把独立 wake 记录当作全部资源退休证明。
全部 Wi-Fi API 完成后集中运行与实机测试；长 soak 留到 BLE API 完成后。
无刷写、串口、擦除、前端构建、提交、推送或父仓库 gitlink 更新。
