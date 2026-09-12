# W-08 SmartConfig SDK 解码堆副本清理

## 原因与实现

固定 SDK 的 `TOUCH_v2_free_glob` 在释放 2824-byte `g_config_data` 前只清零
SSID/password/custom 三段；其他 packed 解码数据仍在该分配中。legacy
`TOUCH_Free_glob` 和 AirKiss 的释放路径直接调用 OS adapter 的 `_free`。
仅清理 framework 的凭据 bundle 不能覆盖这些 SDK 副本。

构建修补现在复制当前目标的 `esp_adapter.c`，保留原共享 OSI table，另生成
一份 `static const` SmartConfig table，只替换 `_free`。专属 free 先使用
`heap_caps_get_allocated_size` 取得当前完整分配长度，调用现有 wireless
secure-zero，再以原地址调用普通 free；NULL 不查询分配器。没有新分配头、
分配账本或全局 free hook，不改变 SDK 的 allocation/capability 选择。

构建目录中的 `libsmartconfig.a` 只将 OSI 符号引用改到专属 table；共享 SDK
archive 不变。源码、三个目标的 archive/adapter、OSI ABI 和 heap size API
均有 hash gate。原生回收仍由 SDK 在原位置执行，包含 stop、内部 restart 和
失败清理时实际执行的 free。仅执行 framework Session.close 前一次清零不能
替代这个边界，因为 SDK 可在 capture 期间自行释放或重建 decoder。

## 验证与范围

C3、S3、C5 roaming、C5 no-SoftAP、C5 Wi-Fi-disabled 五种生产构建通过。
独立 ELF/archive 核对记录到 `build/w08-smartconfig-heap-evidence.json`：

- 原 archive 与构建副本的全部 allocated section 内容一致；relocation 的唯一
  改动是 SmartConfig 的 OSI 符号，八个引用成员全部切换。
- 实际链接的共享/专属 table 仅 `_free` word 不同；专属 pointer 和 table
  均在只读段，没有新增常驻 SRAM table。
- 实际 free 调用链包含 allocator size、现有 secure-zero 和 free。
- disabled 配置镜像不变；公共 API/manifest 不变。契约与语法检查继续通过。

专属 table/pointer 占 C3/S3 484 B、C5 512 B flash；S3 的合并 ELF rodata
section 带 W flag，因此同时核对 linker 的只读 DROM region 与实际地址，
没有把单独的 nm 字母当作 SRAM 判据。C5 roaming 镜像较上一批增加 896 B，
当前 3 MiB 应用分区余 19,552 B，后续功能容量安排仍待完成。

原 SDK 反汇编保存在 `build/w08-smartconfig-heap-review/`；只读生产 artifact
验证器为 `build/w08-smartconfig-heap-verify.py`，不执行测试 fixture。

待集中执行的生产函数用例登记完整分配清零、相邻 canary、NULL 和 SDK 输入
漂移拒绝。当前仅 AST；没有导入、编译或执行 fixture，不能当作运行证明。

## 继续待完成

SDK 栈临时副本不属于 heap free 的覆盖范围，仍须审查并清理。审查还发现
固定 C3 decoder 的 `sc_init_sniffer_glob` 与 `TOUCH_Init_guide_glob` 在某些
calloc 后先写字段再检查 NULL，以及 v2 第二次 calloc 失败后释放 `psni_info`
但未就地置空的路径。这些是原生反汇编证据；尚未执行 OOM 复现，须继续核对
三目标实际调用和恢复路径，不能将本批 heap 清零写成 SDK OOM 已安全。

SmartConfig 整项仍为 Candidate；Wi-Fi 其余 API、阶段运行和实机/RF 验收未
完成。未刷写、使用串口、提交或推送；长 soak 继续留到 BLE API 完成后。
