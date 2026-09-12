# W-08 AP WPS 关闭前缀与父对象保留

基线 firmware `d7db8d1`，固定 ESP-IDF
`fff9895c82d744c7237be8847347bdd1b07c6643`。修改只进入 Build Context 的
SDK 副本；共享 SDK、公开 v1 API 和功能稳定等级不变。

## 原生路径核对

- 原 `hostap_deinit` 忽略 `wifi_ap_wps_disable_internal` 的返回值，之后仍调用
  `hostapd_cleanup` 释放配置/父对象。后者还有在配置释放后调用阻塞式公开
  `esp_wifi_ap_wps_disable` 的分支。
- 原 AP disable 把 type/status/deinit 的失败统一降为 `ESP_FAIL`。hostapd
  teardown 忽略广告移除结果；错误后无法知道哪些步骤已经完成。
- `hostapd_wps_event_cb` 会调用可选 `hapd->wps_event_cb`。这个同步调用边界
  允许再次调用 native close，不能在回调栈仍引用父对象时释放它。

以上是固定 SDK 源码证据。本轮按用户安排不执行故障/竞争测试；已登记原函数与
修补函数的对照用例，不把源码核对或 AST 通过写成运行复现通过。

## 已编码

AP 原生结果增加 `callback_depth`、`tracking_fault`、`cleanup_stage`、
`cleanup_error`、`close_prepared`。这些是私有 metadata，不含 PIN 或凭据。

1. 用精确 result identity 进入/退出整个 hostapd WPS event callback，包括可选
   应用回调。关闭意愿或首终态会拒绝新进入，仍允许原 identity 正确退出。
   计数溢出/不匹配的空栈退出保留 tracking fault，禁止释放。
2. close 先撤销交付、清除 PIN；回调未退出时返回错误，不执行 driver 清理。
3. 按 type disable、status disable、AP PIN 辅助 timer、registrar timer、
   Beacon IE、Probe Response IE 顺序处理。只在一步成功后推进；失败记录
   原始错误，重试不重复已完成步骤。固定 eloop cancel 返回移除数量，转换为
   成功；数量为零不代表其他原生输入已经排空。
4. registrar timer 取消只撤销数字 ticket，不释放仍由 EAP 客户端引用的对象。
   关闭后 set-IE callback 拒绝重新发布广告并释放所接管的临时 buffer。
5. AP 退出在释放 WPA/config/context 之前检查 WPS 关闭结果，失败返回 false；
   cleanup 的直接入口也保留父对象。只处理 AP registrar，不停止无关 Station
   enrollee；移除在 Wi-Fi task 里调用阻塞式公开 disable 的后备分支。
6. 未托管 SDK 路径在前缀完成且回调深度为零后沿既有同步析构继续。
   托管 result 刻意禁止从这里释放 EAP/SDK heap：它仍需要后续 native drain。

三目标生产对象 DWARF：result record 从 44 B 增至 56 B，metadata 从 28 B
增至 40 B；均按需分配。静态 pointer/id/atomic hint 仍为 4/4/1 B，不含链接
对齐。此结果不代替实机峰值内存或长期稳定性测量。

## 验证与边界

八配置生产构建与静态产物核对通过，证据写入
`build/w08-wps-ap-close-evidence.json`；包括 C3/S3/C5、
C5 no-SoftAP、C5 Wi-Fi-disabled，以及三个独立 registrar-enabled 配置。
静态检查覆盖 manifest、文档、SDK 字段/覆盖清单、TypeScript 与 MQuickJS。
新增 `test_idf_wps_ap_close.py` 使用生产 result include 和生成后的 SDK 函数，
登记逐步失败重试、回调中关闭、旧 identity、保留状态、计数故障及 AP 退出对照；
本轮仅 AST，不编译、导入或执行这些测试。

普通五配置镜像大小不变；registrar-enabled 的 C3/S3/C5 分别为
2,806,528 / 2,697,744 / 3,192,832 B，比上批减少
15,040 / 13,600 / 15,152 B。移除 hostap cleanup 对公开阻塞 disable 的引用后，
链接器不再保留尚无 framework caller 的公开 SDK IPC 分支；对应生产函数仍编译
在 archive 并核对生成源/object。这是当前调用图的变化，后续接入 AP worker 后
需要重新测量，不能据此预报最终功能尺寸或运行期 SRAM 收益。

尚未完成：EAPOL timer 与队列输入退休、其余 raw-pointer 辅助 timer 的身份化、
初始化部分失败的完整保留、跨任务 IPC/worker、精确托管 heap/result release、
共享 Radio/AP lease 和公开 AP Session。闭源 driver 对 hostap_deinit false 的
后续处理仍需验证，不宣称此返回值可保证整个物理 STOP 已回滚或支持 runtime 恢复。
实机/RF、竞争注入和 GC/OOM 集中验收 `not-run`；长时间 soak 继续延至 BLE 后。
