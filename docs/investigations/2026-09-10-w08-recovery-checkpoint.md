# W-08 Action/ROC 恢复配置 checkpoint

接续 [物理恢复阶段](2026-09-10-w08-action-physical-recovery.md)。本批补内部
`checkpoint_action_recovery`，尚未接 runtime/public 恢复协调器。firmware HEAD
`d7db8d1`、固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`，仍为 Candidate。

## 原生证据与设计依据

普通 restart 要求 native operation 和 leases 已退出；C5 单频来源还会通过临时
Station START/AUTO 捕获另一频段。这不能直接用于仍持有 Action/ROC 责任的恢复。
恢复也不能把 off-channel residency 的当前信道保存为最终 home channel。

固定 C5 archive 中，`esp_wifi_get_protocols` 的公开 wrapper 根据当前 band mode
选择 ioctl message 的 2g/5g 输出指针，隐藏频段的指针为空。它使用 24-byte 消息，
interface 位于 byte 8，两个输出指针位于 word 12/20。`wifi_get_protocols_process`
自身直接读取两频段保存的 PHY policy，并只写非空输出指针，不按当前 band mode
过滤。`wifi_get_bw_process` 以同样的布局读取两频段保存的 bandwidth。

已审查的两个 native handler 只读取 SDK 保存记录、转换协议值并写调用者 output，
没有 START、模式切换、RF 写入、异步保留或分配调用。证据在
`build/w08-recovery-phy-sdk/` 的原始 C5 object 和逐 section 反汇编。完整 C5
archive SHA-256 仍由既有构建 gate 固定；未修改 SDK 或 archive patch 内容。
该假设未扩展到未审查的 target/SDK。

## 实现

`esp32_mquickjs_wifi_action_sdk_saved_phy` 使用已有 Action ioctl 的私有 sentinel，
在真实 Wi-Fi task 执行处依次调用上述两个只读 handler。私有布局用 static assertions
固定；结果暂存于独立局部结构，两次读取均成功才交付。无效 interface/null output、
SoftAP disabled 在 SDK 调用前拒绝。其他 callback identity 不进入该私有路径。
SDK 公共 wrapper 的临时消息分配和 PM wake/done 仍存在，不声称查询没有成本。

C5 的内部恢复 capture 使用该完整快照；C3/S3 继续使用普通单频 getter。所有结果
沿用协议/带宽语义校验，隐藏字段无效时失败，不用默认值，也不启动 driver 修复它。
这是内部读取，不新增公开 getter 或 target capability 声明。

新内部 checkpoint 要求当前 exact recovery lifecycle、原 generation、健康可读的
STARTED driver 和匹配 mode。runtime 必须先停止 managed helper 的 scan/connect/AP
转换并释放其 Radio leases；仅本次精确 Action/ROC lease 可保留。无关 owner、dispatch/
cancel busy、wake/promiscuous 或借用设置仍拒绝。原生 Action owner 如果已经自然退休，
仍可在保留的 lifecycle 下保存零 owner 配置。

复用原有 policy snapshot 与安全凭据 checkpoint，保存 STA/AP 原配置、country、MAC、
省电、event mask、TX power、inactive history、TX rate、interval 和完整 PHY policy。
恢复专用 channel read 使用 SDK home channel 与保存的 band mode 推导 home band，
完整读取前后再次核对，拒绝 home 变化；当前临时驻留的 band/channel 不作为目标。
普通 restart 的读取、临时准备和准入路径保持原行为。

本阶段没有 STOP/START、band 写入、owner 发布或额外长期快照。读取/OOM/验证失败
复用 secure-zero/free；已完成的 checkpoint 不被后续重试覆盖。之后的物理 STOP/deinit
仍保留该 checkpoint，原 owner 退休后可交给已有 rebuild/replay 阶段。

## 检查

- immutable C5 context `build/wireless-contexts/c5` 编译 exit 0，日志
  最终 `build/w08-recovery-checkpoint-c5-build-final.txt`，初次构建也通过，本批无编译失败。
- 内部 checkpoint 入口已编译到 Radio object，但没有 runtime 调用者，因此最终
  ELF 裁剪该入口。saved PHY adapter、native reader 和共用 capture 分支已链接。
  不能把这些符号说成恢复 API 已端到端可用。
- C5 binary 2,834,608 → 2,835,552 bytes（+944）；此前记录的 28 个无线静态对象
  尺寸不变，s_action 92、s_config_restart 40 bytes。复用原 checkpoint allocation；
  未测量运行 heap、largest block、SDK task stack 或 RF。
- Manifest 50 classes/481 functions、feature docs 27、live SDK STA/AP schema 35/21、
  strict TypeScript、MQuickJS syntax 61 sources/55 snippets、SDK map 与 whitespace
  检查通过。快照及 hash 见 `build/w08-recovery-checkpoint-evidence.json`。

五份 fixtures 仅 AST 解析，未导入、编译或执行：

- 新 `test_wifi_saved_phy_sdk.py` 使用生产私有 dispatch/读取/结果提交函数，注入 SDK
  两个 reader 和 ioctl 队列边界，覆盖完整双频数据、任一读取失败后 output 不变、
  参数/SoftAP gate 和其他 callback 的原生转交。host 指针宽度不证明 C5 ABI。
- 新 `test_wifi_recovery_checkpoint.py` 使用真实 capture/验证/安全释放，分别注入
  已有 Radio recovery admission 和 SDK getter 边界；覆盖保留 operation owner、
  完整隐藏 PHY、home 而非驻留信道、逐读取失败/OOM、额外 owner 拒绝、home 变化、
  无效隐藏字段和冻结凭据不被重采集覆盖。
- `test_wifi_action_recovery.py` 补真实恢复 owner 资格 helper，覆盖 exact owner 与
  cancel-busy；其余 native admission/STOP/deinit 路径保留。
- 共用 restart config fixture 补恢复/SDK 边界，普通 restart 默认关闭该分支；原
  Action SDK fixture 同步纳入 AST 检查。

## 尚未完成

runtime/public 协调器还须连接 helper quiesce、checkpoint、STOP、helper/netif 退休、
deinit、原 Action/ROC owner 退休、rebuild/replay 及最终交接。已故障而无法可信读取
配置的来源、隐式/隐藏配置知识缺口及 Raw TX 恢复仍待处理，不能以本批健康可读来源
覆盖它们。完整 Future/worker/SDK/Radio 集成和错误重试验收仍未执行。

全部 Wi-Fi API 后统一 Host/Python/VM、C3/S3/feature-disabled 构建及实机测试；长时间
soak 依用户安排留到 BLE API 完成后。未刷写、操作串口、擦 workspace、构建前端、提交、
推送或更新根 gitlink。
