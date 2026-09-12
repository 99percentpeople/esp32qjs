# AP WPS Radio、Session 与公开接口

基线 firmware `d7db8d1`、SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本记录包含内部 Session 和随后公开接口接入。registrar 启用构建提供
`wifi.wps.startAP/apStatus`、`WiFiWpsAPSession`，保持 Candidate。

Radio 复用现有 WPS operation identity 与 boot event fence。Station/AP 两种
binding 互斥；现有 APPLICATION、STA、AP lease 在操作期间均禁止释放，新增
lease、promiscuous、wake lock 和冲突控制沿共同准入拒绝。AP 必须已启动且由
现有 helper 持有，APSTA 必须提供其 STA helper；不会隐式启动、扫描、断连、
切信道、改 storage 或重放凭据。SDK 自己读取/验证 AP 的 PSK 与加密配置。
固定 SDK 的 registrar device 仅声明 WPS_RF_24GHZ，当前显式拒绝 5 GHz；
5 GHz registrar 的正确 RF 声明与资格仍需后续处理，不能据此删去支持目标。

独立 runtime helper reservation 覆盖后台任务排队期间，阻止 connect/scan/
disconnect 与 helper 清理。它不使用 Station WPS 的 connect generation 或
DHCP 排空状态，保留现有 Station 连接的正常事件；旧连接取消也在 driver
mutation 前被拒绝。AP-only cleanup 在设置 cleanup_pending 之前检查预留。

原生 Session 已连接后台 worker、PIN copy/commit、首次注册成功的 peer 结果、
期限、最后引用关闭和 runtime destroy。成功结果只在 SDK capture retirement
及 boot event fence 后 ready。close 先释放原生/Radio binding，再由 runtime
释放 helper reservation；不关闭 AP/STA netif 或驱逐现有正常客户端。失败保留
原始错误和剩余清理义务，未知 IPC 交接仍阻止复用。观察终态排序保留现有
waiter 门槛，公开 watch 在 Future 处理后发布 metadata。

公开层复用 Station 的严格参数捕获，AP 不接受 allowApChannelChange，即使值为
false。独立 AP Session 提供 status/watch/receive/cancel/close，receive/close
接入 Future。PIN 与首个注册 peer MAC 转换全部成功后才 commit；关闭、GC 或
等待超时不释放仍在原生操作中使用的存储。status/error/watch 不含 PIN 或 AP
凭据；AP watch 有单独四队列预算，单队列容量 1–16，关闭后缓冲仍计费至 GC。
唯一 v1 类型、全局类、manifest/Future 注册和 API 文档同步。详见
[公开契约](../api/wifi-wps.md#ap-registrar-wifiwpsstartapoptions)。

首次公开层编译通过后，产物核对发现 host ROM generator 缺少 registrar 开关，
生成表没有 AP 类/方法，而 target runtime init 已引用它。失败产物摘要在
`build/w08-wps-ap-public-rom-failure.json`；已将实际 SDK 开关传入同一个 host
生成器，并检查启用/关闭时实际 ROM 表和 ELF，不将 archive 内存在函数当成注册。

验证范围为 C3 registrar 启用/关闭增量构建、实际 ROM/ELF 注册与 archive 对象
核对、MQuickJS 语法、源类型、manifest/feature 文档一致性及 fixture AST。
完整矩阵留在模块/阶段验收，动态测试未提前执行。当前收据见
`build/w08-wps-ap-public-evidence.json`，也覆盖本文件前半部的内部 Session/helper。

新增生产 Session/Future/watch 用例和 VM 结果转换 GC/第 N 次 OOM 用例只做
AST，未导入、编译或执行。Radio lease/fence 竞争、SDK 队列、实际 Future/reaper
调度、APSTA 连续性、完整安全、5 GHz registrar 与 RF/实机继续待验收/实现。
本批未刷写、串口操作、提交或修改父仓库 gitlink。

下一步继续其余 Wi-Fi 配网/高级 API 和完整恢复/CSI/资源诊断范围；Wi-Fi API
完成后集中运行与实机测试，BLE 完成后再做长 soak。
