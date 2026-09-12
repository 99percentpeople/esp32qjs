# W-08 TWT probe timeout 参数存活与身份

基线 firmware `d7db8d1`、SDK `fff9895c82`。本项继续修复 probe 原生生命周期，
不注册公开 TWT；代码、静态链接和动态验收分别记录。

## 根因

固定 C5 probe submit 与 TX callback 的两次 timer 设置都把 `node + 1057`
作为参数。`itwt_probe_timeout_fn` 把这个地址交给 `ieee80211_timer_process(7,28,arg)`，
后者只复制地址进八字节 Wi-Fi 消息，没有复制阶段值。

`itwt_probe_timeout_fn_process` 重读当前 node，仍从旧 arg 解引用阶段；阶段
不匹配会进入断言循环，node 已不存在则有空地址访问/异常分支。另一方面，
`ets_timer_done` 只异步删除 esp_timer 并清空 legacy handle，不能证明 callback
已返回或已排队的 Wi-Fi 消息已处理。以上为固定源码/对象控制流证据，动态
竞争复现仍按用户要求后置。

## 生产修复

Wi-Fi OSI table 的 setfn/disarm/done 加入精确 `&itwt_probe_timer` 分支。
其他 timer 原样转交，包括共存组件的其他 timer。构建 gate C5 Wi-Fi/coex
adapter、legacy timer 源文件、既有 net80211/PP/ROM 输入；静态断言 OSI 三个
field offset，最终 ELF 再核对函数表值。

setfn 只在原生 Wi-Fi task 中比较参数与当前 node 的阶段字段地址，读取当前
阶段，保存用于比较的 node 地址值。给 esp_timer 的 callback 参数改为非零
32-bit identity，不再保存 node 地址。每次 setfn 消费新 identity，跨 runtime/
driver restart 不重置；耗尽明确 fault，永不回绕。

callback 不解引用参数，只核对数字身份并最多 post 一次；消息也只携带该
数字。disarm/done 在调用原生函数前使身份失效，所以删除窗口中的旧 callback
和已排队消息不能变成新 timer 的 authority。原生 post 失败保存原错误与
失败 identity，若错误迟到且已换 timer，不污染新 timer。

Wi-Fi timeout handler 消费精确身份一次，再核对当前 node 地址、active 和
阶段；缺失、替换或已结束的 node 不进入原生异常分支。最终传给原生 handler
的是栈上一字节副本；该 handler 只同步读取它，不把地址留给另一条异步路径。
任何保存的旧 node 地址都只参与比较，绝不解引用。

异常 setfn、identity 耗尽或当前 post 失败保持可诊断状态；没有 runtime reset
身份或伪造 drain 的接口。原生 setfn 是 void 且内部使用 aborting allocation
API，本项不声称已修复其 OOM/start 失败策略；guard 无法接受时安装的是无效
数字 callback，后续 probe admission 拒绝故障状态。完整清理/恢复策略仍需在
Radio/公开 Future 接入时完成。

## 证据与边界

新增 boot 静态记录 28 字节，不另分配 heap，不保留 JS/runtime 指针；既有
512 字节 TX ledger 和相关框架静态对象不变。内部 SDK snapshot 增加
`probe_timer` 的值诊断。current_identity 为零只表示当前身份失效，不证明
原生 timer/队列资源已排空。

C5 最终 `g_wifi_osi_funcs` 的 offset 228/232/236 分别指向新 disarm/done/setfn
wrapper；`ieee80211_itwt_probe` 确认调用新 timeout handler。disarm 及其 helper
保持 IRAM；其他目标和 Wi-Fi 关闭无此模块符号。四目标/关闭构建、原始输入
hash、实际函数表与静态存储见 `build/w08-twt-probe-timer-evidence.json`。

生产 fixture 覆盖迟到 timer、旧队列消息、阶段变更、node 消失/替换、提前
投递、disarm/done 窗口、post 失败与迟到错误、身份耗尽及其他 timer 转交。
SDK fixture 使用生产 capture/match；均仅 AST，未导入、编译或执行。

尚未完成：RX Probe Response 的关联语义、完整 timer/native/default-event-loop
退休与故障恢复、Radio owner、Agreement/Future/公开 TWT、其余 Wi-Fi 范围。
所有 Wi-Fi API 完成后集中运行与实机测试；长 soak 留至 BLE API 完成后。
本轮未刷写、串口、擦除、前端构建、提交、推送或更新父仓库 gitlink。
