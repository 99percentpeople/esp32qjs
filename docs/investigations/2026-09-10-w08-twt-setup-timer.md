# W-08 iTWT setup 响应与 dwell 定时器身份

基线 firmware `d7db8d1`、SDK `fff9895c82`，接续
[公开 probe](2026-09-10-w08-twt-probe-public.md)。本批完成 C5 iTWT setup 两个
定时器阶段的生产保护。Agreement 的 owner、setup/teardown/suspend 公开接口和
完整退休仍待完成，保持 contract-pending；现有公开 probe 维持 Candidate。

## 确认的问题与边界

固定 C5 `libnet80211.a` 的 `ieee80211_itwt_setup` 与
`he_recv_action_twt_setup` 分别安装 response 和 dwell callback，参数都是
`setup_timer_param + 136 + slot`，即可复用 pending 表中的 dialog 字节地址。
两个 callback 把该地址交给 `ieee80211_timer_process(7, 26/27, argument)`。
原生 process 到实际消费消息时才解引用，再扫描当前 pending 表；旧队列消息
因而可能读到复用槽中的新 dialog，并对后续请求超时或建立 Agreement。

这是原始 archive 的静态调用链证据，未声称设备运行或竞争 fixture 已复现。
本次重新从共享 SDK archive 比对两个已提取 object，并重新生成 disassembly。
审查的 C5 布局如下，全部 offset 都相对 372 B 的 `setup_timer_param`：

| 内容 | Offset / 大小 |
| --- | --- |
| 每槽请求帧 | `17 * slot` / 17 B |
| dialog 字节 | `136 + slot` |
| ETSTimer | `144 + 20 * slot` / 20 B，handle 在 timer 内偏移 16 |
| pending request ID | `320 + 2 * slot` / int16 |
| native 阶段 | `336 + 4 * slot` / uint32 |
| pending mask | 368 / 1 B |

`ieee80211_itwt_setup` 在安装 response callback 前写入请求/ID/dialog、pending
bit 和阶段 0；ACK 把阶段设为 1 后 arm。RX 接受响应时 disarm/done 原 timer，
写入阶段 2，再安装和 arm dwell timer。两个原生 process 只同步读取参数字节，
未保存其地址。响应阶段 callback 与 dwell 阶段必须使用不同的本地身份。

## 已实现

`wifi_twt_setup_timer.c` 只接管八个精确 timer 地址；其他 timer 沿既有路径。
两个 callback 与随后 native queue 消费都携带 boot-scoped uint32 identity，
单调递增且耗尽失败。setfn 创建新身份；disarm/done 先撤销旧身份权限；重复
callback、旧消息、错误阶段或尚未触发的身份不会进入原始 process。

消费前在 Wi-Fi native task 核对当前关联 node 地址值、pending bit、request ID、
dialog、flow 和 response/dwell 阶段。旧 node 只作数值比较，不解引用；重复的
存活 dialog 因原始 handler 会扫描匹配而拒绝。匹配后只把栈上的 dialog 副本
交给同步 handler，两个异步队列不再借用可复用 SDK 请求存储。

ledger/snapshot 在短临界区内更新，heap、esp_timer、native post 和原始 handler
均在区外调用。callback 先标记 fired 再 post；若 post 返回时旧操作已消费或
被替换，迟到的 post error 不会污染后继 timer。

timer create/start/stop/delete 和 queue post 失败保留原始错误、首个 fault
阶段/槽，以及清理错误。已知已停止 timer 的 `ESP_ERR_INVALID_STATE` 可接受。
删除失败保留同一 handle；后续只重试 disarm/done 后缀，setfn 不覆盖它。
不会退回 SDK 的 abort/借用参数实现。首次 fault 保持可诊断，不能因删除成功
而宣称该 setup 已恢复；setup admission wrapper 在提交前后检查 timer fault。

## 实际接入与尚未完成的部分

最终 C5 ELF 中 `ieee80211_itwt_setup_timeout`、`ieee80211_itwt_setup_dwell`
均调用新 wrapper，再由数字身份核对后调用原始 process。实际 `g_wifi_osi_funcs`
表中五个 timer 入口也经过现有公共 wrapper，再进入 setup helper。仅凭
`--wrap` 参数或源文件存在不能证明这条调用链，证据文件记录实际指针和反汇编。

