# W-07 停机后的事件 mask 与恢复

firmware `d7db8d1` 工作区增量，固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接 [storage/RSSI](2026-09-09-w07-stopped-controls.md)。新增第三个经过审查的
STOP-neutral writer；原记录的 27 项 inventory 保留，当前 3 项中性、24 项尝试即失效。

## SDK 证据与边界

重新提取三目标 libnet80211.a 的 ieee80211_api.o / ieee80211_ioctl.o。
C3/S3/C5 的 esp_wifi_set_event_mask 均先检查 init，分配 24-byte 消息，使用
id 33、offset 12 的 mask 与 wifi_set_event_mask handler，经 ioctl 提交。
实际 writer 只读取该 uint32，写入 g_wifi_event_mask，然后返回成功；未调用
RF/config setter。该写入不改变 STOP snapshot 的功率、band/channel 或 inactive
观察。源码库/object/反汇编 hashes 在 `build/w07-stopped-mask-sdk/evidence.json`。
这是固定二进制的静态审查，不证明 RF、事件交付或运行并发行为。

## 实现

Radio-local mutation header 将 esp_wifi_set_event_mask 改用既有中性宏，仍限定
已审查 C3/S3/C5；其他目标保留原先的失效行为。参数/错误/锁与 setter 事务不变。
restart 原有 checkpoint 捕获最新 mask，重建中重放并核对，无新增账本或 API。

公开 setter 仍只允许 probe-request 观察 bit，拒绝屏蔽内部完成事件。读取失败不
写入；写入或 readback 失败后，只有安全前值才允许 rollback，且必须再次读回核对。
回滚成功保留历史；不确定回滚仍设置 Radio fault/cleanup，禁止 restart。保留
candidate bit 不清故障，不创建缺失记录，不恢复其他 writer 作废的历史，也不绕过
owner、generation、STOP identity 或 helper 退休条件。

## 验证状态

扩展 test_wifi_stopped_controls.py，连接实际 setter、宏、STOP 谓词、registry
准入和完整配置 capture/replay/pre-/post-start/commit；补写 0/1 新值、每阶段失败、
rollback 写入/读回失败、不安全前值、保护 bit、同值、不存在/已失效历史和未审查
目标用例。SDK/锁/物理重建与 START 为注入边界。test_wifi_stop_snapshot.py 的
精确中性 inventory 同步。两份 fixture 仅 AST 检查，未导入、编译或执行。

C5 immutable Context 构建 exit 0，binary 2,797,824 bytes（前批 2,797,840），12 项静态账本大小不变。
manifest 49 classes/470 functions、features 27、SDK schema 35 STA/21 AP、MQuickJS
61 sources/54 snippets、strict TypeScript、SDK map、AST 与 whitespace 检查通过。
构建和静态检查结果记录在 `build/w07-stopped-mask-evidence.json`。
Host/Python/VM/竞争、C3/S3/feature-disabled 构建、实机/RF 完整生命周期均 not-run；
按安排留到全部 Wi-Fi API 完成后，长 soak 留到 BLE API 完成后。
未刷写、串口操作、擦 workspace、构建前端、提交、推送或更新根 gitlink。
其他 STOP 修改、未知来源与完整故障恢复仍未完成，Candidate 稳定等级不变。
