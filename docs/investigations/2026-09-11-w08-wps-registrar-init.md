# W-08 WPS registrar 初始化与凭据边界

接续公开 Station enrollee，核对固定 SDK AP registrar。没有新增公开 registrar
入口；其 native operation identity、共享 Radio、callback 退休与 Session 仍待接入。

## 确认的源代码问题与修正

- 独立 Kconfig `CONFIG_ESP_WIFI_WPS_SOFTAP_REGISTRAR` 默认关闭，仅依赖 SoftAP。
  前批 `sdkApRegistrar` 误用 SoftAP 开关；现改为真实 registrar 开关。原五配置
  均未启用 registrar，不能把其通过结果当作 registrar 分支构建证据。
- `wifi_ap_wps_init` 忽略 `hostapd_init_wps` 返回值；后者又忽略凭据捕获、Identity/
  WSC 注册和 EAPOL 初始化错误。现在逐项检查，保留能取得的原始错误；mode 查询和 factory 初始化也不再
  压成通用 ESP_FAIL。
- registrar 创建失败时 `hostapd_init_wps` 释放了由 `gWpsSm` 仍持有的 wps context；
  调用者继续使用/释放会留下悬挂指针。内部只撤销自己的 registrar/methods，
  context 和 protocol data 由调用者依赖顺序释放，协议 data 先于 context。
- `hostapd_update_wps` 传 NULL 给会无条件解引用 `wps_data` 的凭据函数。现在核对
  实际 `wps_sm` 与 context 身份，传入真实 data，捕获失败不发布新的凭据快照。
- 凭据构造原有长度无界、覆盖 `use_cred` 不释放旧值，最终 `wps_deinit` 也不释放
  该副本。现校验 SSID/key、PSK 能力和可表示的密码套件，先构造新值再清零释放
  旧值；失败保持旧 credential/context。禁止把 SAE-only/OWE/Enterprise 当成 PSK。
- 补齐 `ieee802_1x_init` 在 authenticator 创建失败时的 eap_cfg 释放；PIN/config
  栈副本、credential 栈副本、registrar data/context 和 EAP-WSC 收发缓冲的清零。

更改只通过 reviewed-hash patcher 写入 build-local SDK 源，原 SDK 未修改。
生产模块继续采用同一 v1；`apRegistrar: false` 仍准确表示框架尚未实现。

## 尚未解决的 registrar 生命周期

以下是初始化批次结束时的缺口；随后已完成
[客户端协议隔离与 EAP 父子释放顺序修正](2026-09-11-w08-wps-registrar-peers.md)，
原生身份、排空、调度及公开 API 的缺口仍保留。

SDK server WSC 当前共享单个协议 data；`eap_wsc_reset` 还直接触发全局 disable，
与原生 station/EAP callback 的存储寿命存在待处理交叉。正常 deinit 的 EAPOL
资源退休、timer/callback 身份、队列满终态、IE 清理失败、原生调度参数保留、
多客户端隔离、Radio/AP 配置固定及 Future/runtime 交接均未完成。本批初始化
修正不能据此宣称安全 close/reopen，也不注册占位 API。

## 验证边界

新增原 SDK 与修正后 production function 对照、凭据边界/分配失败/安全释放
用例，以及真实 VM capabilities 转换用例；仅 AST，不导入、编译或运行 fixture。
反例尚未执行，不把源代码确认写成已取得运行失败/修复通过证据。

保留原上下文，另生成 C3/S3/C5 registrar-enabled immutable Build Context：
只改变 context identity/label 和独立 registrar 开关。C5 继承已有 4 MiB app
上下文。差异/hash 见 `build/w08-wps-registrar-init-contexts.json`；没有刷写、
修改设备分区或 workspace。首轮新分支构建暴露遗漏的 WSC 常量头文件，已补齐，
失败日志保留。原五配置与三种 registrar-enabled 构建全部通过；静态产物核对
确认 generated source、编译对象、archive 与 ELF 对应，记录 239 项 hash。
证据为 `build/w08-wps-registrar-init-evidence.json`，本轮结束时 hash 全部一致。
后续增量会改变共享构建目录，这份记录代表初始化批次的历史产物。

| 配置 | 镜像 bytes | app 分区剩余 bytes |
| --- | ---: | ---: |
| C3 | 2791040 | 354688 |
| S3 | 2683312 | 462416 |
| C5 roaming/WPS | 3177568 | 1016736 |
| C5 no-SoftAP | 3053408 | 92320 |
| C5 disabled | 459024 | 2686704 |
| C3 registrar | 2819552 | 326176 |
| S3 registrar | 2709440 | 436288 |
| C5 registrar | 3205952 | 988352 |

实机、RF、动态故障/队列/GC/RTOS 测试继续后置；完整 Wi-Fi 目标与 BLE/soak
顺序不变。未提交、推送或更新根仓库 gitlink。
