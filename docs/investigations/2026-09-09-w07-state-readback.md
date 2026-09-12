# W-07：Mode、Country 与 Current/Home Channel 读回

firmware HEAD d7db8d1，固定 SDK fff9895c82d744c7237be8847347bdd1b07c6643。
本批补四个正式唯一 v1 getter，完整 Driver 与 Wi-Fi 总目标仍未完成。

## 生产实现

wifi.driver.getMode/getCountry/getChannel/getHomeChannel 严格要求零参数。共用
Radio read_state 在 mutation mutex 内验证 driver ownership、storage、stable
started/stopped、lifecycle/operation/fault/cleanup/restart 状态。没有隐式初始化、
额外 owner、SDK mutation 或新静态表。home channel 按固定 esp_wifi.h 要求 started。
storage 没有公开 SDK getter，不新增伪装读回的方法；既有 status.radio 保留框架记录。

Mode 直接 SDK get_mode，映射现有 off/station/softAP/station+softAP；off 表示
WIFI_MODE_NULL，不是物理 deinit 证据。未知 mode/未编译 SoftAP 的 AP mode 拒绝
解码，NAN 完整生命周期仍在总目标。Country 直接 get_country，验证两个国家字符、
第三个环境字符、policy 与 2.4 GHz 范围。保留 max_tx_power 与原始 5 GHz mask，
无 5 GHz 时 mask=null；零或 AUTO mask 不构成信道白名单，不改变法规。

Current channel 使用生产 callback-aware refresh：捕获 generation/revision，若
SDK 查询期间更新完成，则保留更新的观察。可更新缓存及标记既有 fixed lease conflict，
不会写 RF。Home channel 直接 get_home_channel，不修改该缓存、不借用它的 revision，
公开 channelGeneration=null。两者可因 scan/off-channel 活动不同；单次 getter 和
跨 getter 的结果不是原子 Radio 快照，也不承诺后续信道稳定。current generation
只是观察修订，不是操作 identity/时间戳；不保证观察到每次 RF 转换。

Native 返回前验证已知/target 支持的信道及 secondary enum，不额外查询 country
来冒充原子法规判定。未初始化、停机无可用信道及 SDK 错误均明确失败，不造默认值。
所有错误清空输出，复用 WIFI_DRIVER_READ_FAILED/interface=null 与分步 stage。
JS 结果使用 rooted converter；Country converter 从原 scan helper 提升为共享内部
函数，原 scan/setCountry 行为保留。结果分配失败不能改变 native 配置。

## 延后测试准备

test_wifi_driver_state_read.py 提取生产 Radio read_state、实际 refresh、SDK channel
adapter、channel lookup、公开 wrapper、共享 Country converter 与错误 converter。
注入原生状态/SDK/锁；三目标、SoftAP on/off、逐 getter SDK 部分输出后报错、未知
mode/country/channel/secondary、home started gate、操作/生命周期故障准入、home/current
独立性；在旧 SDK read 中受控执行更新的生产 refresh，验证新观察不被旧结果覆盖。
VM 准备 strict arity/原错误/null interface/返回字段及 moving GC/Nth allocation。
既有 scan/status GC fixture 同步新共享 helper 名。仅 AST parse，未导入或运行测试。

完整 C3/S3/disabled 构建、Host C/Python/VM/故障注入、真实 SDK task 并发、RF、实机
与动态内存均 not-run。Wi-Fi API 完成后集中阶段和实机功能测试，长 soak 等 BLE API。

## 本批构建与契约检查

C5 immutable Build Context firmware-ci-esp32c5-representative 构建 exit 0；ELF
链接四个 getter、Radio read_state 与共享 Country converter。binary 0x29ebf0 /
2,747,376 bytes，较前批增加 1,856 bytes，app 空余 13%。静态账本未增长：
s_radio=824、s_raw_tx=136、s_tx_rates=68、s_tx_rate_lease=36、s_sessions=32、
s_jobs=32、s_retired=56、s_lane=44 bytes；动态 heap/stack 峰值不由此推断。

MQuickJS 61 sources/53 snippets、manifest 49 classes/458 functions、strict TypeScript、
feature docs 27、config schema 35 STA/21 AP（live SDK）、recorded SDK map、两份 Python
AST 及 whitespace 通过。SDK 工作区干净。build/w07-state-read-evidence.json 保存
日志/符号/source hashes；所有运行与 RF 验收仍 not-run，未提升 feature 稳定等级。
未刷写、操作串口、擦 workspace、构建前端、提交、推送或更新父仓库 gitlink。
其余 Driver、共享 AP policy、Raw TX 恢复、高级模块、资源预算与阶段验收继续按
[完整清单](2026-09-08-wifi-api-remaining.md)推进。
