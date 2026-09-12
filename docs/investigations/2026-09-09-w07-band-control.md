# W-07：Station 频段与频段模式控制

firmware HEAD `d7db8d1`，固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批实现唯一 v1 的 wifi.driver.setBand/setBandMode，完整 Driver 与 Wi-Fi 总目标仍未完成。

## 实现和边界

生产 wrapper 严格验证一个字符串参数，使用 SDK 命名常量。中央 Wi-Fi coordinator
复用 helpers_idle，传递实际 Application/Station/AP lease；释放 helper mutex 后
进入 Radio mutation mutex，SDK 调用不进入 IRQ critical section。共用 helper 准入
也用于前批 connection controls，未新增常驻恢复表、JS root 或异步 job。

生产 Radio 复用精确 owner 检查，要求 started、健康、Station-only、无其他 feature/
wake/promiscuous/临时 rate owner，额外拒绝固定信道与未退休 Raw TX identity。
AP client 数量查询不能阻止查询后新的 RF association，因此要求调用者显式 stopAP，
不将“当前零客户端”当作 AP 频段切换的安全凭证。共享 AP/global policy 仍待实现。

固定 esp_wifi.h 的 setBand 要求 AUTO；非 5 GHz target 拒绝 5 GHz/AUTO 请求。
同值请求在读回当前 mode/band 后无写入返回，允许已关联 Station；实际变更前 SDK
sta_get_ap_info 必须返回 NOT_CONNECT。helper scan/connect/drain 必须已结束。
setBandMode 可能恢复 SDK 保存的该频段 home channel，未设置时默认为 1 或 36。

成功写入后核对 mode/band，并通过既有 callback-aware channel refresh 读当前信道，
检查 band/channel/secondary 和法规约束。回调读取当前 SDK，而非迟到 event payload。
这些是逐次观察，不能证明 RF 行为或把事后检查视为撤销短暂 RF 变更。

SDK setter 失败也可能已经生效；任何写入后的失败均保留原始错误并 fault Radio。
公开 SDK 无法完整快照 inactive-band 隐藏状态，不猜测 rollback 或自动重放。
既有 owner 不被释放；现阶段该故障需要设备重启，JS runtime restart 不提供物理
恢复。公开物理恢复协调器/配置恢复仍在总清单。FLASH storage 时记录可能持久化，
不声称原生失败意味着 Flash 未变。错误与阶段复用 Radio.configuration 和
WIFI_DRIVER_WRITE_FAILED；JS 结果分配失败不能撤销已经成功的 native write。

## 延后测试准备

test_wifi_band_control.py 调用生产 wrapper、runtime coordinator、registry/owner
验证、Radio band mutation/快照/法规/fault helper。只注入 SDK、锁、原生状态和已有
channel refresh 的读边界；实际 callback refresh 已有独立生产 fixture。
准备覆盖三目标、AUTO/单频限制、同值 no-op、关联/AP/固定信道/旧 token/helper
排空拒绝、每个 SDK 步骤失败、写入后报错、错误读回、故障后无重放，以及 VM 严格
枚举/NUL/GC/第 N 次分配失败。SDK 和 VM 分配计数分离。共享 fixture 只扩展了可选
inventory 类型依赖及生产 helper 提取，默认不变。

这些用例仅静态审阅和 AST parse；未导入、编译或执行。生产构建与契约检查证据
记录在 build/w07-band-control-evidence.json，不能据此提升功能稳定等级。

## 本批构建与契约检查

C5 immutable Build Context firmware-ci-esp32c5-representative 编译完成 exit 0。
ELF 链接两个 public wrapper、runtime coordinator 和 Radio change_band。binary
`0x29e4b0` / 2,745,520 bytes，比上一批增加 2,928 bytes，app 空余 13%。静态账本
s_radio=824、s_raw_tx=136、s_tx_rates=68、s_tx_rate_lease=36、s_sessions=32、
s_jobs=32、s_retired=56、s_lane=44 bytes 未增加；未测动态 heap/stack 峰值。

MQuickJS 61 sources/53 snippets、manifest 49 classes/454 functions、strict TypeScript、
feature docs 27、config schema 35 STA/21 AP（live SDK）、recorded SDK map 与 whitespace
检查通过。三份相关 Python 文件 AST parse 通过，未导入测试模块。SDK 工作区干净。

Host C/Python/VM/故障注入、真实 SDK task 并发、C3/S3/disabled 构建、实机/RF/heap
均 not-run。Wi-Fi API 完成后集中阶段测试及实机功能测试，长期 soak 留待 BLE API
完成。未刷写、操作串口、擦 workspace、构建前端、提交、推送或更新根仓库 gitlink。
共享 AP/global policy、其余 Driver、Raw TX 显式恢复、高级模块和资源预算继续按
[总清单](2026-09-08-wifi-api-remaining.md)推进。
