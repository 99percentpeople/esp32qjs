# W-08 AP registrar 的 EAP 客户端所有权

接续 registrar 初始化修正；公开 AP registrar 仍未注册，不能将这一批作为
close/reopen、原生排空或 RF 验收完成的证据。

## 源码确认的问题

固定 SDK 的 `eap_wsc_init` 为每个 EAP 客户端分配外层对象，却把所有客户端的
`wps` 指向 `gWpsSm->wps`。状态、DH/nonce、fragment 之外的 WPS transcript 和
PIN-lock 标记因而共用；一个客户端可能改变另一个客户端的握手状态。

`eap_wsc_reset` 又从 EAP method destructor 内直接执行全局
`wifi_ap_wps_disable_internal`。调用者可能仍在 `eap_server_sm_step/deinit`
中，而 disable 会释放整个 EAP 树及方法注册。正常父对象 deinit 还先释放
registrar，再释放客户端，且遗漏 `ieee802_1x_init` 持有的 authenticator 和
`eap_cfg`。这些是源代码确认，尚未取得动态复现结果。

## 本批实现

- 每个 AP enrollee 的 EAP-WSC 对象经真实 `wps_init` 创建独立协议实例，并复制
  经过验证的 AP credential。DH、nonce、transcript 和 PIN-lock 状态各自持有；
  不借用全局协议实例。PIN 选择仍由原生 registrar 在 M1 路径校验及加锁。
- 从 EAP server 的不可变配置捕获 WPS context；新客户端必须匹配当前 owner、
  pending 状态和仍开放的 hostapd context。外部 registrar 身份不进入此 AP
  registrar 握手，也不借此开放重配置 AP 的能力。
- reset 只清理自身缓冲、协议实例和凭据；不再关闭全局 registrar 或调用者。
  分配失败只撤销本次实例，保留父对象及其他客户端。
- 父对象先撤销新客户端准入，再释放每个客户端 EAP、authenticator/eap_cfg、
  registrar，最后注销方法。客户端释放 PIN-lock 时 registrar 仍存活。
- 为已关闭 authenticator 的 EAP 分配入口补 NULL guard，避免晚到帧在空指针上
  创建状态。新增 helper 无 boot 静态池；协议和凭据按客户端分配。

改动由 reviewed-hash patcher 写入各 Build Context 的生成源，未修改共享 SDK。
现有 AP 关联数量限制仍生效；整体资源预算与正式 Session 准入继续属于后续接入。

三目标生产对象的 DWARF 均为：EAP-WSC 外层 40 B、独立 `wps_data` 712 B、
credential 128 B；相较旧版共用协议，每个实际进入 WSC 的客户端增加 840 B
固定对象，另有按握手分配的密码/密钥及消息缓冲。这不是完整峰值内存或预热后的
实机 free/largest-block 结果。`eap_cfg` 为 120 B，本批补齐其父对象关闭释放。
布局记录：`build/w08-wps-registrar-peers-layout.json`。

## 验证与尚缺证据

新增 `test_idf_wps_registrar_peers.py` 调用生产 EAP init/reset、hostapd deinit 和
authenticator cleanup 函数体，覆盖原版 alias/global-disable、双客户端独立状态、
凭据快照、逐次分配失败、准入拒绝及清理依赖顺序。边界替身只用于 allocator、
协议构造和驱动；不以独立测试状态机替代生产函数。按阶段安排仅解析 Python AST，
没有导入、编译或执行 fixture。

八种生产配置构建及静态产物核对通过，证据为
`build/w08-wps-registrar-peers-evidence.json`（230 项产物 hash）。生成源码与
reviewed patcher 一致，编译对象与 archive 一致；反汇编确认 reset 不再调用
全局 disable，并链接 per-peer protocol cleanup 和 authenticator cleanup。
这属于生产编译及静态核对，不是延后用例的运行结果。

| 配置 | 镜像 bytes | 相对上一批增量 | app 分区剩余 bytes |
| --- | ---: | ---: | ---: |
| C3 | 2791040 | 0 | 354688 |
| S3 | 2683312 | 0 | 462416 |
| C5 roaming/WPS | 3177568 | 0 | 1016736 |
| C5 no-SoftAP | 3053408 | 0 | 92320 |
| C5 disabled | 459024 | 0 | 2686704 |
| C3 registrar | 2819856 | 304 | 325872 |
| S3 registrar | 2709744 | 304 | 435984 |
| C5 registrar | 3206256 | 304 | 988048 |

manifest 55 classes / 538 functions、类型、27 feature docs、配置 schema、
1267 条 SDK coverage 分类和 MQuickJS 61 sources / 63 snippets 检查通过；
API 注册数量没有改变，classification 仍不代表完整实现或硬件合格。

原生 timer/回调 identity、调度参数保留、
控制终态与观察队列分离、IE 清理失败后缀、延迟 station-remove 的身份隔离、
Radio/AP 固定和公开 Session/Future/runtime 仍待完成。当前 peer 隔离修正不代表
这些存储在全部原生队列中都已退休。集中运行/实机/RF 为 `not-run`；BLE 在 Wi-Fi
验收后，长 soak 在 BLE API 完成后。
