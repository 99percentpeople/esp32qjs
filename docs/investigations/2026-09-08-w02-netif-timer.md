# W-02：SDK 延迟 IP timer 与 netif 地址复用

基线 firmware `d7db8d1`，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批继续上轮 netif 退休工作，修补固定 SDK 的 timer 生命周期；不注册尚未完成
的 configure/APSTA，不缩减剩余 Wi-Fi 功能目标。

## 源码事实

固定 `components/esp_netif/lwip/esp_netif_lwip.c`：

- `esp_netif_start_ip_lost_timer` 用 netif 指针作为 sys_timeout 的 arg，timer
  周期受 Build Context 的 CONFIG_ESP_NETIF_IP_LOST_TIMER_INTERVAL 控制。
- callback 通过 `esp_netif_is_active` 检查指针是否仍在接口列表，没有 generation。
- stop 包含“接口不 up 则移除后直接返回”的路径，destroy 从列表移除并 free。
  两者都未取消已安排的 IP lost timer。
- 因此 allocator 重用 netif 地址后，旧 timer 的指针检查可能通过，对新对象的
  timer_running/ip_info_old/default-netif 状态进行操作并产生过期 IP event。
- 原始 translation unit SHA-256：
  `0b448749d365832307173ad9c21b692e8a899983d15d2f194766cfa34df4acd9`。

上述因果链来自固定 SDK 源码。原始失败与修补后的运行对比用例已编写，尚未
执行；不能把源码推导写成已完成的实机复现。

## 修补方式

框架 `scripts/patch_idf_netif_timer.py` 只接受上述已审查的原始 SHA，在构建目录
生成单个 `esp_netif_lwip.c` 副本。根 CMake 在 project 后将 SDK esp_netif target
中的这一源文件替换为副本，保留同一 component 的 include、defines 和链接依赖。
SDK 安装与原始源码不写入；输出位于当前 Build Context 对应的 build directory。

新增 helper 在 TCP/IP task 内使用
`sys_untimeout(esp_netif_ip_lost_timer, esp_netif)` 并清除 timer_running：

- stop_api 一进入即调用，覆盖已经 down 的早退路径。
- destroy_api 在从列表移除和 free 前调用，覆盖不经过 stop 的销毁。
- sys_timeout callback 与这两个 API 在同一 TCP/IP task 执行，取消后该 timer
  无法再访问释放的对象；只匹配此回调与此 arg，不取消其他接口/模块的 timer。
- 正常连接断开但 netif 继续存活时的延迟 IP notification 保留。停用/销毁接口
  不再留下旧 timer；此前已排队的事件仍由上轮的默认 event-loop fence 排序。

没有新增对象池或延时等待 120 秒。没有关闭 timer Kconfig，也不改 Context。
CMake 仅在 esp_netif component、LWIP/IPv4 和 LOST_IP_TIMER 均启用时替换源文件；
其他构建沿用 SDK。源码变更或重复修补会明确报错，升级 SDK 需要重新审查，不会
静默绕过修复或尝试模糊匹配。输出内容相同时不重写，避免无意义的重编译。

## 验证与剩余边界

C5 immutable Context 构建通过，app 为 `0x279d20` bytes，分区空余 17%。相对上一
退休批次 binary 增加 48 bytes；未增加常驻状态。此大小比较不等于运行 heap 回收
验收。compile_commands 只有一个 netif_lwip translation unit，指向生成副本。
SDK 原文件 hash 与 Git 工作区状态保持不变。

`test_idf_netif_timer.py` 在设置 IDF_PATH 后提取原始/修补 SDK 的实际 timer、
start_timer、stop_api、destroy_api，替换 LWIP scheduler/allocator/事件边界。
用例覆盖地址复用、已 down/仍 up 的 stop、直接 destroy、无 timer、timer-disabled、
其他 callback 不受影响，以及源码漂移/重复修补拒绝。不用独立修复状态机代替
生产实现；运行原始版本时应观察旧事件，修补版应无旧事件。

| 验证 | 当前状态 |
| --- | --- |
| C5 immutable build、MQuickJS 语法、manifest/feature/map、whitespace | passed，hash 见 build/w02-netif-timer-evidence.json |
| 新脚本及用例 Python 语法 | passed |
| SDK 原始/修补生产函数运行对比、Host C/Python 全套 | not-run，Wi-Fi API 完成后 |
| 三 target / feature-disabled 全构建矩阵 | not-run |
| 实机生命周期/快速地址复用/GC/内存/RF | not-run，Wi-Fi API 完成后 |
| 长时间 soak | BLE API 完成后 |

该 timer 风险已有构建修补，仍待运行验证。整体 stop/start 事件身份隔离、公开
配置协调器与 APSTA、中央 runtime teardown 仍未完成。未刷写、串口操作、提交、
推送或更新父仓库 gitlink。
