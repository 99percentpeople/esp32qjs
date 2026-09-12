# W-03：公开单帧 Monitor、元数据和字节所有权

firmware HEAD `d7db8d1e40ee9f6a522c22806252b2ef712843a7`，本批为未提交增量。
SDK `fff9895c82d744c7237be8847347bdd1b07c6643`；仅使用既有不可变 C5 Build
Context `firmware-ci-esp32c5-representative`。根仓库无本批修改。

## 已接入

新增 `wifi.monitor.open/capabilities`、`WiFiMonitorSession` 的
status/stats/receive/start/stop/close，以及 `WiFiMonitorFrame` 的
bytes/copyBytes/source/close。新类使用 46/47 偏移，实际 class count 为 USER+48；
stdlib、内部 ID、VM fixture、正式类型、manifest 及共享 API 文档同步。
`wifi.capabilities()` namespaces 包含 monitor，promiscuous 表示已有可调用实现。

open 在 Radio mutation 前捕获全部参数、创建 JS 对象/队列并登记实际 receive
函数的 EventQueue Future driver；最后启动 native Session 后才发布 JS opaque。
构造失败关闭原生前缀、dispose 已创建的队列并释放独立 caller ref；未确认关闭的
原生 suffix 由原 Session registry/reaper 保留，不丢弃回调引用的 storage。

Queue bridge 已将 EVENT 转成 PUBLIC；Frame converter 先取得独立 Session ref，
在元数据 JS 构造全部成功后才挂载 opaque。转换失败只释放 adapter/context ref，
PUBLIC root 由 bridge 回收一次。Frame 关闭先关 payload root，再释放 Session ref。
View/Source 先 retain payload 与 Session，再创建 JS 包装，失败使用共用消费者的
release/destroy 契约。Source 原生 iterator 活跃时，JS destroy 延迟到 iterator close。

Session.close 后仍保留状态查询对象。此前原生实现只在 control 最后引用释放时
尝试 deinit pool，会让一个空闲 closed JS Session 继续持有全部 pool bytes；
本批改为每次 closed context release 均尝试在 resources lock 下退休空闲 pool。
Frame/View/Source 仍占用 slot 时不退休；最后 payload owner 的 context release
归还 pool，即使 JS Session/control 仍存活。`resources_initialized` 是一次初始化
事实，不从 worker 清零；并发释放以 resources snapshot/deinit 确认已有退休。
此路径的回归源码已补，按用户要求未先运行，不声称取得运行复现/修复验证。

## 实际契约与未完成项

详细可调用契约见 [Monitor API](../api/wifi-monitor.md)。直接 receive 和
Future.call 使用相同队列语义：stop 保留 wait，timeout/cancel 只结束 wait；close
关闭并丢弃排队帧，未取得帧的 receiver 返回 null。已经取得 EVENT 的 Future 可以
在 Session 关闭后完成 Frame 转换，持有的 payload/context 保留到对应 owner 释放。

元数据使用 callback monotonic time；driver RX clock 扩展仍属于 W-06。
HE format 用固定 SDK 命名枚举，未解码 SIG 字段为 null。legacy bitrate 不使用
原始 SDK code 冒充数值速率。short-header subtype、未知 secondary 可为 null，
同步修订目标 v1 类型。MAC role 仅在完整、type-matching parse 时发布；FCS 不推断。

Source 仅输出原始捕获字节，不是统一 wire。configure、receiveBatch、完整 PHY/time、
统一 wire/PCAPNG、W-09 byte budget、AP/APSTA 共存和连续 require-none lease 尚未完成。
capabilities 对未注册能力返回 false。八 Session control 上限仍包括 closed/retired
引用，payload pool 退休不自动消除仍可查询的 JS control。公开接口仍为 Candidate。

## 本批检查

- 最终 C5 build passed：`build/w03-monitor-public-final-c5-build.txt`。
  bin `0x289ad0`，相较上一批 `0x284c40` 增加 `0x4e90`，app 分区剩余 15%。
- 首次 C5 build 因误把 MQuickJS `JS_SetPropertyUint32` 的 JSValue 结果按 `<0`
  判断而失败；已改用 `JS_IsException`，保留最初日志供追踪。
- ELF nm 已确认所有公开 Monitor entry、capture/options/resources/metadata 路径
  实际链接；不再只有无调用者的 target object。Session pointer registry 仍为 32 bytes，
  本批没有静态 payload buffer；不将二进制大小或符号大小当作运行堆验收。
- MQuickJS syntax：59 sources / 48 snippets；manifest：46 classes / 410 functions；
  feature docs 27；config schema 35 STA / 21 AP 且 live SDK header 一致；strict TS
  declaration、recorded SDK map、三个相关测试文件 AST、git diff whitespace 通过。
- SDK source clean，保留既有 SRAM 优化；没有硬件内存比较或新 RF 证据。

## 已编写、未执行的测试

`test_wifi_monitor_metadata.py` 组合真实 RX parser、生产 metadata converter 与
vendored MQuickJS：完整/短 header、type mismatch、metadata-only、unknown PHY、
原生时间越过 UINT32_MAX、地址角色以及逐次 JS 分配/写属性 OOM 与 moving GC。
此文件仅 AST 解析，未 import/编译/运行。

`test_wifi_monitor_session.py` 继续使用生产 Session/capture/queue/resources/reaper，
新增 closed JS control 不 pin idle pool、最后 View 释放退休 pool，以及生产 Source
helper 在 Frame/Session 关闭后仍可读取、JS destroy 与活跃 native iterator 的寿命、
重复 open 拒绝和未打开 Source 立即释放。JS/SDK/task 边界保持显式 fixture；不能
把这些测试当作完整 public JS constructor/Future/VM runtime teardown 证明。

Host C/Python、public open/Frame/View/Source 的全链路 OOM/GC/队列竞争、C3/S3及
feature-disabled build、设备实测均 `not-run`，等待全部 Wi-Fi API 编码后集中执行。
Wi-Fi 实机功能测试仍在本阶段最终范围；长时间 soak 等 BLE API 完成后再做。
本批未刷写、未操作串口、未改设备 workspace、未提交/推送、未更新根 gitlink。
