# W-08 WPS 凭据与设备信息边界

固定 SDK 为 `fff9895c82d744c7237be8847347bdd1b07c6643`。本批先修复 WPS
公开 Session 所依赖的生产 SDK 数据路径，没有注册 `wifi.wps` 占位 API。

## 确认问题及实现

- `wps_dev_init()` 将四项 factory 字符串直接作为 snprintf 的 format；现在
  固定使用 `%s`，`%n`、`%s` 和百分号作为普通设备信息复制。
- `wps_finish()` 在 get_config 读回旧配置后直接覆盖 SSID/password 的前缀，
  短凭据会留下旧尾部。现在预验证凭据数组与目标字段长度，清零两字段后复制，
  保留其余 Station 配置及原 SDK 的 strict/non-strict 认证策略。
- 原路径先发布成功状态、取消总超时，再分配配置；OOM 会停在完成过程中，
  get/set/disconnect 的错误也被忽略。现在保留原防重入标记，配置写入成功后才
  进入成功清理；失败进入既有 failure 清理并返回原始错误。这里尚未替换旧 SDK
  failure event 的阻塞发布，也未建立面向 JS 的可靠错误交付。
- credential callback 在复制前检查长度和有界数量；不再持有 parser 输入的
  `cred_attr` 借用指针。移除这一路径的 SSID/key dump、PIN dump，以及恢复配置
  日志中的 SSID。
- 复用 SDK `forced_memzero`、`bin_clear_free`、`wpabuf_clear_free`：清理原生
  WPS owner、context、parsed credential/session keys、临时 AP 配置、previous
  Station 配置、DH private buffer 和 last message；初始化的栈 cfg/PIN event
  及成功事件缓存也使用不可优化掉的清零。先销毁 WPS data，再释放其 context。

这些是已检查到的具体副本，不代表所有 WPS 解析/密码学/事件队列秘密副本均已审完。
现有 SDK 的自动配置、自动连接和 reconnect-on-failure 政策也未变成公开 Session
的契约；后续必须接入共享 Radio 准入及精确连接 owner，不能直接包装 start。

`scripts/patch_idf_wps.py` 逐输入 SHA-256 校验，只生成构建目录中的三份 source。
CMake 替换原 component source，保留 SDK 编译开关与唯一 v1；共享 ESP-IDF 不修改。
WPS 是该 component 的编译定义，不能误用不存在的 `CONFIG_WPS` Kconfig 布尔值
跳过修复。Wi-Fi-disabled 配置不启用此 source 替换。

## 验证与边界

确认依据是固定 SDK 源码中的实际调用/复制/释放路径；按用户要求，本批不执行
失败复现 fixture。`test_idf_wps_credentials.py` 已登记原始短凭据尾部残留、原始
不安全 format 参数，以及修复后严格/非严格模式、各 driver/OOM 失败、完整长度、
超长/整数最大值、池容量、借用指针清除和释放前清零。测试调用生产函数或原始
调用片段，仅 AST 检查，不导入、编译或运行；原始 format 用观察边界避免执行 UB。

生产构建 C3、S3、C5 roaming、C5 no-SoftAP、C5 Wi-Fi-disabled 五项通过；
manifest（54 classes / 530 functions）、类型、文档、schema、SDK coverage 与
MQuickJS（61 源文件 / 62 片段）静态检查通过。证据归档于
`build/w08-wps-credentials-evidence.json`，只读核对脚本为
`build/w08-wps-credentials-verify.py`：生成 source 与生产 patcher 一致，三份
object 与各自 SDK archive 的对应 member 字节一致，实际 object 保留了长度
校验、driver 调用、secure-zero/free，指定凭据日志字串已移除。

公开 WPS 尚未接入，当前 ELF 未链接这批 WPS 函数；五种镜像尺寸均不变，
disabled 镜像 hash 不变。本批不增加静态存储；不将 archive 编译等同于公开 API
已链接或实际执行。C5 roaming 的 3 MiB app 分区仍仅余 19,312 B，后续新能力
需要通过合法 Build Context 处理容量，不修改现有 immutable context。

首轮生产构建发现搬移 source 后局部头文件寻址失败，修正为既有
component include root 下的 `wps/...`，原失败日志保留。

下一步仍需：精确 operation identity、enable/start/disable 所有权、原生 timer/TX
退休、控制完成先于零等待观察、PIN/凭据专属 owner、Radio/Session/Future/GC
接入及阶段运行/RF 验收。固定 SDK 的 start 没有 timeout 参数，总计时为 120 秒；
公开能力须如实描述，不伪造 SDK timeout 控制。长 soak 继续留到 BLE API 完成后。
