# W-08 iTWT teardown TX 身份与 PM 引用

本批连接单 flow teardown 提交、实际 TX 账本和 callback 18。它是完整
Agreement 生命周期的内部基础；没有新增公开 JS 方法，也没有完成 Agreement
退休、信息定时器、bTWT、物理恢复或 RF 验收。

## 实现边界

- 原生提交前保留唯一 teardown scope，绑定结果 identity、关联 node、flow 和
  Wi-Fi task；失败保留原始 driver error，不能盲目重试或复用仍存活的 scope。
- output 在现有 TX ledger 上标记精确 identity。完成、output 返回及 recycler
  通知均核对身份；同地址的新分配不能被当成旧请求。沿用 64 × 12 B 的延迟
  INTERNAL 账本，不新增 TX pool。
- callback 在原生调用前固定自身状态，并核对当前关联和 established request。
  重复或迟到的托管 callback 不再修改当前 Agreement。原生 callback 返回后
  不读取可能已经回收的 EB。非托管 TX 保持原生分支。
- 仅重定向固定 C5 SDK 中两个已核对的 PM 调用点：
  `ieee80211_itwt_teardown + 0x58` 的 wake-up，以及
  `he_twt_teardown_txcb + 0x28` 的 wake-done。其他 PM 调用（包括 IRAM 路径）
  保持 SDK 原目标；共享 ESP-IDF 不修改，修补仅落在 Build Context 产物。
- PM 权限先在原生状态撤销，再于锁外调用 SDK。管理帧分配失败在提交返回后
  归还本次引用；无 callback 的回收在 Wi-Fi 原生任务检查 quiescence 时归还。
  通用 recycler 只更新 INTERNAL 原生状态，不调用 PM、不分配内存。

## 释放与尚缺证明

TX quiescence 只说明本次 output/recycler 已返回及本次 PM 引用已归还。
release 还要求调用方已经完成 timer/native/event 顺序证明，并提交同一 revision；
本 helper 自身不提供这套联合证明。未知旧 callback 在完整释放后不能再出现，
这一条件仍须由完整 teardown 退休执行器建立。pending setup 退休继续拒绝
teardown-attempted 结果，不能绕过该门槛释放记录。

失败后保留诊断与存储；不声称 runtime restart 可以恢复全部物理故障。

## 验证范围

`test_wifi_twt_teardown_tx.py` 组合生产 teardown scope 与共享 TX ledger，
仅替换 SDK/分配/调度边界，覆盖提前 callback、重复完成、回收后地址复用、
无 callback 的 PM 清理及分配/driver 错误。fixture 中的 release 假定外部顺序
证明已经提供，不能作为完整 timer/event/RF 退休证据。

按既定顺序，本批 fixture 只做 AST 检查，不导入、编译或运行。目标构建、实际
ELF 调用与 IRAM 路径检查的最终结果记录在
`build/w08-twt-teardown-tx-evidence.json`，本批静态检查已通过：

| Build Context | binary bytes | 相对 teardown-submit |
| --- | ---: | ---: |
| C5 roaming | 2,986,816 | +4,512 |
| C5 no-SoftAP | 2,863,216 | +4,512 |
| C5 Wi-Fi-disabled | 459,024 | 0 |
| C3 | 2,665,696 | 0 |
| S3 | 2,570,768 | 0 |

C5 的新增 scope 为 44 B 静态 INTERNAL；TX/setup timer/result 延迟账本分别
保持 768/192/576 B。已核对 recycler、新 hook 及其状态 helper 位于 IRAM，
原始 recycler 仍指向 ROM trampoline `0x40000bfc`；两个 C5 context 的修补
archive hash 相同，共享 SDK 未修改。生成 manifest（全项目 52 classes /
513 functions）、27 feature docs、1,267 项 SDK map、配置 schema、严格类型、
MQuickJS 61 sources / 60 snippets 与 9 份 fixture AST 均通过。

所有阶段运行、硬件与 RF 项目均为 `not-run`。不刷写、不改设备 workspace、
不提交、不推送、不更新父仓库 gitlink。
