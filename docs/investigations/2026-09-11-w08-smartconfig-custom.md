# W-08 SmartConfig 二进制 custom data

公开凭据结果新增 `customDataBytes`：ESPTouch v2 为精确 0–64 字节数组，包含
嵌入及末尾零字节；零长度为 `[]`，其他协议为 `null`。唯一 v1 的类型、文档和
capabilities 同步，没有新增方法或兼容格式。整体仍为 Candidate。

## 固定 SDK 的长度与时序

核对 `fff9895c82` 的 C3/S3/C5 `libsmartconfig.a`（现有构建脚本逐一校验完整
archive hash）：`g_config_data` 为全局指针；v2 分配大小为 `0xb08`，长度字节
在 `0xa41`，custom data 起点为 `0xaac`，SDK 清理区间包含 65 字节。公开 getter
按 `min(actual, len-1)` 拷贝后补 NUL，无法返回准确长度。

更关键的是 `sc_get_ssid_passwd` 经 adapter 同步 post 116-byte 凭据事件后，
立即清零该区域。因而先把事件交给异步队列、再调用 getter 已经太晚，等待
decoder finish/stop 后读取同样不可行。三目标 getter/init/free/producer 的
反汇编保存在 `build/w08-smartconfig-custom-review/`，是固定二进制的静态证据，
不声称运行或 RF 验证。

生产代码在已有同步 SC_EVENT 捕获边界、原生 Wi-Fi task 持有 decoder 时读取
长度和数据；此路径不调用 SDK API、不分配或等待队列。空指针或大于 64 的
长度使本次捕获失败并保存错误，不裁剪为伪成功，不读取超出有界区域的数据。
后续重复事件保留第一份结果，不再访问可能已经释放的 SDK 存储。

## 所有权与秘密

network credential 和 custom data 合并为单个内部 bundle，一次 copy/commit
贯穿 events、decoder、Radio、Session 和公开转换。领取整个 JS 对象成功才
commit；任一属性/数组元素分配失败可重试原结果。关闭、commit 和临时转换
退出均沿用 secure-zero，status/error 不包含 custom data。应用持有的 JS 数组
不能由 native cleanup 清除，也不承诺 VM heap 或 SDK 全部秘密副本已清理。

未新增常驻 custom data 池。C5 DWARF 确认 bundle 为 184 B，按需事件记录为
232 B（此前 168 B）；Session 也随同一 bundle 扩大，其 `reservedBytes` 使用
真实 sizeof。最终预热 heap/largest-block 比较仍待实机阶段。

## 验证和剩余范围

C3、S3、C5 roaming、C5 no-SoftAP、C5 Wi-Fi-disabled 五种生产 Build Context
构建成功。manifest 54 classes / 529 functions、类型、27 feature 文档、配置
schema、SDK coverage 和 MQuickJS 61 sources / 62 snippets 检查通过。源码、
ELF/ROM/库成员、SDK 反汇编和日志哈希见 `build/w08-smartconfig-custom-evidence.json`。

事件 fixture 增加 64-byte/嵌入与末尾 NUL、SDK 在 post 后清零/释放、无效长度和
空指针；MQuickJS fixture 增加空数组/非 v2 null、Nth allocation/移动 GC 后
整包重试及清零断言；既有 Session/decoder/Radio fixtures 同步内部 bundle。
全部仅作 Python AST 解析，未导入、编译或执行，不报告回归通过。

未刷写、使用串口、提交或推送；共享 SDK 和父仓库 gitlink 未变。SmartConfig
专属观察、SDK 内部秘密副本清理及集中运行/手机/RF 验收仍未完成；其余 Wi-Fi
功能继续推进，长 soak 留到 BLE API 完成后。
