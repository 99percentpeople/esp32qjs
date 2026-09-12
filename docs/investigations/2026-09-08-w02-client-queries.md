# W-02：SoftAP 客户端与接口 MAC 查询

承接[首次 SoftAP 实现](2026-09-08-w02-softap.md)。基线仍为 firmware HEAD
`d7db8d1` 与 ESP-IDF `fff9895c82d744c7237be8847347bdd1b07c6643`；新增代码
保留在独立 firmware 工作区。本记录与首次 SoftAP 的历史验证快照分别计账。后续 [includeIp 增量](2026-09-08-w02-client-ip.md)已开放可选 DHCP 查询；下文无参数限制属于该首次查询批次的历史状态。

## 用户确认的实施顺序

先完成全部 Wi-Fi API，再集中执行 Wi-Fi 阶段测试；相关测试完成后才开始 BLE。
实现期间保留必要编译和生成物一致性检查。新增功能的生产路径竞争、错误注入、
GC/OOM 与硬件项目先登记为 not-run，不重复执行每批全套 Host/七构建矩阵，
也不以历史 118/461 通过数代替新增代码验证。长时间与共存验收继续单独记录。

## 已接入的唯一 v1 API

- `wifi.apClients()`：无参数，返回本模块运行中 SoftAP 的客户端数组，每项为
  address/aid/rssi/phy。空 AP 返回空数组，未启动或清理中的 AP 返回错误。
  address 为小写冒号分隔 MAC；rssi 是 SDK 列表内平均 dBm；phy 是 SDK 上报的
  11b/11g/11n/11a/11ac/11ax/lr 位，不是协商速率。没有 includeIp/options。
- `wifi.getMac("station" | "access-point")`：读取已初始化 driver 的当前接口 MAC，
  返回小写冒号分隔字符串。不启动、不创建 netif、不增加 owner；健康的已停止
  driver 也可读取。未初始化、故障或生命周期清理中明确失败。参数严格匹配完整
  字节长度，拒绝未知值、NUL 后缀、非字符串与多余参数。NAN/setMac 未注册。

Radio 的任务 mutex 串行化查询与 stop/shutdown。AP 查询在锁内检查精确 lease、
运行状态和清理状态，再读固定容量的 SDK wifi_sta_list_t；返回 num 在索引之前
校验上下界。没有增加持久 allocator 或 pool，JS 分配在解锁后进行，每一层数组、
对象与字符串使用 GC roots。转换失败不改变 AP 或客户端关联状态。

列表读完后逐 MAC 查询 AID；ESP_ERR_NOT_FOUND 转为 null，其他 SDK 错误使本次
查询整体失败，原生临时结果清零。列表、RSSI 与后续 AID 查询不是原子关联快照。
AID 可复用，不作为持久客户端 identity，也不接受它作为公开控制 token。

AP 查询错误使用 WIFI_AP_FAILED 和原始 espCode；清理已经失败时保留原清理 stage。
getMac 错误使用 WIFI_MAC_FAILED 与原始 espCode。SoftAP-disabled 构建的 apClients
返回 WIFI_AP_UNSUPPORTED，getMac(access-point) 报 ESP_ERR_NOT_SUPPORTED，
上述行为尚未在本增量重新执行 disabled 构建或运行验证。

## 定向断开尚缺的 SDK 保证

本节描述首次查询批次的缺口；2026-09-11 的实现与原生依据见下一节。

本地固定 SDK `components/esp_wifi/include/esp_wifi.h`：

- esp_wifi_ap_get_sta_list 返回 MAC/RSSI/PHY 列表，不包含不可复用的连接 identity。
- esp_wifi_ap_get_sta_aid 根据 MAC 查询当前 AID，离开时可返回 ESP_ERR_NOT_FOUND。
- esp_wifi_deauth_sta 只接受 AID；0 表示断开全部客户端。

这些公开声明没有提供 MAC 匹配与断开的原子调用，也没有提供跨上述两次调用锁住
关联表的契约。Radio mutex 只能排除框架生命周期/driver mutation，不能禁止
无线端客户端在两次 SDK 调用之间离开与重新关联。AID 重用导致误断开属于尚未
排除的竞争风险，未声称已经在硬件复现。再查一次列表或 AID 同样不能封闭窗口。