`__wrap_wifi_sta_itwt_setup_process` 的提交前后错误检查目前只编译进入 archive，
没有公开 Agreement caller，尚未进入该镜像的 ELF；不能把它报告为公开 setup
已经可用。公开 probe 的三个 ROM 方法和 Future driver 九个 callback 指针仍
匹配，未加入新的公共类型或空注册项。

本批没有完成 setup TX 回调与 RF dialog 复用的关联、广播/Information timer、
取消/teardown/HW 清理、完整 timer/TX/native/event 退休及 Agreement Radio/Future
owner。SDK 的 RF dialog 是循环使用的字段，本地 numeric timer identity 不构成
RF cookie。保留 handle 和 sticky fault 是失败证据，不是释放所有原生资源或
物理恢复的证明；这些边界继续作为后续 Agreement 实施前提。

## 构建与静态检查

通过 immutable Build Context 的生产构建，没有烧录：

| Context / build directory | 镜像字节 | 相对公开 probe 批次 |
| --- | ---: | ---: |
| c5-roaming / wireless-c5-roaming | 2,975,360 | +3,216 |
| c5-no-softap / wireless-c5-no-softap | 2,851,760 | +3,216 |
| c5-disabled / wireless-c5-disabled | 459,024 | 0 |
| c3 / wireless-c3 | 2,665,696 | 0 |
| s3 / wireless-s3 | 2,570,768 | 0 |

从 firmware 执行 `source /home/zach/esp/esp-idf/export.sh` 后使用
`.venv/bin/python scripts/remote.py --build-context build/wireless-contexts/<context> --build-dir <directory> --assume y build`。
五份日志均正常完成，无 compiler warning/error；生产 object 与对应 archive
member 一致。新入口只在 C5 HE Wi-Fi gate 内存在。共享 SDK 工作区干净，SDK
原始 archive 与上批 build-local patched archive hash 均未改变。

C5 静态 SRAM 新增 28 B；八槽 ledger 首次 setup 按需分配 192 B INTERNAL/8BIT，
boot 保留且不随 close 释放。每个 callback 只保存数字，不新增 callback heap
owner。SDK timer 自身的分配另计。其余 38 个已跟踪 framework 静态对象和
128 B probe Future capture 不变。实际 ELF 的新增 arm/disarm 路径及其可解析
直接调用均位于 IRAM；ledger 固定内部内存。这不是运行 heap/free/largest block
比较，也不声称抵消新增开销。

manifest 52 classes/513 functions、feature 文档 27、SDK schema STA35/AP21、
strict TypeScript、MQuickJS 61 sources/60 snippets 和 whitespace 检查通过。
证据：`build/w08-twt-setup-timer-evidence.json` 与同名前缀的 summary、build
日志、原始 SDK/最终 ELF disassembly。证据包括实际 timer table 五个指针、
16 个函数的直接调用关系，以及源码、构建输出和 SDK hash。

新增 `test_wifi_twt_setup_timer.py` 调用生产 helper，注入 esp_timer/native table
边界，描述八槽复用、迟到消息、阶段/关联改变、post 中消费/替换、重复 callback、
分配及 timer 错误、保留 handle、后缀重试和 identity 耗尽。SDK fixture 补真实
pending-table capture/match；probe fixture 仅对新分流边界注入不匹配 stub。
三份文件仅 AST 检查，未 import、编译或执行。Host packed ETSTimer 仅维持槽距，
不是 C5 ABI 证明；生产 C5 的 layout static assertion 与实际 object 独立验证。

not-run：Host/VM fixture 运行、完整 Future/SDK 调度、设备 close/reopen/GC/
runtime restart、RF 对端/共存与实际内存比较。集中运行和实机测试仍在所有
Wi-Fi API 完成后，长 soak 在 BLE API 完成后。没有串口/烧录/擦除 workspace、
前端构建、提交、推送或根仓库 gitlink 更新。
