# Wi-Fi 收发帧身份统一

日期：2026-09-13。本次接续管理帧 Raw TX 扩展，按用户“继续”推进 API
统一，未扩展控制帧、QoS、Protected 或其他 Data subtype 的发送准入。

## 当前唯一 v1 契约

- `WiFiFrameSelector`：`{ type: 0 | 1 | 2 | 3, subtype: number }`，subtype 为
  0–15 整数，表达 PV0 的全部 64 个数值组合；可附带 name 校验名称。
- `WiFiFrameType`：上述身份加 `name: WiFiFrameName | null`。共用生产解析器
  的名称表；未知或保留布局为 null。数值身份不是解析、发送或 RF 支持保证。
- Raw TX 的 `capabilities().frameTypes[]` 和发送结果 `frameType` 使用此
  描述结构。普通 Data 统一为 `data`，不再使用 `non-qos-data` 字符串。
  已启用扩展的构建仍仅声明 14 种管理帧和 1 种 Data；发送仍从原始字节
  推导 type/subtype，不增加与帧头可能冲突的显式发送选项。
- Monitor 的 `packetTypes` 是 SDK 回调大类；`frameTypes` 是生产解析器
  认识的 39 种布局描述：14 Management、10 Control、15 Data。
- Monitor/CSI 的 `info.packet.category` 表示回调/接收分类；`frameType`
  表示从 PV0 MAC Frame Control 取得的描述。缺少可读的 PV0 Frame Control
  时为 null。原有平铺的 `type/subtype/subtypeName` 已替换，不保留别名。
  Monitor 的头部与 SDK 类型冲突或 MAC 头不完整时，身份仍可存在，必须
  单独检查 `capture.parseValid`。CSI packet 仍要求生产 packet 路径先完成
  结构校验。本次不改变 native wire 布局。

类型名称通过共享的 `esp32_mquickjs_wifi_frame_type.c` 转换；能力查询每次
新建数组及描述对象。名称目录来源于已有公共 RX 表，避免复制另一套收发
名称表。协议的数值 type 3 与 SDK 的 misc 分类保持区分。

## Monitor 精确过滤

`filter.frames` 接受至多 64 个无重复 `{ type, subtype }`，对列表内的配对
取并集，与 `types`、`subtypes`、MAC、RSSI 等其他条件取交集。省略表示不
限制，空数组匹配零帧；type/subtype 必须是数值，拒绝未知键、缺失字段和重复配对。
可直接传入能力描述对象；可选 name 必须与公共目录中的数值配对一致。
`open()` 与 `configure()` 复用同一生产参数捕获路径。

例如 `[{ type: 0, subtype: 8 }, { type: 2, subtype: 0 }]` 只选 Beacon 与
普通 Data，不会误选 Management subtype 0 或 Data subtype 8。原生过滤
用四个固定 16 位 mask，回调不分配内存。身份过滤要求有效 PV0 FC 且 SDK
分类匹配；`validOnly` 另外要求完整的结构解析和无 SDK 接收错误。当前 misc
回调只有元数据，type 3 布局没有解析支持，不因过滤器可表达而获得接收能力。

## 验证

证据目录：`build/wifi-frame-model/`（本地忽略的过程产物）。新增断言调用
生产参数捕获、原生过滤、SDK 元数据解析和真实 MQuickJS 转换实现。

- `before.json`：修改实现前，新描述结构与精确配对参数两个用例均失败。
- `native.json`：首轮覆盖 Raw TX、Monitor 参数/过滤/元数据/能力以及 CSI。
  11 个用例通过；新增元数据断言把夹具的普通 Data 写成 QoS Data，另有
  CSI 组合夹具未剥离平台头依赖，分别修正测试预期和组合方式。
- `receive-final.json`：上述修正后，Monitor 元数据和 CSI 的 7/7 通过，
  包括 packet 转换、Frame/Batch/View/Source、分配失败与移动 GC。
- `descriptor-final.json`：Raw TX 与 Monitor 能力的 5/5 再通过，增加完整
  64 个身份转换、未知名称、越界拒绝和能力对象修改后重新查询的快照验证。
- `options-final.json`：接受完整能力描述并校验 name 后，参数捕获的 5/5
  通过；`selector-boundaries.json` 再补查全部 64 个配对、65 项拒绝、空
  列表、重复和不一致名称、各 getter 单次读取以及失败路径 GC。
- `native-final-summary.json` 汇总最新结果：18 个相关原生用例通过；复跑
  不重复计数。
- `python.log`：29 个公共契约、manifest 和 CSI 架构检查通过；新增 Python
  用例只检查 TypeScript 名称集合与 C 目录一致，原生行为留在 C 夹具。
- MQuickJS 语法：69 个源文件、71 个文档片段通过；生成 manifest 和 Wi-Fi
  coverage map 一致性检查通过。本次没有新增公共可调用函数。
- C3、S3、C5、C5 Wi-Fi disabled 的最终增量构建全部通过；命令、退出状态
  和日志见同目录 `build-matrix.json` 与各目标 build log，复用上一阶段
  合法且不可变的 Build Context。名称校验调整后重编译受影响对象并链接，
  没有清理构建目录或重新全量编译。

设备 JS 已更新契约断言但未执行。未 flash，真实射频、完整协议交换及
长时间测试均为 `not-run`。稳定等级仍为 candidate。
