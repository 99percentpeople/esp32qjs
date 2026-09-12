# W-04A：Raw TX 输入校验与完成快照

已增加生产 Raw TX 模块中的 MAC validator 和 TX callback snapshot，并接入组件
CMake。C5 目标对象已编译，但没有调用方，最终 ELF 尚未链接这两个 helper；这不是
Raw TX API 已实现。Radio lease/activation、单 in-flight broker、callback 注册/注销、
timeout quarantine、ByteSource ownership、Future/关闭及公开绑定仍待完成，之后再做
queue/batch/periodic。没有把 contract-pending 接口加入正式类型/manifest。

基线 firmware HEAD `d7db8d1e40ee9f6a522c22806252b2ef712843a7`，固定 SDK
`fff9895c82d744c7237be8847347bdd1b07c6643`，本批修改未提交。

## SDK 证据

直接核对本地固定 SDK，不依据其他 IDF 版本猜测：

- `components/esp_wifi/include/esp_wifi.h` 的 esp_wifi_80211_tx 注释：24–1500 bytes、
  STA/AP/APSTA 的 interface 选择、Beacon/Probe Request/Probe Response/Action/
  non-QoS Data 范围，以及已有 Wi-Fi connection 时 en_sys_seq 必须为 true。
- `docs/en/api-guides/wifi-driver/wifi-vendor-features.rst`：以实际 Addr1/Addr2 与
  本接口/关联 peer 判定连接路径。该路径的数据帧有 STA ToDS/AP FromDS 要求，所有
  该路径发送帧的 Power Management/More Data/Retry 应为零；MAC 地址建议并非硬性
  driver 约束，未新增默认拒绝任意地址的限制。
- `esp_wifi_types_generic.h` 的 wifi_tx_info_t：src_addr/des_addr/data 是 callback
  指针；data_len 是 uint8 的 body report，不是完整 MAC frame 可读跨度。回调没有
  operation cookie。单靠地址或 data 指针不能证明一次请求的身份。

## 无副作用 MAC 校验

validator 只读 caller 已证明且保持稳定的输入 span，无 SDK/JS 调用和分配；失败不写
output，不改输入。检查 NULL、24–1500 长度、指针区间溢出及 output 与 input/policy
重叠后才读取 header。完整 header 验证复用生产 RX parser，包括 Addr4 与 management
Order/HT Control 的可变长度，不重复另一套 header 状态机。

当前明确 allowlist 为 management subtype 4/5/8/13 和 Data subtype 0；没有声称
支持 Null/CF、QoS、Control、其他 management、encrypted/Protected frame。其他
non-QoS 子型的支持需另有 SDK/目标证据才可扩展，公共能力不能笼统宣称任意 Data。

原生 policy 的 connection_active 要求 driver sequence；associated_path 表示 native
已按 SDK 的地址定义识别到真实关联路径，不允许从 JS 接收这一布尔断言。针对数据帧
检查接口对应的 DS bits；该关联路径的所有允许 frame 类型都检查 PM/MoreData/Retry。
无连接时 driver/application sequence 都可通过。地址不属于实际关联路径的帧不被
额外套用关联数据的 DS 要求。

policy 只是前置快照，未来 Radio driver admission 必须重新核对关联/lease；当前 helper
不能消除无线关联状态变化，也不能当作公开“无效输入不调用 driver”集成测试已通过。
strict/basic 的公开 capture 与完整发送边界尚未实现；这套检查供两者复用必要约束。

输入契约是无额外 FCS 的纯 MAC frame。validator 不解析任意 management/action body，
也不能从任意 Data 最后四字节可靠判定调用者是否追加了 FCS，或识别所有伪装成合法
header 的容器字节。不得将 header 校验通过描述为 FCS/容器自动识别或 RF 可发送证明。

## 无指针 callback snapshot

只在实际 SDK callback 内调用；复制 callback-time、STA/AP、success/failed/unknown、
raw rate/status、uint8 body report，以及非空时各六字节 MAC。NULL 地址用明确
availability=false 和全零存储；不保存 src/des/data 指针，完全不解引用 data。
输出在全部输入复制完成后提交，允许有足够存储的合法输入/输出别名。

新增静态断言固定 data_len 的 1-byte 宽度，SDK 类型改变时强制复核。snapshot 没有
虚构 operation identity、完整帧长度、应用 ACK 或可靠业务送达。无 cookie completion
必须由后续单 lane/quarantine 生命周期承担，不能通过此快照提前复用超时 lane。

## 测试源码与验证状态

新测试源码均调用这些生产实现，没有编译或运行：

- `test_wifi_raw_tx_validate.py`：完整 type/subtype allowlist 表、全部非零 protocol
  version、Protected/QoS、最小/最大/溢出长度、Addr4/HT Control 短 header、连接
  sequence、STA/AP 四种 DS 组合、关联 management/data 的 PM/MoreData/Retry、
  非关联地址路径、不一致 policy、NULL/output alias/指针区间溢出、0–64 字节实际
  guard-page 输入，以及拒绝后的 input/output 保持。
- `test_wifi_raw_tx_snapshot.py`：从 recorded C3/S3/C5 inventory 读取真实 SDK 类型
  声明；data 指向不可读 guard page，MAC 地址恰在六字节边界；body report 0/255、
  缺失地址、未知 status、跨 callback 存活、输出 alias、非法 interface 和输出失败
  不变。Host 的 recorded declarations 不证明 ESP32 ABI 或真实 SDK 调度。

允许的检查已完成：既有不可变 `firmware-ci-esp32c5-representative` C5 构建通过；
两个新符号存在于目标 object，最终 ELF 中不存在。镜像仍 `0x28c7d0`，app 分区余量
15%；不能据此计算新 API 最终 SRAM/flash 成本。SDK 工作区保持干净。

MQuickJS 61 sources / 48 snippets；manifest 47 classes / 417 functions；feature
27；config schema 35 STA / 21 AP，live SDK 匹配；严格 TypeScript、recorded SDK map、
两份 Python 文件 AST 和 whitespace 通过。AST 没有导入测试或执行 Host compiler。

证据 `build/w04-raw-tx-input-evidence.json`，日志 `build/w04-raw-tx-input-c5-build.txt`。
三目标/feature-disabled、Host/VM、callback 隔离与双板 RF 均 not-run，等待完整 Wi-Fi
API 后集中验收；长 soak 等 BLE API 也完成。未 flash/串口/擦除 workspace/前端构建，
未提交/推送或更新 root gitlink。
