# CSI / Monitor 帧过滤契约统一

当前开发工作区接续 frame identity 与 scan partial-result 修改；固件 HEAD 仍为
`c90047b`，本轮不提交、不改根仓库 gitlink、不刷设备。

## 问题与实现

原 CSI 的 `frameTypes` 类别和 `frameSubtypes` 独立匹配，不能表达只选择
Beacon (0,8) 与普通 Data (2,0) 而排除交叉组合。新增生产选项测试先失败：
`build/wifi-filter-unification/before.json`，旧实现拒绝 `filter.frames`。

- Monitor 与 CSI 复用 `WiFiRxFilter`：`types`、`subtypes`、`frames`、MAC、RSSI、
  decimation 和 rate limit。CSI 的旧字段直接从 sole v1 替换，不保留别名。
- `types` 为 management/control/data/misc；unknown 仍可出现在观测元数据中，
  不是显式类别筛选项。遗漏字段不添加条件，空数组不匹配任何帧。
- `frames` 复用 `wifi_common/esp32_mquickjs_wifi_frame_filter.c` 的生产解析器，
  最多 64 个唯一 PV0 type/subtype 对；可选 name 校验共享目录。解析成功后
  才复制四个 uint16 掩码，失败不发布部分输出。
- 同一列表 OR，各过滤项 AND；CSI 回调只使用本次可信、完整解析的帧头，
  无帧头、非 PV0 或未知布局不能伪装成有效匹配。数字 type 3 可表达，但当前
  CSI 无相应解析布局，且它不等于 SDK misc。
- packet.content=none 仍可筛选，不分配 packet storage，回调不创建 JS 对象。
- requested.filter.frames 为按数字顺序生成的独立描述符快照，支持 OOM/GC；
  pair 拒绝复用 filteredFrameSubtype 计数。
- maximumRateHz=0 在两者均表示不限速；CSI validOnly 保持 CSI 样本/信道估计
  判断，Monitor 保持 MAC parse/RX status 判断，并在共享类型中明确区别。

## 验证

证据目录：`build/wifi-filter-unification/`。

- 原生 VM/回调集成 17 个不同用例通过：options.json 中 Monitor 5 项，
  options-csi-final.json 中 CSI 4 项，以及 capture.json 中 8 项。
  覆盖第 N 次失败、移动 GC、getter 单次读取、0/64/65 上界、名称/重复/类型
  校验、快照隔离、原生回调无包存储、缺失帧头与交叉组合，以及已有 owner/pool 回归。
- 额外 CTest test_wifi_csi_filter 1/1：64 对循环匹配、类别/子类型条件 AND、
  空集合、未知类别；测试更换 decimation 配置时须同时复位 phase。
- Python 契约 39/39：共享类型、名称目录、Radio/CSI 架构。
- manifest 一致性：62 classes / 662 functions；无新增注册方法。
- MQuickJS 语法：69 sources / 71 doc snippets；新增设备 JS 契约断言仅通过语法检查。
- 使用现有合法 Build Context 的 compile_commands，对 4 个受影响生产 C 文件
  分别进行 C3/S3/C5 编译，12/12 通过。输出隔离在证据目录，见
  target-compilation.json。没有修改现有构建对象或 SDK。

本轮完整链接/feature-disabled 构建、设备 JS 执行、RF、共存及长测均 not-run，
留待阶段验证。既有完整构建结果不冒充本轮链接结果，不提升 candidate 等级。
