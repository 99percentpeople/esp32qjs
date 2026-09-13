# W-08：专属 roaming watch

基线 firmware `d7db8d1`，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本项为未提交 Wi-Fi API 工作区增量，保持 Candidate；不是全 Wi-Fi 完成或实机验收。

## 已接入

`wifi.roaming.watch(options?)` 返回 `EventQueue<WiFiRoamingEvent>`。注册 gate
与 roaming 模块相同：RRM / WNM / 11r 任一启用。默认选择九种已审查的 Station
与 home-channel 事件；RRM 启用时另含 Neighbor Report。完整名称、输入与语义见
[公开契约](../api/wifi-roaming.md)。固定 SDK 的公共事件 enum 没有 BTM 接受或
漫游完成事件，不能从连接/信道观察推导出请求因果或成功。

生产 `esp32_mquickjs_wifi_watch.c` 拆出共用 `wifi_watch_options` 和
`wifi_watch_open`；专属入口使用编译期事件 mask。通用 watch 保留全部事件与
原 raw 选项，专属 watch 不读取 raw 属性，也不允许 `all` 逃出限定范围。
事件数组重新构造 mask，拒绝重复、空数组、holes、未知名称及 NUL 后缀。
捕获结果只在完整验证成功后输出，getter 异常保留；全部解析先于 native 订阅。

订阅仍使用共用四个 source、16 项 ingress 与两个 Neighbor pool slot，无新增
全局 broker 或 Radio owner。关闭、转换失败、满队列、sole retired pool 和
runtime 清理沿用真实共享实现。Neighbor 请求观察发布继续在 Future polling 后，
本项不改变 native terminal/Future 与观察队列的依赖。聚合账本由
`wifi.status().watch` 暴露；WiFiWatchError.operation 补入专属入口名称。

类型使用 `WiFiEvent & {name: WiFiRoamingEventName}`，保留原多名称 union 分支
中的 START/STOP/BEACON_TIMEOUT。不能以简单 Extract 丢掉这些事件。

## 已执行的检查

- 合法 immutable C5 roaming context 构建通过，binary 2,896,224 bytes，较上一
  Neighbor public 构建增加 320 bytes。
- 普通 C5 roaming-disabled context 构建通过，binary 2,856,896 bytes，增加
  240 bytes；ROM/ELF 没有专属入口，通用 watch 保留。
- 两个构建追踪的 30 个 framework static object 大小保持不变；不是运行 heap、
  largest block 或 PSRAM 比较，不据此宣称运行内存验收。
- manifest 52 classes / 504 functions、feature 27 项、固定 SDK schema
  STA35/AP21、SDK map 一致性、严格 TypeScript 与 whitespace 检查通过。
- MQuickJS syntax：61 sources / 59 doc snippets 通过。

日志与 hash：`build/w08-roaming-watch-evidence.json`、
`build/w08-roaming-watch-c5-build.txt`、
`build/w08-roaming-watch-c5-disabled-build.txt`、
`build/w08-roaming-watch-manifest-check.txt`、`build/w08-roaming-watch-check-js.txt`。

## 延期测试与剩余项

新增 `tests/c/integration/wifi/station/test_wifi_roaming_watch.py` 使用生产 options/parser/mask、
固定 SDK descriptor、capture/poll 过滤与真实 VM 注入边界，覆盖 RRM 开关、
GC/getter/OOM、精确事件限制及通用 watch 回归。仅 AST 检查，没有 import、编译或
执行 fixture；共享完整 EventQueue factory/关闭竞争也未验证。

Host/VM/Python、C3/S3 与完整 feature 矩阵、RRM-only/WNM-only/11r-only、
实机 GC/关闭/队列/runtime restart/RF、内存静止态比较全部 `not-run`。
RF dialog-token wrap/reset 隔离、完整漫游验收及
[其余 Wi-Fi 范围](2026-09-08-wifi-api-remaining.md)继续保留。
按用户要求，Wi-Fi API 完成后集中阶段测试，BLE API 完成后再做长 soak。

没有刷写、串口操作、擦除 workspace、前端构建、提交/推送、SDK 工作区改动或
更新父仓库 gitlink。
