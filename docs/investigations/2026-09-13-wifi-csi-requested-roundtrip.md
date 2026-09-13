# CSI requested 配置回读修复

## 已确认原因

`WiFiCsiStatus.requested` 声明为 `WiFiCsiOpenOptions`，但生产转换器无条件
导出 `sourceMac:[]`、`destinationMac:[]`、`bssid:[]`；没有 RSSI/限速条件时
还会导出 `minimumRssi:null` 和 `maximumRateHz:null`。这些值均被配置解析器
拒绝。JS 保存状态中的 requested 后无法直接复用，属于底层配置回读缺陷。

先向实际 VM fixture 加入“解析输入 → 生产 requested 转换 → 重新解析”的
测试，旧实现失败。证据：build/wifi-csi-requested/before.json。

## 修复

- 未配置的 MAC/RSSI 条件省略；已有 MAC 条件仍导出独立数组。
- 无速率限制导出已支持的 maximumRateHz:0。
- 显式空 types/subtypes/frames 继续保留，不能把“不匹配任何帧”转成无条件。
- 移除不再需要的两个 JS root 和无条件空数组分配。
- 不增加自动 stop/reopen、重试或业务组合逻辑。JS 负责调用顺序，C 只保证
  requested 是语义等价的有效配置；configure 仍要求停止 Session，open 仍
  遵守资源/Radio/target 准入。

## 验证

build/wifi-csi-requested/native-final.json：5/5 原生测试通过。实际 MQuickJS
运行生产配置捕获、结果转换、再次捕获；比较完整已清零初始化的原生配置，
涵盖 legacy/HE、associated/promiscuous、默认/显式过滤、空集合、packet
配置、pool 容量，以及全路径第 N 次分配/属性读取失败和移动 GC。

Python CSI/帧契约 27/27，manifest 62 classes / 662 functions 一致，
MQuickJS 语法 69 sources / 72 doc snippets 通过。设备 JS 已改用
session.configure(session.status().requested)，但本轮未在设备执行。

本轮未进行目标编译、完整链接、刷机或 RF/长测；集中阶段验证前不把这项
Host 结果写成硬件通过。固件仍未提交，根仓库和 SDK 未修改。
