# W-02：stopAP timeout 与 AP 类型修正

## 本批实现

`wifi.stopAP(timeoutMs?)` 使用任务书中的可选标量参数；默认 1000 ms，合法值为
整数 1–60000。显式 undefined 等同省略。null、字符串、布尔、对象、数组、非有限
数、小数、越界和多余参数在原生准入前拒绝，不接受 options 对象或 force 别名。

返回仍是现有 WiFiStatus。SoftAP-disabled 的 stopAP 仍为无 AP 副作用的状态查询，
现在同样校验参数；不因增加 timeout 改为启动 driver 或产生新 owner。

复用 `esp32_mquickjs_wifi_wait` 的同 task scope，将 AP_STOP/fence、AP netif
退休、独占 AP 的 shutdown callback drain 以及可能接管的既有配置清理等待计入
同一个预算。API 返回原生错误或完成时，先结束 scope，再构造错误/状态 JS 对象。
如果 scope 准入失败，不结束其他调用持有的 scope。

该参数是协作等待预算，不是硬性 wall-clock deadline：同步 SDK 调用、mutex
获取和结果对象分配不能被抢占。已接受的清理操作不会因超时撤销，也不会释放仍被
callback 引用的资源；新的 stopAP 调用重新获得预算，只继续原 token 的未完成
后缀。APSTA 仍保留 Station/App 的 identity 和 netif，忙碌 Future、wake lock、
其他 feature owner 准入规则不变。长 timeout 不授权强制断连或资源抢占。

输入捕获在 Wi-Fi config 单元实现，供 SoftAP enabled/disabled 入口共用。
Radio、AP 和 netif 沿用既有清理状态，不新增 pool、异步槽或永久资源预算器。

## 公共类型修正

上一批当前 AP 状态新增的 WiFiAccessPointStatus 与旧 startAP 配置快照类型重名。
同名 interface 会合并，造成 ssid/channel 等字段可空性冲突；此前 manifest 检查
不覆盖这种 TypeScript 语义错误。已检查源文件并直接修复 sole v1：

- `startAP` 配置读回结果使用 `WiFiAccessPointStartResult`，started 为 true，
  保留原有非秘密配置字段和实际运行返回行为。
- `status().accessPoint` 使用 `WiFiAccessPointStatus | null`，允许采样字段不可用，
  含当前 clientCount/MAC、query/cleanup 诊断。不是启动配置快照的兼容别名。
- 任务书、源类型与 manifest 同步，增加整份 `types/esp32qjs-c-api.d.ts` 的严格
  TypeScript 静态编译检查。该检查没有执行 JS 或构建前端。

## 测试源码（not-run）

- `test_wifi_stop_ap_timeout.py` 用实际 MQuickJS/实际 scalar capture 覆盖合法范围、
  默认、非整数/非有限数/错误类型、异常分配失败；实际 feature-disabled adapter
  覆盖不增加 native 依赖的幂等返回与输入拒绝。
- 复用实际 AP/中央 stop helper fixture，检查 capture/scope 失败不进入原生准入，
  失败关闭后 scope 确实结束、37 ms 自定义值传入预算、新调用沿用未完成 token，
  成功后保留 Station/Application identity。
- `test_wifi_radio.c` 新增 `partial-ap-stop-wait-budget`：完整生产 Radio/budget，
  10 ticks 中前置步骤消耗 8，AP_STOP 等待仅剩 2；迟到 AP_STOP 后重试不重发
  set_mode，不 global stop，不更换 Station/App owner。
- 已有 whole-stop 两个 netif 连续共享预算的生产 fixture 继续适用。没有执行
  Host C/Python、故障注入、RF 或运行测试；这些源码不作为测试通过证据。

## 本批验证与交接

C5 immutable Build Context 编译、MQuickJS 语法、manifest/features/live SDK schema、
recorded map、Python AST/whitespace 与 TypeScript 声明检查的实际结果记录在
`build/w02-stop-ap-timeout-evidence.json`。C5 日志为
`build/w02-stop-ap-timeout-c5-build.txt`（app `0x2824c0`，剩余 16%），声明检查日志为 `build/w02-stop-ap-types.txt`。

未进行 C3/S3/feature-disabled 构建矩阵、实机/heap/队列/GC/RF 运行验收或 soak。
未刷写、串口操作、擦除 workspace、前端构建、提交、推送或修改根 gitlink。
SDK 源保持原固定版本，既有 SRAM 优化保留。Wi-Fi 完整范围继续按
[剩余清单](2026-09-08-wifi-api-remaining.md) 推进；全部 Wi-Fi API 后统一阶段测试与
实机功能测试，BLE 在相关测试之后，长时间 soak 待 BLE API 也完成后。
