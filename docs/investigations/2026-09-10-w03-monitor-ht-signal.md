# W-03：HE-layout Monitor 的 HT-SIG 解码

firmware `d7db8d1` 未提交工作区，固定 SDK `fff9895c82`。上一批 Monitor 诊断
已完成代码与生产构建；本批推进 callback PHY 解析，不改变接收生命周期。

## 已实现

固定 SDK `components/esp_wifi/include/esp_private/esp_wifi_he_types_private.h`
的 `esp_wifi_htsig_t` 定义 32 位 HT-SIG；公开 HE RxControl 的 `he_siga1`
保存这段信号。生产 target adapter 仅在 `RX_BB_FORMAT_HT` 时解码已经复制
的字，得到 MCS、20/40 MHz、STBC、SGI、FEC、aggregation、smoothing 和
sounding。MCS 77–127 为保留值，整组 HT 字段维持 unavailable。

不从回调指针作额外读取；不使用 CSI feature 或另一套 callback；现有
ht_fields_available 与字段布局足以表达结果，无新增原生控制/帧存储字段。
JS 和 wire 沿用同一 normalized metadata，原有 RX flags 表达新增已知事实，
wire 仍为唯一 v1，长度/offset 不变，现有 Host decoder 可读取这些字段。

HE-layout HT-SIG 没有 antenna 和 AMPDU count。生产 adapter 不生成 antenna，
JS AMPDU count 保持 null、wire 保持 255 unavailable；不把结构体初始零值当
作测得数量。legacy HT 路径的已有 AMPDU count 继续提供。

## 证据与尚未验证

- 生产 C5 immutable context 构建通过，binary 2,853,904 bytes，前一相同
  context 为 2,853,728。adapter、JS converter、wire snapshot 均已链接。
- 跟踪的普通 C5 静态账本尺寸不变。没有实测堆峰值或 RF 丢包改善。
- manifest 51/493、features 27、STA/AP schema 35/21、严格 TypeScript、
  MQuickJS 61/56、SDK map 和 whitespace 检查通过；记录与 hash 位于
  `build/w03-ht-evidence.json`。
- deferred adapter fixture 读取 SDK 的实际 HT-SIG typedef，构造 MCS 0–127
  与标志组合，调用生产 span/metadata adapter，并检查非 HT 格式不被解码。
  JS/wire fixture 增加 HE-layout HT 与未知 AMPDU count，仍覆盖逐次分配失败
  与移动 GC。3 份 fixture 仅 AST 解析，未 import/编译/执行。

所有运行测试、C3/S3/full feature matrix、实机 RF/关闭/GC/队列/内存比较仍
not-run，待全部 Wi-Fi API 完成后集中验证；长 soak 后置到 BLE API 完成。
未刷写、串口、擦 workspace、提交、推送、修改 SDK/根 gitlink 或构建前端。

HE/VHT 其他 SIG、legacy bitrate、时钟资格与 CSI correlated packet 仍未完成。
本次审查另发现既有 CSI VHT helper 将非零两位 CBW 均映为 40 MHz，并将
VHT_MU 合并后读取 SU MCS；需在后续共享 PHY 解码工作中修正，不能用当前
HT 构建证明这些路径正确。保留完整 W-03/W-05/W-06 范围与 Candidate 等级。

后续：[共享 VHT 解码](2026-09-10-w06-vht-signal.md)已修正上述 CSI 宽度与
MU/SU MCS 问题并接入 Monitor。源码修正与构建完成，运行验收仍后置。
