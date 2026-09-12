# W-08 单 flow teardown 本地联合回收

本批扩展现有 setup retirement 执行器，串联成功 single-flow teardown 的
本地资源、TASK/native/event 顺序和结果/TX 两步释放。没有另建一套退休状态机，
没有新增公开 Agreement API；信息 TX 身份、RF 迟到帧、bTWT 和物理恢复仍待完成。

## 准入与清理

teardown-attempted 记录只有在原生成功事件已保存、没有歧义且 submitting/
observation 已结束后才能走这条路径。仍有 established/pending/temporary ID、
本 flow 的 bitmap 或冲突 owner 时不释放。失败 TX 不伪装成成功关闭，也不
自动重新提交 teardown。

原生 cleanup 重试本请求的 setup timer 和 information timer 清理后缀。
information timer 在修改前核对所有受影响 flow：若 all-flow timer 仍包含
其他未退休 flow，返回 `NOT_FINISHED`，不取消其他 Agreement 的恢复定时器。
TX 已回收且本次 PM 引用已归还后才标记本地 cancellation 完成。

## 联合顺序与两步释放

cut 从 TX/setup timer 两个 revision 扩为 TX/setup timer/information timer/
teardown scope 四个 revision。执行器沿用 TASK timer → native queue → copied
identity/sequence event fence；任意 cut 变化撤销旧 marker，重新建立顺序。
原生 result release 再次核对四个 revision 与已交付的事件序号。

结果记录首先释放，原生 TX scope 继续阻止托管 teardown 复用。稳定 owner
记录 `result_released`，随后按原 cut 释放 TX scope。若该步骤失败，下一次
仅重试 TX 释放，不读取或再次释放已移除的结果。Radio lease 和 owner storage
必须保持到全部完成；最终 marker 清理失败也保留未完成后缀。

该执行器不会自行提交 teardown、获取 Radio 或处理 RF 协议歧义。公开 Agreement
调用方尚未接入，不能以本地联合回收代码替代完整 TWT API 完成。

## 验证范围

新增/扩展用例调用生产 SDK 准入与 cut 查询、生产 result/event fence/retirement
执行器及生产 information timer 清理；外部 SDK/调度边界注入错误。覆盖四类
revision 变化、共享 timer 拒绝、结果释放后 TX 失败及只重试剩余后缀。
本批 fixture 只做 AST 检查，未导入、编译或运行。

目标构建、实际原生路由及 archive 中执行器的检查结果写入
`build/w08-twt-teardown-retire-evidence.json`，本批构建/静态检查通过：

| Build Context | binary bytes | 相对 information-timer |
| --- | ---: | ---: |
| C5 roaming | 2,990,992 | +832 |
| C5 no-SoftAP | 2,867,392 | +832 |
| C5 Wi-Fi-disabled | 459,024 | 0 |
| C3 | 2,665,696 | 0 |
| S3 | 2,570,768 | 0 |

无新增常驻状态或 pool；cut 在调用方稳定存储中增加 8 B。最终 C5 ELF 的
SDK cleanup/quiescence/release 路由已核对，information cleanup 和查询已链接。
扩展的退休执行器仍在 archive，尚未被公开 Radio/Agreement caller 使用；
不能将本表解读为公开 teardown 已可调用。

manifest（全项目 52 classes / 513 functions）、27 feature docs、配置 schema、
1,267 项 SDK map、严格类型、MQuickJS 61 sources / 60 snippets 与 3 份 fixture
AST 通过。阶段运行、实机/RF 仍为 `not-run`，不刷写、不提交、不更新父仓库 gitlink。
