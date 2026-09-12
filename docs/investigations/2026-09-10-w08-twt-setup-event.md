# W-08 iTWT setup 观察事件排空

本批补充内部结果记录的事件退休条件，未新增公开 Agreement API。基线为
firmware `d7db8d1` 的现有工作区和 SDK `fff9895c82`；接续 setup 精确取消。

## 实现与边界

- 结果记录新增 fence sequence，Radio 常驻事件处理器接收复制的
  `{identity, sequence}`。事件 ID 为内部 control base 的 5，不携带 JS、
  Future、runtime 或 owner 地址。每个存活 identity 的序号不回绕。
- 原生 owner 独立确认 TX/timer/native/HW 不再引用请求后，才可按精确
  revision 发布 fence。零等待发布期间固定结果记录；提前送达回调不能
  解除发布占用。发布失败撤销确认并返回原始错误，输出序号不变。
- 发布成功只表示已入队。释放同时要求当前 identity、revision、sequence、
  已成功发布及已观察状态，并且没有提交或发布仍在进行。
- 后续 setup 观察（包括相同 payload）撤销旧 fence。发布期间被撤销时返回
  `ESP_ERR_NOT_FINISHED`，调用方必须重新确认原生静止状态。旧序号、其他
  identity、重复回调和已释放 slot 的迟到回调均不能确认新 owner。
- 结果记录仍只是独立退休证明的消费者。取消成功、收到 setup 结果或 fence
  通过，都不能单独证明完整 SDK/RF/HW 退休。联合退休及 Agreement caller
  尚未接入；fence 发布/释放目前仍只有内部可调用实现。

不新增 pool。现有八槽结果记录因 64-bit 对齐由每项 56 B 变为 64 B，延迟
INTERNAL 分配上限从 448 B 增至 512 B（+64 B）。全局结果对象不增加，TX
ledger 768 B 和 timer ledger 192 B 不变。该数字是原生存储预算，不是实机
预热 heap/碎片测试结果。

## 验证

`test_wifi_twt_setup_result.py` 使用生产结果记录、原生事件路由及完整
`wifi_radio_lifecycle_fence` 函数，注入分配、锁和事件投递边界；覆盖精确
revision/identity、队列失败、提前回调、发布时新观察、旧事件撤销、重复及
跨 slot 回调、计数耗尽和释放占用。按用户安排，仅检查 Python AST，未导入、
编译或执行测试 fixture，不声称已动态复现或通过竞争验收。

生产构建、静态路由及生成物检查结果保存在
`build/w08-twt-setup-event-evidence.json`。Host/VM/RTOS、实机/RF、完整特性矩阵
和同等预热内存比较均未执行；长 soak 继续留到 BLE API 完成后。未刷写、
串口操作、擦除 workspace、构建前端、提交、推送或改变根仓库 gitlink。
