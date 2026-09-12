# W-08：企业认证凭据存储与 SDK 边界核对

基线 firmware `d7db8d1`，固定 SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
本项是企业认证实现的内部前置工作，不注册 `wifi.enterprise`，不把内部存储当作
企业 Wi-Fi 已可用。完整范围仍以 02 第 18 节和剩余工作表为准。

## 已编码的生产存储

新增 `esp32_mquickjs_wifi_enterprise_profile.h` 与
`wifi_enterprise/esp32_mquickjs_wifi_enterprise_profile.c`，纳入正常 component
编译。仅在 framework Wi-Fi 与 SDK ENTERPRISE 同时启用时产生实现。

profile 是一次分配的不可变 native record，包含 policy 与十个独立 span：外层
匿名身份、认证用户名、密码、新密码、CA、客户端证书、私钥、私钥密码、PAC、
校验证书的 domain。输入只需在同步 create 调用中存活；返回后无 JS/调用者借用。
每个非空字段附加一个不计入原长度的 NUL，其他二进制内容不改变。

- 最多两个 profile，单个完整分配最多 65536 bytes，合计 131072 bytes；预算
  包括 native record 与 NUL padding，不包括 allocator overhead 或 SDK/TLS 副本。
  入场先保留预算，malloc 失败归还；最终清零/free 完成之后才归还最后预算。
- native refs 保留 SDK 借用所需的寿命；引用耗尽拒绝 retain，不回绕。最后释放
  复用 `esp32_mquickjs_wireless_secure_zero`，覆盖整个分配块，包括密钥及 metadata。
  不在 critical section 内分配、复制、清零或 free。
- policy 保留非零 EAP method bitmask、TTLS phase2、FAST provisioning/PAC 参数、
  time-check、Suite-B、bundle、OKC。显式 gate 与证书/key 配对在分配前检查；
  domain 与 private-key password 不允许嵌入 NUL。其余 byte span 保留精确长度。
- 字段上限：匿名身份/用户名 128、密码/新密码/私钥密码 1024、CA 32768、
  client cert/PAC 16384、private key 4096、domain 255 bytes。长度验证先于
  读取或 padding 加法，合计按剩余容量减法检查。
- `_view` 仅供持有 native ref 的内部 SDK bridge 使用；不得进入 JS status、
  error 或日志。`_counts` 只提供资源计数，不提供秘密或可逆内容。

## 本地固定 SDK 源码确认

检查范围是 `wpa_supplicant/esp_supplicant/include/esp_eap_client.h`、对应
`src/esp_eap_client.c`、`src/eap_peer/eap.c`、component CMake 与 Wi-Fi Kconfig。
没有依赖在线最新版文档推断固定 SDK。

1. `set_identity` 写 `g_wpa_anonymous_identity`，`set_username` 写认证 identity。
   两个值不同；正式 JS 字段必须明确这层映射，不能制造三个实际只有两槽的字段。
   头文件称 1..127，实际 setter 接受 128；本层按实际范围保留 128。
2. CA/client certificate/private key/private-key password 仅保存调用者指针。
   `eap_peer_blob_init`/config 继续引用它们。公开 setter 返回不是释放证据。
3. username/password/new-password/anonymous-identity 会形成 SDK global 副本，
   `eap_peer_config_init` 又形成 SM 副本。两层释放目前使用普通 `os_free`；框架
   自己清零 profile 不能替代 SDK 副本清零，须在公开接口启用前处理。
4. enterprise disable 会 reset 全局配置；已经 disabled 的路径也会 reset。
   disable 后再次 enable 必须重新安装 profile。`set_disable_time_check` 默认
   为 true，且 globals reset 没重置它；正式默认值必须显式安装，不能继承旧状态。
5. FAST 在关闭 `ESP_WIFI_MBEDTLS_TLS_CLIENT` 时编译；内部 TLS 不支持 domain
   matcher。FAST provisioning 实现支持 0/1/2；PAC 长度小于 512 时 SDK 分配
   空 512-byte 区且丢弃输入，重设 PAC 直接覆盖旧指针。其替换/清零/真实格式语义
   仍需前置修正和延期测试，不能将本层“保留输入”冒充 SDK 完整支持。
6. setter 的 config-changed timer 注册返回值普遍未处理，setter 不是事务。
   EAP task 删除存在同步 semaphore 路径，也有 queue-send 失败分支；仍须逐项
   核对部分 enable 失败、SDK task 排空与 disable 原始错误，不能仅凭 setter
   或日志就释放被 SDK 引用的 profile。

这些是源码边界/缺口；没有执行失败注入来宣称动态复现，也未在本项修改共享 SDK。

## 验证与未完成项

C5 roaming-enabled 与 Wi-Fi-disabled immutable Build Context 编译通过；正常
context 的 profile object 已编译，关闭 context 的 object 不产生实现。由于公开
入口/SDK bridge 尚未接入，这些函数未链接进最终 ELF；构建证明的是生产 C 编译，
不是函数运行。最终数值、对象符号、源码 hash 与日志见
`build/w08-enterprise-profile-evidence.json`。

manifest、feature 文档、固定 SDK schema、严格 TS、MQuickJS syntax、SDK map 与
whitespace 一致性检查通过，正式公共 API 没变。

新增 `test_wifi_enterprise_profile.py` 调用生产实现，注入 allocator/critical
边界，检查精确复制、binary/NUL、引用/双 profile 上限、失败回滚、SIZE_MAX、
字段/整体预算、SDK compile flags 与最后 free 前整个缓冲区清零。仅 AST 检查；
没有 import、编译或执行 fixture。

JS 参数/ByteSource/GC 捕获、公开 typed contract、SDK 副本清零/可靠取消及退休、
Radio/helper 准入、configure/enable/disable/clear/status、启动/停止/恢复与
runtime teardown 接入都仍待完成。公开企业认证保持 contract-pending。

完整 Host/VM、C3/S3/企业认证关闭独立矩阵、认证对端/RF、实机生命周期与静止态
内存比较均 `not-run`。Wi-Fi API 完成后集中阶段测试；长 soak 放在 BLE API 完成后。
本项未刷机、串口操作、擦除 workspace、前端构建、提交/推送或更新根 gitlink。
