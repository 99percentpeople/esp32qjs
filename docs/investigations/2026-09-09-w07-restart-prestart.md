# W-07 restart 启动前配置核对

firmware `d7db8d1` 工作区增量；SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接 [band/channel 恢复](2026-09-09-w07-band-restore.md)。本批改进内部恢复路径，
没有注册公开 restart，也没有将 AP/APSTA 完整 activation 写成完成。

## 源码发现

此前 replay 按步骤读回 Station/AP 配置，随后执行 PHY/频段准备、临时 START/STOP、
原模式与策略恢复。resume 在最终 START 后才再次核对完整配置和实际 home channel。
如果这段准备流程结束时配置已与冻结值不一致，或 AP 首选信道本就不同于先前跟随
Station 的实际信道，最终检查发生在 AP 可能开始广播之后。

这是生产调用顺序和固定 SDK `wifi_softap_start` 信道选择路径的静态证据；不是
实机复现，也不声称临时 band cycle 必然修改凭据。SDK AP 启动反汇编证据仍见
`build/w07-storage-sdk-wifi_softap_start.txt`。框架 mutex 和尚未发布 JS owner 都
不能证明 START 之后没有原生客户端关联，因此不在该位置强制 set_channel。

## 已修改

新增生产 `wifi_radio_restart_configs_pre_start_locked()`，由真实 resume 路径在
模式核对后、首次 staged lease acquisition 和 START 之前调用。没有 restart
checkpoint 的普通 start/resume 路径保持原有事务。存在 checkpoint 时要求精确
lifecycle identity/generation、当前物理 replay generation、重放完成、完全停止、
零 owner、无 operation/wake/promiscuous/fault/cleanup/restart-required。

逐项读取已捕获的 Station/AP SDK config，复用完整语义比较和 secure-zero scratch，
包括 inactive interface。SDK 错误或配置不一致保留冻结凭据和 token，记录原始
`restart-station-pre-start-readback` / `restart-ap-pre-start-readback` stage/error，
不分配 owner identity、不执行 START、不发布 owner。此前的恢复写入已经发生，
因此 fault 仍按整个 restart 的 mutation 记账；不会自动重复不确定的重放。

AP/APSTA 另检查冻结 AP 配置主信道与冻结实际主信道相同且非零。不匹配或为
SDK 自动选择值时提前报 `restart-ap-channel-admission`，继续等待完整 activation
协调。该检查只是必要条件，不能证明 SDK 最终选择、secondary channel 或 RF
状态；原有 START 后 band/current/home 精确检查和最终配置校验全部保留。

这不解决原 AP 首选信道与旧实际信道不同的完整恢复，也不放宽独占或安全配置。
未新增常驻字段、heap 分配、公开类型或 callable；完整 Wi-Fi 范围仍然开放。

## 验证范围

C5 immutable Context `build/wireless-contexts/c5`（8 MB/no PSRAM）生产构建 exit 0，
日志 `build/w07-restart-prestart-c5-build.txt`。binary 2,767,936 bytes，比前批增加
512。静态大小和链接证据记在 `build/w07-restart-prestart-evidence.json`，不作为
运行时内存或 RF 验收。

deferred `test_wifi_restart_configs.py` 提取真实新 helper，补全 STA/AP/APSTA
配置漂移、逐 getter 部分输出错误、旧 token/代际、状态/owner 准入、AP 主信道
不匹配/自动选择，以及无新写入/START、原 checkpoint 保留与安全释放。
`test_wifi_policy_replay.py` 在真实 resume 入口注入上述 helper 的失败结果，检查
调用顺序与 identity 未消耗；helper 本体由前一 fixture 覆盖，不以独立状态机代替。
仅 AST 解析，未导入、编译或运行两份 fixture。

manifest、feature/schema、MQuickJS 语法、strict TypeScript、SDK map 和 whitespace
检查结果见 evidence。Host/Python/VM/故障注入、C3/S3/feature-disabled、完整
restart、AP activation、RF/共存与实机均 not-run。Wi-Fi API 完成后集中阶段与
实机功能测试，长时间 soak 留到 BLE API 完成后。未刷写、串口操作、擦除 workspace、
前端构建、提交、推送或更新根 gitlink。
