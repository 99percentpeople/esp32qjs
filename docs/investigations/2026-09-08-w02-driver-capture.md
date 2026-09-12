# W-02：内部 driver config capture

基线 firmware `d7db8d1`、SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
新增 `wifi_config.c` 内部入口，准备把完整原生输入接入配置事务。它已参与 C5
编译，但尚无公开调用方；不注册 configure 或发布 pending driver 类型。

## 已接入

字段生成器除声明提案外，新增 `esp32_mquickjs_wifi_config_fields.h`，从同一
checked mapping 生成实际 raw capture 接受的字段清单。未知键、reserved 和
派生的 SSID-length/BSSID-enable 不作为输入。检查命令同时核对两个生成物。

原提案的 Uint8Array 并不存在于当前 MQuickJS API，已改为项目实际的
`string | ByteSource`。SSID 在任何 driver 操作前复制到最多 32 bytes 的栈存储：
文本拒绝 NUL；array-like 先校验有限整数长度和每个 0..255 byte；ByteView
仅在复制期间持有原生读锁，成功/失败均释放，不保留 JS 地址。Station 没有
独立 SSID length，二进制输入仍拒绝 NUL；AP 使用显式长度，允许原始零字节。

Raw capture 通过受 GC root 保护的临时参数对象复用现有 Station/AP 安全/PHY
验证器。它不修改输入对象、不动态调用 JS API、不触发原生连接。内部暂用已知
合法的文本 SSID 完成公共参数验证，再覆盖为已捕获的真实 bytes；字节身份
不经过 UTF-8 解码。临时对象仅引用调用方已有值，不额外复制密码字符串。

`pmfRequired:true` 转换为 required，false 在共享验证后检查，不允许覆盖
SAE-PK/纯 WPA3 等要求的 PMF；开放 AP 的显式 false 正常接受。AP 空密码按无
凭据处理，安全模式是否允许仍由共享 validator 判断。TU 输入是 100..60000
的 100 倍数，直接写入 native 字段，不经毫秒浮点换算和二次向上取整。

AP raw channel 可为 0 或 target 支持的信道，0 把自动选择交给 SDK；固定信道
沿用事务的 country 验证。原生 AP validator 放开二进制 SSID、合法信道形状与
target gate；现有公开 startAP 的 parser 仍是字符串与 1..11，不因此开放
其 5 GHz/shared/auto 参数。自动选信道的 SDK 结果和 RF 行为待实机验收。

任何解析失败都会清零整个输出 config，包括无效 interface；成功输出由调用方
持有并负责 secure-zero。没有新增 driver/helper owner 或跨调用 native storage。

## 剩余范围与验证

完整高级认证的 credential owner/gate 尚未接入，现有安全 parser 对不支持的
认证选择仍失败。公开 configure 还需完整复合选项事务、外层 operation 错误、
结果和 APSTA 生命周期绑定；当前内部 helper 不宣称这些已经完成。

C5 immutable Context 编译通过，app `0x27ba90` bytes，空余 17%。未公开引用的
helper 可被链接器移除；构建证明其编译，不证明公开路径已启用。语法/生成物/
SDK map 结果与 hash 在 `build/w02-driver-capture-evidence.json`。

新增 production raw capture + 真实 MQuickJS/ByteView 用例，覆盖文本/原始 byte
身份、array-like 边界、ByteView 关闭/空/过长、PMF false 冲突、TU 整数、拒绝
其他接口的参数名、policy/native validator 失败、逐 property/分配 OOM 和移动
GC。此 fixture 注入共享安全 parser/native validator 边界；完整安全语义另由
Station/AP parser fixtures 覆盖。生成器用例同步 C header，AP fixture 补齐
新增信道 validator 的生产依赖。所有这些用例已写、未运行。

完整 Host C/Python、其他目标和 feature-disabled、实机功能仍留到全部 Wi-Fi
API 完成后；soak 放到 BLE API 完成后。无串口、刷写、提交或父仓库 gitlink 更新。