因此 deauthClient 继续 contract-pending，不注册“先查 AID 再断开”的不安全实现，
不通过全局断开或重启 AP 绕过。后续须取得 SDK 可证明的定向原子边界，再实现
MAC 身份校验/离开竞争与原生完成契约；这不阻止其他 Wi-Fi API 继续实施。

## 2026-09-11：单个 Wi-Fi task 命令中的定向断开

`wifi.deauthClient(address): boolean` 已注册，正式参数只有一个非零单播 MAC。
固定 SDK 的 C3/S3/C5 原始 archive 均重新检查：

- `esp_wifi_ap_get_sta_aid` 通过同步 `esp_wifi_ipc_internal` 调用本地 MAC 查找；
  本地查找检查关联状态和完整六字节 MAC，返回当前 AID。
- `esp_wifi_deauth_sta` 调用 `ieee80211_ioctl`；后者根据
  `current_task_is_wifi_task()` 在本任务直接执行 `ieee80211_ioctl_process`。
  只有外部任务路径才调用 `pp_post` 并等待。
- deauth handler 当场从该 AID 取节点并生成定向管理帧；零 AID 是全员分支，
  生产 helper 在调用前拒绝零值和越界值。返回成功不代表管理帧已送达或客户端已离线。

新的生产 helper 把两个公开 SDK 调用放进一次同步 IPC。调用方不能提交 AID；
关联表在命令执行时选择当前 MAC，所以旧查询快照不会成为本次控制的 token。
框架 Radio mutex 只负责框架生命周期串行化；无线关联命令之间的窗口由 Wi-Fi
task 的直接执行边界封闭。原始 archive 与 imported patch 仍由现有
`patch_idf_vendor_ie_context.py` 的三目标精确 hash gate 约束，未修改共享 SDK。
反汇编输出在 `build/w02-client-control-review/`，本批检查记录为
`build/w02-client-control-evidence.json`。

一次命令持有已有 Radio operation 和精确 AP lease。结果不明时保存堆命令，
generic end/release 不能提前放弃它；`wifi.status().radio` 保留活动操作和
`ap-deauth-ipc-unconfirmed`。迟到回执只解除这一 dispatch 故障，不清除其他故障，
不自动重复断开。AP 清理在命令尚被原生引用时停止于退休检查。公开错误包含
原始 espCode、stage、可空 requestAccepted 和 handoffUnknown；这不是连接离线证明。

生产函数 fixture 覆盖同任务查找/断开、执行前 AID 复用、空 MAC、非法 AID、SDK
失败、未确认 IPC、迟到回执、旧 token/释放、OOM 和 runtime 清理准入；当前只作 AST，
不运行。公开参数与错误转换也保留真实 VM GC/OOM fixture 到 Wi-Fi 集中阶段执行。

## 本增量验证与后续待测

本轮执行 API manifest、feature 文档与 diff 一致性检查，并执行代表 C5 immutable
Build Context 的编译。C5 编译通过（退出码 0，日志 `build/w02-clients-c5-build.txt`），未刷写；
不覆盖 C3/S3/disabled。manifest 为 43 classes / 390 functions，feature 文档
27 项一致，既有 SDK inventory/map 与 actual manifest 的结构一致性检查通过；
没有重采集三目标 SDK inventory，也没有运行 Host/硬件测试。

Wi-Fi 集中阶段须补生产实现覆盖：

- AP 空列表、多客户端、全部 PHY flags、MAC 格式、AID 查询期间客户端离开。
- 旧/重复 lease、未启动/清理中查询，以及查询与释放/stop/shutdown 的竞争。
- SDK list/AID/MAC getter 错误与非法列表长度；失败不发布部分结果。
- MQuickJS 移动 GC、第 N 次分配失败、严格输入拒绝、无 AP 状态变化。
- C3/S3/C5 与 feature-disabled 构建；真实 AP 关联/离开与短生命周期。

以上新增测试均 not-run，BLE 未开始。本批未操作串口、刷写、提交、推送或更新
父仓库 gitlink，未更改原 SRAM 优化或既有 pool 预算。
