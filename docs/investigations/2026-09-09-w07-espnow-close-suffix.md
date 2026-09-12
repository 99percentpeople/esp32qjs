# W-07：ESP-NOW 关闭失败后缀与 Future 结果

firmware HEAD d7db8d1，SDK fff9895c82d744c7237be8847347bdd1b07c6643。
本批完成关闭路径的修复性编码，为共享 interval 恢复接入作准备；未执行运行测试，
interval 的旧值恢复与完整 Wi-Fi 总目标仍未完成。

## 原因与代码变更

原 finish_close 忽略 callback unregister、power-save 默认恢复和 esp_now_deinit
失败，清除标志并释放 Radio/session/queue；worker 忽略 finish_close=false 仍完成
Future，destroy 无论清理结果都 dispose queue、retire handle。同步 close_native
和后台 worker 还可能同时执行清理；重复 close 在 CLOSING 时可能直接返回无操作。
这些依据为当前生产源码审查，没有执行故障注入或声称实机复现。

现在 receive/send callback 的注销都进入串行清理后缀，成功后才清除注册标志。
先停止 TX worker，再注销并排空 callback，之后依次处理 wake window、interval、
deinit 和 Radio lease 释放。power phase 保留已经成功的前缀，失败只重试未完成
后缀；deinit 失败保留 now_initialized，Radio release 后 acquired 仍为 true 时
保留资源。直到所有 native 义务完成才 reset storage、清密钥、释放 queue retain
并发布 CLOSED。该批仍写旧行为的 default interval，尚未接入已知前值核心。

cleanup_scheduled 同时保护同步清理、后台清理和显式 Future worker，竞争者不能
清空别人的预留或覆盖 active_close。显式 close 在提交前记录关闭意图；队列满
保留 reaper 请求，不误报成功。Worker 仅在 TX 退出/成功注销后的 callback drain
等待；SDK 清理错误返回一次，不在原地循环 SDK 重试。reaper 后续重试保留后缀。

Worker 将实际 err/failed_step/result_bool 写入原 Future storage 后完成。finish
在清理失败时返回 ESPNOW_CLEANUP_PENDING，原始 SDK code 保留；destroy 结束 JS
roots，但关闭未完成时不 dispose queue/retire session handle，保留 native reaper
义务。CLOSING session 可再次 close；其他原有操作仍被生命周期 gate 拒绝。旧
handle 不可影响新 generation。已有超时完成和 native worker storage 退休机制
继续适用，未把超时当作 native termination。

## 待完成边界

- Interval 尚未换成同代已知前值恢复，仍需 Radio adapter/公开 setter 的共享排他。
- ESP-NOW power-save 配置部分失败和其他 recovery/deinit 路径仍需联合审查，不能用
  本批 close 修复冒充它们已完成。
- 真实 callback、任务、队列饱和、超时/GC/runtime teardown 与 reaper 容量的阶段验收。

## 延后测试准备

test_espnow_close_suffix.py 提取生产 begin/finish/worker/schedule/native close、control
finish/destroy；SDK、queue/free、锁调度和 JS 构造/错误边界注入。逐个失败测试回调
注销/window/interval/deinit/Radio release，验证没有提前 free/release、Future 报错、
JS root 可释放而 handle/native 仍保留，以及重试不重复成功前缀。另准备 callback
drain、已经预留 cleaner、队列满、后台 worker 与同步清理互斥。它不替代 moving GC
或真实调度测试。原 capture allocation fixture 同步新 helper/lease 类型与原子预留，
architecture 检查同步 receive unregister 的位置。三份 Python 仅 AST parse。

## 关闭后的代际交接

CLOSED 发布后、worker 清除 reservation 前存在一个很短的退休窗口。新 open 和
runtime init 现在同时要求 CLOSED、cleanup_scheduled=false、active_close=null、
旧 reaper 已退休，避免重置 singleton 后被旧 cleaner/reaper 触及。open 参数 getter
可执行 JS，故在解析结束、生成新 generation/重置 Session 前再次检查该条件；拒绝时
清除新 capture 的 PMK，不清理 getter 期间产生的另一条 Session。capture fixture
准备了在解析边界改变 reservation 的用例，仍未执行。

## 构建与契约检查

C5 immutable Build Context firmware-ci-esp32c5-representative 最终构建 exit 0；ELF
链接新的 close suffix、worker、native reservation、Future finish/destroy 与 reopen
gate。binary 0x29feb0 / 2,752,176 bytes，较前批增加 1,152 bytes，app 空余 13%。
当前 s_espnow_session=1,368 bytes；其余账本 s_radio=824、s_raw_tx=136、s_tx_rates=68、
s_tx_rate_lease=36、s_sessions=32、s_jobs=32、s_retired=56、s_lane=44 bytes。
未将这些静态符号大小当作动态 heap/stack 或资源释放实机证据。

MQuickJS 61 sources/53 snippets、manifest 49 classes/463 functions、strict TypeScript、
feature docs 27、config schema 35 STA/21 AP（live SDK）、recorded SDK map、三份 Python
AST 与 whitespace 通过。SDK 工作区干净；原始日志/符号/source hashes 在
build/w07-espnow-close-evidence.json。Host C/Python/VM/故障注入、C3/S3/disabled 构建、
真实 SDK callback/任务/队列/GC/runtime teardown、实机/RF/内存均 not-run。Wi-Fi API
全部完成后集中阶段和实机功能测试，长 soak 等 BLE API。未刷写、串口操作、擦
workspace、构建前端、提交、推送或更新父仓库 gitlink；完整 Wi-Fi 目标仍未完成。
