# W-08：企业认证 profile 安装与 SDK 借用退休

基线 firmware `d7db8d1`、SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
接续 [EAP control](2026-09-10-w08-eap-control.md)。新增原生安装事务，尚未连接
Radio/runtime/JS，公开 enterprise API 仍为 contract-pending。

## 已编码行为

`esp32_mquickjs_wifi_eap_install.c` 提供内部 install/clear/status。所有事务及
记录访问在 Wi-Fi task，其他任务通过固定 blocking eloop 同步调用；EAP worker
拒绝调用以避免互相等待。记录不保存 JS/runtime 指针或输出凭据。

1. 安装前拒绝已有 binding、SDK 外来资源或未完成的 driver 清理 hook。初始
   driver unknown 只有在资源为空且无 hook 时才允许；此观察不替代调用方
   exclusive Radio admission，也不是 driver 实时状态读取。
2. 取得独立 profile 引用并分配单活动 64-bit identity。identity 耗尽明确失败，
   不回绕；失败的安装也消耗编号。旧/重复 clear 不能释放其他 binding。
3. 先完成基线 disable/reset，再写入 methods、时间检查、phase2、条件支持的
   Suite-B/bundle、anonymous identity、username/password/new password、CA、
   client cert/key/key password、domain 或 FAST/PAC，最后 enable。
4. SDK enable 默认打开 OKC，事务在其成功后写入 profile 明确的 OKC 值。
   配置通知在同一任务直接标脏并取消旧 timer，不依赖 setter 的通知分配成功。
5. 任一步骤失败保留原始 error 和静态 stage，尝试 disable 清理。只有 SDK
   disable 成功且实际 snapshot 资源为零，才归还借用引用；清理失败保留
   identity/profile/cleanup error，供精确 clear 重试。调度失败不会释放 binding。
6. clear 还显式恢复时间检查和关闭 OKC。调用方自己持有的 profile 引用不受
   影响，可用于后续再次安装；事务不覆盖或自动替换已有 binding。

因此未来 JS configure 可持有配置 profile，enable 建立 SDK 借用，disable
退休借用后仍保留配置。这里尚未实现该 JS 行为或 Radio admission。外部 SDK
调用与框架绕过 owner 的混用也不受本层支持，不能据此声称完整多 owner 安全。

## 输入与认证策略

固定 `tls_mbedtls.c` 将证书和密钥 blob 的 length 原样传给 parser。profile
每个字段已有私有末尾零：PEM 检测采用有界 BEGIN marker 搜索，长度包含所需
终止零；已有终止零不再追加，DER 保持原长度。此转换不等于已解析或验证证书。

同一 adapter 在没有 CA 且没有启用 default bundle 时使用 VERIFY_NONE。
安装在任何 SDK mutation 前，要求 TLS/PEAP/TTLS 提供 CA 或 build 支持的 bundle。
时间检查默认 false（即不禁用），显式配置传给 SDK；认证对端、域名匹配及完整
安全策略仍需公开契约与运行测试，不将 setter 成功称为认证成功。

固定 SDK 会丢弃短于 512 bytes 的 PAC 内容，并分配空 provisioning buffer。
profile 现在拒绝非空短 PAC，也拒绝 FAST 选项配合非 FAST methods；无 PAC 仅在
明确非零 provisioning mode 下允许。安装时以非空 sentinel + length 0 调用 SDK
创建空 PAC；不会把用户短文件当作空配置悄悄丢弃。FAST 仅适用于支持它的内部
TLS build，相关生产 build 和认证验证尚未运行。

## 验证边界

C5 roaming-enabled 与 Wi-Fi-disabled immutable Context 构建、manifest、feature、
SDK schema、strict TypeScript、MQuickJS syntax 与 SDK map 结构检查通过。
详细二进制大小、SDK archive/object、原生 installer object、最终 ELF 可达性、
源码 hash 和原有 framework static 大小核对见 `build/w08-eap-install-evidence.json`。
未接入的 installer 编译通过不代表其运行、Radio 集成或企业认证已通过。
C5 启用/关闭镜像分别为 2,896,240 / 459,024 bytes，与上一批不变；跟踪的 30 个
framework static object 不变。installer object 新增 60 bytes 静态记录，尚未
链接进 ELF；该数值不包含 future SDK/TLS 动态分配，也不代表实际堆测试。

新增 `test_wifi_eap_install.py` 编排实际 profile 引用/分配/清零和实际 installer，
仅注入 SDK/eloop/allocator 边界，覆盖逐个 setter/enable 失败、失败清理保留、
迟后重试、dispatch 拒绝、外来资源、精确旧 identity、最终 owner 释放、PEM/DER、
安全默认值、FAST 空 PAC 和 identity 耗尽。profile/control fixture 同步补输入
和实际 SDK helper 用例。三份 fixture **仅 AST，未 import/编译/执行**。

Host/VM 动态失败复现、C3/S3/full feature matrix、实际 SDK/Radio 联合执行、
认证/RF/GC/队列/关闭/runtime restart、静止态内存/最大块比较均 not-run。所有
Wi-Fi API 完成后集中阶段测试，长 soak 留到 BLE API 完成后。

下一步是 Radio exclusive owner 与 runtime 退出时的借用退休，再接 JS capture、
公开 enterprise 类型/API。其余 Wi-Fi 范围保持 remaining 文档所列。本轮未刷写、
串口操作、擦除 workspace、前端构建、提交/推送或更新根 gitlink。
