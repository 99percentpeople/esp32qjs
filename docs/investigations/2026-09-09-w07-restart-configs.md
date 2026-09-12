# W-07 内部 restart 的 Station/AP 配置 checkpoint

firmware HEAD `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接 [策略快照/重放](2026-09-09-w07-policy-replay.md)。完整 Wi-Fi API 和最终实机功能测试
仍未完成，长时间 soak 按用户安排留到 BLE API 完成后。

## 实现与边界

新增单份原生 checkpoint，属于一个精确 lifecycle generation/identity。已有 driver
的快照在 deinit 前分配并捕获；捕获失败安全清除整个分配，不保留部分输出。冷初始化
没有旧 driver 配置，不为默认值制造快照或分配凭据存储。存在任何 Radio owner 时拒绝捕获。

Station 与编译启用的 AP 配置都保存，包括当前 inactive interface。固定 SDK getter
没有 setter 的 interface-enabled 限制，现有 AP reopen 也已采用 inactive AP getter；
真实设备行为仍待集中验证。捕获 mode 读回须与框架记录一致；storage 使用框架唯一写入记录，
没有伪造 public SDK getter。Station/AP 原生配置中的密码/SAE 标识只在 native storage 中存在，
不进入 status、错误、日志或 JS 对象。

恢复先选择 RAM，再临时启用需要写入的接口（driver 始终 stopped）。每个接口写入后读回，
复用现有 `wifi_radio_config_equal` 比较公开语义字段，不比较 reserved/padding，不接受安全降级。
最后恢复目标 mode 与原 storage 选择；框架 replay 中的 `set_config` 均在 RAM 模式下执行。
这不是 SDK 内部/NVS 实机无写入的证明，也不是 NVS rollback 承诺。

SDK setter 使用独立 scratch 拷贝，不能修改 frozen original。所有 getter 的部分输出和
setter scratch 在返回前 secure-zero，包括错误路径。全部 frozen storage 在 owner handoff
或显式 lifecycle cleanup 成功后 secure-zero/free；清理失败仍保留。跨物理 generation 的
重试重放全部步骤，同一 token 不重新从已失败的 driver 输出捕获原值。

pre-start 恢复完成是 resume 的前提。start、策略重放之后再次读回 Station/AP 配置；
比较失败时不发布 staged owner，也不丢弃策略或凭据快照。普通 configure/start 没有
restart checkpoint 时不触发这些恢复调用。完整 country/PHY/band/MAC/channel/power/interval/
rate 恢复、公开 restart 及 runtime helper 集成仍待完成，不能将本批解释为全部配置已恢复。

`wifi.status().radio.restartSnapshotBytes` 只报告本 checkpoint 保留的原生字节数；不包含
固定控制 state，不暴露内容，也不是全 driver 内存总账。错误的 `radio.configuration.stage`
使用 restart-config/station/ap 前缀，保留原始 SDK 错误；恢复失败还设置 Radio fault。

## 编译与测试证据

C5 immutable Context `build/wireless-contexts/c5`（8 MB/no PSRAM）生产编译通过：
`build/w07-restart-config-c5-build.txt`。ELF 诊断 getter 常量为 560 bytes，固定
`s_config_restart` 为 28 bytes；这是编译大小，不是 live heap 或内存回归结果。

capture/replay 与内部 restart 在生产对象中编译，因公开入口未接线仍从 C5 ELF 中移除；
final verification、discard 和字节诊断路径已链接。没有 callable `wifi.driver.restart()`。
最终 hash/链接边界/一致性检查见 `build/w07-restart-config-evidence.json`。

新增 `test_wifi_restart_configs.py` 使用真实 capture/replay/语义比较/secure-zero/cleanup，
SDK 与 heap 边界注入，覆盖分配失败、getter 部分输出、各 SDK suffix 失败、inactive AP、
SDK 修改输入、RAM-only setter、原始快照保留、最后读回不一致、旧 token 和全部字节清除。
策略 fixture 增加最终配置读回失败时的 owner 发布检查，status GC fixture 同步字节 getter。
这些文件本批仅 AST 检查，没有导入、编译或执行，不冒充已复现/已通过的竞争证据。

Host/Python/VM/GC/OOM/竞争、C3/S3/feature-disabled、coex-enabled SDK、实机关闭重开/队列/
运行时重启、RF/共存均 **not-run**。未刷写、串口操作、workspace 擦除、前端构建、提交、
推送或更新根 gitlink。Wi-Fi API 全部完成后仍需集中阶段测试和实机功能回归。
