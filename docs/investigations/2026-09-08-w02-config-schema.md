# W-02：完整 SDK 字段映射与 Station 频段偏好

基线 firmware `d7db8d1`，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
前批共用配置执行器已接入 startAP；本批继续公开 configure 前的完整输入 schema，
没有把尚未实现的公开配置方法加入注册表。

## 字段核对与生成

`wifi_sta_config_t` 在展开 threshold 和 PMF 后有 37 个 leaf，其中 2 个 reserved；
`wifi_ap_config_t` 展开 PMF 和 BSS idle 后有 22 个 leaf，其中 1 个 reserved。
公开字段总数为 Station 35 / AP 21。不能只数最外层成员，否则漏掉
threshold.rssi_5g_adjustment 等配置。

新增 `docs/wifi-driver-config-fields.json` 记录每个字段的固定 C 声明、拟定 JS
字段和类型、单位、边界/依赖、适用 gate、秘密属性与写入方向。reserved 不导出；
PMF capable 已被 SDK 弃用，不能用 false 承诺关闭 PMF；BSSID enable 和 AP SSID
length 由输入推导。拟定 driver schema 用原始 TU 表达 beacon，现有 startAP
仍保留已实现的毫秒接口。原始 SSID 输入形式和新增字段解析尚待实现。

`scripts/generate_wifi_config_schema.py` 复用已审核的预处理 inventory，递归展开
命名结构；五个记录 variant 逐一比较，不从第一个 target 推断其他 target。
字段新增/删除、类型或 bitfield 宽度变化、未知指针/递归布局、重复 JS 字段、
SDK revision/header hash 漂移都会失败。`--idf-path` 还检查当前磁盘上的 SDK
配置 header。凭据字段不能去掉 secret 标记。

输出 `docs/generated/wifi-driver-config-schema.md` 为完整字段的输入提案，明确
contract-pending。它不注册 API、不推断启用状态，不表示 parser、目标特性或 RF
通过验收。正式类型仍只包含当前已实现的选项。原始 config 的所有交叉字段/gate
实现与秘密读回还需在公开 configure/driver API 接入时完成。

## 实际新增参数

`wifi.connect` capture 新增 `rssi5gAdjustment`，对应 SDK
threshold.rssi_5g_adjustment：整数 0..255 dB，默认 0；在候选 AP RSSI 差距内优先
考虑 5 GHz。不是 band 强制选择，也不改变独立 scan 结果。有限整数检查与
目标 gate 均在 native 操作之前，未支持 5 GHz 时显式 0 同样返回 unsupported。
`wifi.capabilities().stationOptions.rssi5gAdjustment` 同步实际 target gate；类型、
API 文档同步。没有新增 top-level callable，仍为 18 个。

## 验证与待运行用例

C5 immutable Context 编译通过，app `0x27b100` bytes，分区空余 17%。生成 schema
检查覆盖五份 recorded variant，并核对当前 SDK header。MQuickJS/manifest/
feature/map/文件语法与 hash 见 `build/w02-config-schema-evidence.json`。

已写、未运行：生产 schema validator 的嵌套新增字段、单 variant 位宽/删除漂移、
指针/递归、重复/保留字段错误导出和秘密属性丢失；生产 Station extension parser
配合真实 MQuickJS 的数值边界、类型拒绝、两个编译 gate、逐 property OOM 与
移动 GC。该 parser 用例注入 SDK 数据类型/错误边界，不建立 RF 偏好效果证据。

按用户顺序，完整 Host C/Python、其他目标/feature-disabled 构建和实机功能测试
留到全部 Wi-Fi API 完成后；soak 放到 BLE API 完成后。无串口、刷写、提交或
父仓库 gitlink 更新。完整 schema parser、公开 configure/APSTA 和后续模块未完成。
