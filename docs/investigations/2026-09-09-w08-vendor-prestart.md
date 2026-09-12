# W-08 Vendor IE 预启动交接

承接 [配置接口](2026-09-09-w08-vendor-ie.md)，firmware `d7db8d1` 工作区增量，
SDK `fff9895c82d744c7237be8847347bdd1b07c6643`。本批解除 set 的 STARTED-only 限制：
已配置接口的健康完整 STOPPED 状态也可设置 IE，然后通过同 mode/storage 的
wifi.start() 保留到首次发帧。watch 接收侧仍未实现，模块继续 Candidate。

## 准入和所有权转移

原 generic lifecycle 仍拒绝 Vendor IE 等其他 owner。新增私有显式 dependent
参数仅由 pre-start admission 使用：验证两个模块私有 lease 的完整 identity/
generation/client/required-mode，以及所有槽已知、无 pending、无固定信道/借用
设置/其他 owner。mode/storage 必须与停止的驱动完全相同。预检查重建后所需的
Vendor IE + Application/STA/AP lease identity 容量，耗尽拒绝且不改写原 owner。

同一 Radio mutation mutex 内先申请 exclusive lifecycle，再把 Vendor IE lease
从 registry 退休，将其副本责任绑定到该 token。没有 driver 调用或去掉保护的
间隙。parked 时 public status 的 startPending=true，owners 可以为零；这不代表
驱动副本已释放。set/clear 不能越过 active lifecycle。

原 start executor 保持 helper 退休/准备顺序。initialize 确认已拥有的驱动；
configure 分支只核对实际 mode，拒绝新 configs/controls 或变更 storage，不触发
通用配置重写。resume 再检查 token、mode/storage、容量，先恢复 Vendor IE 两个
接口的真实 registry lease，再分配 Application/STA/AP lease，最后调用 native
START。所有完成核对成功后才发布 helper owner 和退休启动 token。IE 没有被
清除、重装或复制到另一份框架缓冲区。

## 失败与关闭

resume 失败把临时 vendor lease 重新退休，保留同一个 start token 和 slot 记录，
不丢失可能仍在运行的 SDK 副本。对应的 central configuration cleanup 首先调用
exact-token vendor clear；失败的槽保留，成功的槽立即退休，重试只清剩余槽。
未完成时诊断 stage 为 configuration-vendor-ie-clear。普通 runtime cleanup
先处理已有 central lifecycle，避免 public clear 与该 token 互相阻塞。

finish_lifecycle 和物理 shutdown 不接受尚未清理的 parked source；restart
checkpoint 也不能把它当作另一份配置恢复来源。其他 SDK fault、helper 错误和
设备重启要求不被清掉。此处只完成 Vendor IE 的预启动交接，不扩大 F-CORE 或
W-07 故障恢复的验收结论。

## 待执行用例和证据

test_wifi_vendor_ie_prestart.py 连接真实 registry、slot、start admission、
initialize/configure/resume/finish 和 exact-token cleanup；编写 STA/AP/APSTA、
AP-disabled、相同 mode/storage、首帧前 owner 恢复、不重写 IE、容量耗尽、其他
owner、旧 token、失败 START/核对、清理失败和禁止提前 finish 的用例。物理
START/STOP、helper 及通用配置/policy 是明确的注入边界，不代替 RF/实机证明。

扩展实际 central cleanup 的 fixture 检查 Vendor IE 错误先于其余后缀；相关
共享 fixture 同步新 admission helper。其他无 Vendor IE 的隔离测试使用明确
的空模块边界。所有这些 fixture 只做 AST，未导入、编译、执行。

C5 immutable Context 构建 exit 0，binary 2,804,928 bytes（前批 2,802,608）。
s_vendor_ie 从 112 变为 128 bytes，保存 parked start token 与 mode/storage；
其他 12 项静态账本大小不变。未测量实际 heap/碎片或 stack 峰值。
manifest 49 classes/473 functions、features 27、SDK schema 35 STA/21 AP、MQuickJS
61 sources/55 snippets、strict TypeScript、SDK map、12 份 fixture AST 和 whitespace 检查通过。
C5 构建及静态检查证据见 `build/w08-vendor-prestart-evidence.json`。
Host/Python/VM/竞争、C3/S3/feature-disabled 构建、实机首帧/配对/RF/完整 runtime
teardown 运行测试仍 not-run。待全部 Wi-Fi API 完成后阶段验证，长 soak 留到
BLE API 完成之后。未刷写、串口操作、擦 workspace、构建前端、提交、推送或更新
根 gitlink。
