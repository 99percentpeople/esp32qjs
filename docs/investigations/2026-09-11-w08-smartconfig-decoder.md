# W-08 SmartConfig 定时器、解码器协调与 ACK 交接

本批新增内部 worker coordinator，串联已存在的事件/凭据记录、固定 SDK 的
九个 legacy timer 和 ACK lane。尚无公开 Session 或 Radio caller；这些入口
已编译归档，不把内部协调器或五种构建通过记作配网 API 可用。

## 输入和原生启动

调用端必须先持有 STARTED Radio 的排他授权，并在 release 前保留它；所有
coordinator 调用（含 status）由同一 worker 串行执行。create 在有界单 owner
名额内复制配置与 key；不调用 driver。公开 Future 结束不代表可以释放 owner。

固定 SDK C3/S3/C5 的 IPC ABI、local 函数和九个 timer 地址纳入输入哈希校验。
同步 Wi-Fi task IPC 内先检查 Station 模式、已有 decoder 和 promiscuous 状态，
再保留事件、ACK 与 timer，随后设置 protocol/fast mode/channel timeout 并
启动 decoder。无身份的默认凭据 handler 不参与托管启动。

原生 channel timeout 为 15–255 秒的 SDK 配置，不是未来 JS 操作的 deadline。
SDK 对 AES key 调用 strlen，因此当前内部输入明确要求 ESPTouch v2 的 16 个
非 NUL 字节，并复制到带终止符的独立存储；不把此限制伪装成任意二进制 key。
日志始终关闭。JS options、自定义数据及最终 AES 输入契约仍待完成。

原生 start 一旦进入便持有失败清理责任。dispatch 保存实际 local 返回值；
无执行回执且不属于已核对的提交前错误时保留 owner，status 标记
handoff_unknown，禁止重试启动或释放，不承诺 runtime restart 可恢复。
这条路径仍需集中阶段的原生任务调度验证。

## 定时器存储和完成边界

仅接管九个已核对的静态 ETSTimer 地址：channel_timer、Restart_delay_timer、
TouchRestart_ht20_timer、TouchRestart_ht40_timer、TouchUdpTimer、
KissRes_ht20_timer、weixin_timer、restart_ht20_timer、restart_ht40_timer。
其他 Wi-Fi、BT 和现有 C5 TWT timer 保持原分发。

固定 SDK legacy timer 创建/启动错误会 abort，删除错误却会清空 handle。
托管路径改为记录原始错误；每个 esp_timer 使用不回绕的数字身份，callback
进入计数保护 owner。close 撤销 callback/重新 arm 权限；stop_blocking/delete
失败保留同一 handle，删除重试跳过已成功的 stop。cleanup 保留 registry，
避免清理循环中被释放/重开；最后一个 handle/callback/busy 未退出时禁止 release。

arm/disarm 及其本地查找/错误记录保持 IRAM，registry 以 INTERNAL 按需分配。
头文件和定义重复 IRAM_ATTR 造成的编译段名冲突已修正。SDK 校验依赖统一使用
规范化路径，修复包含 '..' 的重复路径导致 Ninja 再生成失败的问题。

## 捕获结束、ACK 与完整关闭

finish_capture 和 close 的职责分别明确：

1. finish_capture 撤销并排空 timer；在 Wi-Fi task 中确认 scan stop、列表清理和
   promiscuous disable，再调用 decoder stop。随后另一条 IPC 检查原生状态和
   RX 状态，并清零 SDK 的 32-byte 静态 crypto record；调用端 key 同时清零。
2. finish_capture 保留凭据和 ACK reservation，供之后显式授权的 Station
   连接、凭据转换与 ACK 使用。这里没有自动连接或隐含的 Radio 释放。
3. ACK 只允许捕获停止后提交一次；使用尚未 commit 的凭据取得元数据，临时
   凭据副本在所有退出路径清零。receipt 在 task 提交前产生；提交失败也可能
   留有 receipt，不自动重发。原生完成只表示本地 UDP 提交，不代表手机收到。
4. ACK reservation 跨 task 完成继续保留。无 owner 的旧 SDK start/stop 不能
   占用或取消托管 ACK；精确 owner/receipt 的 stop 不影响其他操作。
5. close 首先关闭凭据交付并清零，撤销 ACK；排空捕获后等待 ACK socket/参数
   退出，再释放 timer、ACK、事件记录。release 必须在完整关闭成功之后。

已完成的 scan/list/native-stop/fence 步骤不会因后续 ACK 等待重做。RX disable
在尚未成功的 native-stop 尝试前重新确认，不能把更早一次成功当作持续不变的
硬件状态。stop 返回不是单独的 release 依据。

## 验证和未完成项

C3、S3、C5、C5-no-SoftAP、C5-Wi-Fi-disabled 构建通过。manifest 53 classes /
523 functions、feature 文档、STA/AP schema、SDK coverage、TypeScript 与
MQuickJS 61 sources / 61 snippets 一致性检查通过。实际链接的 timer dispatch、
IRAM 位置、归档一致性及分配记录大小见 `build/w08-smartconfig-decoder-evidence.json`。

DWARF 显示 decoder record 为 112 B、timer registry 为 288 B；与上一批凭据
记录 168 B 合计为 568 B 按需记录，不含 SDK decoder、esp_timer 对象和 ACK task/
socket。当前尚无 caller 分配这三份记录。已链接的 timer 常驻状态 C3/C5 为
8 B、S3 为 16 B；ACK 控制记录在归档中为 32 B，不能当作已经运行时分配的账本。
关闭 Wi-Fi 的镜像仍为 459024 B，无 SmartConfig timer 符号。

timer、完整生产 coordinator 和 SDK ACK fixture 已编写，仅 AST 检查，未导入、
编译或执行；包括分配失败、重复 owner、外部 decoder 拒绝、迟到 callback、
stop/delete 后缀、凭据保留、ACK 等待和未知 handoff。构建不是竞争/终止证明。

下一步是 Radio 排他准入、helper/runtime 持有与回收，随后完成公开 Session、
Future/credential/event、custom data 与显式自动连接；SDK decoder 内部凭据/自定义
数据副本的异常清零也须继续核对。完整 SDK callback 调度、
OOM/GC、真实配网/RF 和 heap/largest-block 均 not-run。没有刷写、串口操作、
提交、推送或改变父仓库 gitlink；长 soak 仍留到 BLE API 完成后。
