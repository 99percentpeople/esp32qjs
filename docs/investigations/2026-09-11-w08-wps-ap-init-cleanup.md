# W-08 AP WPS 初始化失败与关闭义务

基线 firmware `d7db8d1`、SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批继续内部 AP registrar；公开 `apRegistrar` 仍为 false。

## 已确认路径

此前 `wifi_ap_wps_init` 出错后立即释放 SDK heap 并 detach 结果，enable 的错误
出口随后调用 type/status reset，却忽略返回值。`hostapd_init_wps` 的失败出口
也忽略两处广告清理错误。这样会失去后续关闭所需的身份、原始错误和清理进度。

另一个边界是 EAP 方法注册：initializer 会拒绝已有方法，但 release 曾不区分
注册所属身份。初始化因已有方法而被拒绝时，后续关闭不能注销已有注册。
以上是固定源码路径证据；动态失败注入仍按用户安排后置。

## 生产改动

- enable 完成参数/模式/已有 AP 状态检查后，在首次 factory/type/status 修改前
  bind 原生 owner。绑定失败没有后续修改；原构造函数要求 owner 已存在。
- 构造失败保留已分配的 WPS data、设备数据与 context。公共错误出口先保存
  原始失败，再进入已有逐步关闭前缀；默认事件队列投递失败不阻止控制路径。
- type/status/广告等关闭步骤失败时保留结果、heap 和当前 cleanup stage。
  新 enable 被拒绝，显式 disable 按同一身份重试未完成后缀。
- heap 尚未分配时也可经过完整关闭并 detach，不再因 `gWpsSm == NULL` 永久
  返回失败。托管结果仍等待未来的完整 native release，不走普通 SDK 释放捷径。
- host release/子对象排空失败写入独立 `cleanup_error`，不覆盖原始初始化错误。
- EAP identity/WSC 方法登记归属 AP identity；失败及释放只注销本次登记。
  不属于当前 owner 的方法不被清理，重复关闭不会再次注销已经释放的登记。
- 初始广告设置的原始 driver 错误从 callback 保留到返回 NULL 的 registrar
  构造边界。没有更具体来源时仍保留 `ESP_FAIL`，不猜测所有 NULL 都是 OOM。
- 移除失去 caller 的无返回值广告清理 helper，统一由 checked close 执行。

## AP PIN 辅助路径的核对修正

固定 `wps_hostapd.c` 的 `hostapd_wps_reenable_ap_pin` 除定义外只有取消调用，
没有定时器注册路径。`hostapd_wps_ap_pin_set/random` 没有 SDK 源码 caller，
当前 registrar-enabled ELF 也没有这两个入口。它们与已实现的 registrar
PBC/PIN walk-time 不同，后者已有数字 ticket。

因此，不能将辅助 PIN 回调描述为当前固件中已复现的迟到回调问题。本批记录
可达性证据，没有新增辅助 PIN API。若后续入口确实调用这些 SDK 函数，必须先
补 timer 身份、PIN 替换失败、清零和安全边界，再让入口可达；当前 AP Session
仍需完成真实 native 输入排空和托管 release。

## 验证

九配置生产构建和静态产物核对通过，记录为
`build/w08-wps-ap-init-cleanup-evidence.json`。首轮 registrar 配置发现生成代码
混用 tab/space 触发 `-Werror=misleading-indentation`，已使用显式分支括号修复；
原日志保留为 `*-before-indent-build.txt`，没有关闭告警。
普通五配置镜像大小未变；四个 registrar 配置各增加 64 B。EAP/STA wrapper
大小未变，本批新增两个 4 B 原生 bookkeeping 值，运行期堆比较仍待实测。

新增生产 helper 用例登记每个 cleanup stage 失败、原始错误与观察队列失败、
managed retention 和 cleanup error 隔离；原初始化/父对象 fixture 更新为先
保留 allocation，再执行 checked deinit。仅 AST 解析，未导入、编译或执行
运行用例。没有刷机，GC/OOM/RTOS/RF/队列实测均未运行。

## 剩余

AP registrar 仍缺完整 queued RX/driver 静止、托管 heap/result release、worker/
IPC、Radio/AP lease 和公开 Session。其他 Wi-Fi 功能与集中运行/实机验收继续
按总剩余表推进；长时间 soak 保留到 BLE API 完成后。
