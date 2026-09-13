# Raw TX 全部已命名管理帧实验扩展

日期：2026-09-13。实施起点：firmware `c90047b`；本地 ESP-IDF
`fff9895c82d744c7237be8847347bdd1b07c6643`。本记录对应用户要求
“全部，用于完整实验平台”，不代表 RF 或完整协议交互验收通过。

后续收发类型 API 已统一为数值描述对象；下文字符串数组是本轮扩展时的
验证形态，当前唯一 v1 以[帧身份统一记录](2026-09-13-wifi-frame-identity.md)
和 API 文档为准。

## 范围与公共契约

Raw TX 现在接受全部 14 种已命名的 PV0、未设置 Protected 位的管理帧：
Association Request/Response、Reassociation Request/Response、Probe
Request/Response、Timing Advertisement、Beacon、ATIM、Disassociation、
Authentication、Deauthentication、Action、Action No Ack。
原有 Non-QoS Data subtype 0 保留。

`capabilities().frameTypes` 使用与发送结果相同的 `WiFiRawTxFrameType`
字符串数组；数组是快照，顺序无意义。`supports.extendedManagement`
标明本构建是否接入经过审核的 SDK 扩展。单次发送、Session 和周期发送
共用生产准入与完成转换逻辑。类型、API 文档和设备 JS 契约断言已同步。

管理帧保留 subtype 7/15、非零 Protocol Version、Protected/PMF 变体、
控制帧、QoS 和 Null/CF Data 仍不支持。本次不提供管理帧 body/IE 构造器，
不更改 PMF 配置，不执行管理帧加密或认证，也不改变接收端的 PMF 校验。

## 原因与实现

本地 SDK 的 `esp_wifi_80211_tx()` 公开契约只列出 Beacon、Probe Request、
Probe Response、Action 和 Non-QoS Data；SDK 实际准入函数也拒绝其余管理
子类型。仅扩展框架枚举不足以实现发送。

`scripts/patch_idf_raw_tx_management.py` 对 C3/S3/C5 的
`libnet80211.a:ieee80211_output.o` 执行完整 SHA-256 门禁，再仅修改
`ieee80211_raw_frame_sanity_check` 中管理帧子类型分支的比较操作数。
分支位置在 management 类型与 Protected 检查之后，跳转目标、重定位、
指令长度及其余对象字节保持原样。具体哈希与指令对照以脚本为准。

SDK 此处扩展到 16 个管理子类型，框架生产 validator 进一步拒绝保留的
7/15，形成公开的 14 种准入集合。不能将框架的保留子类型检查归功于
修改后的 SDK 分支。长度、接口、锁、关联状态、序号与 Data 路径约束保留。

CMake 在既有 SDK 修复之后生成独立的构建目录 archive，保留此前的
off-channel 等修复；只有哈希检查与补丁成功才定义
`ESP32_MQUICKJS_RAW_TX_EXTENDED_MANAGEMENT=1`。未知对象或不支持的
后端终止构建。共享 ESP-IDF 目录未修改。Wi-Fi 关闭配置不启用该扩展。
这是超出 Espressif 公开支持范围的实验性框架实现，稳定等级不提升。

## 验证证据

本地过程日志和 JSON 位于忽略目录 `build/raw-tx-management/`；可重复的
用例保存在 `tests/c/integration/wifi/tx/` 与对应 `fixtures` 中。

| 检查 | 实际结果与边界 |
| --- | --- |
| 修复前复现 | `before-admission.json`：真实 C3/C5 SDK 拒绝 Association Request，期望扩展准入的断言失败 |
| 生产 validator 与 MQuickJS 转换 | `native-contract.json`：7/7；包含扩展开/关、全部 type/subtype 组合、结果字符串、能力快照、分配失败及移动 GC |
| 原生队列与生命周期回归 | `transport.json`：4/4；broker、Session、结果收集和周期任务 |
| 真实 SDK 发送入口最终验证 | `sdk-production-final.json`：2/2；C3/C5 原始与修改后对象在 QEMU 中执行真实准入函数和 `esp_wifi_80211_tx()`，覆盖接口、长度、Protected、关联序号、分配失败和锁平衡 |
| SDK 修改范围 | 上述最终 SDK 用例同时覆盖 C3/S3/C5 原对象及既有 off-channel 修复对象、逐字节范围、重定位、重复/损坏对象拒绝、archive 其他成员不变、链接及反汇编 |
| 公共生成物 | `contracts.log`：API manifest 62 classes / 662 functions；Wi-Fi coverage map 1267 entries；分类数量不代表新增功能数量或硬件通过 |
| MQuickJS 语法 | `contracts.log`：69 sources / 70 documentation snippets 通过；设备 JS 未执行 |
| 合法 Build Context 增量构建 | `build-matrix.json`：C3、S3、C5、C5 Wi-Fi disabled 全部 returncode 0 |

最终 SDK 的 2 个用例属于前述 7 个用例中的复跑，不另计为新增通过数。
测试中的 HMAC 入队函数被替换为可观测注入边界；断言发送字节与序号标志，
不执行真实 HMAC 任务或射频发送。S3 只有补丁范围、链接、反汇编和固件构建
证据，没有模拟器执行证据。

构建使用已有不可变 Context：

- C3：`build/wifi-stage-tests/contexts-link-retry/c3-nan-usd-quota`。
- S3：`build/wifi-stage-tests/contexts-final/s3-wps-registrar`。
- C5：`build/wifi-stage-tests/contexts-final/c5-no-softap`。
- Wi-Fi disabled：`build/wifi-stage-tests/contexts-final/c5-disabled`。

四项均通过 `scripts/remote.py --build-context … --build-dir … --assume n build`
执行。最终核对三个启用构建的生产编译命令含扩展宏、链接到独立 archive；
关闭配置无该宏和 archive。共享 SDK 的 `git status --short` 为空。

保留的首次夹具编译、链接及 C3 描述符偏移失败是测试夹具调试过程，不能
归因为固件缺陷；最终入口夹具分别使用 C3/C5 实际描述符布局。首次构建的
Python 环境缺少 `rich_click`，在现有 ESP-IDF 环境下重新执行后四项通过。

## 未执行的验收

- Flash、设备 JS 运行、真实 TX 完成回调：`not-run`。
- 每一种管理帧的对端抓包、空口内容与接收行为：`not-run`。
- 关联/认证等完整交换、PMF 交互与多设备共存：`not-run`。
- 长时间和完整生命周期压力测试：`not-run`，依用户要求保留到阶段验收。

本次仅完成实现与上述软件验证，不宣称完整无线实验平台已通过硬件验收。
