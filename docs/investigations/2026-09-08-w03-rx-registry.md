# W-03：共享 RX registry 的准入、分发保留与关闭

## 本批实现

`wifi_common/esp32_mquickjs_wifi_promiscuous_broker.c` 新增生产 registry 层，分发
实际调用已有 target adapter、MAC parser 和 native filter。最多 8 个 subscriber，
boot 常驻表只保存有界指针、identity、phase 与进入标记；filter/state 属于调用方
session 的控制存储，没有每包 allocation，也没有新 JS 类型或 API 注册。

目前没有安装 SDK promiscuous RX callback，也未将现有 Radio 独占 enable lease
改成多 owner。此 registry 是完整 broker 的一部分，不能称 Monitor 或 CSI 共用
接收已经可用。下一步仍须接入 driver 操作事务。

## 生命周期与精确身份

| 内部入口 | 原生效果 | 调用方责任 |
| --- | --- | --- |
| reserve | 校验有界 filter、拒绝重复控制地址/非空输出 token/容量或 identity 耗尽；复制不可变 filter 并预留 slot，尚不参与 dispatch | 成功后保留 subscriber 和 sink context，持有独立 Radio ownership |
| activate | 仅 RESERVED/ACTIVE 可进入或保持 ACTIVE | driver callback/filter/enable 提交成功后才调用；失败不能先发布正在采集 |
| begin_close | RESERVED/ACTIVE/CLOSING 均进入 CLOSING，停止新 dispatch 准入 | 保留控制存储并完成 driver 需求缩减/最后 owner 恢复；重复调用安全 |
| finish_close | 仅 CLOSING 且 entered=0 时移除 slot、清空 token；否则返回 DRAINING/错误，保留资源 | 只有 driver cleanup 也成功后才释放 session/context；不把 registry 排空当作原生注销证明 |

identity 使用 boot 单调 u32，UINT32_MAX 分配后永久耗尽，不因最后 owner 关闭、
runtime teardown 或 Radio restart 复用。旧 token、重复 finish、同地址重新预留
都不能关闭新订阅；关闭期间仍占用有界 slot，不能为了新 open 提前复用。
输出 token 仅在 reserve 成功/finish 成功时改变，失败不覆盖现有 token。

调用方必须串行化控制事务，并在 reserve 成功到 finish 成功之间保持 subscriber
及 context 存活、filter 不可变；不能绕过这些入口重写 filter_state。registry
不持有 JS roots，不负责 JS 对象 GC 或 session 的 driver 失败后缀。

## 分发与并发边界

dispatch 在一个短 critical section 中选择所有 ACTIVE slot，并一次性保留整个
准入集合。随后退出 critical section，解析一次原生 callback，逐 subscriber
执行真实 native filter 和 sink，sink 返回后才释放该 slot 的进入保留。

因此关闭第一个 subscriber 时，第二个即使尚未进入 sink、但已被本次 dispatch
选中，也不能释放。关闭后本次已准入的 sink 仍可能运行；session sink 必须使用
自己的 closing 状态决定是否丢弃，公共 close 的完成必须等 drain。分发途中新增
或重新启用的 subscriber 只参加下一次 dispatch。

完成 finish 后，后续 callback 只访问 boot registry，不读取已释放的 subscriber。
一个 sink 返回后，dispatcher 不再读取该 subscriber/control；释放全部 slot
保留后才解除全局 dispatch_busy。

并发或重入 dispatch 不等待，整次丢弃并累计饱和 overlapping_dispatches，避免
同一个 filter 的抽样/限速状态并发修改。SDK 文档将正常 callback 放在 Wi-Fi
driver task；此防护不是允许 sink 阻塞的理由。sink 在临界区外运行，但仍必须
无 JS/SDK/Radio 调用、无分配/日志/等待，仅做有界复制、native counters 和队列
操作。最大 payload 复制成本及真实负载 profile 仍由后续 Monitor 实现与验收。

snapshot 返回 RESERVED/ACTIVE/CLOSING、entered、busy、耗尽与重叠计数，以及
RESERVED+ACTIVE 的类型/Control subtype/错误帧需求并集，排除 CLOSING。这些
mask 是公共内部语义，尚未转换为 SDK filter bit；不把 OR 结果直接当 driver 配置。

## 测试源码（not-run）

`tests/python/test_wifi_promiscuous_broker.py` 使用记录的 C3/S3/C5 native 类型，
编译真实 adapter/parser/filter/registry；唯一线程边界替身是 pthread 实现的
FreeRTOS critical section。测试源码包括：

- 8 slot 上限、预留不接收、重复 activate、独立抽样状态、sink 中实际 payload 复制；
- 旧 token、重复 finish、同地址新 identity、最后 u32 identity 分配后永久拒绝；
- 可控暂停第一个 sink，同时关闭两个已选 subscriber；第二个尚未进 sink 时
  finish 也必须 DRAINING，不能回收其控制存储；
- 分发途中新增第三个 subscriber，只在下一次 dispatch 收到包；重入与并发
  dispatch 返回 busy，sink 可读取 snapshot，证明回调不在 registry critical 中；
- 关闭完成后将原控制页设为 PROT_NONE，迟到 dispatch 不读取该页；
- 预留/启用/关闭的需求并集及错误帧请求变化。

受控 sink 的等待仅用于注入调度交错，不是生产允许的 callback 行为。fixture
不假装 SDK unregister/enable/filter 具有已证明屏障。测试自带编译/执行 30 秒
超时；本批没有执行或编译该测试，只做 Python AST 检查。

## 已执行验证与未完成边界

C5 immutable Context `build/wireless-contexts/c5` 编译通过，新 registry 目标
object 已生成。因尚无 live driver caller，最终 ELF 仍未链接本组 helper；app
大小 `0x2824c0`、余量 16%。这不证明真实收包或多 owner 共存。

MQuickJS 59 sources / 48 snippets、manifest 44 classes / 398 functions、feature
文档 27 项、config schema 35 STA / 21 AP 与 live SDK header、recorded map、
TypeScript strict declaration、测试 AST 与 whitespace 均通过。日志与 object/
源码 hash 记录于 `build/w03-rx-registry-evidence.json`。

SDK 保持干净，无刷写/串口操作/擦除 workspace/前端构建/提交/推送/根 gitlink
更新。Host/Python 竞争测试、C3/S3/feature-disabled 构建、实机/RF/heap 均未执行。
按用户顺序，Wi-Fi API 完成后统一阶段测试与实机功能验证，长 soak 待 BLE API
完成后。完整 Wi-Fi 目标仍未完成。

下一步需要在 Radio 串行化范围内实现 driver filter 快照/需求合并/恢复、稳定
callback 的 first/last owner 管理与失败后缀，再将 CSI enable 需求和 Monitor
订阅接入。仅 registry 排空不能替代这些 driver/owner 边界。
