# W-07 restart 的 runtime helper/netif executor

firmware `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接[原生 restart 阶段](2026-09-09-w07-restart-phases.md)，新增内部
`esp32_mquickjs_wifi_restart_interfaces()`，接入现有 Station/AP helper 和中央清理。
未注册公开 `wifi.driver.restart()`；公开准入和停止状态 checkpoint 保留仍待完成。

## 实际执行路径

这是内部、明确授权的整个 Wi-Fi driver 重建入口，可退休 Wi-Fi 自己的 idle helper
和 AP；不构成公开 restart 对 STA/AP 子 owner 的准入政策。已连接 Station、扫描/
连接/Future 等未结束操作、已有 lifecycle/cleanup 在接管前拒绝。Radio 的原有
精确 lease admission 继续排除 ESP-NOW/CSI/Monitor/Raw TX/wake 和其他 owner。
不调用 disconnect，不取消 pending Future，不替其他功能关闭资源。

AP 校验用的临时原生存储在接管前分配。随后执行：

1. 通过原 `wifi_begin_configuration_cleanup(false)` 和 AP handoff 取得同一个
   排他 token，将最终 mode 记录在中央清理状态中；释放三个旧 helper/application
   lease，保留旧 helper 和 netif。
2. 调用 checkpoint/STOP，成功后退休 AP 和 Station helper。任何一步失败均不
   进入物理重建；helper 的 detach/fence/回调排空仍使用原有生产实现。
3. 物理 rebuild 返回停止状态后，准备新 Station helper；最终 mode 含 AP 时再
   准备 AP helper。Station 准备不取 lease，AP prepare 也不发布 owner。
4. 调用真实配置/策略 replay。AP-only 也先准备 Station，因为双频准备可能临时
   START；replay 返回停止后，再通过真实 retire 入口释放该临时 Station helper。
5. 最终 mode 含 AP 时，通过原生 copy/validate 检查重放后的配置并取得空 AP
   lease slot。校验前不启动最终 AP，不将凭据交给 JS。
6. 原 resume 完成 START、配置/PHY/策略核对和 storage commit 后，才交接最终
   APPLICATION/STA/AP 输出。AP-only 只保留 AP owner，与现有 configure 一致。

调用方的 native wait scope 贯穿原生等待；本 executor 不新建 JS Future、timer、
callback 或另一份持久 checkpoint。临时 AP 配置在所有成功/失败返回路径
secure-zero/free，长期冻结配置继续由 Radio 持有。

## 失败与清理

接管后的错误记录在既有 `s_wifi_state.cleanup_stage/error` 和每次调用的 execution
中。`s_wifi_configuration_cleanup` 及精确 token 保持有效，后续 `wifi.stop()` 或
runtime teardown 使用原 `wifi_finish_configuration_cleanup()`：排空、STOP、
退休 AP/STA、physical shutdown，成功后才清除 token 和冻结配置。

新 executor 遇到已有 pending 状态立即拒绝，不能把第二次调用作为自动重放重试。
新 helper 部分分配、AP 配置拒绝、临时 Station 退休失败或最终 resume 失败同样走
中央后缀；不会在一个 helper 失败后继续创建、恢复或发布其他 owner。原 netif
不可恢复错误/driver reboot-required 诊断仍可能阻止清理完成，不声称所有错误都
能靠 runtime restart 修复。

## 验证范围

C5 immutable Context `build/wireless-contexts/c5` 生产构建 exit 0，日志
`build/w07-restart-runtime-c5-build.txt`。executor 存在于生产 Wi-Fi object，因公开
入口尚未调用而从最终 ELF 移除。binary 仍为 2,780,080 bytes；九项既有静态账本
不变，冻结配置 688 bytes、restart control 40 bytes。没有 live heap/碎片测量。

新增 deferred fixture `test_wifi_restart_runtime.py` 调用真实 executor、准入和
中央清理，注入 Radio 阶段/helper/SDK 存储边界，编写 STA/AP/APSTA、AP-disabled、
参数/忙碌/foreign owner/OOM、各阶段失败、部分 helper 存储保留、AP-only 临时
Station 退休、最终输出选择和中央 shutdown 后缀失败/重试用例。它验证的是 runtime
协调顺序，不能替代 Radio 物理重建、netif detach/fence 或 RF 生产路径的独立验证。
本批只 AST 解析，没有导入、编译或执行 fixture。

manifest 49 classes/469 functions、feature 文档 27 项、live SDK schema 35 STA/
21 AP、MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map 和 whitespace
检查通过。源 hash、object/ELF 链接边界及日志见
`build/w07-restart-runtime-evidence.json`。

公开 restart、停止状态快照保留、完整配置恢复与其他 Wi-Fi 功能仍按
[剩余清单](2026-09-08-wifi-api-remaining.md)继续。Host/Python/VM/竞争、真实 helper/
netif/physical restart、C3/S3/disabled 构建、实机/RF/共存均 **not-run**，全部 Wi-Fi
API 完成后统一阶段测试和实机功能验证；长 soak 留到 BLE API 完成后。
未刷写、串口操作、擦除 workspace、构建前端、提交、推送或更新根 gitlink。
