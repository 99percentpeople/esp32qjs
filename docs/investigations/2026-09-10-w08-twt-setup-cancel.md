# W-08 iTWT setup 精确取消

基线 firmware `d7db8d1`、SDK `fff9895c82`，接续
[setup TX 回调隔离](2026-09-10-w08-twt-setup-tx.md)。本批接入内部 setup 取消
命令及保留 handle 的清理后缀，没有新增公开 Agreement API；完整 teardown、
联合退休、RF 关联和阶段运行仍待完成。

## 原生边界

从当前固定 SDK archive 重新核对 `he_recv_action_twt_setup`、response timeout
及 dwell handler。有效 AP 响应先删除 response timer，再更新同一 pending
记录为 phase 2 并安装 dwell timer；真正的 established ID/flow、PM 和硬件
配置在 dwell native handler 内发生。因此取消 pending 不能等同 teardown
已经建立的 Agreement，也不能通过断开 Station 来代替精确取消。

审查另确认异常 AP 参数分支（原 RX handler 的 `0x172` 分支）会置 flow bitmap
后发布错误，可能没有对应 established ID。取消遇到这类归属不完整状态返回
`ESP_ERR_INVALID_STATE`；不擦除 bitmap，不声称 runtime restart 能恢复。
该异常分支的完整物理恢复继续保留在 W-08，不将静态审查写成动态复现通过。

## 请求、flow 和 timer 的精确清理

worker 入口复用既有私有 Action ioctl，由精确 sentinel type、Station interface
和 framework callback 识别；不作为 RF Action 发送，不改变信道或共享省电设置。
原生命令按结果 registry identity 读取不可复用 request ID，拒绝旧身份和仍处于
SUBMITTING 的记录。结果记录的 release 同样要求 native owner 在队列上持有
独立退休证明；本命令不会释放结果或 JS/Future owner。

在任何清理前核对：

- 已有匹配 established ID 返回 `ESP_ERR_NOT_FINISHED`，交给后续 Agreement teardown。
- 本请求只能对应一个合法 phase 0/1/2 pending 槽。
- pending、temporary ID 和已观察 AP 配置相关 flow 不得与其他请求或 established
  bitmap 冲突；AP-selected flow 与最初请求 flow 分开处理。
- timer ledger 必须拥有对应 request 的 handle。外来 handle/active 槽不被清理；
  本请求已无 pending bit 的保留 handle 仍参与清理。

先撤销本请求所有匹配数字 timer 的 active 权限，再在 critical section 外
stop/delete。过期 timer 的 `ESP_ERR_INVALID_STATE` stop 返回视为已经停止；
其他原始 stop/delete 错误保留。stop 成功而 delete 失败时记录该步骤，下一次
仅重试 delete；原 handle 未删除前不清除指针、不重用。

所有 handle 清理成功后才清除匹配 pending bit/phase 和等于本 request ID 的
临时 ID。任何清理失败都保留 pending，允许按同一 identity 重试；其他请求
不因本请求的取消而减少计数或失去临时 ID。首次 timer fault 继续可诊断，
本次重试返回值独立反映清理结果。

取消不发送伪造 setup event，不把已结束 Public Future 当成原生操作结束。
返回 ESP_OK 仅为上述取消确认，尚不证明 TX 回收、已在 ESP_TIMER_TASK/native
queue 的任务、default event loop、RF late RX 或硬件完全退休。后续调用方必须
继续完成联合退休；公开 setup/Agreement caller 尚未接入。

## 生产构建与预算

五份 immutable Build Context 生产构建通过：

| Context | 镜像字节 | 相对 TX 隔离批次 |
| --- | ---: | ---: |
| c5-roaming | 2,979,440 | +1,472 |
| c5-no-softap | 2,855,840 | +1,472 |
| c5-disabled | 459,024 | 0 |
| c3 | 2,665,696 | 0 |
| s3 | 2,570,768 | 0 |

命令为 SDK export 后的
`.venv/bin/python scripts/remote.py --build-context build/wireless-contexts/<context> --build-dir wireless-<context> --assume y build`。
实际 C5 ELF 中私有 Action native dispatch 已连接 setup cancel、结果读取和
stop/delete helper；worker 取消入口目前只在 archive，等待后续公开 caller。
setup submit/Agreement 仍不因内部 native 命令已链接而成为可调用 JS API。

timer entry 将同锁管理的三个 bool 压入现有空间，编译断言及分配点确认仍为
24 B × 8 = 192 B；共用 TX ledger 仍为 768 B。本批没有新增 static/heap 预算，
既有 40 个跟踪静态对象尺寸不变。原始 SDK archive 与现有 build-local patch
保持不变；共享 SDK 干净。预算不替代预热后实机 heap/最大块/owner/pool 比较。

manifest 52 classes/513 functions、feature 文档 27、schema STA35/AP21、严格
TypeScript、MQuickJS 61 sources/60 snippets、Wi-Fi coverage 和 whitespace
检查通过。证据在 `build/w08-twt-setup-cancel-evidence.json`、summary、构建日志
和 SDK/ELF disassembly。

## 后置验收

更新生产 SDK/native dispatch 与 setup timer fixture：旧 identity、SUBMITTING、
其他 owner、重复 pending/flow、已建立 Agreement、异常 AP bitmap、dwell、原始
清理失败、成功后缀重试、孤立 handle、提前排队 timer 及错误 interface/callback。
SDK fixture 的 timer/result 是注入边界；timer fixture 调用真正的取消 helper，
只注入 esp_timer 和当前 SDK identity。

fixture 仅 AST，未 import、编译或执行；RTOS 竞争、实际 close/reopen、GC、队列
饱和、runtime restart、双端 RF 和完整生命周期全部 `not-run`。所有 Wi-Fi API
完成后集中运行与实机验证；长 soak 留到 BLE API 完成后。未串口/刷写/擦除
workspace、构建前端、提交、推送或更新父仓库 gitlink。
