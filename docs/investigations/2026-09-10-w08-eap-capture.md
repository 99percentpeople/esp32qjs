# W-08：企业认证凭据捕获与预算内 profile 构造

基线 firmware `d7db8d1`、SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
接续 [Radio/runtime 清理](2026-09-10-w08-eap-radio.md)。本项新增 JS 输入到原生
profile 的捕获实现，尚未注册公开 enterprise API 或接入配置替换/启停 Future。

## 已编码

`esp32_mquickjs_wifi_eap_options.c` 使用实际 MQuickJS GC roots 与现有严格 options
校验。所有属性值先独立 root，预检长度，再在现有双 profile / 131072-byte 总预算
内分配最终 profile；没有另一套凭据 staging pool 或未计入预算的临时秘密副本。

profile 增加 begin/field/seal 内部构造接口：begin 检查 build gate、长度、跨字段
关系与总容量，预留精确最终存储；未 seal 时禁止 view/retain，mutable field 只
允许构造方访问。成功复制全部字段并完成内容校验后才 seal。任意失败沿原有
release 清零整个分配并释放，预留账本在 wipe/free 完成后才归还。原有 native
create 同样经完整校验后生成 sealed profile。

捕获规则：

- `methods` 是非空、无重复的 tls/ttls/peap/fast 数组，不能由数字/string coercion
  替代。明确使用 SDK bits；各 target/build 能力由 native validator 再检查。
- anonymousIdentity 与 username 对应 SDK 两个独立 identity 槽，不添加第三个别名。
  password/newPassword、CA/client certificate/private key/key password、PAC/domain
  都复制到 native 字段；接受 string 的 UTF-8 bytes 或既有 ByteSource。
- string 转换指针仅用于当次长度/复制，转换用局部缓冲立即清零；没有 JS pointer
  跨后续 getter/GC 保存。ByteView 的每次观察/复制使用 read lease 并立即释放。
- ArrayLike.length 读取一次，作为固定捕获边界；逐个 getter 获取整数 0..255。
  后续增长不会扩大分配，空洞、缩短导致缺失或非整数值失败。用户 getter 异常
  原样保留；框架错误只有静态阶段名，不插入凭据内容。
- verify-time 默认开启（checkCertificateTime=true），OKC/Suite-B/bundle 默认
  false，TTLS phase2 默认 mschapv2。TLS/PEAP/TTLS 在分配前要求 CA 或支持的 bundle。
- FAST provisioning 使用 disabled/unauthenticated/authenticated，精确映射固定
  SDK 0/1/2；非 FAST methods 不接受 fast 配置。短 PAC、无配置 provisioning、
  unsupported FAST/bundle/domain 与嵌入零的 domain/key password 均被拒绝。

以上属性名已在内部 capture 实现中使用；正式源类型、manifest 与 API 文档仍
等待真实公开入口一起注册，不能据此称 enterprise configure 已可用。空字符串/
零长度 ByteSource 视作字段缺省；null 拒绝。证书 bytes 捕获成功不是证书解析或
对端认证成功，JS 原始字符串仍归调用方/VM 所有。

## 验证与后续

C5 roaming-enabled / Wi-Fi-disabled immutable Context 构建、manifest 52/504、
feature 27、SDK schema STA35/AP21、strict TypeScript、MQuickJS 61/59、SDK map
与 whitespace 检查通过。对象/archive/ELF、大小和 hash 见
`build/w08-eap-capture-evidence.json`；capture 入口尚未链接进最终 ELF。
C5 启用镜像 2,940,912 bytes（较上批 +16），关闭镜像 459,024 bytes（不变）；
跟踪的 30 个 framework static object 不变。C5 profile 元数据为 104 bytes，
与字段及其 NUL padding 一起计入预算；不是运行堆、最大块或 GC 验证。

新增 `test_wifi_eap_capture_gc.py` 使用真实 MQuickJS、生产 ByteView、options、
profile builder 和 secure-zero；注入 allocator/RTOS lock 边界。用例覆盖单次
getter、移动 GC、ByteView 提前关闭、原异常保留、整数/长度/未知字段、每次
分配失败、profile 复制后源释放及整个 native 分配清零。profile fixture 同步
新增未 seal 时不可读取/retain、成功 seal 和部分秘密销毁。两份 fixture 仅 AST，
未 import、编译或执行，不能称为 GC/OOM 运行验证。

仍需配置 owner/revision 重入保护、configure 替换、enable/disable/clear/status/
capabilities、Future/GC 与 stop/restart 策略，之后统一注册 sole v1 契约。C3/S3/
完整 feature matrix、Host/VM 运行、实际 SDK/Radio、认证/RF、关闭/GC/队列/
runtime restart 实机及静止态内存比较继续 not-run。全部 Wi-Fi API 后集中阶段
测试，长 soak 留到 BLE API 完成后。其他 Wi-Fi 剩余范围不变。

本轮未刷机、串口操作、擦除 workspace、前端构建、提交/推送或更新根 gitlink。
