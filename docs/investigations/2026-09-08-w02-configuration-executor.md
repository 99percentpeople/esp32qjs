# W-02：共用接口配置执行器

基线 firmware `d7db8d1`、SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批连接已有配置提交/回滚、Radio 启停事件与 Station/AP helper 生命周期，并把
生产 `startAP` adapter 改为使用该执行器。公开 `configure`、APSTA 与完整 SDK
配置 schema 仍未完成，不新增占位方法或正式类型。

## 实际路径

`esp32_mquickjs_wifi_configure_interfaces` 在原生输入预验证后取得精确的中央
lifecycle token，拒绝现有连接、未完成操作、外部 owner 和未完成清理。调用方
必须先解析并验证输入；此入口没有隐式断连授权。

取得 token 后依次释放被交接的 owner、quiesce、退休旧 AP/Station helper、
初始化停止状态的 driver、准备目标 helper、提交配置、恢复启动与发布新 owner。
所有 helper 准备均在配置写入前；SDK 配置核心仍在写入前分配安全快照。
初始化入口只接受精确且没有 owner 的停止/未初始化 token，不改变不确定 init
失败后的设备重启政策。

- 原生模式为 STA/AP/APSTA，storage 为 RAM/FLASH；AP-start 必须提供已验证的
  AP config。STA-start 可以保留既有配置，不自动发起连接。
- `start=false` 不创建 helper、不发布 owner。该原生能力尚未公开为 JS options。
- 启动 STA 发布 Application 和 Station owner，启动 AP 发布 AP owner。AP-only
  不额外发布 Application owner，保留现有 `stopAP` 所有权边界。
- 新 lease 只在 Radio START 事件、marker 与 mode readback 完成后发布。实际
  公共 `startAP` 仍是冷启动、独占、2.4 GHz 的既有契约。
- 将 AP 法规信道检查放入共用配置核心，在配置写入前执行，避免新调用路径
  绕过旧 AP start 的 country 检查；失败记为 `ap-regulatory`。

## 失败与清理

准入失败不接管事务；已保留 AP lease 的 adapter 沿用本地释放路径。准入之后
失败保留中央 token 和原生资源，不向调用方发布可用 owner。输入 config 仍由
调用方持有并清零；中央执行器不保留凭据指针，也不以回收后的输入重放配置。

`s_wifi_configuration_mode` 与中央 token 一样位于 helper reset 外，标识该清理
是否属于 AP-only 配置。`stopAP` 仅允许完成 AP-only 失败事务；混合事务和中央
runtime 清理不能由这个局部入口收尾。公共 `wifi.stop` 遇到中央事务时转交完整
AP/STA 清理后缀，避免直接走旧 Station-only helper 清理。失败重试保留排他，
成功才清除 token、模式及 pending 状态。

新增 cleanupStage：configuration-initialize、configuration-station-prepare、
configuration-ap-prepare、configuration-ap-slot、configuration-commit、
configuration-resume；保留原始错误。配置读回/回滚详情仍在 radio.configuration，
启动故障仍在 Radio fault，不能用配置 complete 宣称启动成功。

配置快照回滚只描述 SDK 配置，不会重建旧 helper 或恢复旧运行生命周期；准备
或启动失败后需要显式清理。FLASH 的潜在持久化副作用同样不属于 RAM 回滚保证。

## 验证与剩余工作

C5 immutable Context 编译通过，app `0x27b0c0` bytes，分区空余 17%；MQuickJS
syntax 为 59 sources / 47 snippets，manifest 为 44 classes / 397 functions，
feature 文档 27 项及 recorded SDK map 一致性检查通过。四个 Python 文件仅做
语法检查。日志、源码和 Context hash 见 `build/w02-config-executor-evidence.json`。

生产路径用例已编写、未执行：

- 共用配置执行器的 11 个边界失败、三模式 × 启停的 owner 发布、无输入 STA
  start、pending 时拒绝重复配置、AP-only 清理限制。
- Radio 初始化的精确 token、存活 owner/operation/wake/fault 拒绝和原始错误。
- `startAP` adapter 的准入失败本地释放、接管失败保留中央清理及凭据清零。
- 公共 stop 的中央失败转交和重试，不调用旧 Station-only 清理；既有中央
  AP/STA 清理 fixture 同步新入口。

这些测试调用生产函数、注入原生边界；不代替完整 SDK/default-event-loop/RF。
完整 Host C/Python、C3/S3/feature-disabled 矩阵和实机功能留到全部 Wi-Fi API
完成后集中执行；长时间 soak 放到 BLE API 完成后。当前仅 C5 编译，未执行
串口、刷写、提交或父仓库 gitlink 更新。

下一项仍为 docs02 §8.1 的完整 SDK 字段与 checked mapping、声明生成和 parser，
随后注册公开 configure/APSTA；不以这个内部执行器宣称 W-02 完成。
