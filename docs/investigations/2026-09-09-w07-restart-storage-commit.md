# W-07 restart 存储策略最终提交

firmware `d7db8d1` 工作区增量，固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批继续检查 AP/APSTA activation 时发现并修正恢复顺序缺陷；公共 restart、AP
首选信道与实际 home 不同的完整恢复仍待完成，未缩减[完整范围](2026-09-08-wifi-api-remaining.md)。

## 已确认的生产顺序与 SDK 路径

旧 `wifi_radio_restart_configs_replay_locked()` 在停止状态完成 RAM 配置写入后，
通过末尾 RESTART_CONFIG_STORAGE 恢复原来的 FLASH 策略。随后真实 resume 才执行
最终 START、post-start channel/TX power 和最终配置验证。这使 SDK 启动过程中
隐含的配置写入重新具备持久化权限，不符合重放期间只修改 RAM 的设计。

重新从当前 SDK C5 `libnet80211.a` 提取并反汇编四个实际函数，保存于
`build/w07-restart-storage-sdk/`，object/archive hash 一并记录：

- `esp_wifi_set_storage` 在 SDK global lock 内写 g_ic+0x235，未调用 NVS flush。
  固定公共 enum FLASH=0、RAM=1。
- `wifi_softap_start` 在信道选择路径写入 channel，并读取相同的 storage 字节，
  将 storage==FLASH 传给 `wifi_nvs_set`；随后调用 `wifi_nvs_commit`。
- `wifi_nvs_set` 的 persistence 参数决定是否调用 NVS 写入。
- `wifi_nvs_commit` 检查 NVS 编译配置和同一 storage 字节，RAM 时直接返回成功。

这是已核对的调用顺序/二进制分支证据，足以说明旧代码过早授权持久化；不是实机
NVS 写入复现，也不能证明每个 START 都写出不同内容。C3/S3 的二进制函数体尚未
核对；最终验收仍需相应构建、故障注入与设备证据。未修改 SDK 或调用 private API。

## 实现

移除 pre-start replay 中恢复 storage 的阶段。原生 replay、最终 START、policy
恢复、channel/TX power 恢复与完整配置读回均保持 RAM。pre-start 新增明确的 RAM
检查，避免其他策略漂移时启动；post-start 同样要求 known RAM。

真实 resume 在释放 checkpoint、发布 staged owner 和退休 lifecycle token 前，调用
`wifi_radio_restart_configs_commit_locked()`：先执行原来的完整 verification；原值
为 RAM 时保留已接受策略；原值为 FLASH 时才调用一次 SDK setter 恢复后续写策略。
普通无 checkpoint 的 resume 保持原行为。没有增加 API、状态枚举、分配或常驻字段。

setter 前将 storage_configured 清除，成功才记录原策略并标为 known。失败记录原始
`restart-storage-commit` stage/error，保留冻结凭据和 lifecycle token，不交接 owner；
status 中 storage 为 null，后续直接重调该 helper 不重复未知结果的写入。物理清理/
重建继续走既有 lifecycle；没有伪造原值回滚、NVS 事务或 getter。原始 RAM 策略
无需再发一次无意义 setter，最终验证失败时也不恢复 FLASH。

本改动约束重建端 replay/START/验收的写入策略，不保证退出后 SDK 所有自主活动均
不写 NVS：成功返回 FLASH 恢复的是用户原本选择的后续写策略。旧驱动清理、AP
最终信道/secondary 协调和公共 restart 的 helper/netif 集成仍需独立核对。

## 验证和边界

C5 immutable Context `build/wireless-contexts/c5` 生产构建 exit 0，日志
`build/w07-restart-storage-commit-c5-build.txt`。binary 2,778,864 bytes，比前批增加
384。新增 commit helper 已链接；checkpoint 688 bytes、restart control 40 bytes，
九项既有静态账本不变。该比较不代表运行时 heap、碎片或 NVS 验收。

新增 deferred fixture 调用实际 capture/replay/pre-start/post-start/commit helpers，
SDK START 边界记录 AP 隐含保存所见的 storage。覆盖 STA/AP/APSTA、RAM/FLASH、
全 verification 调用及最后 setter 故障、unknown/不重复写、冻结快照、安全释放和
pre-start 策略漂移。原 checkpoint fixture 同步 replay 后仍为 RAM 的断言；真实
resume fixture 保留最后验证/commit 失败不发布 owner 的覆盖。三份仅 AST 解析，
未导入、编译或运行；不能将输入的 SDK 模型视作原生 NVS 证明。

manifest 49 classes/469 functions、features 27、live SDK config schema 35 STA/21 AP、
MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map 和 whitespace 检查通过。
源码、SDK object/archive、ELF 和仓库边界见 `build/w07-restart-storage-commit-evidence.json`。
Host/Python/VM/故障注入、C3/S3/feature-disabled、完整 restart、NVS/实机/RF/共存均
not-run，Wi-Fi 全 API 完成后集中执行；长 soak 留到 BLE API 完成后。未刷写、串口
操作、擦除 workspace、构建前端、提交、推送或更新父仓库 gitlink，不提升稳定等级。
