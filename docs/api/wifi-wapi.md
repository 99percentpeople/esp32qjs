# Wi-Fi WAPI

需要 Wi-Fi 和 `CONFIG_ESP_WIFI_WAPI_PSK`。固定 SDK 的 C3/S3/C5 支持 WAPI-PSK；
未编译此选项时不注册 `wifi.wapi`。当前为 **Candidate**，运行和 RF 验收后置。

| 方法 | 行为 |
| --- | --- |
| `wifi.wapi.capabilities()` | 返回 `wifi-wapi/1`、PSK、原生 owner 与控制边界 |
| `wifi.wapi.status()` | 返回非秘密策略、实际原生生命周期及故障快照，不初始化 Radio |
| `wifi.wapi.enable(options?)` | 允许 SDK supplicant 初始化实际 WAPI 支持 |
| `wifi.wapi.disable(options?)` | 禁止 SDK supplicant 在下一次初始化时创建 WAPI 支持；已初始化时通过完整重建立即应用 |

`options` 只接受 `timeoutMs`：整数 1..2147483647 ms，默认 10000。未知字段、非整数和
非法范围在修改前拒绝。控制使用普通同步生命周期等待预算，不是 Future 方法。

默认策略为 enabled，与 SDK 启用 WAPI 构建的行为一致。首次 Radio 初始化前，控制
只选择初始化策略，不隐式启动 Wi-Fi；此时 `policyApplied` 为 false。已有原生实例
需要从健康、完整停止且没有任何 Radio owner 的状态重建，沿用 `wifi.driver.restart`
的 checkpoint、helper 排空、物理 deinit/init、配置重放和读回。**成功后会按保存的
Station/AP/APSTA 配置启动接口**；调用前应使用 wifi.stop 并关闭其他功能 owner。
AP 配置因此会重新广播。相同且已应用的策略为幂等操作，不重复初始化。

SDK 的 WAPI init/deinit 只由原来的 supplicant 生命周期调用；enable 不再注册第二份
callback table，disable 不会在活动连接期间释放原生认证状态。整个重建使用原有
Radio 排他生命周期；原生与 helper 失败保留已完成前缀及待清理后缀。超时不回滚
已经提交的策略或保证物理操作未发生，应查询 status 和 wifi.status().radio。
原生清理结果不确定时保留原始错误并要求设备重启，runtime restart 不构成恢复证明。

状态字段：

- `requestedEnabled`：接受的策略；`policyRevision` 在修改时递增，不回绕。
- `enabled`：实际原生 WAPI 是否初始化；修改中或不确定时为 null。
- `supplicantActive`、`nativeGeneration`：实际 SDK 生命周期观察，代次不回绕。
- `policyApplied`：当前原生实例已应用请求策略；`busy` 表示正在执行原生生命周期调用。
- `restartRequired`：原生清理不确定；`runtimeCleanupPending`：配置/helper 清理未完成。
- `error`、`cleanupError`：原始原生错误，不含密码或密钥。

WAPI-PSK 连接继续使用 `wifi.connect(ssid, {password, minimumAuthMode:"wapi", ...})`
及现有 Station 凭据 owner；本模块不另存密码，不接受证书模式，也不支持 WAPI SoftAP。
`minimumAuthMode` 是 SDK 认证下限，**不是 WAPI-only 选择器**；capabilities 的
`exactAuthSelection` 为 false。应核对连接结果的实际认证模式，不能仅凭 threshold
或模块 enabled 宣称建立了 WAPI 连接。disable 不删除已保存的 Station 凭据。

当前公开控制、原生生命周期包装和类型已编码；定向编译记录见 Wi-Fi 实施表。
原生 callback/TX 退休、GC/OOM、故障注入、三目标全矩阵和 WAPI 对端认证验收仍待
集中执行，不以源码、局部链接或普通 WPA 连接代替 WAPI RF 证据。
