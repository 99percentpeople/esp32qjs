# W-08 AP 输入客户端存活与 WPS IE 所有权

基线 firmware `d7db8d1`、固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
继续 AP registrar 的真实输入路径，公开 `apRegistrar` 仍为 false。

## 源码路径与修改

SDK `wpa_ap_rx_eapol` 原先先读取 driver 给出的 `sta` 指针，再进入已经加锁的
IEEE802.1X wrapper；WPA3 任务可能在这之前删除客户端。`hostap_sta_join` 也
会在表锁外读取旧客户端或新建客户端的 semaphore。修复先在真实 STA 表中
匹配候选指针或地址，并在解锁前取得 SAE semaphore；候选值不在表内时不解引用。
新建客户端从 `ap_sta_add` 返回后再次验证；SAE 忙仍按原行为临时拒绝关联。
关联入口最外层拒绝非 Wi-Fi task，避免内层查询失败后继续创建或断连。

收包期间持续持有 peer semaphore。WPS 输入额外持有 AP activity，通过专用
内部入口调用真实 IEEE802.1X 主体，避免重复获取非递归 semaphore。普通 WPA
key 帧保持原认证路径，关闭 WPS 不将它们阻塞；Station enrollee 不取得 AP
EAP 输入所有权。关联结束若已经 remove_pending，不发布即将释放的 peer。

`check_n_add_wps_sta` 原先在检查 WPS 状态之前分配 IE，关闭/overlap 提前返回
会遗漏释放，重复关联又直接覆盖旧 IE。现在：

- 先验证当前 AP registrar 和 PBC/PIN 准入，再解析/分配 WPS IE。
- 用有界 TLV 检查区分无 WPS IE、非法长度和实际分配失败；OOM 不回落为普通
  WPA 关联成功。已核对并固定 IE 合并实现及相关头文件 hash：有匹配 IE 时，
  此函数只有 allocation 失败会返回 NULL。未启用 WPS 或新关联没有 WPS IE 时，
  清理该客户端旧 WPS 状态。
- overlap 释放新 IE；替换前退休旧 EAP、释放旧 IE；EAP 构造失败或关联响应
  发送失败时释放新 IE 并撤销对应 EAP。
- 固定 `eap_server_sm_init` 会复制 assoc_wps_ie，退休旧 EAP 不继续借用 STA
  的 IE allocation。此处依据已固定 hash 的 SDK 源码，未把它当作 OOM 测试结果。

## 与 Enterprise 的编译组合

`esp_wpa_main.c` 已由 Enterprise patcher 修改。WPS patcher 从同一已审查 SDK
源组合 Enterprise 修复与 AP 输入修复，CMake 将原文件或已生成的 Enterprise
文件替换为同一个最终源。不会丢掉 `wpa_deattach` 先检查 EAP disable 的行为。

从 C3 registrar 上下文复制独立 no-Enterprise 上下文，只改变 context identity/
label 和追加 `CONFIG_ESP_WIFI_ENTERPRISE_SUPPORT=n`；保留板型、分区与 flash_data。
记录为 `build/w08-wps-ap-input-no-enterprise-context.json`，未修改原上下文。

## 验证边界

十配置生产构建与源码/object/archive/ELF 核对记录为
`build/w08-wps-ap-input-evidence.json`，包括原九配置和新增 no-Enterprise 配置。
校验最终编译列表不再重复编译 Enterprise 目录下的 WPA main，并保留 Enterprise
的 deattach 检查；所有修改都在 build-local 输出，共享 SDK 未改动。

新增 `test_idf_wps_ap_input.py` 使用生产 acquire/RX/association helper，登记旧
候选指针、SAE busy、收包期间 pin、关闭/WPA key/Station owner 分流、IE 缺失、
overlap、OOM、非法长度、替换与响应失败路径。仅 AST 解析，未导入、编译或
执行运行用例；全 SDK 关联交错、原生队列、GC/OOM、RF 和实机仍 `not-run`。

## 未完成

真实 STA 表匹配提供当前存储存活保护，不能凭此声称 driver 的无 cookie 回调
已经具备跨关联 generation，也不能把它当作完整 native 队列排空。仍须完成
关闭后的输入静止/顺序屏障、托管 heap/result release、worker/IPC、Radio/AP
lease 与公开 Session。其它 Wi-Fi 剩余功能和集中运行/实机验收继续保留，长
时间 soak 留到 BLE API 完成后。

后续托管入口还须隔离普通 SDK `wps_reg_eloop_handler` 的 enable/start/disable：
这些排队命令没有 operation cookie，不能仅凭“当前 owner 是 registrar”允许
旧命令修改后继托管操作。需要精确命令授权、未知 IPC 交接保留及可核对的
retire/checkpoint/release 顺序；本批没有提前开放这些入口。
