# W-08 pending iTWT setup 联合释放

本批接续 setup cancel/event fence，将未建立 Agreement 的 setup 关闭步骤串成
生产执行器；基线为 firmware `d7db8d1` 的工作区及固定 SDK `fff9895c82`。
这不提供公开 Agreement API，也不能替代已建立 Agreement 的 teardown。

## 原生条件与执行顺序

1. 按结果 identity 取消 pending。完成后把涉及的 flow mask 保存在同一结果
   记录中，再标记 cancelled；清空 pending/temporary ID 后不会丢失这份证据。
   后续原生 setup 观察撤销 cancelled 和事件确认，原 flow mask 保留。
2. 原生 Wi-Fi 队列检查本请求没有 established/temporary/pending ID，涉及的
   flow 没有 established/suspend/resume bitmap、information timer 或其他
   pending owner 冲突。已建立或歧义状态仍保留，不强行清理硬件位图。
3. 检查 TWT TX 账本没有在途 buffer/output/recycler/probe call，且没有失效
   账本；检查本请求 setup timer 没有 active authority、保留 handle 或未完成
   cleanup。返回 TX revision 和 timer revision，只是当前静止快照。
4. 复用现有 TASK timer marker，确认 callback 退出、删除 timer，再排空原生
   ioctl 队列；随后发布复制 identity/sequence 的 setup 事件屏障。
5. 再次进入原生队列核对相同 TX/timer revision、当前 cancelled 状态与精确
   事件确认，才释放结果记录。执行器随后清理自己的 marker 存储。

执行器持有精确 owner token/generation，失败保留尚未完成的步骤。已完成的
取消不重复执行；marker stop/delete/native-fence 失败只重试未完成后缀。
TX/timer 修订变化重新建立顺序点；新 setup 观察要求重新取消/判断是否已经
建立 Agreement。公共 timeout、GC 或 runtime 退出不能提前丢弃这份关闭记录。

静止查询对整个 TWT TX 账本保守等待，不回收其他请求的 buffer。历史 timer
错误保持诊断，但本请求 handle 已删除、authority 已撤销后不单凭历史错误
阻止该请求退休。计数耗尽和无法确认的物理状态仍拒绝释放。

## 接入与资源边界

- 内部 Action ioctl 增加 setup quiescent/release 分派，仍要求 Station 和
  项目专用 callback。原生校验及结果释放已随分派进入 C5 链接路径。
- `setup_retire_poll`、worker 提交及完整 Radio/Agreement caller 仍待公开
  生命周期接入；不能将 archive 中执行器当作已可调用的 JS 能力。
- 结果记录利用现有 padding 保存 flow mask，仍为 8 × 64 B；既有 timer/TX
  账本不增大。本批不新增全局 pool。后续 owner 必须预算独立关闭记录和
  marker 的 SDK timer；完整资源预算、实机预热 heap/碎片验收仍待完成。
- RF dialog 重用/迟到 RX、bTWT、information/suspend timer 身份，以及完整
  Agreement/物理故障恢复仍在 W-08，未从总体范围删除。

## 验证记录

延后执行用例调用生产 timer/result、SDK 校验及完整退休执行器，覆盖本请求
与其他请求、取消失败、已建立/歧义位图、在途 TX、timer 清理后缀、变化的
revision、提前 timer callback 尚未退出、队列失败、迟到观察撤销及重复释放。
用例仅 AST 检查，未导入、编译或运行。

构建、链接与生成物结果保存在 `build/w08-twt-setup-retire-evidence.json`。
Host/VM/RTOS、RF/实机、完整 feature matrix、预热 internal/PSRAM/largest-block
对比未执行。所有 Wi-Fi API 完成后集中验证；长 soak 仍在 BLE API 完成后。
未刷写、操作串口、擦除 workspace、构建前端、提交、推送或更新根 gitlink。
