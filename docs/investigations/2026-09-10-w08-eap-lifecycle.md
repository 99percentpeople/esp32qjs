# W-08：EAP worker 退休与原生观察边界

基线 firmware `d7db8d1`、SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。
接续[SDK 凭据清理](2026-09-10-w08-eap-secrets.md)。本项不是完整 enterprise
启用/关闭事务，也没有注册公开企业认证 API。

## 源码确认与实现

固定 SDK `esp_eap_client.c` 先释放 config/blob/method/TLS，再发 TASK_DEL。
`wpa2_task_delete` 不向调用方传递投递失败；init 先创建 task，随后才创建同步
semaphore，且不检查 queue 创建结果。这些是源码缺陷，动态失败复现仍未运行。

新增 `scripts/patch_idf_eap_lifecycle.py` 组合上一轮 secrets 修补，继续在独立
build-local SDK source 中编译。CMake 依赖包含两个 patch script 及审查过的
EAP/eloop/private header；共享 SDK 不变。

已编码的顺序与失败行为：

1. init 完成 queue、RX/START sync semaphore、独立 exit semaphore 后才启动
   worker。逐个检查分配；创建 task 前准备 SM 初始字段。前一个 SM 若未成功
   退休，新 init 返回错误，不能覆盖 `gEapSm`。
2. deinit 先标记 retiring 并取消精确 EAPOL-start timer，然后请求 worker 退出。
   新 RX 与 start timer、普通 post 在 retiring 状态被拒绝。
3. worker 结束时删除 queue、清空 task handle，再只向 exit semaphore 发送退出
   确认。普通 RX/START 的旧 sync 确认不能满足退出等待。确认后的 worker 不再
   访问 SM/queue，仅执行自身 task delete。
4. `task_started` 记录持续到退出确认被消费。等待失败后，即使 worker 已清空
   handle，重试仍先消费 exit 确认；不能因为 handle 为 NULL 就释放 semaphore。
   TASK_DEL 重复待处理或 queue-send 失败返回错误，保留全部原生资源及 cleanup error。
5. 收到退出确认后，才按 method/abort/config/blob/TLS、队列内容、semaphore、
   mutex、SM 顺序释放。失败后 SDK 的 void deinit callback 保留 SM；disable
   callback 检查这个事实，未退休时不 reset globals 或 unregister methods。
   callback-table unregister 失败也在 globals reset 前返回原始错误。
6. 软件状态已经 disabled 但 SM 仍存在时，public SDK disable 不得跳过退休流程。
   现有 API lock 初始化/完整 enable 失败事务仍需后续处理。

新增内部 `esp32_mquickjs_wifi_eap_sdk_snapshot()`，通过固定 eloop blocking
调度在 Wi-Fi/supplicant task 读取：software-enabled、SM、task、queue、data lock、
两种 semaphore、retiring、exit-pending 以及 cleanup error。调度未执行时
`entered:false`、resources=`UINT32_MAX`，不把调度失败当作空闲。

观察不包含指针或凭据。resources=0 **不是**完整 SDK driver/callback-table/借用
全局凭据已退休的证明，也不是调用者身份所有权证明。完整 owner/安装/关闭事务
必须补齐后才能释放已安装 profile。

## C5 driver 二进制证据

本轮从固定 `libnet80211.a` 读取 `ieee80211_supplicant.o` 与 `ieee80211_ioctl.o`，
没有修改它们。register 函数保存传入 callback 指针，unregister 负责释放它；
启用/禁用入口通过 ioctl 调用对应 process。两个 process 在调用 EAP callback
之前，先对同一 driver 状态字节分别写 1/0，再 tail-call callback。

因此 callback 返回错误可能留下 driver 与 SDK 软件状态不一致。native snapshot
的 ENABLED 位只是 `s_wpa2_state`，没有观察该 driver 字节。后续安装/清理必须
处理这个部分完成状态，不可只看公开 SDK 返回值或 software-disabled。
此结论限定于已读取的 C5 object；C3/S3 archive 尚未执行同等审查。

另有 `esp_wpa_main.c::wpa_deattach` 忽略 enterprise-disable 返回值继续 deinit，
仍需与框架 Radio 的 deinit 前置门槛一起处理。本项不声称解决全部 driver teardown。

证据：`build/w08-eap-supplicant-disassembly.txt`、
`build/w08-eap-ioctl-disassembly.txt`。原始 object/archive hash 随本项 evidence 保存。

## 验证边界

C5 roaming-enabled 与 Wi-Fi-disabled immutable Context 构建通过。SDK 静态库
中的 object 与组合修补源码的 object hash 一致；新增 native snapshot 已编译。
公开入口尚未接入，相关函数仍未进入最终 ELF；不能把构建称作生命周期运行证明。
两镜像分别为 2,896,224 / 459,024 bytes，与前轮相同；跟踪的 30 个 framework
static object 不变。新增 SDK exit semaphore 是运行时分配，预算/堆比较尚未验证。

manifest 52 classes / 504 functions、feature 27、SDK schema STA35/AP21、strict TS、
MQuickJS 61 sources / 59 snippets、SDK map 与 whitespace 检查通过。日志与 hash
见 `build/w08-eap-lifecycle-evidence.json`、`build/w08-eap-lifecycle-c5-build.txt`、
`build/w08-eap-lifecycle-c5-disabled-build.txt`。

新增 `test_idf_eap_lifecycle.py` 使用生产 init/post/worker/deinit/disable callback
和 native snapshot。注入 allocator/queue/semaphore/task/crypto-storage 边界；
调度器只在真实 worker 的空队列等待或 task delete 处返回。用例覆盖逐次分配失败、
queue 拒绝、SM 不被覆盖、旧 RX ack、wait 失败后迟到完成再重试、unregister 错误、
快照调度失败和输入边界。仅 AST，未 import、编译或执行；不是替代状态机验收。

Host/VM、C3/S3、FAST 与完整 feature matrix、认证对端/RF、实机 GC/关闭/队列/
runtime restart、静止态内存比较均 `not-run`。Wi-Fi API 完成后集中阶段测试，
长 soak 放在 BLE API 完成后。

后续仍需 SDK callback/driver 所有权、enable/install/disable 完整事务、API lock
OOM、配置通知失败、Radio/helper 准入、JS capture 和公开 enterprise 类型/API。
其余 Wi-Fi 范围不变。本项没有刷机、串口操作、擦除 workspace、前端构建、提交/
推送或更新根 gitlink。
