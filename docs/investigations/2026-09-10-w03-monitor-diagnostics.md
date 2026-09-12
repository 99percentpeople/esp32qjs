# W-03：Monitor 能力与队列/owner/过滤诊断

基于 firmware `d7db8d1` 未提交工作区，固定 SDK `fff9895c82`。
本批补公开观察契约；不改变捕获、Radio mutation、资源生命周期或稳定等级。

## 实现

- capabilities 以唯一 `wifi-monitor/1` 返回实际编译 target/SDK、四种 callback
  frameTypes、supports 中已接入的接收/过滤/导出能力和 limits。原扁平 callable
  与 limit 字段直接替换，不加旧字段别名。没有注册尚未实现的能力。
- stats.filtered 从 enum 下标数组改为十个命名拒绝原因；原生数组和 uint64
  饱和账本保持不变。每个字段显式对应生产 enum，不依赖字符串表下标顺序。
  accepted 仍是队列成功发布数；不把 filter 的 ACCEPT 槽冒充接收结果。
- status 读取实际 EventQueue stats，返回 open/queued/capacity/receiverPending，
  detach 或无法采样返回 null。增加 accepting、Radio lease/subscriber/fixed
  channel 持有标记及启动信道 generation，支持停机失败时判断清理责任。
  queue 和 pool 各自加锁，并非跨组件原子快照，不用于计算精确 JS 交付数。
- JS 子对象在分配和发布期间显式 rooting；所有 native snapshot 查询均先于
  对应对象的 JS 构造。不引入 callback 上的 JS 操作或新的原生持有记录。
- 类型、02、剩余清单、API 文档及设备 configure fixture 同步；清除 API 文档
  末尾仍声称 wire/PCAPNG 未实现的过期段落。manifest callable 数没有改变。

## 验证和边界

生产普通 C5 immutable context 构建成功，binary 为 2,853,728 bytes（前批同
context 为 2,852,064）。status/stats/capabilities 均在最终 ELF 中链接。
既有普通 C5 静态账本尺寸不变；未测量查询产生的 JS 堆峰值。

manifest 51 classes / 493 functions、features 27、STA/AP schema 35/21、严格
TypeScript、MQuickJS 61 sources / 56 doc snippets、SDK map 和 whitespace
检查通过。证据与文件 hash 见 `build/w03-diagnostics-evidence.json`。

新增 deferred fixture 提取生产 capabilities/stats/queue 转换函数，在真实
MQuickJS 下设计逐次分配失败、移动 GC、命名映射、队列 detach/不可读、三
目标身份参数化及错误参数拒绝；仅 AST 解析，未 import/编译/执行。
原生状态存储/queue 读取为注入边界，未用替代生命周期状态机作证明。

按用户安排，Host/Python/VM、C3/S3/feature matrix、实机关闭/重开/GC/队列/
RF 和内存比较均 not-run，待全部 Wi-Fi API 完成后集中执行。BLE 随后推进，
长 soak 后置。未刷写、串口、擦除 workspace、提交、推送、修改 SDK/父 gitlink
或构建前端。本批不宣称完成 W-03 的 PHY/time、W-09 跨代预算或全部 Wi-Fi。
