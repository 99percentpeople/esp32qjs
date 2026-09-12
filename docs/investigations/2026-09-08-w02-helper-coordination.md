# W-02：Station/AP helper 协调基础及资源失败路径

基线 firmware `d7db8d1`，本地 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`；
本批为工作区增量。继续配置协调所需的 helper 准备/退休，并修正实际生产路径中
不可恢复的 SDK convenience constructor 调用。完整 Wi-Fi 功能目标未缩减。

## 确认的源码事实与修复

固定 SDK `components/esp_wifi/src/wifi_default.c`：

- `esp_netif_create_default_wifi_sta` 在 new 失败时 assert，attach/default-handler
  失败时 ESP_ERROR_CHECK；旧框架在其返回后检查 NULL 无法处理这些中止路径。
- `esp_netif_destroy_default_wifi` 丢弃 clear_default_wifi_driver_and_handlers
  的返回值，然后销毁 netif；旧框架无法发现 detach 错误。
- clear helper 的 disconnect_and_destroy 无论 set_driver_config 返回什么都会
  destroy_if_driver。若清空失败，netif 可能仍带着已释放的 driver handle，不能
  再调用一次同一 destroy helper。AP 之前已有该限制，Station 现已补齐。
- attach 在设置默认 netif 指针后才分配/附接 driver，错误路径仍需清理已经创建
  的 netif；不能把 attach 失败当作没有任何副作用。

根因依据是上述固定 SDK 与生产调用链，未执行运行时失败复现。修复为显式
new/attach/default-handlers 的可返回错误步骤；创建失败的原始错误保留，资源
清理由现有 suffix 处理。Radio lease 改为 netif attach 前预留，避免准入失败时
先改 SDK 的 netif 状态。new 失败返回 ESP_ERR_NO_MEM。

Station detach 使用显式 clear/destroy。clear 失败永久保留错误及 netif/剩余
helper 存储，之后直接返回该错误，既不再次清空 dangling driver，也不把资源
已归还写成成功。这个状态需设备重启，runtime restart 不会清掉证据。

## 供配置协调器使用的入口

- Radio check_stopped_lifecycle 是只读精确 token 准入：无 lease、operation、
  wake/promiscuous、started/stop_required。准备只允许健康未初始化/停机状态；
  退休还可接纳已停机的诊断故障，但不接纳不确定 init 的 restart_required。
  检查在 Radio task mutex 内，netif/handler 实际工作在释放该 mutex 后执行。
- Station init 分为有 owner 的普通入口和无 owner 的配置准备，二者调用同一
  生产 helper。配置准备可复用已准备的空闲 helper，并清掉本地旧 started 标记；
  不创建/发布 Radio owner。资源释放有保留 caller token 的模式，不结束事务。
- AP 的 netif prepare/retire 从现有 startAP/stopAP 提取复用；配置入口只允许
  未被独立 AP 生命周期占用的资源。成功准备后提供空 lease slot 给上轮 resume
  发布 owner。准备失败保留待清理状态，退休失败保留 detach 证据。
- caller 必须先停止/排空原生操作与相关事件，再调用这些入口。检查 token 与
  stopped 并不等于 ESP default event loop 已排空，尤其共享 STA/AP 默认 handler
  不能靠“移除一个 netif”假定其余 callback 全部退出。

仍未完成：公开 configure capture/正式 driver config 字段映射、allowDisconnect
独占 drain、stop-event/fence 协调、整体配置提交后的 owner 发布，以及 runtime
中断时的中央清理顺序。新 helper 入口不构成公开 APSTA 支持或其竞争验收。

## 公共诊断

WiFiStatus 新增 setupStage/setupError，保留最近一次 helper setup 失败，即使其
资源 unwind 成功也可查看；下次 setup 成功清除。stationNetifCleanupError 与
stationNetifRestartRequired 表示 detach 隔离状态，cleanupStage 为 netif-detach。
Common WiFiError details 增加 setupStage、cleanupStage、restartRequired，最后
一项合并 Radio 与 Station netif 的设备重启要求。全为错误元数据，不暴露凭据。

## 验证账本

必要 C5 immutable Build Context 编译、MQuickJS syntax、manifest/feature docs/
recorded SDK map、Python 用例文件语法及 whitespace 检查结果/hash 记录在
`build/w02-helper-coordination-evidence.json`。软件编译不能代替资源故障运行证明。

新增 test_wifi_station_netif.py 调用两个生产 netif helper，覆盖逐步创建失败、
清理、重复 retire 和失败 detach 不重试；已有 cleanup 用例增加保留 caller token
及 detach 隔离，并同步 AP 提取函数依赖。Host C 新增 helper-lifecycle-admission，
调用生产 Radio 检查运行状态、stale token、停机故障与不确定初始化边界。

| 验证 | 状态 |
| --- | --- |
| 新增生产 helper 故障注入、既有 Host C/Python 全套 | not-run |
| netif/status/error 转换的 GC/OOM 与 runtime teardown | not-run |
| 双 netif default-handler/stop-event 排空与 APSTA 协调 | not-run |
| 三目标及 feature-disabled 完整构建矩阵 | not-run |
| 设备功能/关闭重开及等静止态内存账本 | not-run，Wi-Fi API 完成后 |
| 长时间 soak | BLE API 完成后 |

无串口/实机/刷写、提交、推送或父仓库 gitlink 更新。

后续进展：[netif 退休串行化](2026-09-08-w02-netif-retirement.md)已替换此批的同步
detach，使用默认事件任务与后续 fence；SDK 延迟 IP timer 与整体配置协调仍待处理。
本文件上述构建/测试账本仅对应原 helper 批次，不代表后续增量已运行验收。
