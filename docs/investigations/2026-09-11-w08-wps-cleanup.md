# W-08 WPS 分步停止与 SDK 状态释放

本批接续[原生结果 owner](2026-09-11-w08-wps-native.md)。公开 WPS Session
仍未实现；SDK 状态释放与允许下次操作复用是两个独立门槛。

## 生产路径变更

- 托管 stop 依次处理 disconnect、type、start flag、scan、六个 timer、连接
  timer、callback table、Probe/Association IE、status。逐项保存原始清理
  返回值和当前位置；失败保留 SDK 存储，重试只执行尚未完成的后缀。
  timer 取消还保留 handler 游标；零个匹配表示成功，负值表示错误。
- 12 个真实 SDK callback 的原名保留为 wrapper，body 的所有返回都经过
  callback enter/leave；其中六个 timer 仍检查完整 64-bit identity，其他
  无 cookie 的入口在 closing/terminal 后拒绝访问 SDK 状态。计数溢出保持
  sticky tracking fault，不回绕，不允许据此释放状态。
- 托管初始化失败只释放尚未注册的本地 callback table，保留部分 SDK
  context/device 状态供停止和后续释放。SDK deinit 的 driver mutation 在
  托管路径上已由逐步 stop 完成，不重复调用不检查返回值的原分支。
- `native_retire_state` 必须在同一 Wi-Fi task、精确 identity、输入停止、
  无执行中 callback、无 tracking fault 时运行；清除 SDK 状态及其秘密，
  处理早期 init OOM 遗留的 factory storage。WSC fragment 使用 clear-free。
  SDK heap 释放后，原生结果 record、held 与旧回调终态防护仍保留。

## SDK 证据与限制

固定 SDK 的 `esp_wifi_set_wps_cb_internal` 可以在 ioctl 分配阶段失败；不能
把调用尝试当作注销成功。实际 Wi-Fi task 上的 ioctl 同步执行，原生 callback
table 替换处理在释放旧表、保存新表后返回成功。三个目标的相应 archive
均纳入 hash gate；本批不修改共享 SDK 或二进制库。

`eloop_cancel_timeout` 删除待执行 timer，不代表已进入的 callback 退出，
因此 SDK 状态释放另外检查 callback depth。callback depth 为零也不证明
所有队列排空。`esp_wifi_scan_stop` 内的普通扫描与随机 MAC 扫描 callback
路径不同，不能从单个 callback 全局指针清零推导完整 scan retirement。

SDK `wpa_ether_send` 使用复制输入的 `esp_wifi_internal_tx`；WPS EAPOL
发送完成不携带 WPS Session 指针，共享 `eapol_txcb` 对非 EAPOL_KEY 返回。
这些只能缩小 WPS 指针存活范围，不证明 RF 完成或整个 Station 已退休。
只读 archive 审阅产物保留于 `build/w08-wps-retire-review/`。

仍须完成 scan/队列/原生来源排空、record release/reuse、worker IPC 参数
寿命、Radio/连接所有权、完整初始化/启动原始错误交付、公开参数与凭据
转换、Future/GC/watch。此批不把设备重启作为正常 WPS close/reopen 实现。

## 验证边界

`test_idf_wps_native.py` 继续调用生产 `.inc` 和生成的实际 timeout body/wrapper，
SDK 边界替换为注入点。补充每个清理阶段失败后的后缀重试、六 timer 中间
失败、callback depth/溢出、状态释放重试和 retained result 阻止复用用例。
按安排只做 AST 检查，不导入、编译或执行 fixture；尚无运行通过结论。

生产构建与静态产物核对记录在 `build/w08-wps-cleanup-evidence.json`。
首次四个 Wi-Fi-enabled 配置遇到 Ninja 重复规则：同一 SDK archive 被
相对路径和真实路径同时加入 CMake 输入。已统一 REALPATH，首次日志保留
为 `build/w08-wps-cleanup-*-build-before-paths.txt`，不把失败尝试计作通过。
已有无效 build.ninja 无法触发自动再生成，第二次尝试同样失败；随后用原
Build Context/cache 执行显式 CMake reconfigure，再由 remote.py 正常构建。
第二次失败和 reconfigure 日志也保留，未删除 workspace 或修改构建输入。

最终 C3、S3、C5 roaming、C5 no-SoftAP、C5 Wi-Fi-disabled 五项生产构建
均通过。生成的三个 WPS 源文件与生产 patcher 一致，对应 object 与 SDK
archive member 字节相同；12 个 wrapper、受保护 deinit 和独立 retire 入口
已进入实际 SDK object。最终 ELF 仍无公开 WPS caller。

四种启用配置的 DWARF 确认 record 384 B、status 40 B、credential bundle
332 B，均未增加。五镜像尺寸不变，disabled 镜像 hash 不变，C5 roaming
应用分区仍余 19,312 B。manifest 54 classes / 530 functions、feature 文档、
35/21 字段 schema、1,267 项覆盖清单、严格 TypeScript 与 MQuickJS
61 源文件/62 片段静态检查通过。这些不替代 GC/OOM/队列/实机运行验收。

未刷写、操作串口、提交、推送或更新根仓库 gitlink。集中运行/实机验收
留到 Wi-Fi API 完成后，长 soak 留到 BLE API 完成后，稳定等级不提升。
