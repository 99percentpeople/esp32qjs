# W-02：configure 外层有界捕获及严格字符串校验

基线 firmware `d7db8d1`，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
继续[授权断连](2026-09-08-w02-config-disconnect.md)之后的外层输入工作。

## 已编码的捕获

新增内部 `esp32_mquickjs_wifi_configuration_t` 和
`esp32_mquickjs_wifi_capture_configuration`，不调用 SDK、不初始化 Radio/helper、
不取得 owner。调用方提供原生存储；失败清零整个结果，成功由调用方使用后
secure-zero。Station/AP 继续复用实际 raw SSID/ByteSource 与个人安全/PHY 捕获。

| 外层字段 | 本批具体捕获 |
| --- | --- |
| mode | station/ap/apsta；未指定保持 mode_set=false，显式 AP 受编译 gate |
| storage | ram/flash；独立 storage_set 标记，不把零值枚举视为缺省 Flash |
| start | 仅 bool；独立 start_set 标记，不在 parser 猜测是否沿用当前 started |
| allowDisconnect | 仅 bool，未指定为 false；不在捕获阶段断连 |
| station/accessPoint | 现有完整 raw key 清单；含原生二进制 SSID，保留各 parser 的真实支持范围 |
| country | 两字符大写 code/01，或下述 object；不维护自建国家法规数据库 |
| protocols | station/access-point 两键；各自 ghz2/ghz5 为非空、不重复的完整协议数组 |
| bandwidths | station/access-point 两键；各自 ghz2MHz/ghz5MHz 为整数 20/40 |
| txPowerDbm | 有限 number，2–20 dBm，0.25 dBm 步长；安全换成 int8 quarter-dBm |
| powerSave | none/minimum/maximum；按已有原生 enum 映射 |

显式 mode 与接口配置/PHY/powerSave 冲突、start=false 与 TX power 冲突、协议
基础位/频段/HE gate、11AC/AX 与 HT40 冲突使用实际 Radio 纯 validator 拒绝。
缺省 mode 时使用 APSTA 作为静态校验的集合上界，不是默认初始化模式。协调器
仍须解析缺省并用最终 mode/start 再验证；原生接口执行器已重复进行纯检查。

Country object 具体为：code 必填；policy 为 auto/manual，省略 auto。仅这两个
字段时调用意图为 set_country_code，由 SDK 选法规参数。若提供 startChannel、
channelCount、environment 或 ghz5ChannelMask，必须显式提供 startChannel 和
channelCount，二者各 1–14 且区间末端不超过 14。environment 为 indoor/outdoor/
null（不指定环境），ghz5ChannelMask 为 uint32，非零仅 manual 生效且不得含未知
信道位；非 5 GHz target 连显式零 mask 也拒绝。maxTxPowerDbm 不属于输入，
不能写 country 的只读 power 字段。本捕获不承诺验证实际当地法规。

Protocol 数组只接受真正 Array，最长 7，不允许洞、重复、类型强转和省略基础
协议位；不采用 SDK 的“最高协议自动展开”简称。空接口 object/空 PHY object
被拒绝。各层 root、当前 property/array element 均按实际 MQuickJS API 生命周期
保护；没有跨 JS 分配借用原生 ByteView pointer。调用方存储不在 parser 内 malloc。

## 共享字符串校验的静态缺陷

核对发现 options helper 的 enum 与 allowed-key 比较均使用 JS_ToCString/strcmp：
包含 NUL 后缀的 enum 会匹配合法前缀；同类属性名会通过 key 校验，随后因正常
字段查找不匹配而被静默忽略。改为 JS_ToCStringLen、完整长度及内容比较，保持
唯一共享 helper，未复制另一套无线验证器。该修复同时作用于原 helper 的其他
用户，合法字符串行为不变。没有运行前/后动态复现；新生产路径回归源码已写，
按用户的测试后置要求登记 not-run，不声称失败用例已执行。

## 尚未开放的契约与验证

公开 configure 仍未注册。缺省 mode/storage/start 的当前状态解析、AP 客户端
授权与模式协调、最终结果/错误字段、Station readback、共享 AP/stopAP、高级
认证 credential owner 仍需接入；当前个人安全 parser 不代表全部高级认证。
候选字段仅写入任务书/调查记录，没有进入正式 JS 类型或 callable registry。
当前没有 JS caller 调用外层 helper，链接器可裁剪它；编译记录含 wifi_config.c
object，仅证明实际 SDK 类型下可编译，不是 API 可运行或完整 feature 验收。

新增 `test_wifi_configuration_capture.py` 使用生产外层/raw 捕获、共享 options/
secure-zero/ByteView/MQuickJS 和实际 Radio 纯 validator；个人安全 parser 是
明确注入边界，其完整实现已有单独测试。覆盖完整复合输入、presence、整数/
长度/枚举/NUL key、错误后全体清零、每次属性/分配失败与移动 GC、C3/C5/AP-disabled
gate。仅 AST 检查，所有用例 **not-run**。任务书双频示例的 11AC bandwidth 已
改为固定 SDK 支持的 20 MHz，不再给出 11AC+HT40 组合。

必要 C5 immutable Build Context 编译、MQuickJS syntax、manifest/features、
raw schema/live SDK、recorded map 和 whitespace 的结果及 hash 见
`build/w02-outer-capture-evidence.json`。Host C/Python、三目标/disabled 矩阵与
实机功能等全部 Wi-Fi API 完成后集中执行；长时间 soak 等 BLE API 完成。
本批未刷写、串口操作、擦除 workspace、提交、推送或更新父 gitlink。
