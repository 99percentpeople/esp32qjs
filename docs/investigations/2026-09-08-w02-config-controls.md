# W-02：停机复合配置的控制项

基线 firmware `d7db8d1`，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批为独立 firmware 工作区增量，继续公开 configure/APSTA 的依赖实现。

## 已编码的原生路径

共用接口执行器和精确 lifecycle 配置入口新增可选 typed-native controls：国家
code/details、STA/AP 各频段的完整 protocol bitmap、带宽和 Station power-save。
现有 startAP 传 NULL，因此不改变其默认配置。尚无公开 JS 调用方传入 controls；
公开 configure/APSTA、完整参数 capture、结果和 allowDisconnect 仍未完成。

- 纯字段、target gate、显式 PHY/带宽组合检查在 helper 停机前执行，Radio 再次
  检查。依赖现有 driver 值的检查在独占停机快照中完成；失败不执行配置写入，
  但不能据此宣称 helper 停机/退休已回滚。
- mode、待改 STA/AP 配置、国家、涉及的 PHY/带宽和省电前值在首次配置写入前
  读取。控制项快照进入已有一次 calloc 的暂存块，全出口 secure-zero/free，
  不新增常驻凭据或另一套 owner。setter/getter 在 Radio task mutex 内，临界区
  只记录原生状态；callback 不获取该 mutex。
- 协议采用完整 bitmap，拒绝未知位及缺少基础协议的组合；HE/5 GHz 根据真实
  target gate。支持固定 SDK 允许的 HT20/HT40；HT40 要求 11N 且不能同时有
  11AC/11AX。省略项合并已读取前值后再检查，不把省略当默认覆盖。
- C5 使用双频 getter/setter，C3/S3 使用单频入口。请求当前 band mode 未启用
  的频段，在写入前报错，不能让 SDK 静默忽略。此事务不隐式改 band mode。
  PHY 写入先 HT20，再协议，最后所需带宽；读回比较全部当前有效频段，包含
  本次未修改的同接口频段。读回规范化的真实行为仍待 SDK/硬件测试。
- 国家先提交/读回，再按新国家检查非零 AP 信道，随后 mode/接口配置、PHY 和
  power-save。组合提交结束再次读取接口配置，用原有 accept validator 检查，
  然后才更新 caller 的非保留输入。AP 自动信道 0 继续由 SDK 决定。
- 任意已尝试 setter 都按可能部分修改处理。失败先 RAM storage，再国家恢复，
  启用所需接口、恢复配置及 PHY/power-save，最后 mode/storage。恢复每项都
  读回，保留原始 stage/error，另记 rollbackStage/error；失败后拒绝新事务，
  凭据释放后不重放配置。精确 token 保留，供中央显式清理。

## 固定 SDK 的边界

依据本地 `components/esp_wifi/include/esp_wifi.h` 与
`esp_wifi_types_generic.h`，不是 RF 或 SDK 执行结果：

- set_country 与 set_country_code 都有 Flash 副作用，独立于 storage=RAM。
  在 country setter 调用之前即设置 persistentMutationPossible；即使 RAM
  回滚读回一致也不声称原 NVS 历史恢复。失败保留 Radio fault。
- country details 的 max_tx_power 是只读字段，输入必须为 0；不能用它设置
  TX power。环境 octet 只接受 SDK 定义值，比较时将 NUL 与空格规范化。
  details 只做字段/范围检查，不能冒充逐国家合法频段数据库。
- 国家恢复时还检查 max_tx_power；若 country/PHY 导致其变化且 stopped 下
  无法恢复，则回滚失败。不会忽略该差异后报告完整恢复。
- TX power、band mode 要求 started driver。它们的启动后提交、失败隔离及
  生命周期恢复尚待实现，本批没有在停机事务中调用这些 setter。
- 5 GHz 法规 mask=0 继续代表 SDK 隐式规则，不声明所有信道均被验证；实际
  SDK regulatory 接受/拒绝与 RF 能力仍待阶段验收。

## 检查与待执行测试

本批必要检查：C5 immutable Build Context 编译、MQuickJS syntax、manifest、
feature docs、raw schema/live SDK header、recorded SDK map、一致性及 whitespace。
最终结果与文件 hash：`build/w02-config-controls-evidence.json`。

新增 `tests/c/integration/wifi/config/test_wifi_config_controls.py` 调用实际事务、精确 token 入口、
纯 validator、semantic config compare 和 secure-zero；SDK typedef 从已记录
C3/C5 inventory 读取，注入边界仅模拟 driver 调用及返回数据。覆盖成功路径
每次调用失败、先部分写入后返回失败、逐回滚失败、读回不一致、内存失败/
清零、非法输入、旧 token/存活 owner、未启用频段、隐含与显式 HT40 冲突、
RAM 模式国家持久化标记及以新国家校验 AP 信道。执行器/AP adapter 现有用例
同步 controls 参数，并覆盖纯验证在停机前拒绝。测试源码 AST 已检查。

所有这些 Host C/Python 用例均 **not-run**，按用户安排等 Wi-Fi API 完成后
集中执行。三目标及 disabled 矩阵、实际 SDK 规范化/持久化故障、实机功能均
not-run；长时间 soak 等 BLE API 完成后。未刷写、操作串口、提交或更新根仓库。
