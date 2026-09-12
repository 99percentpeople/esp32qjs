# W-03：Monitor 参数捕获与 VM 测试源码

## 已编码范围

新增 `wifi_monitor/esp32_mquickjs_wifi_monitor_options.c` 与内部头文件，使用现有
plain-options、bounded-number 和精确 enum helper。内部结果为不含 JS 指针的
capture/filter、pool/queue/snap 与 requireComplete 快照；全部校验成功才写入
output，任何失败不提交部分配置。不保留长期 JS roots，不获取 Radio 或分配原生
Session/pool。public JS open/configure 调用方仍待接入。

所有嵌套 object/array 在读取期间使用 JSGCRef；列表元素先复制为整数/MAC 字节，
不把可移动 VM 字符串地址留给 native callback。错误仅指出字段，不回显参数值。

## 参数契约

| 字段 | 默认 | 当前接受范围 |
| --- | --- | --- |
| channel | current | current 或本 target 的数字信道；法规和运行 owner 仍由 Radio 准入 |
| filter.types | management/control/data/misc | 四种类型的无重复数组；空数组不匹配任何帧 |
| filter.subtypes | 无限制 | 0–15 的无重复数组；显式空数组不匹配任何 subtype |
| filter.sourceMac/destinationMac/bssid | 无限制 | 单个 MAC 字符串或 1–8 个无重复 MAC；固定 17 字节冒号格式 |
| filter.minimumRssi | 无限制 | 整数 -128–127 |
| filter.sampleEvery | 1 | 整数 1–UINT32_MAX |
| filter.maximumRateHz | 0（关闭限速） | 整数 0–1000000 |
| filter.validOnly | true | boolean |
| capture.snapLength | 2048 | 整数 1–16384 |
| capture.requireComplete | false | boolean |
| buffering.poolCapacity/queueCapacity | 各 16 | 各为整数 1–128 |
| buffering.overflow | drop-newest | 仅 drop-newest |
| powerSavePolicy | preserve | preserve / require-none |

undefined 表示省略，null 不作为省略别名。未知字段、NUL 后缀、字符串数字、非整数、
NaN/Infinity、非法 MAC、大小写不同但字节相同的重复地址、重复 type/subtype 均拒绝。
MAC 空列表拒绝，避免 native count=0 的“未设置过滤”语义被误当作空匹配。原生
filter 本身仍按同 role OR、跨 role AND 执行。

数字信道先做 target 静态检查：1–14 或具有 5 GHz 能力的 target 所支持的明确
信道编号。5 GHz 映射复用 Radio 的纯函数，不调用 SDK。该检查不等于当前国家/
法规准许；后续 Radio transaction 仍必须查询法规和 owner。require-none 当前
底层只作启动快照校验，持续策略约束仍未完成；本 parser 不因此发布完整公共能力。

## 已编写的 VM 测试，未执行

`tests/python/test_wifi_monitor_options.py` 使用 vendored MQuickJS、生产 parser、
生产 options helpers、原生 filter/capture 类型与纯信道映射，覆盖 2.4 GHz-only 和
5 GHz gate。公共 getter/array-index 边界可逐次注入 OOM，并在读取前执行真实
移动 GC，检查输出原子性、exception 保留与 root/native 账本。

已编写默认与完整配置、最大列表、空 type/subtype、MAC 大小写/重复/长度、非法
enum/未知字段/NUL/空值、数值上下界、coercion 拒绝、target 信道组合。测试从
真实 header 提取 filter/capture 类型和 limits，未另写 parser 状态机。

按用户安排，本批仅 AST 检查；没有导入/编译/运行这个 fixture。所有 movable-GC、
第 N 次故障、输入边界的运行结论仍 not-run，不能因为测试源码存在就写成通过。

## 已执行检查与后续

C5 immutable Context `build/wireless-contexts/c5` 编译通过，日志
`build/w03-monitor-options-c5-build.txt`；app `0x284c40`，余量 16%，与上一批相同。
options 入口存在于目标 object；当前无 JS caller，未保留到最终 ELF。未分配新
常驻资源，也未做实机 heap/largest-block 比较。

MQuickJS 59 sources / 48 snippets、manifest 44 classes / 398 functions、feature
文档 27 项、config schema 35 STA / 21 AP（live SDK）、recorded map、strict
TypeScript declaration、Python AST 和 whitespace 检查通过；hash 和未运行范围见
`build/w03-monitor-options-evidence.json`。

仍需 Frame/Session JS 构造/最终释放、真实 metadata converter、公开 open/configure
与生成契约同步，以及此前列明的 AP 共存、持续省电策略、PHY/time、Batch/Source、
wire 和 W-09 预算。完整 W-01～W-12 目标未缩减。

Host/Python、C3/S3/disabled 构建和实机功能/RF/GC 均 not-run。Wi-Fi APIs 完成后
统一阶段和实机功能验证；BLE 在相关测试后开始；长 soak 待 BLE APIs 完成。没有
刷写、串口操作、workspace 擦除、前端构建、提交、推送或根 gitlink 更新。
