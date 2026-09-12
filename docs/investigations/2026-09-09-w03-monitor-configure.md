# W-03：Monitor 停止后的完整配置替换

在上一批公开单帧 Monitor 基础上增加 `session.configure(options)`，覆盖完整
open options，包括 pool/queue capacity 与 snapLength。仅接纳已停止且原生回调已
排空、无 reaper 清理的 Session；成功保持停止状态，之后显式 `start()`。
start/stop/configure 现在按目标契约返回 `WiFiMonitorStatus`，close 仍返回 void。

firmware HEAD 仍为 `d7db8d1e40ee9f6a522c22806252b2ef712843a7`，工作区未提交。
SDK `fff9895c82d744c7237be8847347bdd1b07c6643`，本批仍只使用既有 immutable
C5 Context `firmware-ci-esp32c5-representative`。

## 交接与所有权

- 所有参数先按 open 的严格规则捕获；这是完整替换，缺省字段恢复默认。
- 新 native Session、pool 和 EventQueue 在交接前创建。需要一个临时 Session
  control 和新 pool 的内存；分配/准入失败关闭临时对象并保留旧停止状态。
- JS Session 的两个内部 queue slot 分别保存当前与待提交队列。`_eventQueue`
  原生 getter 只读取当前 C selector 指定的 slot。暂存 JS 属性可能分配失败，因此
  放在交接前；提交只更新 native pointer、配置副本和 selector，不再写 JS 属性。
- 新增生产 `esp32_mquickjs_wifi_monitor_session_replace`，要求两 Session 同
  runtime/同 task，均停止、无 close/reaper/RX/channel claim，replacement 尚未
  启动且 pool 空闲。检查失败不移动 lease。
- 准入后把**同一精确 Radio lease**从旧 capture 移交到新 capture。旧 capture
  已无 RX、channel 和 Radio claim，随后关闭旧队列/control 不调用 Wi-Fi driver。
  不出现 owner 计数归零窗口，不以 resize 触发共享 Wi-Fi restart。
- 成功提交后 dispose 旧 JS queue，释放旧 JS Session 的 control ref。旧 pending
  receive/Future、已交付 Frame、View、Source 保持自己的旧 pool/context owner；
  最后 owner 释放时退休旧 pool/control。连续 configure 不积累空闲旧队列 control。
- 防御性 handoff invariant failure 仍把 lease 归还旧 owner，并保留其实际 close
  状态。不能将这种异常当作驱动可无条件重启。

成功 configure 更换 Session/pool generation、重置当前 pool stats。status 只报告
当前 pool；所有退休 pool 的统一字节账本仍待 W-09。新 channel/PS policy 在显式
start 时才根据当时 Radio/法规做实际准入；configure 不隐式启动新 capture。

configure/start/stop 的返回对象在原生操作完成后分配。如果该最终状态快照 OOM，
操作可能已提交；调用者应先查询 status。参数捕获、临时对象创建或暂存属性失败
发生在交接前，不应被误报为已提交。公开说明见 [Monitor API](../api/wifi-monitor.md)。

## 已补测试源码，运行后置

`tests/python/test_wifi_monitor_session.py` 新增生产 replace helper 场景：
运行中拒绝、entered RX/reaper 未排空拒绝、错误 task/runtime、self replacement、
新 Session 第 N 次分配失败不影响旧 lease、同 token 交接无 acquire/release，
旧 View 跨交接及新 Session restart 存活，最终仅释放一次 Radio owner。
沿用真实 Session/capture/resources/queue/reaper，SDK/JS/task 仍为显式边界。

`tests/js/flash_data/modules/wifi/monitor-configure-hardware.js` 已加入设备测试
目录并登记到 wifi 模块，要求显式 `wireless-hardware`。覆盖实际公开 API 的完整
buffer 尺寸替换、10 次配置/启停、旧 receive Future 关闭、直接与 Future receive
切换到新队列、无效参数不改变 generation、Radio owner 数量和 idle pool 退休。
该脚本仅做 MQuickJS 语法检查，未刷写、加载或执行；不作为 RF 或 soak 验收。

public configure staging/commit 的完整 real-VM GC/OOM、原生 teardown 与所有
目标的 callback/queue 并发证据仍需要集中运行，不能由上述源码或静态检查推断。

## 本批检查

- C5 最终 build passed：`build/w03-monitor-configure-final-c5-build.txt`，
  bin `0x28a110`，相较上一批 `0x289ad0` 增加 `0x640`，app 剩余 15%。
- ELF 已确认 configure、queue getter、native replace 真实链接。
- MQuickJS：60 sources / 48 snippets；manifest：46 classes / 411 functions；
  feature docs 27；config schema 35 STA / 21 AP 与 live SDK header 一致。
- strict TypeScript declaration、recorded SDK map、相关 Python AST 和 whitespace
  检查通过；SDK source clean。
- Host C/Python/fixture 编译、GC/OOM/并发运行、C3/S3/disabled 构建、实机功能/RF
  测试均 `not-run`。Wi-Fi 功能完成后集中验收；长时间 soak 等 BLE API 完成后。
- 本批未动硬件/串口/workspace、未构建前端、未提交/推送或更新根 gitlink。

Batch、统一 wire、完整 PHY/time、Raw TX、driver/高级模块、W-09 与其余 Wi-Fi
待办继续保留在[剩余工作清单](2026-09-08-wifi-api-remaining.md)，不因当前 configure
可调用就把 W-03 或 Wi-Fi 总目标标为完成。
