# W-08 TWT probe TX callback 身份隔离

基线 firmware `d7db8d1`、SDK `fff9895c82`。本项修复上一批确认的共用 TX
callback 缺口；公开 TWT 及完整 timer/RX/event 退休仍待完成。代码为 Candidate，
动态、实机和 RF 证明未执行。

## 根因与实际变更

固定 C5 `wl_cnx.o:cnx_probe_rc_tx_cb(eb)` 先读取 EB 的发送状态，再无条件调用
`itwt_probe_rc_tx_cb(status)`。后者只检查当前 node 的 pending 状态，已经丢失
EB 身份，普通连接 Probe Request 的迟到完成可改变当前 TWT probe 的状态。
TWT callback 失败又返回 -1，让外层继续普通连接候选遍历。

build-local net80211 修补把唯一调用前的四字节 `lbu a0,19(a5)` 改为
`addi a0,s0,0`，传入原 EB；保留完整函数、所有 relocation、symbol/member 和
archive 大小。固定 archive hash、完整唯一函数 bytes 及 C5/HE gate 共同约束。
该符号唯一的 undefined caller 已核对；`--wrap=itwt_probe_rc_tx_cb` 指向新的
EB guard，guard 调用 `__real_itwt_probe_rc_tx_cb(status)` 时仍用原生 status ABI。
不修改共享 SDK。C3/S3 和 Wi-Fi 关闭构建不启用此修补。

guard 在现有 ledger 找到精确 live EB、probe 标记和当前 slot，才允许进入
原生 TWT callback；每个 probe 只交付一次。普通连接回调返回 -1，保持其原
候选遍历；已识别 TWT 回调返回 0，即使原生 TWT 处理返回 -1，也不会推进普通
候选连接。没有把 observer queue 当作完成通道。

## 提前完成、存储与准入

- 原生 probe handler 在发送返回后才设置 pending 和 response timer。提前 TX
  callback 只复制一字节状态，并返回“已处理”；handler 成功返回后再交给原生
  TWT callback。提交失败时不重放该提前状态。
- probe slot 新增 submit/callback pins；即使 EB 已回收，也保留独立 native
  slot 至延后 callback 返回。只读取保存的状态，不读已经释放的 EB。
- 新 probe 拒绝覆盖已有 scope/slot；原生 task 内先核对实际关联状态、node
  pending byte 与 probe timer handle。拒绝未关联或仍存活的旧 probe，保持
  BUSY 的原生边界；这些检查本身不是 timer callback/event drain 证明。
- 无法建立身份的 probe 在进入原生发射前归还已分配 EB，返回 NO_MEM。原生
  handler 已有非零发送返回的 failure event/PM wake 释放路径。不会发出一个
  无法区分完成身份的 probe；其他 Action 管理帧的观察故障策略保持原实现。

slot 的新状态字节使用原 padding，仍是 C5 8 字节/项、64 项、512 字节 lazy
boot ledger。新增当前 probe slot 指针，静态记录由 28 增至 32 字节。既有
30 个框架静态对象与 32 字节 EAP stop 记录不变，真实 heap/PSRAM 比较未运行。

## 已验证与剩余

C5 最终 ELF 确认原生 cnx callback 把 s0 中的 EB 传给新 guard；guard 再调用
原生 status callback。回收 wrapper/其 helper 保持 IRAM。SDK probe submit
尚无公开 caller，其 ioctl wrapper 仍在 archive 中、未进入最终 ELF；不能把
此项描述成公开 probe API 已完成。

C5 roaming 构建为 2,955,984 B，比上一批增加 608 B。C5 Wi-Fi 关闭、C3、S3
构建大小不增加。生成 manifest 和 MQuickJS 语法检查保持通过。具体构建/链接、
archive 修补范围和文件 hash 见 `build/w08-twt-probe-callback-evidence.json`。

生产 wrapper fixture 已补普通/重复完成、提前完成+回收、提交失败不重放、
callback 返回前回收、OOM 前置失败和 scope BUSY；生产 SDK fixture 补关联/
node pending/timer 准入，修补 fixture 检查其余 archive 字节不变。仅 AST，
未导入、编译或执行 fixture；不把代码审查或构建记为竞争测试通过。

仍待处理：RX Probe Response 的无 cookie 关联、旧 timeout 消息/所借 node
存储、timer rearming 与 native/default-event-loop 联合排空、Radio owner 和
公开 Agreement/Future/probe 控制。长 soak 保留到 BLE API 完成后。本轮无
刷写、串口、擦除、前端构建、提交、推送或父仓库 gitlink 更新。
