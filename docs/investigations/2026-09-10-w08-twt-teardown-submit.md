# W-08 iTWT teardown 提交与控制观察

基线为 firmware `d7db8d1` 的现有工作区及固定 SDK `fff9895c82`。本批增加
内部单 flow teardown 提交与结果捕获，未注册公开 Agreement API；TX 身份、
PM 释放及完整 teardown 退休仍是公开接入前提。

## SDK 核对与实施

- 重新从原始 C5 archive 核对 `ieee80211_twt.o`、`ieee80211_ioctl.o`、
  `ieee80211_api.o`，并读取 `libpp.a` 的 `pm_twt.o`。SDK public teardown
  使用 operation 111 和 24-byte message，native handler 只读 flow byte 8。
- 原生提交前检查当前 Station/node、关联状态、精确 setup result identity、
  established request ID 和 flow bitmap；拒绝 temporary/pending 冲突、同 ID
  多槽及其他记录仍占用同 flow 的 teardown。只操作 0..7 的单个 flow，不隐式
  使用 FLOW_ID_ALL。所有 driver 调用都在现有 Wi-Fi ioctl 队列且不持临界区。
- 原始 SDK native handler 继续负责管理帧、PMF 和异步 TX；本批保存其原始
  提交错误。结果记录在调用前标记 attempted/submitting，回调可早于提交
  返回。无论 SDK 返回成功还是错误，均不能自动重试同一次 teardown。
- SDK `he_twt_teardown_post_event` 发布事件 29。路由现先保存原生结果再用
  `esp_event_post(..., 0)` 发布观察；队列饱和不阻塞原生 callback 的后续工作。
  setup 提交错误、teardown 提交错误及两类观察错误分别保存。只复制定义
  字段，首个 teardown status 保留；冲突观察标记 ambiguous。
- teardown 事件只有 flow，没有 cookie。只关联已存在的 attempted 记录，
  多个候选不能选择一个冒充精确 owner；未知 flow/全 flow 事件仍可发布观察，
  不替其创建身份。后续事件撤销旧 cancelled/fence 证明。

## 尚未完成的关闭条件

`ieee80211_itwt_teardown` 在发送前调用 `pm_twt_wake_up`；TX callback 首先
调用 `pm_twt_wake_done`，随后才按 TX 状态清理 flow、ID、information timer
和 PM 状态。`pm_twt.o` 的这两个方法分别调用 `pm_wake_up/done` 并设置/清除
同一 TWT awake 标记。SDK 提交成功、flow bitmap 消失或一个观察事件都不足
以证明这些资源已退休。

本批保留这些 SDK 副作用，没有宣称解决晚到 TX callback、分配/输出失败后
的 PM 账目或 information timer 清理失败。发生 teardown 的记录不能通过
pending-setup 的 quiescent/release 路径退休；该路径明确返回 NOT_FINISHED。
后续须补 TX/PM 精确身份和完整关闭/重试证明，再接 Radio/Future/Agreement。
本批不是完整 teardown 功能验收。

八槽结果记录从每项 64 B 增至 72 B，延迟 INTERNAL 预算由 512 B 变为
576 B（+64 B）；无新增 pool。全局结果对象及既有 TX/timer 账本尺寸不变。
相关 flags 扩为 16 bit，所有更新同步保留高位，避免 setup 观察清除 teardown
状态。预热 heap、largest-block 和完整资源预算验收尚未执行。

## 验证

生产实现用例已补单 flow/请求冲突、关联丢失、提交错误、重复尝试拒绝、早到
完成、队列满、错误分离、未知/全 flow 观察和高位 flags 保留；只做 AST，
未导入、编译或运行 fixture。SDK/ELF/构建记录与哈希保存在
`build/w08-twt-teardown-submit-evidence.json`。

Host/VM/RTOS、RF/实机、完整 feature matrix 和预热内存比较继续后置到 Wi-Fi
API 完成后，长 soak 在 BLE API 完成后。未刷写、操作串口、擦除 workspace、
构建前端、提交、推送或改变根仓库 gitlink。
