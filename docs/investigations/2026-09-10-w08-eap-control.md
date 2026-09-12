# W-08：EAP 启停步骤、失败后清理与任务调度

基线 firmware `d7db8d1`、SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
接续 [worker 退休](2026-09-10-w08-eap-lifecycle.md)。本项仍是 enterprise
内部前置实现，公开 configure/enable/disable/clear/status 保持 contract-pending。

## 原因与实现

固定 C5 driver 在执行 EAP callback 前写入启用/关闭状态字节。因此 callback
分配、method 注册或 worker 退休失败时，软件 enabled 位不足以描述实际进度。
原 SDK 的 API lock 首次分配也没有检查 OOM，且多个调用者可能同时初始化。
`wpa_deattach` 忽略 EAP disable 错误后继续释放其他 supplicant 资源。
以上为源码及固定 C5 object 审查结果；动态失败复现尚未执行。

新增 `scripts/patch_idf_eap_control.py`，组合 secrets/lifecycle 修补；CMake
在独立 build 目录替换 EAP client、EAP peer 和 WPA main 三个 source。所有审查
依赖均以 hash 校验，共享 SDK 不变。已编码：

- enable/disable 在 Wi-Fi task 串行执行，首次 API lock 分配在该任务完成；
  OOM 与取锁失败分别返回 NO_MEM/INVALID_STATE。
- Wi-Fi task 内直接执行 handler；其他任务通过 blocking eloop 调度。固定
  `eloop_register_timeout_blocking` 在 Wi-Fi task 调用会 assert，因此不能嵌套
  dispatch。EAP worker 调用启停直接拒绝，避免等待正在等待它退出的 Wi-Fi task。
- driver mutation 前保存 disable hook；分别跟踪最后确认的 driver 写入、
  callback table 和 method registry。启用失败保留清理路径，再次 enable
  拒绝覆盖残留资源；已完整启用时重复 enable 返回成功。
- method 注册失败时清理部分注册列表。固定 C5 register 正常接管 callback
  table 且返回零；意外非零返回无法判断是否接管，保留单个隔离指针并拒绝复用。
  unregister 成功后也不猜测释放隔离指针，不重复 unregister，明确需要重启。
- disable 先确认 driver 关闭、worker 退出，再 unregister callback、reset
  globals、unregister methods。已确认的 driver 写入不重复执行；清理失败仅重试
  未完成后缀，借用凭据和方法不会在 worker 未退休时释放。
- `wpa_deattach` 在其他 roaming/SAE/WPS/WPA 清理之前检查 EAP disable 返回值，
  失败返回 false。外层 driver 是否完整处理 false、框架 Radio deinit 前置门槛
  仍待接入，不能据此声称物理 teardown 已可靠。

内部 snapshot 增加 driver unknown/enabled、callback、methods、global credential
存在性、callback quarantine 及最近已执行 control error，不暴露指针/凭据。
driver 位表示确认过的写入，**不是实时 driver getter**；资源位全零仍不能替代
Radio lease 和调用方 identity。调度失败与已执行 control 错误分开保存。

新增内部 configuration-changed helper，取消精确旧通知并直接标记配置变化，
供后续完整 profile 安装使用；尚未连接安装事务，现有 SDK setters 也没有因此
全部变成串行事务。证书/密钥借用 owner、PEM/DER 长度、安全默认值仍待落实。

## 验证与保留项

C5 roaming-enabled / Wi-Fi-disabled immutable Context 构建通过；manifest
52 classes / 504 functions、feature 27、SDK schema STA35/AP21、strict TS、
MQuickJS 61 sources / 59 snippets 通过。SDK archive 三个 object 与组合 source
生成的 object 逐一核对；详细 hash、ELF 和大小记录见
`build/w08-eap-control-evidence.json`。构建不代表实际 EAP 执行或生命周期验收。
启用镜像 2,896,240 bytes（较 worker 退休批次 +16），关闭镜像 459,024 bytes
（不变）；跟踪的 30 个 framework static object 大小不变。SDK 新增跟踪字段在
object 中合计 24 bytes（含上一批 10 bytes），不是实际运行堆预算；snapshot/
control-error/configuration-changed helper 尚未链接进入 ELF。

新增 `test_idf_eap_control.py` 使用实际组合后的 SDK 启停、callback、lock、
资源查询和 deattach 函数；只注入 driver/allocator/method/eloop/storage 边界。
覆盖 dispatch/lock/OOM/partial enable、未知 driver、退休/unregister 失败、
后缀重试、任务内直接调度、worker 拒绝和 callback 接管歧义。已有 lifecycle
fixture 改用最终组合 source。两份 fixture **仅 AST，未 import、编译或执行**。

Host/VM 动态复现、C3/S3 driver 同等审查及构建、FAST/internal TLS 和完整 feature
matrix、Radio 集成、认证对端/RF、实机 GC/关闭/队列/runtime restart、堆与最大块
比较均 not-run。Wi-Fi API 完成后集中运行阶段测试，长 soak 留到 BLE API 完成后。

后续仍需完整 profile 安装/回滚、Radio owner 与 runtime cleanup、JS capture 和
公开 enterprise API；其他 Wi-Fi 剩余范围不变。本项没有刷机、串口操作、擦除
workspace、前端构建、提交/推送或更新根 gitlink。
