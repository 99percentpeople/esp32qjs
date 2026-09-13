# Wi-Fi 生命周期等待参数统一

接续过滤契约统一。固件 HEAD 仍为 c90047b；本轮工作未提交、未更新根仓库
子模块指针、未刷设备。sole v1 直接替换参数形式，无标量兼容分支。

## 契约与实现

`wifi.disconnect(options?)`、`wifi.stopAP(options?)`、`wifi.stop(options?)`
共享 `WiFiWaitOptions { timeoutMs?: number }` 与生产生命周期参数解析器。
显式 timeoutMs 限定为整数 1..60000，不转换字符串/布尔/小数，不接受 0、
负数、溢出值、NaN、Infinity、null、数组、未知字段或额外参数。

无参数、undefined、空对象与 timeoutMs:undefined 使用原默认值：
stop/stopAP 为 1000，disconnect 为 wifi.DEFAULT_TIMEOUT_MS（默认 15000，
允许构建配置的默认值高于显式参数上限）。

- stopAP 的启用与 SoftAP-disabled 分支均先解析对象，再进入原逻辑。
- disconnect 的生产 Future capture 先验证参数，再分配 native state；参数
  getter 只读取一次，getter 异常保留。删除已无调用的 scalar timeout helper。
- 不更改原生 disconnect、AP retirement、Radio ownership、事件隔离与清理重试。
  disconnect 原生提交后无法撤销，超时不释放仍在等待原生终态的 lane。
  stopAP/stop 保留未完成后缀，SDK 调用不可被预算抢占。
- 原 WiFiStopOptions 改为共享 WiFiWaitOptions；API 文档补上三种操作的
  成功、deadline 与副作用表；设备 JS 的直接调用与 Future.call 参数同步。

## 验证与边界

证据在 build/wifi-lifecycle-options/：

- before.json：旧 stopAP 参数实现对新 options 对象的真实 VM 回归失败。
- native.json 15/15；disconnect.json 10/10（含重跑的同一 capture 用例），
  合计 24 个不同原生用例通过。生产 capture 包含三入口、默认值/边界、
  未知字段、getter 单次读取/抛错、移动 GC、第 N 次分配失败；disconnect
  直接执行生产 Future prepare，检查零参数、额外参数与 native state 释放。
  其余用例覆盖原关闭、APSTA Station 保留、断连迟到事件、队列满、teardown
  隔离及只重试未完成资源的行为。
- Python Radio 契约 12/12；manifest 62 classes / 662 functions 一致。
- MQuickJS 69 sources / 72 doc snippets 通过。设备 JS 未执行。
- target-compilation.json：现有合法 Build Context 的编译参数，隔离输出
  4 个受影响 C 文件的 C3/S3/C5 对象，共 12/12；C5 为 SoftAP-disabled，
  C3/S3 验证启用 AP 分支。未触碰既有构建对象或 SDK。

本轮完整链接、Wi-Fi-disabled 构建、设备/RF/长测 not-run；集中阶段测试时
再执行，不以本轮对象编译替代完整固件构建或硬件验收。
