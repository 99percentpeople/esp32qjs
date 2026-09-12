# W-07：协议与带宽 SDK 读回

firmware HEAD `d7db8d1`，固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批新增唯一 v1 的 wifi.driver.getProtocol/getProtocols/getBandwidth/getBandwidths，
正式绑定、类型、API 文档和 SDK 覆盖表同步。完整 Driver、恢复协调器及 Wi-Fi 总目标
仍未完成，详见[剩余清单](2026-09-08-wifi-api-remaining.md)。

## 契约与实现

公开接口严格要求一个 station/access-point 字符串，拒绝 NUL 后缀、转换型输入和
多余参数。SoftAP 编译关闭时 AP 返回 unsupported。所有 SDK 调用经过真正 Radio
mutation mutex，要求已初始化、storage 已配置、接口在当前 mode 中启用、状态为
稳定 started/stopped；lifecycle/operation、fault、cleanup、restart-required 均拒绝。
读取不创建 owner、不启动 driver、不改配置，也不进入 IRQ critical section。

固定 esp_wifi.h 的单数 getter 明确不支持 WIFI_BAND_MODE_AUTO；本实现保留 SDK
错误，不选择某个频段冒充结果。C5 的复数 getter 先在同一 mutex 内读 band mode，
再读 protocols 或 bandwidths，只输出当前模式中的频段；C3/S3 使用单数 SDK getter
形成仅 ghz2 的对象。SDK 未填或属于未启用频段的 storage 不会被转成默认配置。

协议按真实 SDK 位映射 11b/11g/11n/11a/11ac/11ax/lr；未知位返回 INVALID_RESPONSE。
带宽只接受实际 WIFI_BW20/WIFI_BW40 并转为 MHz。两个方法的返回是分别取得的配置
观察值，不是组合原子快照，也不是协商后的 link PHY、实际吞吐或 RF 证明。

原生读回先存局部值，成功验证后才发布；失败输出全清零。错误保留原始 espCode，
报告 admission/band-mode/protocol(s)/bandwidth(s)/decode 阶段。公开错误为
WIFI_DRIVER_READ_FAILED，details 含 interface/espCode/stage。JS converter 的 array、
item、外层对象均采用已有 GC ref/property helper；分配失败不会修改原生配置。

## 验证边界

test_wifi_driver_phy.py 准备调用生产 Radio getter，注入 SDK getter 和 mutex/state
边界，使用三目标 inventory 类型并覆盖 SoftAP on/off、cold/transition/fault 拒绝、
单数 AUTO 错误、双频/单频字段、未知 enum、SDK 失败和部分输出丢弃。
test_wifi_driver_phy_gc.py 使用生产 converter 和 interface parser，准备覆盖所有返回
形状、第 N 次分配失败和移动 GC。两份文件仅 AST parse，未导入/编译/执行。

C5 immutable Build Context firmware-ci-esp32c5-representative 编译 exit 0，ELF
确认链接四个公开函数、Radio read_phy 和两个 converter。binary `0x29b7d0` /
2,734,032 bytes，比终止证据批次增加 2,960 bytes，app 空余 13%。未增加静态 Radio
账本：s_radio=824、s_raw_tx=136、s_tx_rates=68、s_tx_rate_lease=36、s_sessions=32、
s_jobs=32、s_retired=56、s_lane=44 bytes。动态 heap/硬件内存未测量。

MQuickJS 61 sources/53 snippets、manifest 49 classes/437 functions、strict TypeScript、
feature docs 27、config schema 35 STA/21 AP（live SDK）、recorded SDK map 和 whitespace
通过。manifest 检查发现生成器选用的 wifi.md 缺少四个 callable 索引，仅更新
wifi-driver.md 不满足该检查；补齐主文档索引后重查通过。日志、链接符号及 hash
见 build/w07-phy-read-evidence.json。

SDK 工作区干净。Host C/Python/VM/故障注入、真实并发、C3/S3/disabled 构建、实机
功能/RF/heap 均 not-run，保留 Wi-Fi API 全部完成后的集中测试安排；长期 soak 留待
BLE API 完成。本批未刷写、串口操作、擦 workspace、构建前端、提交、推送或更新
父仓库 gitlink。协议/带宽 setter、其余 Driver 与完整配置恢复继续在 W-07 范围内。
