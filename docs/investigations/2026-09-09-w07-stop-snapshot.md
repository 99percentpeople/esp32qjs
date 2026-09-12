# W-07 STOP 前的功率/信道历史快照

firmware `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接 [runtime restart executor](2026-09-09-w07-restart-runtime.md)。本批接入真实
STOP 路径的历史观察，为停止状态恢复保留不能在 STOP 后查询的信息；没有将这些
历史值直接作为后续配置的当前值，也没有注册公开 restart。

## 固定 SDK 证据

从当前 C5 `libnet80211.a` 重新提取 `ieee80211_api.o`，保存 archive/object hash
和相关函数反汇编至 `build/w07-stopped-sdk/`。

`esp_wifi_get_max_tx_power` 在初始化检查之后读取 SDK driver 状态；状态不满足
START 时直接返回 `0x3002`，不会调用 `phy_get_most_tpw`。`get_home_channel`
同样先做状态检查，START 后才提交原生 getter ioctl。公共 `esp_wifi.h` 的功率
getter 声明也明确限定 after WiFi start。该证据证明 STOP 后不能读取，不证明
PHY 中的值必然被清零或下一次 START 一定保留它；本批不据此推导默认恢复值。
没有调用 SDK private entry 或修改 SDK；C3/S3 对应实现的运行验证仍待执行。

## 实际生产路径

`wifi_radio_stop_lease_locked()` 成功建立新的 STOP event identity 后，在首次
`esp_wifi_stop()` 提交前调用真实 `wifi_radio_capture_stop_snapshot_locked()`。
同一 STOP 的 SDK 失败重试、已接受 STOP 的事件/fence 排空只走原后缀，不重新
采样。若 driver 本来就没有 stop_required，不制造新的观察。临时 Raw TX rate
owner 的合法 STOP 同样经过此入口。

观察依次读取精确 quarter-dBm TX power、band mode/band、current channel、home
channel，并再次核对 band mode/band，验证信道/secondary 匹配及值域。mode 字段
记录框架当时的 expected mode；它不是额外 SDK mode getter。观察不是跨全部
SDK getter 的原子 RF 快照，不能据此声称无线端没有异步变化。

只在全部读取成功后填写功率/频段/信道 payload；部分 SDK 输出不会发布。固定
20-byte `s_stop_snapshot` 记录 driver generation、STOP identity、步骤和原始错误。
initial stop_identity=0 表示尚未尝试观察。SDK 读取失败、不完整/非法值、故障或
未启动 driver 只更新观察错误，不给 Radio 增加故障，不阻止原有 STOP。

该记录不含凭据、不分配 heap、不保存 JS/回调指针；SDK 调用位于 mutation mutex
内、临界区外。内部 getter 只复制历史记录，不查询/启动 driver。下一次观察尝试
覆盖前一份记录；物理 generation 变化后依然带旧 generation，不能误作新 driver
当前状态。此处尚未建立停机配置写入、再次 START 或跨 modem 变化后的恢复准入，
因此现有 restart 捕获不会自动消费此记录。完整的停止状态 checkpoint/配置保留
及公开准入仍待完成，不能把“已采样”写成“stopped restart 已实现”。

## 验证范围

C5 immutable Context `build/wireless-contexts/c5` 生产构建 exit 0，日志
`build/w07-stop-snapshot-c5-build.txt`。capture helper 已链接到最终 ELF，内部复制
getter 尚无调用而被移除。binary 2,780,512 bytes（较前批 +432）；新增静态记录
20 bytes，原九项静态账本不变，原完整 restart checkpoint 688 bytes/control
40 bytes 不变。此为编译尺寸，不是实机内存/碎片比较。

新增 deferred `test_wifi_stop_snapshot.py` 调用真实捕获与历史读取，注入 SDK/
锁/原生存储，编写精确功率、2G/5G、逐 getter 失败、部分输出、非法功率和
secondary、current/home 不一致、未启动/故障拒绝、generation 变化后仍只返回
历史观察的用例。现有 Raw TX rate fixture 在真实 STOP 路径注入观察边界，核对
首次 SDK STOP 前已观察及重试不重复观察。两份仅 AST 解析，未导入、编译或执行。

manifest 49 classes/469 functions、feature 27、live SDK config schema 35 STA/
21 AP、MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map 和 whitespace
检查通过。hash/产物及静态账本见 `build/w07-stop-snapshot-evidence.json`。

Host/Python/VM/竞争、SDK getter/STOP 实机执行、完整 stopped restart、C3/S3/
disabled 构建、实机/RF/共存均 **not-run**。全部 Wi-Fi API 完成后统一阶段测试
和实机功能验证，长 soak 留到 BLE API 完成后。未刷写、串口操作、擦除 workspace、
构建前端、提交、推送或更新根 gitlink。完整任务范围仍见
[Wi-Fi 剩余清单](2026-09-08-wifi-api-remaining.md)。
