# W-03/W-06：GI、HE-LTF 与 DCM 元数据

firmware `d7db8d1` 的未提交工作区，固定 SDK `fff9895c82`。本批完成可用
GI/LTF/DCM 的生产路径接入；W-06 与完整 Wi-Fi 阶段仍未完成，等级保留 Candidate。

## 实现与依据

CSI 与 Monitor 继续共享固定 SDK SIG 解码 helper。HT/VHT 把 short GI 转为
400/800 ns；SU/ER-SU 与 MU 读取各自 SIG-A 中的 GI/LTF 字段。TB 缺少 trigger
信息，GI/LTF/DCM 保持未知，MU 的 SIG-B DCM 不冒充每用户 data DCM。

| GI code | SU/ER-SU LTF / GI | MU LTF / GI |
| --- | --- | --- |
| 0 | 1x / 800 ns | 4x / 800 ns |
| 1 | 2x / 800 ns | 2x / 800 ns |
| 2 | 2x / 1600 ns | 2x / 1600 ns |
| 3 | 4x / 3200 ns；DCM/STBC 原始位均 1 时为 800 ns | 4x / 3200 ns |

位位置以固定 SDK `esp_wifi_he_types_private.h` 为准；编码表交叉核对
[Silicon Labs GI_LTF 文档](https://docs.silabs.com/wifi91xrcp/2.14.0/wifi91xrcp-developers-guide-wifi-features/wifi-per-mode)。
[Qualcomm SU 字段定义](https://android.googlesource.com/kernel/google-modules/wlan/qcom/wcn6740/wlan/+/05544565496f121bc1ecbf35b55b3b3e1b055022/fw-api/hw/qca6290/v2/he_sig_a_su_info.h)
明确双位特殊组合的 HE data 不启用 DCM/STBC；不引入其专有 400 ns HE 模式。
[Radiotap HE](https://www.radiotap.org/fields/HE.html) 同样区分 GI、LTF size 和
LTF symbol 数量，本批 `heLtfSize` 只表示 1/2/4 倍数。

源码审查确认前批只读取 STBC 原始位，特殊组合因此会被误报为 data STBC，
进而影响 CSI LTF 选择。现已归一化为 false/false，并保留独立 GI/LTF。
这是依据生产源码和字段定义确认的错误；按用户后置测试安排，尚未执行失败
复现或修复后运行验证。未实现完整 SIG CRC/保留组合/RF 合法性校验。

公开 `phy.guardIntervalNs/heLtfSize/dcm`、CSI Frame/Batch Source、Monitor wire
以及共同 Host decoder 已同步；未知值为 null。sole-v1 的 256-byte metadata
用 offset 124 的 u16 表示 GI、126 的 u8 表示 LTF，127 继续为 0；RX flags
18/19 为 DCM availability/value，20–31 保留。Native/Host 均验证枚举、PHY
适用范围、value/availability、SGI 与数值一致性。未新增版本、兼容 reader、
placeholder callable。JSONL 自动携带字段；未新增 HE Radiotap encoder。
这些字段是 PHY 参数，不改变 callback-time 或提高 timestampAccuracy。

## 内存与生产构建

最终普通 C5 immutable context 构建成功：binary **2,856,608 bytes**，上一批
同 context 为 2,855,424。初始字段排列导致每个 CSI slot 增加 8 bytes，已通过
重排到旧填充空隙消除；最终 ELF DWARF 确认 metadata **192 bytes**、slot
**240 bytes**，均与前批一致。新字段 offset 为 18、58、59，timestamp/layout
仍为 32/60。Monitor 局部 PHY snapshot 由 8 增为 12 bytes，不存入帧槽。
这不是设备堆、栈峰值或长期内存稳定性测量。

命令：`scripts/remote.py --build-context build/wireless-contexts/c5 --build-dir wireless-c5 --assume y build`，
使用本地 SDK export 环境。Manifest 51 classes / 493 functions、feature 文档
27 项、STA/AP schema 35/21、严格 TypeScript、MQuickJS 61 sources / 56 snippets、
SDK map、AST 和 whitespace 检查通过。产物、源文件和检查 hash 记录于
`build/w06-gi-evidence.json`，结构尺寸记录于 `build/w06-gi-sizes-{before,after}.json`。

## 待执行验证与剩余范围

- SDK typed HE fixture 覆盖全部 width/MCS/GI 与 DCM/STBC/coding 组合，直接调用
  CSI/Monitor 生产解码和 writer；Host C layout 用例检查特殊组合不误选第二 LTF。
- Native writer 用例覆盖未知值、合法字段、非法枚举、SGI 冲突、HE/非 HE 边界
  以及输出原子拒绝；真实 C CSI writer 与 Host parser 增加 HT/HE/MU 互通用例。
- Monitor VM fixture 为 16 种场景；CSI Frame/Batch/Source fixture 增加新字段
  与 GC/OOM 检查，保持最后 owner 释放后 pool 归还要求。
- 上述 fixture 只编写并做 Python AST 解析，未 import、编译或执行。Host/Python/
  VM、C3/S3/feature matrix、实机关闭/GC/队列/runtime restart/RF/内存全部 not-run。
  Wi-Fi API 完成后统一阶段测试，长 soak 留到 BLE API 完成。

NSS、RU/每用户/puncturing、legacy bitrate、CSI correlated packet、完整
subcarrier 布局、时间与导出资格，以及 remaining 清单中的其他 Wi-Fi 模块继续
保留。没有把尚未实现的模块改为 target-unsupported。未刷写、串口操作、擦除
workspace、提交、推送、修改 SDK、根 gitlink 或构建前端。
