# W-02：配置缺省解析、AP 停机授权与捕获执行交接

基线 firmware `d7db8d1`，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
继续[外层捕获](2026-09-08-w02-outer-capture.md)，本批仍未注册公开 configure。

## 当前代码

新增 `esp32_mquickjs_wifi_radio_begin_configuration_lifecycle`。它在同一次 Radio
operation mutex 内解析缺省、执行最终 mode/start 的纯校验、检查运行 AP 的停机
授权，然后调用原有精确三 owner admission 发布 token。没有 SDK 调用、分配、
JS 转换或凭据存储。解析或准入失败保持 selection、token、lease registry 原样。

| 缺省字段 | 已配置且健康的 driver | 未初始化或当前 mode 为 NULL |
| --- | --- | --- |
| mode | 保持当前 effective mode，不根据新输入自动扩展 | 根据 station/accessPoint 组合选择；均未提供时 Station |
| storage | 保持当前 RAM/Flash 选择 | 未初始化时 RAM；mode 为 NULL 不改变已知 storage |
| start | 保持当前 started | 未初始化时 true；mode 为 NULL 不改变已知 started |

presence 位不改成 true；它们继续表示原输入是否指定。mode 推导后，接口/PHY/
省电冲突及 start=false/TX power 冲突再次验证。当前若最终 start=true 且含 AP，
必须完整提供 accessPoint；没有实现借读旧 AP 秘密配置自动补全。Station 配置
缺省时不覆盖既有配置，也不自动发起连接。这个规则属于已编码的内部候选契约。

故障、未结束的 stop、restart-required、过渡状态均不通过缺省推断绕过。
`stop_required` 在健康运行时也为 true（表示未来必须执行 STOP），因此只有
未 started 但仍有该义务时才据此拒绝；真实故障和清理状态另行拒绝。

## AP 与 Station 的授权边界

配置事务需要停止旧 driver。只要旧的 running mode 含 AP，就要求显式
allow_disconnect=true，包括此刻查不到客户端的 AP。当前公开 SDK 没有在客户端
快照和 STOP 之间禁止新关联的原子边界，零客户端结果不能作为不需授权的证据。
此检查不查询客户端列表，不把 Radio mutex 描述为能锁住无线端关联表。

该权限只覆盖本次配置所管理的 Wi-Fi Station 断连和 AP 停机，不能取消 Future、
扫描、native operation，不能关闭 ESP-NOW、CSI、wake lock 或其他 owner。Station
已连接时仍由真实 helper admission 检查同一权限，随后沿用 disconnect epoch/
netif/IP fence。任何准入拒绝都发生在 AP/STA owner 释放和 STOP 前。

`wifi.stop()`/runtime 的显式清理保留自己的旧 begin 路径；它们的 AP 关闭意图
不依赖配置的 allowDisconnect。未更改公开 startAP 的独占冷启动限制。

## 接入与读回

共用接口执行器拆为 `wifi_configure_selected_interfaces`。现有 startAP 所调用的
`esp32_mquickjs_wifi_configure_interfaces` 构造显式 selection 并走新准入，继续
执行原有 helper 退休/准备、快照事务、START 屏障与 owner 发布。AP-disabled
adapter 同样转发到新 Radio 准入，不引用 AP helper storage。

新增内部 `esp32_mquickjs_wifi_apply_configuration`，把实际 outer capture 的
presence、配置和 controls 交给同一执行器；完整成功后才返回解析后的三项选择。
输入及其秘密仍属 caller，成功/失败均须 secure-zero；失败只保留原有中央清理
义务，不保存或自动重放 caller 配置。该 capture/apply 入口尚无 JS caller，链接器
仍可裁剪它；新准入及显式 wrapper 已在实际 startAP 链路。

Station 配置接受器复用原有逐字段 semantic equality，校验 SSID、password、PMF、
认证、PHY 等字段；不比较 native padding，不允许读回悄悄降低安全要求。实际 SDK
若有未支持的规范化，会报读回失败并进入既有回滚，而非假定为等价。此行为仍待
SDK/硬件阶段验证，不将 host 声明映射当作认证验收。

## 验证与剩余事项

新增 `test_wifi_configuration_selection.py`，抽取生产 selection、精确 lease/token
admission、纯 validator、semantic equality；使用已记录的 C3/C5 SDK 类型，注入
mutex 入口的状态变化检查缺省读取顺序。覆盖冷/热/停机缺省、AP 授权、外部 owner、
wake/native/promiscuous、旧身份、identity 耗尽、AP-disabled、安全字段读回。
这不是 pthread 或实机并发验收；没有提供任何 SDK 调用替身，准入不应调用 driver。

更新实际 executor 和 AP/STA helper fixture，覆盖 selected admission 拒绝后不释放
owner、同一权限传递、所有失败后缀、捕获执行结果仅成功发布。测试源码只作 AST
检查，Host C/Python 及新用例全部 **not-run**，遵守用户的集中阶段测试安排。

必要 immutable C5 编译、MQuickJS syntax、manifest/features/raw schema/live SDK/
recorded map 及 whitespace 结果和 hash 见 `build/w02-selection-evidence.json`。
未执行 C3/S3/disabled 构建矩阵或实机功能；长时间 soak 留到 BLE API 完成之后。
未刷写、操作串口、擦除 workspace、提交、推送、构建前端或更新父 gitlink。

下一步是公开 configure 的绑定、最终结果/错误语义与已支持 raw 类型同步；高级
认证 credential owner、共享 AP、保留 Station 的 stopAP 与 W-03～W-12 仍未完成。
完整剩余范围见[当前清单](2026-09-08-wifi-api-remaining.md)，不把本内部交接写成
整个 configure 或 Wi-Fi feature 完成。
