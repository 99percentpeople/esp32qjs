# W-08 SmartConfig SDK 分配失败的原生保护

## 已确认的路径

固定 SDK 的 C3/S3/C5 archive 均存在以下路径，原始反汇编见
`build/w08-smartconfig-heap-review/`：

1. `sc_init_sniffer_glob` calloc 92 bytes 后向返回地址 +88 写零，然后才检查
   NULL。分配失败时无法进入原有失败分支。
2. `TOUCH_Init_guide_glob` 第二次 calloc 16 bytes 后向返回地址 +12 写零，
   然后才检查 NULL。
3. `TOUCH_v2_init_glob` 已取得 16-byte `psni_info` 后，如果 2824-byte
   decoder calloc 失败，会 free `psni_info`，但未将全局指针清空就请求
   SDK 内部 restart。后续释放/重用因此会看到已释放的地址。

这是生产 archive 的静态指令/调用证据，不是已经执行的目标 OOM 注入。

## 修复

前两处写零与 calloc 的初始化重复；在 build-local archive 中替换为同宽
NOP，保留后面的 NULL 检查和原有失败分支。成功分配仍得到同样的全零内存。
没有改动分支目标、section 长度、符号大小或 relocation，也没有重新实现
SDK 解码器。修补先核对整个 SDK archive SHA，再核对 ELF section、精确
opcode 和 patch span 内无 relocation；任何漂移都拒绝构建。

| 目标 | sc_init_sniffer_glob section offset | TOUCH_Init_guide_glob section offset |
| --- | --- | --- |
| C3/C5 | 0x2e，4-byte NOP | 0x5e，4-byte NOP |
| S3 | 0x37，3-byte NOP | 0x54，2-byte NOP |

S3 opcode 由 ESP32-S3 target 工具链汇编核对；不能使用通用 Xtensa assembler
的默认大小端设置推断。链接后再次检查实际原生函数，避免只证明中间 archive。

第三处沿既有 SmartConfig 专属 secure free 修复：仅在即将释放的地址等于
`psni_info` 时，先清空该全局 owner，再清零并释放内存。正常 free 中已有的
NULL 赋值仍幂等；其他对象不会清空它。没有改变共享 Wi-Fi free 或新增全局
owner、分配头和 SRAM 账本。

## 验证与待办

五种生产 Build Context 构建及静态契约检查通过。当前证据为
`build/w08-smartconfig-oom-evidence.json`，验证器为
`build/w08-smartconfig-oom-verify.py`。archive 比较限定只允许两处 opcode 和
此前 OSI 符号重定向，实际 table 的 calloc 入口仍指向原 `wifi_calloc`。
公共 API 不变，Wi-Fi-disabled 镜像不变。

待执行用例覆盖真实 SDK archive 的生产修补器、输入漂移拒绝、生产 secure
free 的匹配 owner 撤销/无关 owner 保留/NULL，以及完整分配清零。仅 AST，未
导入、编译或执行 fixture。opcode 汇编属于生产修补字节的生成核对，不是测试
fixture 编译或运行。

SDK 其他调用者、内部 restart 调度、OOM 的公开原始错误诊断和完整失败恢复
仍需继续处理及集中注入验收；本批没有把“两个 NULL 写入已移除”扩大成所有
SDK 分配失败都安全。SDK 栈上的秘密副本、其他 Wi-Fi API 和实机/RF 验收继续
待完成，长 soak 留到 BLE API 完成后。未刷写或使用串口，未提交或推送。
