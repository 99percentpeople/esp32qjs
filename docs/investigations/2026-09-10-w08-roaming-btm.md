# W-08：Roaming 能力查询与 BTM Query

firmware `d7db8d1` 未提交工作区，固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本批从未实现的 roaming 模块推进四个公开 callable；Neighbor Report 专属请求和
完整 roaming 生命周期仍未完成，Wi-Fi 总目标不缩小，等级保留 Candidate。

## SDK 依据与边界

直接核对本地固定 SDK 下的 `esp_common.c`、`rrm.c`、`wnm_sta.c`、
`ieee802_11_common.c`、`port/eloop.c` 和公开 `esp_rrm.h/esp_wnm.h`：

- RRM/WNM 公共函数直接访问 `g_wpa_supp`；框架 mutex 无法独自排除原生 RX/reset。
  `eloop_register_timeout_blocking` 把调用交给 Wi-Fi/supplicant 任务；它在 handler
  完成或 eloop destruction 后才释放 waiter。现有 WPS/DPP 也使用该同步机制。
- BTM query 接收 reason、字符串 candidate 和 scan-cache 开关，返回 SDK 提交
  结果。它没有完成 callback；SDK general error 也可为 -1，不将 -1 单独解释成
  AP 不支持。查询可能引出后续漫游，没有可确认的取消/超时接口。
- Candidate parser 寻找 ` neighbor=` 并用 `strtol` 读取 BSSID information。
  ESP32 long 为 32-bit；uint32 高位值若直接编码正十进制会溢出。框架改用等价
  signed decimal，保留全部 32 位。其余字段不接收任意字符串或可变 subelement。
- Neighbor Report 公共 request 没有 cookie/arg。SDK 回调先尝试发布默认 event，
  失败可能丢失观察；RRM timer/register 错误、token 回绕/reset、原生取消/退休和
  迟到报告仍需要专属控制记录。不能把现有 `wifi.watch()` 当作可靠 request Future。

上游可定位来源：[固定 SDK esp_common.c](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/wpa_supplicant/esp_supplicant/src/esp_common.c)、
[WNM 实现](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/wpa_supplicant/src/common/wnm_sta.c)、
[SDK eloop](https://github.com/espressif/esp-idf/blob/fff9895c82d744c7237be8847347bdd1b07c6643/components/wpa_supplicant/port/eloop.c)。
本批依据实际本地内容与 HEAD，相关源 hash 收入证据，不用其他 SDK 版本的描述替代。

## 接入

`wifi.roaming.capabilities` 反映 RRM/WNM/11r 编译 gate，`isRrmSupported` 仅在
RRM gate 注册，`isBtmSupported/sendBtmQuery` 仅在 WNM gate 注册；三项均关闭
不创建模块。11r 继续沿用 Station 配置。正式 v1 没有 request/watch 占位方法。

BTM options 解析完整复制最多 16 项到原生值，严格字符串/NUL、plain options、
整数、MAC/去重与 preference 检查完成后才允许提交。最大 SDK candidate 文本
缓冲为 1025 bytes，邻居 IE 合计最多 288 bytes。没有自动 scan-cache 列表；这
不是对整个 SDK allocator 或 W-09 预算的完成证明。

JS 成功结果及其属性全部在 native 调用前分配并 rooting，避免提交成功后因结果
构造 OOM 而误报未知完成。SDK 收到的仅为稳定 native 值；同步返回后无持久操作
对象、JS root 或新的框架全局 owner。原生只检查必要的 Station `btm_enabled`；
复制的配置在成功/失败出口均复用 secure-zero，状态/错误不包含凭据。

Runtime helper 准入沿用 `wifi_driver_helpers_ready`，随后在 Radio mutation mutex
内复核精确 application/STA/AP lease、driver/cleanup/operation 状态。读查询不启动
driver；普通 stopped/unassociated 返回 false，故障或正在关闭不会被伪装成 false。
写入拒绝共享外来 owner、promiscuous、临时 rate、wake lock 和固定/冲突信道。
APSTA 要求 `allowApChannelChange:true`。SDK dispatch 内再次确认连接，再检查
BTM support 和已配置的 BTM 开关；handler 不获取 Radio mutex、不运行 JS，也不
等待 default event loop。提交前保守失效 STOP 快照，避免后续漫游影响原观察。

准入只约束提交时刻。查询发出后 AP 可延迟响应，Station 也可收到 unsolicited
BTM；本 API 不冻结后续信道、不承诺 AP 客户端不断连、不声称完成异步漫游。
`{accepted:true,completion:"sdk-submit",reason,candidateCount}` 只确认 SDK 提交。
错误保留 stage、espCode、nativeEntered、submissionAttempted 和原始 sdkCode。

## 构建与检查

新增生产 context profile `wireless-roaming` 显式启用 11KV/RRM/WNM/11R。独立
`build/wireless-contexts/c5-roaming` / `build/wireless-c5-roaming` 不改普通 context。

首次构建实际发现并修正：使用了不存在的 MQuickJS `JS_ToBool`、wpa_supplicant
依赖未进入 early-expansion graph，以及新属性表遗漏 `JS_PROP_END` 引起 host ROM
生成器崩溃。依赖加入既有无条件图，源/符号继续 gate；三个 SDK gate 同步传给
host ROM generator。失败日志保留，最终构建和 ROM/ELF 检查验证修复。

| 实际构建 | 最终 binary | ROM/ELF |
| --- | ---: | --- |
| C5 roaming enabled | 2,884,448 bytes | 四个公开方法及 Radio → SDK → eloop 调用均链接 |
| 普通 C5，roaming disabled | 2,856,608 bytes | 不注册/链接上述漫游方法，与前批普通 C5 尺寸一致 |

不同 SDK 功能 context 的体积差不能当作本 API 的纯运行内存开销。已跟踪 30 个
框架静态对象尺寸均与前批相同；没有进行设备 stack/heap/PSRAM/largest-block
测量。SDK 启用后的自身存储、动态 candidate allocation 和完整 W-09 仍待验收。

manifest 51 classes / 497 functions、features 27、STA/AP schema 35/21、严格
TypeScript、MQuickJS 61 sources / 57 snippets、SDK map、Python AST、whitespace
通过。证据 `build/w08-roaming-evidence.json`；底层 inspection 记录
`build/w08-roaming-inspection.json`，包含 gate、链接函数和静态尺寸。

## 后置测试与未完成项

新增 `test_wifi_roaming.py` 直接组合生产 encoder/capture/Radio registry、SDK
dispatch 和真实 MQuickJS 转换：准备 unsigned 高位编码、容量/重复 MAC、APSTA
权限、旧 lease、其他 owner、SDK 错误/未执行 dispatch、连接消失、GC/逐次 OOM
以及输入/结果失败前零提交用例。另在 context fixture 增加三目标 profile 检查。
两份 Python fixture 只 AST 解析，没有 import、编译或执行；不能声称这些测试通过。

Neighbor Report 专属 request/watch、完整 roaming 事件、native token/timeout/取消/
重开/runtime teardown 及硬件 RF 仍保留。RRM-only/WNM-only/11r-only、C3/S3
和全 feature matrix、Host/Python/VM、实机阶段测试均 not-run，等待全部 Wi-Fi API
完成。长 soak 放到 BLE API 完成之后。TWT、enterprise/WAPI、WPS、DPP、
SmartConfig、NAN、Mesh 和 remaining 清单中的其他工作不被改为 target-unsupported。

未刷写、串口、擦 workspace、提交、推送、修改 SDK/根 gitlink 或构建前端。
