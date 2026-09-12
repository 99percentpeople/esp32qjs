# W-08 Vendor IE 有界接收与 SDK context 修复

承接 [配置接口](2026-09-09-w08-vendor-ie.md) 与
[预启动交接](2026-09-09-w08-vendor-prestart.md)。firmware `d7db8d1` 工作区增量，
SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。注册 `wifi.vendorIe.watch()`，
正式类型/manifest/API 同步，保持唯一 v1 和 Candidate，不代表 W-08 全部完成。

## 接收与所有权

callback broker 在物理 Radio init/storage 之后、START 之前注册；没有订阅时
也注册，以支持预先创建队列及普通 driver restart 后继续观察。context 为不回绕
的 Radio generation 数值，不是 runtime、queue 或可释放对象指针。callback
进入时精确校验并持有 entered 计数，再把 IE 交给 watch capture。

capture 对已发布的静态 mutex 只尝试 take(0)，锁忙计数丢弃。验证 frame、
非空地址/IE、element 0xdd、最小 length 4；SDK callback 的长度字段代表完整
元素，没有额外可查询的 buffer span。只复制 length+2 字节，最大 257。队列
采用 EventQueue 的 callback 非阻塞 drop-newest 入口，没有第二层 ingress
pool。每次 callback 的事件序号相同，各订阅 OUI 过滤独立；所有事件均为值副本。
SDK 未提供接收 interface 或配置 index，因此不推断这两个字段。

最多四 active source，单容量 1..32、合计 64 slots，最多八 retained native
queue handles。close 仅摘除 source；context_release 到 EventQueue 最后原生
引用销毁后才归还 handle/capacity 并释放 source。已关闭 JS handle、pending
receive、reaper 保留仍计费，不能靠 close/open 绕过预算。此处不是 W-09 的
跨模块总预算；队列 metadata、scratch 和分配器开销不包含在 event slot bytes。

runtime teardown 在 source mutex 内先摘除每个 source，再 retain 可保留的
queue，在锁外 close/release。已由 reaper 接管的 disposed queue 同样先摘除，
避免等待 close hook 移除注册项的重试循环。其原生容量仍保留到最终销毁。
无 SDK/JS 调用在 source mutex 或 broker critical section 内；callback 不取
Radio mutation mutex。EventQueue close/reap 释放 send mutex 后才调用 close hook。

物理 shutdown 在 stop 后禁止接收、注销 callback，并要求 entered=0 才 deinit。
注销失败保留 vendor-ie-unregister；SDK 已注销而 callback 未排空保留
vendor-ie-drain，重试只检查未完成后缀。初始化注册失败保留 vendor-ie-register
和 raw error，不声称失败后盲目 init 能恢复。普通 stop/restart 不关闭观察队列，
旧队列中的值仍携带原 generation；runtime teardown 关闭订阅。

## SDK context 缺陷与构建副本修复

对固定 SDK 三目标的 API wrapper、ioctl handler 和 RX caller 做了二进制审查：

- wrapper 分配 24-byte 消息，将 callback 写入 +12，传入 ctx 写入 +20；
- `wifi_set_vnd_ie_cb_process` 把 **message+20 的地址** 写入 g_ic+204，
  而不是读取该字段的 ctx 值；callback 写入 g_ic+200；
- `ieee80211_parse_beacon` 直接读取 g_ic+204 作为 callback 的第一个参数，
  没有再解引用。C3/C5 是 lw a0,204(s2)，S3 是 l32i a10,a10,204。

因此 SDK 传出的 context 不符合公开参数契约；直接使用 generation cookie 会
丢弃正常接收。没有通过解引用未知/迟到 SDK 消息地址来绕过身份校验。此结论
来自实际目标对象指令，尚未做 RF 注入或实机复现。初次 C5 build 成功不能证明
该 SDK context 正确，修复后的最终 ELF 已另行核对。

`patch_idf_vendor_ie_context.py/.cmake` 在 project() 之后替换 imported
esp_wifi_net80211 的构建输入。SDK 原 archive、Build Context、根 gitlink 均不变。
以三目标完整 archive SHA-256 和唯一函数完整字节签名准入，其他目标或 SDK
变化直接停止构建，必须先重新审查；FEATURE_WIFI disabled 跳过此修复。

只替换一条等长指令：C3/C5 的 c.addi a0,20 改为 c.lw a0,20(a0)，S3 的
addi a2,a2,20 改为 l32i a2,a2,20。archive 长度、成员/symbol offset、relocation、
其余 bytes 全部保留。三目标构建副本经 objdump 确认是 word load；最终 C5 ELF
也确认使用修复后的指令。哈希、差异字节 offset 和反汇编路径记录在
`build/w08-vendor-watch-sdk-evidence.json`。这只是固定版本的 SDK 修复，不是新
公共 API，也不宣称未知 SDK/目标支持。

## 待执行用例与证据边界

`test_wifi_vendor_ie_watch.py` 使用实际 broker/capture/close/converter/status/OUI
parser，SDK、锁、EventQueue send/retain 是注入边界。编写注册期间同步 callback、
注册失败保留、注销失败重试、entered 期间注销成功后的 drain-only、旧 generation
拒绝、完整 257 字节副本、OUI 过滤、queue full/lock busy、非法帧/长度、序号耗尽、
disposed source teardown、closed 容量保留、实际 MQuickJS Nth allocation/GC 转换用例。
`test_idf_vendor_ie_context.py` 编写 exact SDK archive 修改与未知输入拒绝检查。
已有配置/scan/runtime 隔离 fixture 增加明确空 watch 边界，不假装运行完整接收模块。
所有 fixture 本批仅 AST，未导入/编译/执行。

公开 watch constructor 的全参数/GC/OOM、真实 EventQueue wake/close/reaper、线程竞争、
SDK 注销实机保证、callback RF/帧覆盖、完整 runtime teardown 及跨模块 W-09 预算
仍需集中验收，不由边界注入测试替代。C3/S3 与 feature-disabled 全固件构建、
Host/Python/VM 测试和设备测试均 not-run。阶段测试待 Wi-Fi API 完成，长 soak
留到 BLE API 完成之后。未刷写/串口操作/擦 workspace/构建前端/提交/推送。

最终 C5 immutable Context build exit 0，binary 2,810,576 bytes（前批 2,804,928，
增加 5,648）。原有 13 项静态无线账本大小不变；新增 watch/broker 静态状态合计
156 bytes，不包含动态 queue/source 或原生堆/碎片测量。manifest 49 classes/474
functions、features 27、schema 35 STA/21 AP、MQuickJS 61 sources/55 snippets、
strict TypeScript、SDK map、5 份 fixture 加 1 份脚本 AST 及 whitespace 检查通过。
完整事实、源/产物 SHA、SDK 差异与 not-run 见 `build/w08-vendor-watch-evidence.json`。
