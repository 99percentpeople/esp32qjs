# W-08 恢复时的网络接口退休与中央清理

接续[恢复 checkpoint](2026-09-10-w08-recovery-checkpoint.md)。本批补内部 helper
退休与中央失败清理，未增加公开 API；runtime 恢复准入与成功重建/重放协调仍未完成。
firmware HEAD `d7db8d1`、固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。

## 代码依据与实现

普通 `check_stopped_lifecycle` 要求零 operation/lease。Action/ROC 物理恢复则必须
一直持有原 operation/lease 到成功 deinit，并由原 owner 消费终止证明后退休。
若直接调用普通 AP/Station 退休入口，会在 deinit 前被零 owner 条件拒绝。这是代码
路径审查结果，尚未用运行测试复现；不可写成实机根因或测试已通过。

新增内部 `check_stopped_action_recovery`，在 Radio operation mutex 内验证本次精确
recovery lifecycle、已完成 STOP/event fence、无 wake/promiscuous/reboot-required
状态。唯一允许存活的 lease 必须仍匹配实际 Action/ROC token，且不在 dispatch/cancel
调用中；其他 owner 拒绝。允许已产生 physical termination 而原 owner 仍未退休的
后缀重试，也允许原 owner 退休后的零 owner 状态。

该检查只授权 runtime task 退休 helper；不授权配置写入、创建 helper 或 START。
检查退出 mutex 后执行真实网络接口退休，由保留的 lifecycle 阻止新的 Radio owner。
AP 普通与恢复入口复用原来的 netif 退休主体，保留 coordinator 精确匹配、失败状态和
成功后清除 coordinator 的行为。Station 恢复入口仍要求无 managed lease、无连接/
扫描/Future 活动，然后使用原 helper 清理。SoftAP-disabled 的 AP 入口只做 Radio
恢复资格检查，未伪造网络接口。

中央 `wifi_finish_configuration_cleanup` 读取恢复身份，选择恢复 STOP、AP/Station
退休及物理 shutdown。成功 deinit 但原 owner 尚未退出时，保持 lifecycle/checkpoint
和 `configuration-action-shutdown` 错误阶段。重试不重复已完成的网络接口退休、
已接受的 STOP 或成功 deinit；最后仍通过原 `finish_lifecycle` 结束。普通 stop/config
走既有普通分支。这里接入的是失败后清理，并未提供主动恢复准入或成功恢复路径。

## 检查与证据

- 首次 C5 build exit 2：新 AP include 被放到 `cutils.h` 前，引起 IDF/MQuickJS 的
  likely/unlikely 宏重定义。恢复既有 include 顺序后，immutable C5 context
  `build/wireless-contexts/c5` build exit 0；原失败日志与最终日志均保留在
  `build/w08-recovery-helper-c5-build{,-final}.txt`。
- ELF 已链接恢复 STOP/shutdown、资格检查及两个 helper 退休入口。恢复 admission/
  checkpoint 在 Radio object 中编译，但仍因无调用者被最终 ELF 裁剪。
- C5 binary 2,835,552 → 2,836,288 bytes（+736）；此前跟踪的 28 个无线静态对象
  尺寸均不变。该比较不代表运行时 heap、largest block 或 SDK stack 验证。
- Manifest 50 classes/481 functions、feature docs 27、live SDK schema STA/AP 35/21、
  strict TypeScript、MQuickJS 61 sources/55 snippets、SDK map、whitespace 检查通过。
  快照/hash：`build/w08-recovery-helper-evidence.json`。

三份 fixtures 仅 AST 解析，未导入、编译或执行：

- `test_wifi_action_recovery.py` 组合真实 Radio 恢复资格函数，补 STOP 前拒绝、旧
  lifecycle 拒绝、无关 lease/cancel busy 拒绝、STOP 后及 deinit owner-drain 重试。
- `test_wifi_configuration_cleanup.py` 提取真实 AP 退休主体、恢复入口、Station 退休
  入口和中央清理。注入 Radio 物理阶段/owner 消费边界，补 AP 退休失败后重试、
  shutdown 等待期间保留生命周期与最终仅重试未完成后缀。
- 依赖同一提取主体的 `test_wifi_restart_runtime.py` 纳入 AST 检查；普通场景的
  recovery boundary 默认关闭。未把边界注入当作 SDK/RTOS 集成证据。

## 剩余工作

runtime/AP 主动准入、managed scan/connect 排空、checkpoint、成功重建/配置重放、
公开恢复 API 和错误语义仍需连接。不可读/已故障来源、Raw TX 恢复、高级 Wi-Fi 模块
及完整预算/诊断仍按[剩余清单](2026-09-08-wifi-api-remaining.md)保留。

所有新功能保持 Candidate/待验证。Host/Python/VM、C3/S3/feature-disabled matrix、
实机功能/RF 验收留到 Wi-Fi API 全部实现后；长时间 soak 留到 BLE API 完成后。
本批未刷写、操作串口、擦除 workspace、构建前端、提交、推送或更新根 gitlink。
