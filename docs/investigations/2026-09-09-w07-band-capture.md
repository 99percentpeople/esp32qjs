# W-07 单频捕获准备与部分快照寿命

firmware `d7db8d1` 工作区增量，固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
后续 [band mode 与 Station channel 恢复](2026-09-09-w07-band-restore.md)已补写入；下文保留本批当时的边界。
承接 [SDK 内部重启事件屏障](2026-09-09-w07-band-cycle-events.md)。完整剩余范围仍以
[Wi-Fi 清单](2026-09-08-wifi-api-remaining.md)为准，本批没有注册公开 restart。

## 已编码的行为

C5 原 2.4/5 GHz 单频模式先只读捕获 Station/AP 配置、国家/MAC/省电/事件 mask、
TX power、interval/rate 记录、原 band mode/band、当前与 home channel，以及当前可见
频段的 PHY。当前/home 必须一致；可见 PHY 读取后再次核对 band/channel，避免把离开
主信道时的观察作为稳定前值。未观察到的另一频段 home channel 不作推断。

完成上述只读步骤后才发布一个私有 checkpoint，并开始 RAM storage → STOP →
Station mode/readback → START/events → AUTO/readback → 内部 STOP/START/marker 的准备。
原 AP/APSTA 先停止；临时启动的接口只有 Station。没有输出 lease、额外常驻队列或
临时 AP 广播。RAM 选择先于配置改变，原 FLASH 策略仍冻结在 checkpoint 中。

AUTO 下逐接口读取完整 PHY，先验证原可见字段未变化，再填入隐藏字段；失败不覆盖
已冻结的可见值。Station 完成后 AP 读取失败，重试不重新捕获 Station、凭据或全局值。
完整捕获成功才允许原有 shutdown/deinit/rebuild/replay 路径继续。

准备开始前失败仍安全清零并释放分配。准备后的部分快照归精确 lifecycle token 与
原物理 generation 所有；普通 resume/post-start 不接受它。读取、事件等待失败只
重试尚未完成后缀，STOP 沿用中央 accepted-once 实现；已接受的 START 和 AUTO 不
重复提交。storage/mode/START/band 写入结果不确定时保留错误与终止阶段，要求显式
清理，不盲目重放写入。失败清理仍保留快照；成功清理沿用 secure-zero 释放路径。

最终 owner 交接新增原 band/channel 的精确读回要求。**恢复这些原值的写入后缀尚未
实现**：单频捕获虽已能完成，后续 AUTO PHY 重放不能冒充完整 restart 成功。公开
helper/scan/connect/AP 协调、其余配置恢复与高级 Wi-Fi 模块继续待实现。零 Radio
owner 的原生准入不等于这些公开异步协调已完成。

## 当前验证与限制

C5 immutable Context `build/wireless-contexts/c5`（8 MB/no PSRAM）生产构建 exit 0，
日志 `build/w07-band-capture-c5-build.txt`。binary 2,763,248 bytes，较前批增加 432。
checkpoint 由 664 增至 688 bytes；capture cursor/error 放入该按需分配，常驻
`s_config_restart` 仍 40 bytes，记录的 Radio/ESP-NOW/Raw TX/policy/interval/rate
静态大小不变。未测 live heap、largest block 或回收曲线。

capture/continuation/内部 restart 已在生产对象编译，但当前 ELF 未引用这些入口；
最终 band/channel 校验及 snapshot 大小查询已链接。构建不能证明准备流程运行正确。

扩展 deferred `test_wifi_restart_configs.py`，提取实际生产 capture/continuation/PHY/
channel/token 实现，仅注入 SDK、storage、锁、事件等待和分配边界。用例包括单频
隐藏字段、原 AP/APSTA 的 Station 准备、每次 SDK 调用失败、成功前缀保留、旧 token/
物理代际拒绝、不确定写入隔离、可见 PHY 改变拒绝、清理失败保留/成功清零，以及原
band/channel 未恢复时不通过最终交接。本批仅 AST 解析，未导入、编译或执行用例。

manifest 49 classes/464 functions、feature 文档 27、STA/AP schema 35/21（live SDK
header）、MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map、whitespace
检查通过。证据汇总 `build/w07-band-capture-evidence.json`。

Host/Python/VM/GC/OOM/故障注入、C3/S3/feature-disabled、完整 restart、RF/共存和实机
功能测试均 not-run，按安排等全部 Wi-Fi API 完成后集中执行。长 soak 留到 BLE API
完成后。未刷写、串口操作、擦除 workspace、构建前端、提交、推送或更新根 gitlink。
