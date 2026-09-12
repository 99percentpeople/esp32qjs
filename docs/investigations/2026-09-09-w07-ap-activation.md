# W-07 restart AP/APSTA 启动信道协调

firmware `d7db8d1` 工作区增量，固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批将 AP-only CSA 与随后 Station 启动接入真实内部 resume，替换此前只允许 AP
首选信道等于冻结实际信道的限制。公共 restart/helper/netif 集成仍未注册/完成，
完整 Wi-Fi 范围见[剩余清单](2026-09-08-wifi-api-remaining.md)。

## 固定 SDK 证据与决策

重新提取 C5 `libnet80211.a` 的 `ieee80211_api.o` 与 `ieee80211_ioctl.o`；证据和
hash 位于 `build/w07-ap-activation-sdk/`。`esp_wifi_set_config` 经 SDK ioctl 进入
`wifi_softap_set_config`；后者在运行 AP 更新时读取 net80211_funcs +220/+216 并
调用。`net80211_softap_funcs_init` 将对应项初始化为 wifi_softap_stop/start。
因此 live AP config 更新不能作为不影响当前 AP 的“只恢复下次启动首选信道”。
本实现不使用这些 private entry，也不修改 SDK。

固定公共 `esp_wifi.h` 的 set_channel 明确区分：AP-only 有客户端时使用 CSA；
APSTA 有客户端时不应调用，Station 扫描/连接时也不应调用。仅凭尚未发布 JS
owner 或一次零客户端查询，无法排除随后原生客户端关联。

本批采用公开 SDK 支持的 AP-only CSA 路径。启动时可能先广播在 AP 原配置信道，
再通过 CSA 移到冻结的实际信道；这是明确的重启过渡，不承诺 RF 零中断或客户端
不曾到达。没有更换 SSID/密码、临时开放安全模式或用 max_connection 伪造关联锁。
C3/S3 的二进制实现尚未逐体核对；公共声明、C5 静态路径和构建不替代 RF 证据。

## 真实路径

pre-start 仍核对完整 Station/AP 凭据、RAM storage 与精确 lifecycle/零 owner。
允许 AP 首选/自动信道不同于冻结实际信道。随后 resume 在排他范围内建立 staged
leases，由 `wifi_radio_restart_configs_start_locked()` 完成：

1. 若目标为 APSTA，在停止态先选择 AP-only；读回 mode。
2. 以保存的 AP 配置 START，沿用真实事件与 queue marker 屏障。
3. AP-only 调用一次 set_channel 恢复冻结 primary/secondary；通过 native wait
   合作等待 band mode/band/current/home/secondary 全部匹配。等待使用现有共享
   timeout scope 和启动 fallback；SDK 读取错误立即保留，普通未匹配继续只读等待。
4. 目标 APSTA 再选择 APSTA，启动 Station；新 `eventPhase:"sta-start"` 要求
   STA_START、AP 和 Station 都 live，以及当前 revision 的 marker。
5. 再次核对 mode 和 band/current/home，返回真实 resume。随后仍执行原有 policy、
   TX power、完整 PHY/凭据核对和最终 storage commit，最后才交接 owner。

Station-only 与没有 checkpoint 的普通 start 继续原启动路径。AP-only 等待期间
没有 Station 扫描/连接；有客户端到达时由 SDK 的 AP CSA 语义处理。此路径不调用
live AP set_config。APSTA 添加 Station 后信道漂移会在 handoff 前失败，不以一次
先前 AP-only 成功读回替代最终实际状态。

write、事件等待、CSA 超时、取消或 readback 失败记录具体 restart stage/原错误；
保持 checkpoint/token，不发布 owner，不在同次失败物理尝试中重复写入。后续走
既有显式 cleanup/重建。SDK set_channel 的 5 GHz secondary 自动选择仍必须与
冻结观察匹配；不接收近似值，也未声称 cross-band CSA 已获 RF 验证。

`status().radio.eventPhase` 正式类型/转换新增 sta-start，其余 eventExpected/Seen/
Live/Fence 语义沿用。未新增 placeholder API、额外 pool 或常驻字段。

## 验证和未完成范围

C5 immutable Build Context `build/wireless-contexts/c5` 生产构建 exit 0，日志
`build/w07-ap-activation-c5-build.txt`。binary 2,780,080 bytes（较前批 +1,216）。
activation 与 channel wait helpers 已链接；ELF checkpoint 688 bytes、restart
control 40 bytes，九项既有静态账本不变。没有实机 heap/碎片或 RF 测量结果。

新增 deferred fixture 调用真实 capture/replay/pre-start/AP activation/post-start/
commit，注入 SDK 和定时器边界：AP/APSTA、不同首选/实际信道、C5 原 5 GHz、
CSA 延迟/超时/零预算/取消、逐 SDK 调用失败、旧 token、Station 加入后的信道漂移、
错误后不重复 mutation、原配置/快照/RAM 保留。事件 fixture 使用真实 observe/
wait/marker helpers，补 STA_START、缺 AP live、迟到 STOP 和 stale marker 场景。
原 pre-start 与 resume fixture 同步新入口和准入。四份仅 AST 解析，未导入、编译
或执行；上述是已编写覆盖，不是测试通过证明。

manifest 49 classes/469 functions、features 27、live SDK schema 35 STA/21 AP、
MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map 和 whitespace 检查通过。
完整记录见 `build/w07-ap-activation-evidence.json`。

公开 restart、停止状态 checkpoint/恢复协调、helper/netif 重建、不同 AP 配置的
live activation、其他 Wi-Fi 高级模块仍待完成。Host/Python/VM/竞争、C3/S3/disabled、
完整 restart、真实关联/CSA/RF/共存与实机均 not-run，全部 Wi-Fi API 完成后统一
阶段测试；长 soak 留到 BLE API 完成后。未刷写、串口操作、擦除 workspace、构建
前端、提交、推送或更新父仓库 gitlink，不提升稳定等级。
