# W-08 information TX 身份与省电引用回收

本批将原生信息帧的完成与 PM 引用放入现有 TX ledger，并把未完成清理接入
SDK snapshot/setup 关闭检查。公开 Agreement、暂停/恢复 Future 与控制结果
关联仍未实现；本批不代表 TWT API 完成。

## 实现边界

- 发送前验证实际 callback bit 20、3/11-byte body、关联 node、单个或所有
  flow 的精确 request ID，以及当前 native producer 的同步 task scope。
  无身份或分配失败时不发出帧；producer 归还尚未交给 TX 的 PM 引用。
- 在原有 64 个 TX 槽旁延迟分配同下标的身份数组。C5 为 64 × 24 B，
  基础 ledger 仍为 64 × 12 B；总 reserved_bytes 在分配后为 2304 B。
  数组在 boot 内保留，无 callback/recycler 堆分配或释放。
- callback 在读取 EB 前锁定对应存活槽，复制身份并保留 busy；核对当前
  node/flow/request ID 与帧 control。匹配时调用原始 SDK callback，保留
  PMF、timer 和原始成功/失败行为；失配时不修改当前 flow，但归还该 TX
  自己的 PM 引用。重复、已回收或错误 task callback 不重复完成。
- 原生 callback 可能同步回收 EB，返回后只访问已保留的槽。旧 callback
  仍执行时，同地址的新 TX 使用另一空槽；旧 callback 不能释放新槽。
- 未经过 callback 的回收保留 PM 债务。只有 output 与 recycler 均返回后，
  Wi-Fi task 的 cleanup 才归还一次，并释放槽。IRAM recycler 不调用 PM。
  现有 SDK snapshot 和 setup quiescence 调用 cleanup，关闭仍检查全局 TX
  活动及 revision，不将回收等同 timer/event/RF 排空。

无 cookie 完成依赖已检查的原生 callback-before-recycle 顺序。不能声称
在原生完全退休、地址重新分配之后，人为再注入旧地址 callback 仍能辨别；
这需要原生顺序证据，不能用地址或当前 flow 伪造新的请求 cookie。

## 验证

生产 fixture 已补：信息身份第二次分配失败、短帧、错误 task、迟到/重复
callback、无 callback 回收、output 内提前 callback/回收、旧 callback
执行中地址复用、原生错误/无 output 回滚及事件队列饱和。
按照阶段顺序，仅检查 Python AST，未导入、编译或执行 fixture。

本批生产构建和静态链接检查通过，记录见
`build/w08-twt-information-tx-evidence.json`：

| Build Context | binary bytes | 相对 information-submit |
| --- | ---: | ---: |
| C5 roaming | 2,993,072 | +1,808 |
| C5 no-SoftAP | 2,869,472 | +1,808 |
| C5 Wi-Fi-disabled | 459,024 | 0 |
| C3 | 2,665,696 | 0 |
| S3 | 2,570,768 | 0 |

最终 C5 ELF 的 `ieee80211_he_attach` 注册了新的 callback 20 wrapper；
SDK snapshot/setup quiescence 实际调用 native PM cleanup。callback/cleanup
无堆调用，共用 recycler 及所调用的框架 helper 保持 IRAM，未引入 PM 调用。
最终 ELF 已核对 64 × 24 B 分配参数；静态 TX 对象 40 → 48 B，其他检查过的
现有对象大小不变。两个 C5 的 build-local SDK patch 与上一批相同，共享
ESP-IDF 工作区仍干净。

**producer wrapper 与 PM 获取 hook 仍在 archive 中，因未接入公开 suspend
caller 而没有进入最终 ELF。** callback guard 的链接不代表公开暂停路径已
交付，后续接入 Agreement 必须重新验证完整 producer/回调调用链。

manifest 52 classes / 513 functions（全项目）、27 feature docs、配置 schema、
1,267 项 SDK map、严格 TypeScript、MQuickJS 61 sources / 60 snippets 及
5 份 fixture AST 通过。集中 Host/VM/RTOS/GC/OOM/RF 和实机测试保持 not-run。
未刷写、串口操作、提交或更新父仓库 gitlink。
