# W-08 TWT probe 精确取消与存活状态

基线 firmware `d7db8d1`、固定 SDK `fff9895c82`。本项接入内部原生取消入口；
Radio/Agreement/Future 和公开 TWT API 尚未接入，不能将此项记为 TWT 已完成。

## 取消行为

`esp32_mquickjs_wifi_twt_sdk_probe_cancel(identity)` 通过已有 Action 私有 ioctl
通道进入 Wi-Fi task。专用 sentinel、精确 callback 和 Station interface 检查
在原生 mutation 前执行；这条分支不发 Action 帧，也不取消其他 off-channel 操作。

最新 probe result 的非零 identity 必须精确相同，而且 submission 和原生事件
publication 已返回。旧 identity、尚未提交完成和零 identity 都不会进入 timer
清理。`cancel_requested` 在清理之前写入，清理失败时保留；未完成取消阻止结果
记录被下一次 begin 覆盖。原生 begin 的 timer、PM 和 TX 准入仍独立保留。

原生 timer 取消按以下顺序执行：

1. 核对独立 wake 引用及当前 active 状态；active 时还必须匹配保存的 node
   地址值和 phase。旧地址只用于比较，不解引用。归属不明时返回错误且不清理。
2. 使 timer 数字 identity 失效，执行原有受控 stop/delete。删除失败保留同一
   handle；已发出的 timer callback 或原生消息不再有推进操作的权限。
3. 仅清除当前匹配 probe 的 active byte，并通过独立 wake guard 释放一次引用。
   不停止协议、不拆除其他 TWT agreement、不主动断开 Station、不回收未完成 TX。
4. 保留失败的 handle 清理后缀，供同 identity 重试。已清除的 active/PM 不重复
   释放；原始 timer fault 保持可诊断，不因为后续删除成功而重置。

已正常完成或断连、没有 active/PM 的 probe 也可以清理遗留 handle。晚到的 timer
queue-post 错误可由上层读取 snapshot 后调用本入口清理；自动协调器仍待接入。

`cancel_error` 保存最近一次清理返回，`cancel_complete` 只表示本次原生取消
已完成。先于取消捕获的成功/失败事件保持不变；取消开始后的观察不替换结果，
仍按已有零等待方式发布。没有制造 SDK RF timeout 或 success 事件。

## 取消与完整退休的区别

SDK snapshot 新增 `probe_active` 和 `probe_phase`（不活跃时为 `UINT8_MAX`）。
TX snapshot 新增精确 `probe_buffer_present`，覆盖 output、callback 和 recycler
对 probe slot 的存活引用；不能用其他管理帧数量代替这个字段。

取消返回 ESP_OK 不表示 TX buffer、esp_timer callback、已排队原生消息或默认
event loop 已排空。原生 ioctl 不能等待 timer callback 退出：调用者可能持有
`wifi_api_lock` 等待 ioctl，而 timer callback 正在等待同一锁投递消息。既有
外部 timer/native marker、TX 观察和 event fence 仍须由后续 owner 联合协调，
并复核 operation identity/revision。此次不新增 `drained=true` 伪证明。

## 当前验证

合法 immutable Build Context 下 C5 roaming、C3、S3、C5 Wi-Fi disabled 均构建
通过且日志无 warning/error。C5 镜像 2,960,320 字节，较上一批增加 816 字节；
其余三个镜像尺寸不变。probe result 36→44 字节、TX 静态记录 32→36 字节，
合计新增 12 字节静态内存；timer 36、wake 8、懒分配 TX ledger 512 字节不变。
既有跟踪框架静态对象保持原尺寸，没有新 heap 分配。

实际 C5 ELF 已连接私有 ioctl → 精确 result 准入 → timer 取消 → node/PM
释放路径；对外内部调用函数本身尚无 Radio/Future caller，不能写成公开可调用。
证据记录为 `build/w08-twt-probe-cancel-evidence.json`。

manifest（52 classes / 510 functions）、feature 文档、Wi-Fi schema、严格 TS、
MQuickJS（61 sources / 59 snippets）及 whitespace 检查通过；没有公共契约变化。
生产 result、timer、SDK dispatch 和 TX fixture 已补身份、终态先后、迟到 callback、
删除/停止失败重试、node/phase 不匹配及存活字段用例；仅 AST 检查，未导入、编译
或运行 fixture。构建/静态证据不替代运行调度、RF 或实机验收。

完整联合退休、故障恢复、Radio/Future/公开 TWT、其他 Wi-Fi 剩余功能仍待完成。
所有 Wi-Fi API 完成后集中运行与实机测试；长 soak 留到 BLE API 完成后。
无刷写、串口、擦除、前端构建、提交、推送或父仓库 gitlink 更新。
