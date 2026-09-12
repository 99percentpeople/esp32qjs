# W-02：正常 AP/APSTA 的统一 stop

基线 firmware `d7db8d1`，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
前一批[启动后控制](2026-09-08-w02-start-controls.md)已加入 TX power；本批补
公开 configure/APSTA 所需的普通关闭路径。未注册 configure 或 APSTA start。

## 原有缺口与新增行为

原 `wifi_stop_idle` 正常路径只向 Radio lifecycle 提交 application/Station
owner，AP owner 不在精确 owner 集合内，因此现有 startAP 成功后的 wifi.stop
会被拒绝。此前只有失败配置会转交中央清理，普通 AP/APSTA 缺少这个入口。
本批扩展行为，不将未运行的新测试写成旧缺陷的动态复现证据。

- 正常 AP owner 存在时，wifi.stop 使用实际中央 admission，提交三角色的精确
  identity，再释放 AP/Station/application owner。foreign owner、wake lock、
  原生 operation、connected Station、未完成 Future/结果/原生 drain 仍阻止
  admission，不执行清理或强行取消。
- 成功准入后停止整个 Wi-Fi，AP 客户端会随 AP 关闭而断开；APSTA 下须先显式
  disconnect Station 并等原生 drain 完成。这个入口不提供保持 Station 的
  “只关闭 AP”模式；该 stopAP 共享接口流程仍待后续实现。
- AP/STA 均复用中央 stop/event fence、AP retirement、Station cleanup 和 token
  finish。正常 stop 选择保留 initialized driver，不把已有 stop 语义变成
  shutdown。失败配置与通常 runtime cleanup 的中央默认策略继续 shutdown。
- 新增 boot-static `s_wifi_configuration_stop_only` 记录已接受的清理意图，避免
  helper reset 后重试意外改为 deinit。新 admission 先恢复默认 false；成功完成
  后清除。已接受 stop 的重试，包括 runtime retry，保留原 stop-only 选择。
- 每个 native/helper 成功前缀由已有 helper 状态删除，失败保留 exact token 和
  unfinished suffix。最终阶段为 configuration-finish-stop 或
  configuration-shutdown，源类型同步。没有新增强制取消/广播 deauth API。
- AP-only 的 pending stop 可由 stopAP 延续原已接受意图；APSTA mixed stop 仍须
  wifi.stop/中央 runtime cleanup，不能由本地 AP helper 越权退休 Station。

普通 Station-only stop 路径复用 `wifi_helpers_idle` 检查，消除同一准入条件的
重复维护；其后续清理行为保持不变。AP feature-disabled 的 control-lease getter
返回 NULL，继续走该路径。旧 SoftAP 独占策略尚未解除。

## 检查与未完成验收

`test_wifi_public_lifecycle.py` 调用实际 public start/stop 和 idle helper，新增
正常 AP/APSTA 的转交、准入拒绝无副作用、保留 stop-only/mode 以及失败重试
不再准入；SDK/helper 清理边界注入。`test_wifi_configuration_cleanup.py`
直接使用生产 AP/中央协调器，将每步失败/重试扩展为 stop-only 与 shutdown
两种策略，并检查 helper reset 后仍保留意图。两文件 AST 已检查。

测试均 **not-run**，按用户要求等全部 Wi-Fi API 完成后统一阶段测试。必要 C5
immutable Build Context 编译、MQuickJS/manifest/features/schema/recorded map/
whitespace 结果与 hash 见 `build/w02-unified-stop-evidence.json`。实际 AP 客户端
断开、APSTA 生命周期、GC/队列饱和/关闭重开、三目标/disabled 矩阵仍待验收；
长时间 soak 放到 BLE API 完成后。本批无串口、刷写、workspace 擦除或提交。
