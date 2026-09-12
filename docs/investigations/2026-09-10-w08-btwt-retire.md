# W-08 广播 pending setup 联合退休与 held 释放

本批连接 pending setup 的取消/静止检查、timer/native 顺序屏障、默认事件
队列确认和精确 held 释放。已建立 Agreement 的 teardown、Radio/公开 Future
接入仍未完成，未注册广播 setup/close 占位 API。

## 回收顺序

新的 worker 协调器使用稳定 owner 存储，按精确 Radio token/generation、
广播 ID 和 TX request identity 工作。取消成功后取得 TX/timer revision
快照，复用现有 `wifi_twt_fence`：ESP_TIMER_TASK marker 运行、确认 callback
真正退出、delete 成功，再排入原生 Wi-Fi 队列 marker。任何步骤失败都保留
存储和已完成的步骤；失败的 marker 先完成 clear 后才允许重建。

随后向既有 boot Radio control handler 发送复制的数字标记：广播 ID、
请求身份、确认序号及状态快照，不携带 runtime/Future/owner 地址。control
事件 ID 为同一私有 base 下的 6，与既有生命周期、Action、FTM、probe、
individual setup 标记分开。观察队列满时保留 held 并返回本次 post 错误。

确认序号每个请求不回绕。pending post 期间禁止释放；事件在 post 返回前
提前到达时可记录 seen，但只有 post 本身成功后才可释放。失败 post 清掉
确认标记，后续重试使用新序号。旧请求或旧序号不能替后继请求确认。

最后在原生任务重新检查精确 held/cancelled、当前 TX/timer 静止、同一状态
revision、事件 posted/seen 且没有 post in flight，才撤销 held。留下的
值记录只供诊断，后继非复用身份可以替换它；重复 release 不会释放后继。
状态变化会令协调器清除旧 marker、重新取快照并重新走顺序屏障。

fence 序号/确认位的变更不增加它所证明的 native-state revision；独立序号
区分重试，所有原生状态变化仍由 revision 检测。没有把一次空 bitmap、
超时、断连、单纯取消成功或观察事件当作联合退休证明。

## 失败与边界

Public Future 超时/GC 后仍须保留协调器存储，直到 poll 成功；这层不持有
JS roots，也不释放 Radio lease。lease 和公开 Agreement 的关闭仍由后续
owner 接入负责。清理失败只重试未完成步骤，已释放 held 的记录不再重复
进入原生取消/释放。

本路径只处理 pending setup。dwell 已建立的协议会被原有 cancel guard
拒绝，后续必须走精确 teardown/TX/PM 回收；不能把这个拒绝当作 close 已完成。
本地退休也不代表已经撤销发送到 AP 的 RF 请求。

## 预算与接入证据

结果增加确认序号和控制位，由 56 B 增至 64 B；timer entry 从 96 B 增至
104 B，32 项延迟 INTERNAL 池从 3072 B 增至 **3328 B**。没有新增第二个
结果池或静态 owner 数组。timer 静态对象仍 52 B、dispatch 36 B、TX 主账本
768 B、可选 storage 1568 B。worker 的 marker/协调器沿用稳定 owner 存储。

Radio control handler 的广播确认路由、private ioctl 的精确 release 在生产
ELF 有调用链。worker 退休协调器、post marker 和 SDK release 包装已编译入
archive，尚待公开广播 owner 调用；不把内部编译成功写成公开 API 完成。

五种生产构建及 manifest/feature/schema/coverage、严格 TypeScript、MQuickJS
语法检查通过；C5 镜像 3030592 B，无 SoftAP 为 2907584 B，C3/S3/disabled
镜像大小不变。220 项文件哈希已核对。构建、ELF/archive/IRAM、静态检查证据记录为
`build/w08-btwt-retire-evidence.json`。新增 fixture 同时调用生产广播结果/
取消/释放、生产 timer/native fence、生产退休协调器及 Radio control handler；
只注入 SDK 队列、timer 调度、分配/停止/删除和事件投递边界。覆盖 callback
退出等待、delete/native fence/post/release 错误、状态变化撤销旧证明、迟到
数字 marker、提前投递、错误后重试和重复 poll。

fixture 仅 AST，未导入/编译/执行。RTOS/GC/竞争/实机 RF 仍在 Wi-Fi API
全部完成后集中验证，长 soak 留到 BLE API 完成后。未刷写、串口操作、提交、
推送或更新根 gitlink，共享 SDK 不作修改。
