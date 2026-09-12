# W-07 STOP 历史快照的写入失效边界

firmware `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接 [STOP 前历史观察](2026-09-09-w07-stop-snapshot.md)。本批增加原生快照有效性
标记和 Radio SDK 写入边界，仍未自动使用历史值恢复，也未注册公开 restart。

## 真实调用边界

新增仅由 Radio translation unit 包含的 `esp32_mquickjs_wifi_radio_mutation.h`。
在 SDK 声明之后、Radio 实现之前，为当前 27 个 SDK writer/lifecycle 名称安装
function-like alias，调用真实 SDK 之前清除 `s_stop_snapshot.unchanged`。
宏的 self reference 在展开后仍调用原 SDK symbol，不替换第三方实现，不影响
其他 translation unit，不持有新的锁或更改 SDK 返回值/参数求值次数。

覆盖 init/start/deinit、mode/storage/country/MAC、band/channel、power save/TX
power、protocol/bandwidth、PMF/config、event mask、RSSI/inactive time、TX rate、
dynamic CS/11b/coexistence 以及 connectionless interval。写入失败和回滚同样
清除标记；即使写回旧值成功，也不能据此重新证明旧 STOP 观察仍有效。

只读 getter 和 STOP 不在 writer alias 中。STOP 仍先捕获自己的观察；SDK STOP
或事件排空重试不重新采样。调用边界沿用 Radio mutation mutex，未将 SDK 调用
移入临界区。feature-disabled 路径不包含这个私有 header，继续原 SDK 调用。

## 标记的含义

完整捕获先建立内部 candidate。内部 getter 只有在以下条件都满足时返回
`unchanged:true`：candidate 未被后续 writer 清除；generation 和 STOP identity
仍匹配；观察完整成功；driver 仍由框架持有且 storage 已知；真实 STOP/事件屏障
已完成，driver 为 STOPPED、无 started/stop_required、fault/cleanup/reboot-required。

这避免了 STOP 尚未完成、失败后事件仍在排空或新 physical generation 出现时
过早认可记录。读取历史记录不会自行恢复 candidate；失效后旧功率、信道、错误
和 identity 仍可作为历史返回。复用原 20-byte 结构的 padding 保存 bool，没有
新增计数器、identity 回绕、分配或另一份配置存储。

`unchanged` 只说明已跟踪 Radio 写入和状态前提，不是完整 restart 准入、全部
driver 配置未变或跨 modem/RF 的原子证明。当前其他文件中的 SDK 写入包括
Station 连接前 set_config、CSI/RX callback/filter 和 netif 默认 handlers；它们
不由这个本地 header 拦截，仍须结合原 owner/START 和各自生命周期边界审查。
尤其不能让新 W-08 writer 直接绕过 Radio 后，还声称此标记覆盖其配置。

## 验证与后续工作

C5 immutable Context `build/wireless-contexts/c5` 生产构建 exit 0，日志
`build/w07-stop-validity-c5-build.txt`。真实捕获和失效 helper 已链接，内部历史
getter 及其停止状态谓词尚无公开调用而未链接。binary 2,780,800 bytes（+288）；
STOP snapshot 20 bytes、完整 checkpoint 688 bytes、restart control 40 bytes，
原十项静态账本大小不变。没有实机内存/碎片比较。构建后仅将 header 注释中的
“framework SDK mutation”澄清为“tracked Radio SDK mutation”，未修改实现。

扩展 deferred `test_wifi_stop_snapshot.py`：真实状态谓词、实际 mutation header
和失效 helper，SDK/锁边界注入；编写 pending STOP、generation/identity/已知
storage/故障条件、只读查询保留、失败 setter、参数仅求值一次、回滚不恢复标记
的用例，并添加 Radio writer/alias inventory 一致性用例。现有其他隔离 fixture
仍以自己的 SDK boundary 检查各函数；不把它们解释为新 alias 的运行覆盖。
本批只对该文件做 AST，未导入、编译或执行任何 fixture。

manifest 49 classes/469 functions、features 27、live SDK schema 35 STA/21 AP、
MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map 和 whitespace 通过。
源与产物 hash、链接边界见 `build/w07-stop-validity-evidence.json`。

下一步仍须将合格历史值接入停止状态的完整 checkpoint 捕获，准备其可能需要的
临时 Station helper，并处理停机期间配置变更、未知前值及公开 restart 的准入。
不能将当前标记当作全部停止状态恢复已完成。完整范围见
[Wi-Fi 剩余清单](2026-09-08-wifi-api-remaining.md)。

Host/Python/VM/竞争、真实 stopped restart、C3/S3/disabled、实机/RF/共存均
**not-run**；全部 Wi-Fi API 完成后统一阶段测试和实机功能验证，长 soak 留到
BLE API 完成后。未刷写、串口操作、擦除 workspace、构建前端、提交、推送或
更新根 gitlink。

后续 [停机后 storage/RSSI 写入边界](2026-09-09-w07-stopped-controls.md)已根据固定 SDK C3/S3/C5 证据允许两项写入保留原 STOP RF 历史；其他失效/故障/未知来源保护仍保留。
