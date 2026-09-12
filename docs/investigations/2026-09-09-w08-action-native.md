# W-08 Action/ROC 原生终态记录与取消保护

firmware `d7db8d1` 工作区增量，固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接 Vendor IE 后推进 Action/ROC。本批是内部原生实现；尚未注册 `wifi.action`，
公共 options/results/Future/ROC Session 仍 contract-pending，不更新能力为已实现。

## 实际 SDK 行为

固定三目标的 `esp_wifi_action_tx_req` 和 `esp_wifi_remain_on_channel` 把 request
指针放入 24-byte ioctl 消息的 +20，由 wifi_action_tx_process/wifi_roc_process
在 SDK 执行点调用 offchan_action_tx_req/offchan_roc_req。没有独立用户 cookie。
事件 context 来自 request.rx_cb；SDK 自增 op_id 截断到 8 位，零也是合法值。
Action 保存 header+payload 的 SDK 副本；接口可能在返回前启动流程并发布事件。

Action 的 TX_DONE/TX_FAILED 只表示发送结果；成功流程还会有
TX_DURATION_COMPLETED。offchan_txop_end 的正常/取消分支分别发布 duration/cancel
终态，之后才释放 channel-manager lock/请求等原生清理后缀。因此收到 terminal
事件也不等于可以马上重用 SDK op_id、释放 driver 责任或重启。
ROC 的 done_cb 非空时不会发布 WIFI_EVENT_ROC_DONE；后续框架路径应保留 NULL
以使用集中控制事件，不另造一个混合 cookie 约定。

二进制审查还确认：三目标 cancel 分支只比较当前 channel-manager handler
是否为 offchan_txop_end/roc_op_end，再执行 chm_cancel_op；没有比较 op_id、
rx_cb/context、接口或请求信道。不能把框架 local token 当成 native cancel
身份保证，也不能在 JS task 先读 owner、稍后取消来避免 TOCTOU。

实际对象/各函数反汇编保存在 `build/w08-action-sdk/{esp32c3,esp32s3,esp32c5}/`。
这是 SDK 输入、控制流程和指令证据，不是实际 RF/调度运行结果。

## 生产原生记录

`esp32_mquickjs_wifi_action_lane` 由未来 Radio owner 串行访问，不保存 SDK 指针、
JS roots、runtime 指针或 heap payload。调用方只在 boot 初始化 next_identity=1；
reserve 发出 exact generation/identity，耗尽不回绕。release 不重置 identity 来源。

reserve 与 begin_submit 分离，尚未产生 driver side effect 的请求可直接退出。
begin_submit 后，失败或 timeout 不能直接释放。SDK 返回前最多保存四组不同的
op_id/status；重复组不消耗额外空间，溢出隔离为 ambiguous。submitted 只归入
返回 op_id 对应的记录，支持 SDK id=0。context/kind/channel/Action interface
不匹配的事件不影响当前请求；提交后不同 SDK id 也不污染当前状态。

发送结果与 native terminal 单独记录。终态后要求真实 driver 串行 fence，
再等待携带 exact token/revision 的 default event-loop marker。每个相关新事件
（包括重复终态）都会改变 revision 并使之前 fence 无效；矛盾终态、未知 status、
revision 耗尽只保留责任。物理 deinit 加 default event-loop drain 的真实证明
可以解除未知完成隔离；仅软件 timeout、Future 结束或 cancel 返回 OK 不行。

request_cancel 只记录用户意图；begin_cancel/cancelled 覆盖 SDK 执行窗口，
cancel 失败保存 raw error 并允许显式重试，成功后不重复提交，仍等待 native
终态和 fence。取消正在执行时禁止释放或提交物理终止证明。

## SDK 串行取消保护

`esp32_mquickjs_wifi_action_sdk` 提供专属 receive callback 作为唯一 context；
当前不暴露 Action RX stream，该 callback 不保存输入。LINKER --wrap 接入
wifi_action_tx_process/wifi_roc_process。只有 rx_cb 等于框架专属 callback 的
cancel 请求才检查 owner；其他 SDK 模块与普通 submit 仍调用原实现。

检查在 ioctl 原生执行点立即完成：比较 g_offchan_ctx 的 context、interface、
channel、secondary、op_id，再进入原始取消函数，避免跨 task 的先读后改窗口。
不匹配返回 ESP_ERR_INVALID_STATE，不提交取消；成功提交仍不代表 native 终止。
SDK 原函数保留 operation kind 检查。该 adapter 依赖已审查的私有 28-byte record：
context +4、interface +8、op_id +12、secondary +20、channel +2。
编译静态断言还检查 pointer width、Action 48-byte/ROC 36-byte request 和 callback/
op_id offset。固定三目标 archive SHA gate 复用已在 Wi-Fi Build 配置执行的
`patch_idf_vendor_ie_context.cmake`；变更 SDK 必须同时重新审查两处适配，不能只
更新 Vendor IE 的哈希。没有修改共享 SDK archive 或 immutable Build Context。

## 接入与未完成项

本批 CMake 编译两个生产 source，添加条件 linker wrap；当前没有 Action/ROC
公开调用，因此 C5 最终 ELF 的 section GC 可以移除尚未引用的 ledger/SDK shim。
编译通过不代表 wrapper 已经在设备调用链中生效。后续必须接入并检查最终 ELF
中 API wrapper 的 handler 地址解析到 __wrap 函数，不以 linker flag 自身作为证明。

下一步继续完成 Radio mode/法规/主副信道与共享 owner 准入、默认 event loop 的
控制终态路径、实际 driver/event fence、原生 request 捕获、Future/Session 和
runtime 关闭。timeout 后责任、native 内部模块共存与 cancellation 原生串行
保证仍需目标运行验证。未开放占位 API；正式类型/manifest 不增加草案名称。

`test_wifi_action_lane.py` 编写实际生产记录的早到事件、两阶段完成、SDK error、
cancel 后缀、旧 token、重复终态/fence、SDK id=0/255、容量/revision/identity
耗尽及物理终止用例。`test_wifi_action_sdk.py` 编写生产 wrappers 的 owner 各字段
不匹配时零 native delegate、匹配委托、非框架调用/普通提交透传。后者注入 SDK
record/real handler，Host pointer 不代表 target ABI；实际 layout 由 C5 编译断言
检查。所有 fixture 仅 AST，未导入/编译/执行。

C5 immutable Context 初次和最终 build 均 exit 0。binary 2,810,576 bytes，之前的
无线静态状态大小不变；本批未引用的 native helpers 由 linker GC 移除，没有新增
运行时 native 记录实例。manifest 仍 49 classes/474 functions；SDK map、两份
fixture 与一份 build script 的 AST、whitespace 检查通过。原生目标 object
的符号、源码/SDK SHA、产物和未运行项见 `build/w08-action-native-evidence.json`。
尚未构建 C3/S3/disabled 全固件、执行 Host/Python/VM 或实机测试；未刷写、串口
操作、擦 workspace、构建前端、提交、推送或更新根 gitlink。集中测试仍待 Wi-Fi
API 全部完成后，长 soak 保留到 BLE API 完成之后。
