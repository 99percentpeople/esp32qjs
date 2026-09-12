# W-08：固定 SDK EAP 凭据副本清理与替换

基线 firmware `d7db8d1`，ESP-IDF `fff9895c82d744c7237be8847347bdd1b07c6643`。
接续[原生凭据 profile](2026-09-10-w08-enterprise-profile.md)，本项处理 SDK 内部
已确认的存储路径。不是 enterprise API 完成、原生任务退休证明或认证运行验收。

## 源码缺陷与修补

原始 `esp_supplicant/src/esp_eap_client.c` 的全局 identity/username/password/
new-password 释放使用普通 `os_free`；`src/eap_peer/eap.c` 的 SM config 副本也
直接释放。框架 profile 清零不能擦除这些独立副本。

SDK 的四个 byte setter 先释放旧值再分配；分配失败会丢失旧指针并留下原长度。
PAC setter 直接覆盖全局指针，正常重设和 OOM 都可能失去旧分配的引用。domain
与 FAST phase1 也在新分配成功前释放旧配置。以上是源码确认，动态失败复现未运行。

新增 `scripts/patch_idf_eap_secrets.py` / `.cmake`，由顶层 CMake 引入。仅当
framework Wi-Fi 与 SDK enterprise 同时启用时，验证八个已审查 SDK 依赖的
SHA-256，生成 build-local `esp32qjs_sdk_fixes/eap_secrets/{esp_eap_client,eap}.c`，
并精确替换 wpa_supplicant 中对应的两个 source；共享 SDK 不改动。

已编码的修补：

- 全局四种身份/密码副本和 SM config 四种副本改用 SDK `bin_clear_free`。
  EAP key 领取/成功路径中的两处旧 key 释放同样清零。复用 SDK 的现有
  `forced_memzero`，没有新写另一套秘密清零实现。
- 四个 byte setter 通过共同 helper 分配并复制新值，成功后才清零旧分配并换入。
  OOM 不改变旧指针或长度，不通知“配置已改变”；NULL 正长度输入明确拒绝。
  新值可以来自旧分配的一部分，因为复制在释放前完成。
- PAC 替换先准备新副本，再清零/释放旧分配；原先小于 512 bytes 的 empty-PAC
  分配按完整 512 bytes 清理。normal reset 与 clear-certificate 的 PAC 清理一致。
- domain 使用 `str_clear_free`，先复制后替换，允许安全的旧字符串子串输入；
  FAST phase1 同样在新分配成功后替换，并在 reset 时清零。
- 证书、私钥、私钥密码和 CA 属于调用者借用，仍只解除指针，不由 SDK free。
  它们的寿命将由原生 profile 和后续可靠退休边界控制。

这是单次 setter 的分配/释放修复，不是跨 setter 的配置事务，也没有增加 EAP
任务同步。部分 enable/disable 失败、config-changed timer 注册失败、EAP worker
排空和 native ownership 查询仍待处理；不得据此允许认证期间并发改凭据。

PAC 长度小于 512 时原 SDK 创建空 PAC 并丢弃输入，本项保留这个 SDK 行为；
公开 FAST API 仍须明确格式/空 PAC provisioning 契约，不能把它包装成“任意文件
已经完整安装”。同样没有修改证书校验默认值或省略后续显式 time-check 设置。

替换时旧/新 SDK 副本短暂共存，SDK/TLS 的额外内存不包含在 framework profile
字节账本内。完整 W-09 预算与实机内存比较继续保留。

## 已完成的构建与静态验证

- C5 roaming-enabled immutable Context 构建通过，镜像 2,896,224 bytes。
  两个修改后的 SDK object 已编译；`ar` 成员与这些 object 的 hash 精确相同，
  relocation 显示实际调用 `bin_clear_free`。
- Wi-Fi-disabled immutable Context 构建通过，镜像 459,024 bytes；不启用补丁，
  不生成这组修补源文件。两个镜像大小均与上一轮一致。
- 当前没有企业认证公开入口/SDK bridge 引用相关函数，linker 尚未将 setter /
  global reset / SM config deinit 引入最终 ELF。不能把 object 编译当作运行验证。
- 跟踪的 30 个 framework static object 大小不变；没有新增 SRAM 大型静态 buffer。
- manifest 52 classes / 504 functions、feature 27 项、固定 SDK schema STA35/AP21、
  strict TypeScript、SDK map、whitespace 一致性通过。MQuickJS syntax 为
  61 sources / 59 snippets。正式 API 没变。

日志、archive/object 证明、SDK/input/output hash：
`build/w08-eap-secrets-evidence.json`、`build/w08-eap-secrets-c5-build.txt`、
`build/w08-eap-secrets-c5-disabled-build.txt`、
`build/w08-eap-secrets-manifest-check.txt`、`build/w08-eap-secrets-check-js.txt`。

## 延期回归与剩余工作

新增 `tests/python/test_idf_eap_secrets.py` 编写了原始/修补 SDK 函数的对照用例：
普通释放遗留、正常替换、OOM 保留、别名输入、PAC 泄漏/完整 512-byte 清零、
FAST/domain 失败、SM 部分初始化释放、借用证书不被释放、hash drift/double patch。
使用实际 SDK setter/reset/deinit/clear helper 与 SDK 通用 zero 实现，注入 allocator、
timer 与存储边界；不另建替代状态机。本轮仅 AST，未 import、编译或执行 fixture。

Host 失败复现/回归、FAST internal-TLS 构建、enterprise-off/Wi-Fi-on、C3/S3 全矩阵、
EAP 原生任务/Radio、认证对端/RF、实机 GC/关闭/队列/runtime restart、静止态 heap
与 largest block 均 `not-run`。Wi-Fi API 完成后集中阶段测试，BLE API 完成后长 soak。

下一步是 SDK 配置安装/可靠退休、Radio/helper 归属与 runtime 关闭，然后接入
JS capture 和 enterprise 公开 typed API。其余 Wi-Fi 功能范围不变。
本项未刷机、串口操作、擦除 workspace、前端构建、提交/推送或更新根 gitlink。
