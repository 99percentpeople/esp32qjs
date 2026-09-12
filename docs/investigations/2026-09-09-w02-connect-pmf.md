# W-02 直接连接的 PMF 关闭

firmware `d7db8d1` 工作区增量，SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
承接 [configure PMF 三态](2026-09-09-w02-config-pmf.md)，本批将显式 disabled
接入直接 `wifi.connect()`。完整 Wi-Fi 目标仍按[剩余清单](2026-09-08-wifi-api-remaining.md)推进。

## 顺序与边界

固定 SDK 的 dedicated disable 要求 set_config 后、START 前执行。原连接路径先
ensure_started，再在 native connect 前 set_config；只删除 parser 拒绝会被后一次
setter 重新启用 PMF。因此本批在 Future 注册前调用生产 prepare helper，disabled
复用现有独占配置执行器：准入、Station 断连 fence、停止、helper 退休/重建、配置、
dedicated disable、完整读回、START 与 owner 交接。普通 optional/required 保持既有启动路径。

selection 仅提供 Station config，要求 START，允许本 Station 断连；mode/storage
缺省在 Radio mutation mutex 内解析。已有 AP/APSTA 因缺少 AP config 拒绝；其他
feature 的 lease 无法通过独占生命周期准入。不会以连接请求隐式关闭 AP 或 ESP-NOW。
新 Radio 使用 STA/RAM，已有 Station 保留 storage 选择。

准备完成后才注册精确 Future identity；identity 耗尽在准备前拒绝，交接后再次
检查注册条件。disabled 的 native connect 在 CONNECT reservation 下读回完整配置，
与已接受配置比较，匹配才提交 esp_wifi_connect；不再调用 set_config。SDK 读失败或
任何字段不匹配都不发起连接。普通 setter 和 disabled getter 的临时凭据副本均 secure-zero。
后续连接 timer、断连排空、原生完成和 operation 释放路径保持原有机制。

准备阶段沿用现有有界生命周期等待；timeoutMs 和结果 elapsed 从随后连接阶段
计时，不包含这次准备。配置失败仍保留可诊断 cleanup 后缀，不恢复已断开的连接，
不承诺回滚 FLASH 持久凭据。错误后应检查 status，不自动重试 mutation。

## 公共契约

Station parser、WiFiConnectOptions 和 capability PMF enum 同步接入 disabled。
安全校验与 configure 共用，要求 eligible auth 和显式 disableWpa3CompatibleMode，
拒绝 WPA3/OWE 弱化。缺少 WPA3-compatible build 支持时，Station capabilities 不列出
无法满足该前置条件的 disabled。没有新增 callable、兼容别名或常驻账本。

## 验证与未运行项

最新 C5 immutable Context `build/wireless-contexts/c5`（8 MB/no PSRAM）生产构建
exit 0，日志 `build/w02-connect-pmf-c5-build.txt`。binary 2,770,944 bytes，比上一批
增加 240；九项静态 owner/policy/restart 账本均不变。源码 hash、链接符号、检查和
仓库边界见 `build/w02-connect-pmf-evidence.json`。

七份 deferred fixtures 仅 AST 解析：生产 prepare 与 executor 交接、实际 Radio
selection 的 AP/外部 owner 拒绝、生产 Future 注册顺序/identity 耗尽、原生连接的
读回错误/不匹配/成功且不重复写配置，以及完整 Station parser 的安全/GC/OOM 用例。
SDK/helper 边界是注入项，不代表原生 driver 或 RF 行为已通过。既有 timer fixture
的状态字段也同步到当前生产路径所需字段。

manifest 49 classes/469 functions、features 27、schema 35 STA/21 AP（live SDK）、
MQuickJS 61 sources/53 snippets、strict TypeScript、SDK map 与 whitespace 检查通过。
未导入、编译或执行 Host/Python/VM/fault fixtures；C3/S3/feature-disabled、完整
Wi-Fi 阶段测试与实机均 not-run，按约定集中进行。长时间 soak 放在 BLE API 完成后。
未刷写、串口操作、擦除 workspace、前端构建、提交、推送或更新根 gitlink。
