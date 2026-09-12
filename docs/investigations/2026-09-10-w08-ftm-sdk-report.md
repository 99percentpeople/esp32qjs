# W-08 FTM 报告释放的 SDK 前置修复

FTM Session/API 尚未实现，本批完成固定 SDK 的报告释放缺陷修复及独立 FTM-enabled
C5 Build Context。firmware HEAD `d7db8d1`；SDK 固定为
`fff9895c82d744c7237be8847347bdd1b07c6643`。未注册空的 FTM API 或正式占位类型。

## 原生证据

SDK `esp_wifi.h` 对 `esp_wifi_ftm_get_report` 的说明明确允许 NULL buffer 仅释放
原生报告。当前 C3/S3/C5 `libnet80211.a` 的公开 wrapper 却在提交 native IPC 前
拒绝 NULL，返回 `ESP_ERR_INVALID_ARG`（0x102）。三目标实际 object 反汇编都能看到
这一分支；这是二进制路径确认，尚未在目标设备上复现或执行故障注入测试。

`ftm_initiator_get_report_local` 内层已有正确的 NULL 分支：有 report data 时调用
原生 free、清空指针；无 report 时成功返回。非 NULL 分支复制最多请求项数与原生
报告项数的较小值，然后 free、清空指针。三目标实际 entry 步长均为 48 bytes。
调用公开 getter 后报告不能重复读取；观察事件不能代替详细报告的存储责任。

同次审查 C5 的 `ftm_initiator_end_session_local`：部分状态只发送终止 Action 后
返回成功；其他状态进入 abort。成功返回不能直接作为退休 owner 的依据。
正常 end/abort 路径先处理 RF/定时器与报告事件，再释放 initiator context；报告
事件没有框架 cookie，只有 peer MAC/status/摘要。这些事实要求后续 FTM owner 保持
到原生终态、报告复制/丢弃和 SDK/default-loop 排空，不可按一次 end 返回重开。
具体 timer/迟到 native 事件的完整竞争证明仍待补齐。

## 修复

沿用现有 `patch_idf_vendor_ie_context.py/.cmake` 的完整 SDK archive 哈希与唯一
完整函数匹配 gate，仅在 FTM initiator build 开启时追加报告修复。输入永远是共享
SDK 原 archive，输出仍为本 Build 的替代 archive；SDK 原文件、符号/member offset、
重定位表、函数长度和其余字节保持原值（既有 Vendor IE 修复继续生效）。

NULL 拒绝分支紧前已把 0x102 放进返回错误寄存器。保持分支指令及原重定位，改为
测试这个确定非零的寄存器，使该分支不跳转：

| Target | 原指令 | 修补后 | offset |
| --- | --- | --- | --- |
| C3/C5 | c.beqz s0（buffer） | c.beqz a0（0x102） | 0x28 |
| S3 | beqz.n a2（buffer） | beqz.n a8（0x102） | 0x3b |

每个目标只在该两字节指令内改变一个字节；之前的初始化和 driver 状态检查仍执行，
后面的参数封装、native IPC 和内层报告处理不变。不能直接放入 NOP 而留下原分支
重定位，否则链接时仍会向该位置编码分支位移。修改后的三目标 object 已重新反汇编，
可见保留的 R_RISCV_RVC_BRANCH / R_XTENSA_SLOT0_OP 指向原分支目标。

FTM disabled 的默认 patch 输出保持既有 Vendor IE-only 行为。未知 SDK/hash、缺少
唯一匹配函数或重复修补已修改 archive 继续拒绝构建，不按相近版本猜测布局。

## Build Context 与检查

新增 `prepare_ci_build_context.py --profile wireless-ftm`，显式开启 FTM、initiator
和 responder SDK flags。生成器支持实际声明 FTM 的 C3/S3/C5；本批只编译 C5。
representative profile 和已有 context 不被改写；未声称新增 profile 已进入 CI matrix。

使用独立不可变输入 `build/wireless-contexts/c5-ftm`，build directory
`build/wireless-c5-ftm`，没有复用既有 C5 构建目录去改它的 feature 配置。
`remote.py --build-context build/wireless-contexts/c5-ftm --build-dir wireless-c5-ftm
--assume y build` exit 0，日志 `build/w08-ftm-c5-build.txt`。实际 sdkconfig 三个
FTM flag 都为 1；构建日志确认选用了带 FTM 修复的 build-local archive。

FTM-enabled C5 binary 2,851,504 bytes；此前 FTM-disabled C5 binary 2,842,144 bytes。
两者 context 功能不同，这个 +9,360 bytes 不能当作纯补丁或同状态内存开销比较。
此前跟踪的 28 个框架无线静态对象尺寸一致；SDK 新增 FTM 存储、运行 heap/stack/
largest block 未测量。公开 FTM getter 当前没有框架调用者，最终 ELF 裁剪它；
patched archive/object 反汇编是修补证据，不能称为 FTM API 已端到端运行。

Manifest 50 classes/482 functions、feature docs 27、live SDK schema STA/AP 35/21、
strict TypeScript、MQuickJS 61 sources/55 snippets、SDK map、whitespace 检查通过。
完整源、archive、context、object/ELF hash 见 `build/w08-ftm-sdk-evidence.json`。

两份 fixtures 只写入并 AST 解析，未导入、编译或运行：

- `test_idf_vendor_ie_context.py` 调用生产 patch 函数，准备三目标默认修复与 FTM
  修复之间的精确字节差、长度、其余 archive/relocations 不变及拒绝再次修补。
- `test_ci_build_contexts.py` 调用生产 generator，准备三目标 FTM flags/profile id
  与 representative 不受影响的检查。

## 接下来的 FTM 实现

继续完成逐字段 options/status/report 的正式契约后再注册：peer MAC、channel、
frame count 0/16/24/32/64、burst period（SDK 单位 100 ms）、有界报告容量及超时。
报告摘要 RTT 是 ns、distance 是 cm；entry RTT/t1–t4 是 ps，64-bit 时间必须无损
交付，不转成可能丢精度的 JS number。failure event 的非有效摘要字段不能当测量值。

Session 需接入 Radio 独占原生 operation、Station/共享 AP 与 off-channel 准入、
control-first 原生终态、详细报告单次读取/释放、end/close/GC/runtime 后缀，以及迟到
无 cookie 事件的隔离。Responder offset 还需 AP 与无 getter 设置的所有权/恢复记录。
`wifi.watch()` 保持只观察摘要，不消费 SDK 详细报告。未完成项仍为 contract-pending。

所有 Wi-Fi API 完成后统一 Host/Python/VM、C3/S3/feature-disabled 构建与实机测试；
长时间 soak 留到 BLE API 完成后。本批未刷写、操作串口、擦 workspace、构建前端、
提交、推送或更新根 gitlink。
