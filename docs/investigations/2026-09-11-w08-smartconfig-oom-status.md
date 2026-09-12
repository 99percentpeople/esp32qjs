# W-08 SmartConfig 原生 OOM 交付与凭据门槛

SDK 内部 calloc 失败后可能只请求自身 restart，外层 start 仍返回成功。
移除 NULL 写入并不能让 framework 知道这次失败；若不记录它，调用者可能
最终只看到 acquisition timeout，或在失败后的清理交错中继续交付凭据。

## 实现

- SmartConfig 专用 OSI table 的 `_wifi_calloc` 现在调用一个薄 wrapper，原样
  转发 count/size 并调用原 `wifi_calloc`。非零请求得到 NULL 时同步记录
  `ESP_ERR_NO_MEM`；成功和零尺寸请求保持原返回值。不改变共享 Wi-Fi table。
- 记录位于既有 events owner。hook 只在短临界区写入 sticky control error，
  不分配内存、不发布观察事件、不停止 SDK。无 owner/已经关闭时忽略；新
  owner 的错误从零开始。hook 是当前原生 decoder task 的同步调用，没有
  延迟 cookie；原有 stop/native queue fence 仍是释放及重用 record 的前提。
- start 的 SDK 调用返回后检查该记录；即使 SDK 返回成功，也不能把发生过
  OOM 的 decoder 发布为 started。运行中的 OOM 由 decoder status 读取，沿
  既有 Radio/Session worker 保存 `smartconfig-allocation` 阶段并请求关闭。
  原始 SDK 返回错误和清理阶段保留既有优先级。
- 凭据 copy/commit 在同一 owner 锁内拒绝已记录的 OOM，避免一次较早的状态
  查询与后续交付之间漏掉失败。失败后的 SDK 观察事件只计为 discarded；保留
  的秘密仍由原有 close 路径清零，allocator hook 不提前释放 SDK 正在使用的
  存储。关闭后不再用迟到的 allocation hook 覆盖原操作结果。
- Public `receive()` 使用既有 `WIFI_SMARTCONFIG_FAILED`、`espCode/espName`
  和 status/stage，不增加新 API 或格式版本。失败交付不依赖观察队列。

## 验证边界

C3、S3、C5 roaming、C5 no-SoftAP、C5 Wi-Fi-disabled 五种生产构建通过；
manifest、类型、API 文档、MQuickJS 语法、schema 和 SDK coverage 检查通过。
源码、生成 adapter/archive、实际 ELF 调用和 DWARF 尺寸证据归档于
`build/w08-smartconfig-oom-status-evidence.json`，只读验证器为
`build/w08-smartconfig-oom-status-verify.py`。

新增字段利用原 events status 的对齐空间；目标 record 仍为 232 B、status
为 48 B，不增加常驻 SRAM 或动态 owner 分配尺寸。专属 readonly table 大小
不变，仅 free/calloc 两个入口与共享表不同。Wi-Fi-disabled 镜像不变。

待执行用例调用生产 allocator wrapper、完整 events record 和 decoder helper：
原参数/结果转发、空请求、记录不分配/不 post、重复失败、关闭/新 owner 隔离、
失败后 copy/commit 拒绝、SDK 返回成功但实际 OOM、运行中的 OOM 及清理。
当前仅 AST，未导入、编译或执行 fixture；不代表目标 OOM、原生 restart 调度
或完整 Future/RTOS 竞争已经运行通过。

SDK 栈秘密副本审查、完整原生失败恢复及 Wi-Fi 其余接口/实机验收继续待完成。
保持 Candidate；未刷写、使用串口、提交或推送。长 soak 留到 BLE API 完成后。
