# W-07 天线配置观察

firmware `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
新增 `wifi.driver.getAntenna()` 和 `getAntennaGpio()`，继续 sole v1。

## SDK 事实与接口边界

固定 SDK 的公开接口位于 `components/esp_phy/include/esp_phy.h`，实现位于
`components/esp_phy/src/phy_common.c`，不是旧 `wifi_ant_*` 类型控制面。
两个 getter 仅复制 SDK 的全局已存配置，不读取实际 RF 或 GPIO 路由寄存器。
因此接口称为配置观察，不声称当前硬件已应用、PHY 待应用状态或板级布线正确。

`getAntenna()` 返回五个字段：rxMode、txMode、rxDefault、enabledAnt0、enabledAnt1。
模式映射为 ant0/ant1/auto，default 只允许 ant0/ant1。保留两个选择值的完整
4-bit 范围 0–15，不将它们截为 0–3 或解释为 GPIO 编号。未知枚举、负枚举和
TX AUTO/RX fixed 组合返回 decode 错误。SDK checker 允许 RX/TX 均 AUTO，但
`phy_ant_update()` 的 TX switch 仅单独处理 ANT0/ANT1，AUTO 进入选择 enabled_ant0
的 default 分支；getter 不把保存的 AUTO 写成已证明的自动 TX 行为。

`getAntennaGpio()` 返回四项 gpios tuple，每项含 selected 和 gpio，逐项保留
SDK 1/7-bit 值，包括未选择项的 GPIO 编号。0–127 的原始保存值不意味着目标上
存在该 pin、pin 可用或路由仍然有效。两个调用是独立观察，不是合并原子快照。

现有 SDK `esp_phy_set_ant()` 会根据共享 modem flag 立即或延迟更新 PHY；
`esp_phy_set_ant_gpio()` 会配置 GPIO 并连接输出信号，没有框架 pin 租约转移，
也未恢复旧 routing。仅 Radio lease 为空不能证明其他 modem 或外设不受影响。
因此两个 setter 继续 planned/contract-pending，待共享 PHY/GPIO 所有权协调，
不以 target-unsupported 排除后续实现，不在正式注册表添加占位 setter。

## 生产实现

WIFI component 显式依赖 esp_phy。Radio helper 在既有 mutation mutex 内读取，
要求 driver owned、storage configured、STOPPED/STARTED，且无 lifecycle、operation、
fault、cleanup 或 restart-required。允许正常存活 owner/wake/promiscuous 的只读观察；
不初始化、分配 lease、改变配置或使用临界区包住 SDK 调用。该 mutex 不代表 SDK
内部提供跨 modem 原子快照；后续共享写入仍必须统一协调。

先清空调用者输出，SDK 写入局部 union，仅成功且通过字段校验后复制给调用者。
SDK 部分填充后失败不会泄露成功样式的部分结果。错误沿用 WIFI_DRIVER_READ_FAILED，
带 operation、interface=null、原始 espCode 及 admission/antenna/antenna-gpio/decode。
绑定严格零参数，root object、四项 array 和 item 均有 GC root，转换失败撤销全部
临时 roots，不产生原生 owner 或需要恢复的 native state。

## 验证记录

C5 immutable Context `build/wireless-contexts/c5`（8 MB/no PSRAM）生产构建 exit 0，
日志 `build/w07-antenna-read-c5-build.txt`。binary 2,767,424 bytes，较 mode 批次增加
1,600 bytes。常驻账本与链接符号以 `build/w07-antenna-read-evidence.json` 为准；
编译和符号大小不替代运行时内存测量。

新增 deferred `tests/c/integration/wifi/driver/test_wifi_driver_antenna.py`，提取实际 Radio helper、
JS getter、转换和错误路径；仅注入 SDK storage/返回值、锁及 VM 分配/GC 边界。
覆盖所有非稳定 driver state、逐项 admission、零参数预验证、SDK 部分输出错误、
全部枚举组合和 4-bit 选择范围、disabled GPIO/raw 7-bit 保留、快照值复制、
原始错误详情，以及每次 JS 分配失败/移动 GC。仅 Python AST 解析，未导入、
编译或执行 fixture；不能写成这些场景已通过。

生成物、类型和 MQuickJS 语法检查结果记录在上述 evidence 文件。Host C/Python、
VM/GC/OOM/故障注入、C3/S3/feature-disabled、完整 restart、实机/RF/共存均 not-run。
全部 Wi-Fi API 完成后集中阶段和实机功能测试，长 soak 留到 BLE API 完成后。
未刷写、串口操作、擦除 workspace、构建前端、提交、推送或更新根 gitlink。
完整剩余范围仍见 [Wi-Fi 清单](2026-09-08-wifi-api-remaining.md)。


## 2026-09-12 写入前的原生缺陷修复

开始实现 setAntenna/setAntennaGpio；当前仍不注册这两个公开 setter。
固定 SDK phy_common.c 的保留 GPIO 检查传入 gpio_num，而
esp_gpio_is_reserved 接受 uint64_t 位掩码，导致检查错误的引脚集合。另一个确认
缺陷是 phy_ant_set_gpio_output 忽略 gpio_config 返回值，失败后仍连接天线输出
并可能提交保存的配置。两项由真实 SDK 源码及 esp_gpio_reserve.c 的实现确认；
动态复现按用户要求留到统一测试阶段。

新增 scripts/patch_idf_phy_antenna.py / .cmake，在合法 Build Context 中替换
phy_common.c 编译输入。先验证全部选中引脚的 output 能力、64-bit 范围、重复选择
及正确 BIT64 保留掩码；gpio_config 失败立即返回原始错误，不连接该 pin，也不提交
最终配置。前面已成功写入的 pin 仍可能改变，调用方必须保留并恢复路由，不能把
错误返回解释为无副作用。公共 SDK 安装和 per-target 非连续 ANT_SEL 信号表不变。

生产替换模板的 2 个回归方法已编写，覆盖错误 bit mask、后续无效输入的无提前
写入、重复 GPIO、逐次 GPIO 配置失败、非连续天线信号及最终保存状态。仅 AST，
未导入/编译/执行测试。生产生成及编译记录保存在 build/w07-antenna-write-review。
跨 modem 串行、既有 GPIO 路由占用及失败恢复尚待接入上层写入事务；该修复不等于
天线 setter 已完成，更不等于实机切换或共存通过。


## 2026-09-12 完整写入事务与公开入口

setAntenna/setAntennaGpio 已完成生产接入，之前的 pending 记录为历史状态。
Radio stopped/零 owner 与 SDK PHY access lock 共同串行；BLE logical lifecycle、
原生 BT controller 和启用时的 IEEE 802.15.4 都必须关闭。未启动的 BLE open、
失败/关闭过程及其他 modem 的 sleep/idle 均拒绝。GPIO 全量预验证后原子预约，
保存原路由；SDK build copy 的 owned 入口避免重复预约造成虚假冲突警告，配置
完成后才 enable output/连接对应信号。SDK 配置及实际信号读回后提交；移除旧 pin
恢复原路由。失败恢复所有涉及 pin 并恢复 SDK RAM 快照，不以再次设置旧配置代替
对移除引脚的清理。外部修改的 pin 不被覆盖。最后 pin 释放后归还按需 control 账本。

配置及 pin 预约按共享 PHY 语义保留至显式替换/释放，跨 Wi-Fi deinit 和 JS restart。
未确认 rollback 锁存设备重启故障；Wi-Fi init/BLE open 均检查它。原失败和 rollback
错误分开记录，成功的 runtime restart 不能清除此故障。输入和规范化返回对象在
driver mutation 之前完成捕获/分配，后续出错不会丢失原生完成状态。

C5 13 生产单元编译、44 对象/真实 SDK 局部链接、C3/S3 各三个相关单元编译和
Wi-Fi-disabled 新单元为空均通过。C3/S3 反相字段差异改用对应目标寄存器 mask。
manifest 62/642、SDK map 1267 一致。9 个 Python 回归方法仅 AST，未运行；完整
目标矩阵、动态竞争/GC/OOM、实机/RF/heap 均 not-run。精确输入、命令、产物 hash
见 build/w07-antenna-write-evidence.json。公共契约见 docs/api/wifi-driver.md。
