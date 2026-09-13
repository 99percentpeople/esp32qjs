# Wi-Fi 非必要策略限制移除：第一批

边界：C 提供原生能力、精确所有权和内存/生命周期保护；JS 选择业务策略。
本批接续既有未提交改动，未改根仓库 gitlink、SDK 或设备。

## 已移除

- Raw TX one-shot / Session.open 的 validation:strict/basic。两个值此前
  不影响任何 native 校验行为，仅增加参数分支。删除正式字段与捕获分支，
  保留真实 MAC/SDK 检查及 validationCode 错误详情。旧字段按 unknown key 拒绝。
- CSI / Monitor 的 powerSavePolicy、对应内部状态与启动拒绝分支；CSI 的
  WIFI_CSI_POWER_SAVE_CONFLICT 不再暴露。启动保留实际省电模式，JS 使用
  setPowerSave / acquireWakeLock 决定策略。Monitor 的生产启动测试确认开启
  modem sleep 时仍能接入 capture。没有加入自动 power-save 修改。
- 通用有限等待的 60 秒参数上限：connect/scan、disconnect/stop/stopAP、
  driver.restart、WAPI enable/disable，以及 Raw TX 普通 send/open/flush/
  close/periodic startup 放宽到 INT32_MAX ms，与公共 Future 有限等待范围一致。
  scan dwell、Action/ROC 原生驻留、TWT 协议时长等未被批量替换。
  各独立恢复/高级协议 API 的期限仍按其现有契约执行，后续逐路径审核。

共享 native wait 使用 uint64 进行 ms→tick 换算，检查半个 TickType_t 周期
界限后才窄化，拒绝溢出或 infinite sentinel。默认值不变，仍不抢占 SDK
同步调用。输入整数、范围、资源预算、lane/owner、已提交操作保留等保护不变。

## 未移除的授权分支

allowDisconnect / allowApRestart 本批只核对，未删除。它们覆盖配置事务、
Enterprise 绑定、故障恢复中临时 AP 启动/恢复等多个原生流程。简单删除参数
并默认 true 会扩大副作用；默认 false 又会移除现有显式恢复能力。需要按
各入口确定由 JS 显式操作替代的路径，不能把这些条件与上面无效策略一起删除。
本批不宣称所有策略限制已清理完毕。

## 验证

证据 build/wifi-policy-removal/：

- native-latest.json 汇总最后结果：38 个不同原生用例全部通过。覆盖真实 VM
  capture、GC/OOM、空/最大参数、Monitor 在 modem sleep 下启动、CSI 配置
  回读/数据所有权、Raw TX capture/Session/periodic、生命周期与 disabled AP。
  初轮失败的旧 60000 测试上限已更新；额外 tick 溢出用例在原生准入前拒绝。
- 顺便修复先前 stopAP(options) 变更遗漏的独立测试驱动：不再使用旧 scalar
  fixture；普通和 SoftAP-disabled 分支均使用当前生产 options 捕获。
- Python 契约 39/39；manifest 62 classes / 662 functions 一致。
- MQuickJS 语法 69 sources / 72 doc snippets 通过。
- target-compilation.json：现有合法 Build Context 编译参数，10 个受影响 C
  文件分别在 C3/S3/C5 编译，30/30 通过；对象输出隔离，不改已有构建对象。
  WAPI maxTimeoutMs 常量声明同步为 INT32_MAX。

本轮完整链接、Wi-Fi-disabled 构建、刷机、设备 JS/RF/长测均 not-run；未提交。
