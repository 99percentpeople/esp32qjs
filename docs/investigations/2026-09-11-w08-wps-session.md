# W-08 WPS Station helper 与原生 Session

接续 [Radio 配置恢复](2026-09-11-w08-wps-radio.md)，本批实现原生 Session、
Station helper 预留/排空及 runtime 退出协调。JS 公开选项、Session/Future/
watch 尚未注册；不能把原生 Session 当作公开 API 已完成。

## Station 预留与两阶段关闭

Session 激活在 runtime 任务上先预留已有 Station helper，再提交后台协商。
预留使用 boot 内不复用的 identity，并推进连接 generation；它计入现有
connection reservation，因此配网尚未进入 Radio 的间隙，普通连接/断开/
扫描 Future 和 helper 配置/退休也不能进入。旧 token 不能释放后续预留。
SDK 和 netif 调用不放在 helper lock 或 Session critical section 中。

WPS 协商使用原生 Station 连接，会产生普通断连/IP 事件。其 IP 事件不当作
应用连接成功，仍由原 helper 的 disconnect epoch 和默认 DHCP/IP fence 处理。
新增 Radio `prepare_close` 先停止协商、恢复配置并排空 boot 事件，但保留
Radio binding。runtime 随后检查 netif 已 down，并重试现有 link fence 的
未完成后缀；队列满时保留预留，不重发已经完成的原生断连。

helper 排空后，后台 worker 才最终释放 Radio；runtime 最后释放 helper
预留。整个顺序保持准入关闭。无法证明原生关闭、netif down 或事件排空时，
保留资源和 cleanup error，不把 runtime restart 当成物理故障恢复。

## Session 存储与调度

Session 使用有界 handle 计数、独立 registry/worker 引用和后台调度。create
只校验/复制参数；activate 在公开对象构造成功后调用。配置、PIN 和最多三组
凭据由原生对象持有，JS Future 结束不释放正在执行的 SDK 参数或输出。

PIN 和凭据从 Radio copy/commit 转入 Session；凭据必须先完成捕获关闭及
Station helper 排空。Session 到 JS 仍为独立 copy/commit，转换失败不消费
结果。关闭和最终释放清零全部秘密。worker_busy 隔离后台写入和 runtime
读取；取消发生在复制期间时，worker 返回前完成清零，不发布已取消结果。

超时、worker 队列拒绝、分步清理失败和最后 handle 释放都保留中央关闭职责。
receive/close waiter 计数为后续 Future/观察顺序提供实际门槛；公开绑定仍待接入。
native Session deadline 不改变固定 SDK 自己的 WPS 协商 timeout，公开契约还需
分别说明两者。core 在 Session/worker 排空前不销毁 Wi-Fi helper 或 runtime。

## 构建上下文与证据

首轮生产构建发现遗漏后台 worker 声明头文件，已补 `esp32_mquickjs_future.h`。
修正后 C5 roaming 的实际镜像为 `0x304bb0`，超过原 `0x300000` app 分区
`0x4bb0`（19,376）字节。保留失败日志和原 Build Context，另生成
`build/wireless-contexts/c5-roaming-wps`：相同 8 MiB flash、相同功能/config
输入，app 改为 4 MiB，storage 仍 512 KiB，workspace 变为 `0x370000`。
仅测试构建输入发生变化，没有刷写或改变设备分区/workspace。

上下文差异与 hash 在 `build/w08-wps-session-context.json`；生产构建与
archive/ELF/DWARF 核对记录在 `build/w08-wps-session-evidence.json`。
由于 runtime service 已调用 WPS 清理路径，原生 WPS 实现进入最终 ELF；
这仍不等于存在公开 JS caller。

新增 `test_wifi_wps_session.py` 和 `test_wifi_wps_station.py` 使用生产 Session/
helper 函数，注入调度、Radio、driver/netif/event 边界，登记取消、队列满、
copy/commit、GC 最后 owner、关闭后缀、旧 identity 和真实 helper fence 用例。
Radio fixture 追加 prepare_close 的保留检查。按安排仅 AST 检查，未导入、
编译或执行测试 fixture；RF、设备、GC/OOM/RTOS 集中运行仍为 not-run。

剩余：公开 WPS options/status/capabilities/Session/Future/watch、AP registrar
实际契约、完整安全与错误注入、集中运行/实机验收。其他 Wi-Fi 剩余项继续
按总表推进，BLE 与长时间 soak 的顺序不变。

最终 C3、S3、C5 roaming（新上下文）、C5 no-SoftAP、C5 Wi-Fi-disabled
五配置生产构建通过。生成 SDK 源文件与 patcher 相符，Radio/worker/Session
object 与实际 archive member 字节一致，Session service/teardown 与原生 WPS
存在于启用配置的最终 ELF；公开 JS WPS binding 不存在。

四种启用配置 DWARF 确认 Session 768 B、Session status 136 B。最终 ELF 的
Session/Station 自有常驻符号合计 C3/C5 17 B、S3 25 B，不包含链接对齐、
Radio 和 SDK 全局量，不能冒充全部 WPS 常驻/峰值内存。尚未被公开 caller
引用的 Station identity 分配器目前被链接裁剪，公开接入后须再次核对。

镜像相比前批增加：C3 38,400 B，S3 33,520 B，C5 roaming 38,464 B，
C5 no-SoftAP 38,240 B。新 C5 roaming app 分区余 1,029,200 B；no-SoftAP
原分区余 104,528 B。disabled 镜像仍为 459,024 B，worker/Session object
无定义符号或外部引用，最终 ELF 无 WPS；本次重建 binary hash 与上批不同，
只记录尺寸和 feature-disabled 链接证据，不宣称逐字节相同。

manifest、类型、文档、schema、SDK coverage、MQuickJS 及 whitespace 静态
检查通过。产物核对脚本修正了 `js_wifi_wps_` 被内部 `mquickjs_wifi_wps_`
名称包含造成的误判；公开符号检查改为完整符号名前缀。未运行测试 fixture、
刷写、操作串口、提交、推送或更新根仓库 gitlink。
